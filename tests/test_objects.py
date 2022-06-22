#!/usr/bin/env python3
"""Inspect target objects independently and execute multi-unit native probes."""
import argparse
import concurrent.futures
import hashlib
import json
import pathlib
import platform
import shutil
import struct
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inspect(path, strict=True):
    data = path.read_bytes()
    header = struct.unpack_from('<16sHHIQQQIHHHHHH', data)
    assert header[0][:7] == b'\x7fELF\x02\x01\x01' and header[1:4] == (1, 62, 1), path
    shoff, entry, count, names = header[6], header[11], header[12], header[13]
    assert entry == 64 and shoff + count * entry == len(data), path
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + i * entry) for i in range(count)]
    name_data = data[sections[names][4]:sections[names][4] + sections[names][5]]
    def string(table, offset):
        end = table.index(0, offset)
        return table[offset:end].decode()
    by_name = {string(name_data, section[0]): section for section in sections}
    for section in sections[1:]:
        assert section[8] != 0 and section[4] % section[8] == 0, (path, section)
        if section[1] != 8: assert section[4] + section[5] <= shoff, (path, section)
    symtab = by_name['.symtab']
    strings = sections[symtab[6]]
    string_data = data[strings[4]:strings[4] + strings[5]]
    assert symtab[9] == 24 and symtab[5] % 24 == 0, path
    symbols = []
    for i in range(symtab[5] // 24):
        name, info, other, index, value, size = struct.unpack_from('<IBBHQQ', data, symtab[4] + i * 24)
        symbol = dict(name=string(string_data, name), binding=info >> 4, kind=info & 15, section=index, value=value, size=size)
        assert (info >> 4 == 0) == (i < symtab[7]), (path, i, symbol, symtab[7])
        if index != 0:
            assert index < len(sections) and value + size <= sections[index][5], (path, symbol)
            assert info & 15 in ((1, 2) if strict else (1, 2, 3)), (path, symbol)
            if info & 15 == 2: assert size > 0, (path, symbol)
        symbols.append(symbol)
    for symbol in symbols:
        if symbol['name'].startswith('.LCF'): assert symbol['binding'] == 0 and symbol['size'] == 8, (path, symbol)
        if symbol['name'] == 'private_helper': assert symbol['binding'] == 0 and symbol['kind'] == 2, (path, symbol)
        if symbol['name'] == 'narrow': assert symbol['binding'] == 0 and symbol['size'] == 2, (path, symbol)
    relocations = []
    for name, target, width, kinds in (('.rela.text', '.text', 4, (2, 4)), ('.rela.data', '.data', 8, (1,)), ('.rela.rodata', '.rodata', 8, (1,))):
        if name not in by_name: continue
        reloc = by_name[name]
        assert reloc[9] == 24 and reloc[5] % 24 == 0, path
        assert sections[reloc[6]] == symtab and sections[reloc[7]] == by_name[target], (path, name)
        for i in range(reloc[5] // 24):
            offset, info, addend = struct.unpack_from('<QQq', data, reloc[4] + i * 24)
            assert offset + width <= by_name[target][5] and info >> 32 < len(symbols) and (info & 0xffffffff) in kinds and (name != '.rela.text' or not strict or addend == -4), path
            relocations.append(dict(section=target, offset=offset, symbol=symbols[info >> 32]['name'], type=info & 0xffffffff, addend=addend))
    return dict(sha256=digest(path), sections={name: dict(size=value[5], alignment=value[8]) for name, value in by_name.items()}, symbols=symbols, relocations=relocations)


def run(command, expected=0):
    result = subprocess.run(command, capture_output=True, timeout=30)
    assert result.returncode == expected, (command, result.returncode, result.stdout, result.stderr)
    return dict(command=command, exit=result.returncode, stdout=result.stdout.decode(errors='replace'), stderr=result.stderr.decode(errors='replace'))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('compiler')
    parser.add_argument('--count', type=int, default=1000)
    args = parser.parse_args()
    assert args.count >= 1
    compiler = str(pathlib.Path(args.compiler).resolve())
    native = platform.system() == 'Linux' and platform.machine() == 'x86_64'
    base = pathlib.Path('.agent-local/objects').resolve()
    root = base
    root.mkdir(parents=True, exist_ok=True)
    compiler_hash = digest(pathlib.Path(compiler))
    frozen = root / ('compiler-' + compiler_hash)
    if not frozen.exists(): shutil.copy2(compiler, frozen)
    assert digest(frozen) == compiler_hash
    compiler = str(frozen)
    root = base / compiler_hash
    root.mkdir(exist_ok=True)
    def probe(seed):
        work = root / f'case-{seed:04d}'
        work.mkdir(exist_ok=True)
        narrow = 11 + seed % 20000
        bias = 3 + seed % 53
        small = seed % 7
        a = work / 'a.c'
        b = work / 'b.c'
        a.write_text(f'static short narrow={narrow}; static int private_helper(int x) {{ return x+{bias}; }} double exported(double x) {{ return x+{small}.0; }} int value(void) {{ return private_helper(narrow); }}\n')
        b.write_text(f'int value(void); double exported(double x); static short narrow=7; static int private_helper(int x) {{ return x+2; }} double local_float(double x) {{ return x+9.0; }} int main(void) {{ return value()!={narrow+bias} || exported(2.0)!={2+small}.0 || private_helper(narrow)!=9 || local_float(1.0)!=10.0; }}\n')
        record = dict(seed=seed, compiler_sha256=compiler_hash, native=native, source_sha256=[digest(a), digest(b)], objects=[], execution=[])
        for level in ('-O0', '-O2'):
            objects = []
            assemblies = []
            for source in (a, b):
                obj = work / (source.stem + level + '.o')
                asm = work / (source.stem + level + '.s')
                oracle = work / (source.stem + level + '-asm.o')
                record['execution'].append(run([compiler, level, '-fverify-each', '-c', str(source), '-o', str(obj)]))
                record['objects'].append(dict(level=level, source=source.name, **inspect(obj)))
                record['execution'].append(run([compiler, level, '-S', str(source), '-o', str(asm)]))
                record['execution'].append(run(['clang', '-target', 'x86_64-unknown-linux-gnu', '-c', str(asm), '-o', str(oracle)]))
                record['objects'].append(dict(level=level, source=source.name + '-assembly', **inspect(oracle, strict=False)))
                objects.append(str(obj)); assemblies.append(str(oracle))
            if native:
                for kind, paths in (('object', objects), ('assembly', assemblies)):
                    binary = work / (kind + level)
                    record['execution'].append(run(['cc', '-no-pie', *paths, '-o', str(binary)]))
                    record['execution'].append(run([str(binary)]))
        reference = work / 'reference'
        record['execution'].append(run(['cc', '-std=c17', '-O0', str(a), str(b), '-o', str(reference)]))
        record['execution'].append(run([str(reference)]))
        (work / 'observations.json').write_text(json.dumps(record, indent=2) + '\n')
        return record
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        observations = list(pool.map(probe, range(args.count)))
    assert digest(frozen) == compiler_hash, 'compiler changed during the campaign'
    (base / 'observations.json').write_text(json.dumps(observations, indent=2) + '\n')
    print(f'objects: {args.count} two-unit probes at O0/O2 and assembly oracle passed; native={native}')


if __name__ == '__main__':
    main()
