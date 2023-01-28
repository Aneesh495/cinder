#!/usr/bin/env python3
"""Check materialization and bulk programs against ELF bytes and execution."""
import argparse
import hashlib
import json
import pathlib
import platform
import re
import shutil
import struct
import subprocess
import tempfile

from test_unwind import digest, elf, normalized, tool


def validate_dump(text, sections, relas):
    from collections import Counter
    roles = set(); origins = set(); bulk_roles = set(); pool = Counter(); flags = 1 << 32
    for line in text.splitlines():
        if not re.search(r'machine-kind=(9|10|11) ', line): continue
        match = re.search(r'machine-kind=(\d+) origin=(\S+) dst=(\d+) fixed=([0-9a-f]+) clobbers=([0-9a-f]+) bytes=([0-9a-f]*)(.*)$', line)
        assert match, line
        kind, origin, dst, fixed, clobbers, program, tail = match.groups()
        kind, dst, fixed, clobbers = int(kind), int(dst), int(fixed, 16), int(clobbers, 16); program = bytes.fromhex(program)
        origins.add(origin)
        if kind == 11:
            assert origin in ('nop', 'phi', 'local.begin', 'local.end', 'local.reset', 'local.freeze', 'va.end')
            assert not program and fixed == clobbers == 0
            continue
        if kind == 10:
            bulk = re.search(r' bulk=(\d+) extent=(\d+)$', tail); assert bulk
            role, extent = map(int, bulk.groups()); assert role in (1, 2); bulk_roles.add(role)
            if role == 1:
                assert origin == 'zero.init' and fixed == 128 and clobbers == flags | 131
                assert program == b'\x31\xc0\x48\xb9' + struct.pack('<Q', extent) + b'\xf3\xaa'
            else:
                assert origin in ('object.copy', 'object.init', 'va.copy') and fixed == 192 and clobbers == 195
                assert origin != 'va.copy' or extent == 24
                assert program == b'\x48\xb9' + struct.pack('<Q', extent) + b'\xf3\xa4'
            assert program in sections['.text']
            continue
        material = re.search(r' material=(\d+) result-bank=(\d+) bits=([0-9a-f]{16}) relocation=(\d+) addend=(-?\d+)$', tail); assert material
        role, bank, bits, relocation, addend = material.groups(); role, bank, bits, relocation, addend = int(role), int(bank), int(bits, 16), int(relocation), int(addend)
        assert dst != 0xffffffff and role in range(1, 8); roles.add(role)
        if role in (1, 3):
            assert bank == 1 and origin == ('const' if role == 1 else 'undef') and fixed == 0
            assert role != 3 or bits == 0
            assert program == b'\x48\xb8' + struct.pack('<Q', bits) and clobbers in (1, flags | 1)
        elif role == 2:
            assert bank == 2 and origin == 'fconst' and fixed == 0 and clobbers == 1 << 16
            assert program == b'\xf2\x0f\x10\x05' and (relocation, addend) == (2, -4); pool[bits] += 1
        elif role == 4:
            assert bank == 2 and origin == 'undef' and bits == fixed == 0 and clobbers == 1 << 16
            assert program == b'\x66\x0f\x57\xc0'
        elif role in (5, 6):
            assert bank == (1 if role == 5 else 2) and origin == 'copy' and bits == 0 and not program
            assert fixed == (1 if role == 5 else 1 << 16) and clobbers in ((1, flags | 1) if role == 5 else (1 << 16,))
        else:
            assert bank == 2 and origin == 'fneg' and bits == 1 << 63 and fixed == 1 << 16 and clobbers == 0x30001
            assert program == b'\x48\xb8' + struct.pack('<Q', bits) + b'\x66\x48\x0f\x6e\xc8\x66\x0f\x57\xc1'
        if role != 2: assert relocation == addend == 0
        assert not program or program in sections['.text']
    actual = Counter()
    for relocation in relas.get('.text', []):
        symbol = relocation['symbol']
        if not symbol['name'].startswith('.LCF'): continue
        assert relocation['kind'] == 2 and relocation['addend'] == -4 and symbol['section'] == '.rodata' and symbol['size'] == 8
        offset = relocation['offset']; assert sections['.text'][offset - 4:offset] == b'\xf2\x0f\x10\x05'
        actual[struct.unpack_from('<Q', sections['.rodata'], symbol['value'])[0]] += 1
    assert actual == pool, (actual, pool)
    return roles, origins, bulk_roles


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('compiler', type=pathlib.Path)
    parser.add_argument('--baseline', type=pathlib.Path)
    parser.add_argument('--execution-profile', choices=('native', 'emulated', 'cross')); args = parser.parse_args()
    compiler = args.compiler.resolve(); identity = digest(compiler)
    linux = platform.system() == 'Linux' and platform.machine() == 'x86_64'
    profile = args.execution_profile or ('native' if linux else 'cross'); execute = profile in ('native', 'emulated'); assert not execute or linux
    files = sorted(pathlib.Path('source').glob('*.[ch]')) + sorted(pathlib.Path('runtime/include').rglob('*.h'))
    sources = sorted(pathlib.Path('tests/machine_material').glob('*.c'))
    files += sources + [pathlib.Path(name) for name in ('tests/test_mir_material.py', 'tests/mir_material_probe.c', 'tests/test_unwind.py', 'CMakeLists.txt')]
    inputs = {str(path): digest(path) for path in files}; source_hash = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()
    base = pathlib.Path('.agent-local/mir-material') / (source_hash + '-' + identity); base.mkdir(parents=True, exist_ok=True)
    root = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=base)).resolve()
    tools = {name: tool(name) for name in ('llvm-objdump', 'llvm-mc')}
    tools['gcc'] = tool('gcc-15' if shutil.which('gcc-15') else 'gcc'); tools['clang'] = tool('clang')
    if execute: tools['cc'] = tool('cc')
    identities = {name: dict(path=path, sha256=digest(pathlib.Path(path))) for name, path in tools.items()}
    if args.baseline: args.baseline = args.baseline.resolve(); baseline_hash = digest(args.baseline)
    artifacts = set(); commands = []; observations = []; roles = set(); origins = set(); bulk_roles = set()
    def run(argv, label, expected=0):
        result = subprocess.run(list(map(str, argv)), capture_output=True, timeout=120)
        out, err = root / (label + '.stdout'), root / (label + '.stderr'); out.write_bytes(result.stdout); err.write_bytes(result.stderr)
        artifacts.update((out, err)); commands.append(dict(argv=list(map(str, argv)), exit=result.returncode, stdout=digest(out), stderr=digest(err)))
        (root / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        assert result.returncode == expected, (argv, result)
        return result.stdout.decode(errors='replace')
    for name, path in tools.items(): run([path, '--version'], name + '-version')
    core_inputs = {}
    if execute:
        probe, obj = root / 'mir-material-probe', root / 'mir-material-probe.o'
        run([compiler, '-O0', '-c', '-Isource', '-Iruntime/include', 'tests/mir_material_probe.c', '-o', obj], 'probe-compile')
        core = [compiler.parent / 'libcinder_core.a'] if (compiler.parent / 'libcinder_core.a').is_file() else sorted(p for p in compiler.parent.glob('*.o') if p.name not in ('main.o', 'ir_main.o'))
        assert core
        core_inputs = {str(path): digest(path) for path in core}
        linker, flags = tools['cc'], []; cache = compiler.parent / 'CMakeCache.txt'
        if cache.is_file() and 'CINDER_SANITIZE:BOOL=ON' in cache.read_text().splitlines():
            linker = next(line.split('=', 1)[1] for line in cache.read_text().splitlines() if line.startswith('CMAKE_C_COMPILER:'))
            flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        run([linker, '-no-pie', *flags, obj, *core, '-o', probe], 'probe-link'); artifacts.update((probe, obj))
    else:
        probe = compiler.parent / 'cinder_mir_material_probe'; assert probe.is_file()
        library = compiler.parent / 'libcinder_core.a'
        if library.is_file(): core_inputs[str(library)] = digest(library)
    cache = compiler.parent / 'CMakeCache.txt'
    if cache.is_file(): core_inputs[str(cache)] = digest(cache)
    probe_hash = digest(probe)
    for source in sources:
        probe_obj = root / (source.stem + '.probe.o')
        result = json.loads(run([probe, source, probe_obj], source.stem + '-probe'))
        assert result['rejected'] == 15 * (result['materials'] + result['bulk'] + result['markers']); artifacts.add(probe_obj)
        observations.append(dict(source=str(source), mutations=result))
        for level in ('-O0', '-O2'):
            stem = source.stem + level; obj, asm, assembled = [root / (stem + suffix) for suffix in ('.o', '.s', '.assembled.o')]
            run([compiler, level, '-fverify-each', '-c', source, '-o', obj], stem + '-compile')
            if level == '-O0': assert obj.read_bytes() == probe_obj.read_bytes()
            if args.baseline:
                baseline = root / (stem + '.baseline.o'); run([args.baseline, level, '-c', source, '-o', baseline], stem + '-baseline')
                assert obj.read_bytes() == baseline.read_bytes(); artifacts.add(baseline)
            dump = run([compiler, level, '--dump-mir', source], stem + '-dump')
            sections, _, relas = elf(obj); found, used, bulks = validate_dump(dump, sections, relas); roles |= found; origins |= used; bulk_roles |= bulks
            run([tools['llvm-objdump'], '-dr', obj], stem + '-disassemble')
            run([compiler, level, '-S', source, '-o', asm], stem + '-assembly')
            run([tools['llvm-mc'], '-triple=x86_64-linux-gnu', '-filetype=obj', asm, '-o', assembled], stem + '-assemble')
            reference, _, other = elf(assembled)
            assert sections['.text'] == reference['.text'] and normalized(relas.get('.text', [])) == normalized(other.get('.text', []))
            artifacts.update((obj, asm, assembled))
    assert roles == set(range(1, 8)) and bulk_roles == {1, 2}
    assert {'const', 'fconst', 'undef', 'copy', 'fneg', 'zero.init', 'object.copy', 'object.init', 'va.copy', 'va.end'} <= origins
    for source in sources:
        names, expected, label = (source.stem,), 0, source.stem
        units = [source]
        joined = root / (label + '-joined.c'); joined.write_bytes(b'\n'.join(unit.read_bytes() for unit in units)); artifacts.add(joined)
        for level in ('-O0', '-O2'):
            assert run([compiler, level, '--interpret', joined], label + level + '-interpret') == f'interpret main => {expected}\n'
            for reference in ('gcc', 'clang'):
                executable = root / (reference + label + level)
                run([tools[reference], '-std=c17', level, *units, '-o', executable], executable.name + '-compile')
                run([executable], executable.name + '-execute', expected); artifacts.add(executable)
            if execute:
                executable = root / ('cinder' + label + level)
                run([tools['cc'], '-no-pie', *[root / (name + level + '.o') for name in names], '-o', executable], executable.name + '-link')
                run([executable], executable.name + '-execute', expected); artifacts.add(executable)
    assert digest(compiler) == identity and digest(probe) == probe_hash and inputs == {name: digest(pathlib.Path(name)) for name in inputs}
    assert all(digest(pathlib.Path(value['path'])) == value['sha256'] for value in identities.values())
    assert all(digest(pathlib.Path(name)) == value for name, value in core_inputs.items())
    if args.baseline: assert digest(args.baseline) == baseline_hash
    report = dict(schema=1, compiler_sha256=identity, source_sha256=source_hash, inputs=inputs, core_inputs=core_inputs, tools=identities, probe_sha256=probe_hash,
                  baseline_sha256=baseline_hash if args.baseline else None, execution_profile=profile, native=profile == 'native', target_execution=execute,
                  observations=observations, roles=sorted(roles), bulk_roles=sorted(bulk_roles), origins=sorted(origins), commands=commands,
                  artifacts={str(path.relative_to(root)): digest(path) for path in sorted(artifacts)})
    (root / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Machine materialization: seven value roles, zero/copy memory programs, restored objects and two reference programs passed; native={report["native"]}; evidence={root}')


if __name__ == '__main__': main()
