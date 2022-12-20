#!/usr/bin/env python3
"""Check original DWARF frame rows against actual encoded instructions."""
import argparse
import bisect
import hashlib
import json
import pathlib
import platform
import re
import shutil
import struct
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tool(name):
    path = shutil.which(name)
    if path is None:
        candidate = pathlib.Path('/opt/homebrew/opt/llvm/bin') / name
        if candidate.is_file(): path = str(candidate)
    assert path, f'{name} is required for independent debug validation'
    return str(pathlib.Path(path).resolve())


def elf(path):
    data = path.read_bytes()
    header = struct.unpack_from('<16sHHIQQQIHHHHHH', data)
    assert header[0][:7] == b'\x7fELF\x02\x01\x01' and header[2] == 62
    at, width, count, names = header[6], header[11], header[12], header[13]
    assert width == 64 and at + width * count <= len(data)
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, at + width * i) for i in range(count)]
    strings = data[sections[names][4]:sections[names][4] + sections[names][5]]
    def string(table, offset): return table[offset:table.index(0, offset)].decode()
    labels = [string(strings, s[0]) for s in sections]
    contents = {name: data[s[4]:s[4] + s[5]] for name, s in zip(labels, sections) if s[1] != 8}
    symbols = []
    symtab = sections[labels.index('.symtab')]
    table = sections[symtab[6]]
    text = data[table[4]:table[4] + table[5]]
    for i in range(symtab[5] // 24):
        name, info, _, index, value, size = struct.unpack_from('<IBBHQQ', data, symtab[4] + i * 24)
        symbols.append(dict(name=string(text, name), kind=info & 15, section=labels[index], value=value, size=size))
    relocations = {}
    for name, s in zip(labels, sections):
        if s[1] != 4: continue
        assert s[9] == 24 and s[5] % 24 == 0
        target = labels[s[7]]
        entries = []
        for i in range(s[5] // 24):
            offset, info, addend = struct.unpack_from('<QQq', data, s[4] + i * 24)
            symbol = symbols[info >> 32]
            kind = info & 0xffffffff
            assert offset + (8 if kind == 1 else 4) <= sections[s[7]][5]
            entries.append(dict(offset=offset, kind=kind, symbol=symbol, addend=addend))
        relocations[target] = entries
    return contents, symbols, relocations


def leb(data, at, signed=False):
    value = shift = 0
    while True:
        byte = data[at]; at += 1
        value |= (byte & 127) << shift; shift += 7
        assert shift <= 70
        if not byte & 128: break
    if signed and byte & 64: value -= 1 << shift
    return value, at


def cfi(data, initial, text=None):
    cfa, rules = initial[0], dict(initial[1])
    at = pc = 0
    saved = []
    rows = {0: (cfa, dict(rules))}
    while at < len(data):
        op = data[at]; at += 1
        if op == 0: continue
        if op == 4:
            delta = struct.unpack_from('<I', data, at)[0]; at += 4
            pc += delta
            if text is not None: assert pc <= len(text)
        elif op == 12:
            reg, at = leb(data, at); offset, at = leb(data, at); cfa = (reg, offset)
        elif op == 13:
            reg, at = leb(data, at); cfa = (reg, cfa[1])
        elif op == 14:
            offset, at = leb(data, at); cfa = (cfa[0], offset)
        elif op == 8 or op & 192 == 128:
            if op == 8: reg, at = leb(data, at); location = 'same'
            else:
                reg = op & 63; factor, at = leb(data, at); location = -8 * factor
            if text is not None and reg in range(12, 16) and location != rules.get(reg):
                if location == 'same':
                    assert text[pc - 3:pc] == bytes((0x4d, 0x89, 0xd4 + reg - 12)), (pc, reg)
                    assert text[pc - 10:pc - 7] == b'\x4c\x8b\x95'
                    slot = struct.unpack_from('<i', text, pc - 7)[0]
                    assert rules[reg] == slot - 16, (pc, reg, rules[reg], slot)
                else:
                    assert text[pc - 10:pc - 7] == bytes((0x4c, 0x89, 0xe0 + 8 * (reg - 12))), (pc, reg)
                    assert text[pc - 7:pc - 4] == b'\x48\x89\x85'
                    assert location == struct.unpack_from('<i', text, pc - 4)[0] - 16
            rules[reg] = location
        elif op == 10: saved.append((cfa, dict(rules)))
        elif op == 11:
            assert saved and text is not None and text[pc - 1] == 0xc3
            cfa, rules = saved.pop()
        else: raise AssertionError(('unsupported frame opcode', op))
        rows[pc] = (cfa, dict(rules))
    assert not saved
    return rows


def validate(path, disassembly):
    sections, symbols, relas = elf(path)
    data, text = sections['.debug_frame'], sections['.text']
    by_offset = {r['offset']: r for r in relas['.debug_frame']}
    cie_length, identity = struct.unpack_from('<II', data)
    assert identity == 0xffffffff and data[8:12] == b'\x04\x00\x08\x00'
    code_align, at = leb(data, 12); data_align, at = leb(data, at, True); ret, at = leb(data, at)
    assert (code_align, data_align, ret) == (1, -8, 16)
    cie_rows = cfi(data[at:4 + cie_length], ((0, 0), {}))
    initial = cie_rows[0]
    assert initial == ((7, 8), {16: -8, 6: 'same', 12: 'same', 13: 'same', 14: 'same', 15: 'same'})
    instructions = [(int(m[1], 16), m[2]) for m in re.finditer(r'^\s*([0-9a-f]+):\s+([a-z][a-z0-9]*)', disassembly, re.M)]
    at = 4 + cie_length; observations = []
    while at < len(data):
        length, cie = struct.unpack_from('<II', data, at)
        assert length >= 20 and at + 4 + length <= len(data) and cie == 0
        cie_reloc, address = by_offset[at + 4], by_offset[at + 8]
        assert cie_reloc['kind'] == 10 and cie_reloc['symbol']['section'] == '.debug_frame' and cie_reloc['addend'] == 0
        assert address['kind'] == 1 and address['symbol']['kind'] == 2 and address['addend'] == 0
        symbol = address['symbol']; start, size = symbol['value'], symbol['size']
        assert struct.unpack_from('<Q', data, at + 16)[0] == size and size > 0
        code = text[start:start + size]
        assert code[:4] == b'\x55\x48\x89\xe5'
        rows = cfi(data[at + 24:at + 4 + length], initial, code)
        assert rows[1][0] == (7, 16) and rows[1][1][6] == -16
        assert rows[4][0] == (6, 16) and rows[4][1][6] == -16
        positions = sorted(rows)
        leaves = returns = 0
        for pc, mnemonic in instructions:
            if not start <= pc < start + size: continue
            offset = pc - start
            state = rows[positions[bisect.bisect_right(positions, offset) - 1]]
            if mnemonic.startswith('leave'):
                leaves += 1
                assert state[0] == (6, 16)
                assert rows[offset + 1][0] == (7, 8) and rows[offset + 1][1][6] == 'same'
            if mnemonic.startswith('ret'):
                returns += 1
                assert state[0] == (7, 8) and state[1][6] == 'same'
                assert all(state[1][reg] == 'same' for reg in range(12, 16))
        assert leaves == returns and returns > 0
        observations.append(dict(name=symbol['name'], bytes=size, returns=returns, rows=len(rows)))
        at += 4 + length
    functions = [s for s in symbols if s['kind'] == 2 and s['section'] == '.text']
    assert len(observations) == len(functions) and len(by_offset) == len(functions) * 2
    return observations


def normalized(relocations):
    return sorted((r['offset'], r['kind'], r['symbol']['section'], r['symbol']['value'] + r['addend']) for r in relocations)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('compiler', type=pathlib.Path)
    parser.add_argument('--baseline', type=pathlib.Path)
    parser.add_argument('--execution-profile', choices=('native', 'emulated', 'cross'))
    args = parser.parse_args()
    compiler = args.compiler.resolve(); identity = digest(compiler)
    sources = [pathlib.Path('tests/debug') / name for name in ('unwind.c', 'unwind_provider.c', 'unwind_caller.c')]
    tools = {name: tool(name) for name in ('llvm-dwarfdump', 'llvm-objdump', 'llvm-mc')}
    input_files = sorted(pathlib.Path('source').glob('*.[ch]')) + sorted(pathlib.Path('runtime/include').rglob('*.h'))
    input_files += sources + [pathlib.Path(__file__), pathlib.Path('tests/unwind_probe.c'), pathlib.Path('CMakeLists.txt')]
    inputs = {str(p): digest(p) for p in input_files}
    source_identity = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()
    root = pathlib.Path('.agent-local/unwind') / (source_identity + '-' + identity)
    root.mkdir(parents=True, exist_ok=True)
    commands = []; observations = []; artifacts = []
    def run(argv, stem, expected=0):
        result = subprocess.run([str(x) for x in argv], capture_output=True, timeout=30)
        output = root / (stem + '.stdout'); error = root / (stem + '.stderr')
        output.write_bytes(result.stdout); error.write_bytes(result.stderr); artifacts.extend((output, error))
        commands.append(dict(argv=[str(x) for x in argv], exit=result.returncode, stdout=digest(output), stderr=digest(error)))
        assert result.returncode == expected, (argv, result)
        return result.stdout.decode(errors='replace')
    linux = platform.system() == 'Linux' and platform.machine() == 'x86_64'
    execution_profile = args.execution_profile or ('native' if linux else 'cross')
    execute_target = execution_profile in ('native', 'emulated')
    assert not execute_target or linux, 'target execution requires Linux x86-64'
    native = execution_profile == 'native'
    references = [shutil.which('gcc-15') or shutil.which('gcc'), shutil.which('clang')]
    assert all(references), 'GCC and Clang references are required'
    for role, path in zip(('reference-gcc', 'reference-clang'), references): tools[role] = str(pathlib.Path(path).resolve())
    if execute_target:
        for role, name in (('link-driver', 'cc'), ('debugger', 'gdb')):
            path = shutil.which(name); assert path, name + ' is required'
            tools[role] = str(pathlib.Path(path).resolve())
    tool_identities = {role: dict(path=path, sha256=digest(pathlib.Path(path))) for role, path in tools.items()}
    baseline_identity = digest(args.baseline.resolve()) if args.baseline else None
    for role, path in tools.items(): run([path, '--version'], role + '-version')
    for reference in references:
        for level in ('-O0', '-O2'):
            for label, reference_sources, expected in (('single', sources[:1], 24), ('multi', sources[1:], 11)):
                stem = pathlib.Path(reference).name + label + level
                executable = root / stem
                run([reference, '-std=c17', level, *reference_sources, '-o', executable], stem + '-reference-build')
                run([executable], stem + '-reference-execute', expected)
                artifacts.append(executable)
    for level in ('-O0', '-O2'):
        objects = []
        for source in sources:
            stem = source.stem + level
            obj, assembly, assembled, plain = [root / (stem + suffix) for suffix in ('.o', '.s', '.assembled.o', '.plain.o')]
            run([compiler, level, '-g', '-fverify-each', '-c', source, '-o', obj], stem + '-compile')
            run([compiler, level, '-g', '-S', source, '-o', assembly], stem + '-assembly')
            run([tools['llvm-mc'], '-triple=x86_64-linux-gnu', '-filetype=obj', assembly, '-o', assembled], stem + '-assemble')
            disassembly = run([tools['llvm-objdump'], '--no-show-raw-insn', '-d', obj], stem + '-disassembly')
            frames = validate(obj, disassembly)
            frame_dump = run([tools['llvm-dwarfdump'], '--debug-frame', obj], stem + '-frames')
            assert 'Version:               4' in frame_dump and 'DW_CFA_def_cfa_register: RBP' in frame_dump
            run([tools['llvm-dwarfdump'], '--verify', obj], stem + '-verify')
            original, _, relas = elf(obj); reference, _, other_relas = elf(assembled)
            assert original['.debug_frame'] == reference['.debug_frame'] and original['.text'] == reference['.text']
            assert normalized(relas['.debug_frame']) == normalized(other_relas['.debug_frame'])
            run([compiler, level, '-c', source, '-o', plain], stem + '-plain')
            no_debug, _, _ = elf(plain)
            assert '.debug_frame' not in no_debug and no_debug['.text'] == original['.text']
            if args.baseline:
                baseline = root / (stem + '.baseline.o')
                run([args.baseline.resolve(), level, '-c', source, '-o', baseline], stem + '-baseline')
                assert plain.read_bytes() == baseline.read_bytes()
                artifacts.append(baseline)
            observations.append(dict(source=str(source), level=level, functions=frames, object=digest(obj), assembled=digest(assembled)))
            artifacts.extend((obj, assembly, assembled, plain)); objects.append(obj)
        if execute_target:
            linker, debugger = shutil.which('cc'), shutil.which('gdb')
            assert linker and debugger
            for stem, inputs_to_link, expected in (('single', objects[:1], 24), ('multi', objects[1:], 11)):
                executable = root / (stem + level)
                run([linker, '-no-pie', *inputs_to_link, '-o', executable], stem + level + '-link')
                run([executable], stem + level + '-execute', expected)
                target = 'unwind_leaf' if stem == 'single' else 'unwind_cross'
                recording = run([debugger, '-nx', '-batch', '-ex', 'set pagination off', '-ex', 'set disable-randomization off', '-ex', 'set debug frame 1',
                                 '-ex', f'break *{target}+4', '-ex', 'run', '-ex', 'bt', '-ex', 'info frame', executable], stem + level + '-gdb')
                assert target in recording and re.search(r'#\d+.*\bmain\b', recording)
                if stem == 'single': assert 'unwind_middle' in recording
                assert 'unwinder="dwarf2"' in (root / (stem + level + '-gdb.stderr')).read_text()
                artifacts.append(executable)
    if execute_target:
        probe = root / 'unwind-probe'; probe_object = root / 'unwind-probe.o'
        run([compiler, '-O0', '-c', '-Isource', '-Iruntime/include', 'tests/unwind_probe.c', '-o', probe_object], 'probe-compile')
        core = [compiler.parent / 'libcinder_core.a'] if (compiler.parent / 'libcinder_core.a').is_file() else sorted(p for p in compiler.parent.glob('*.o') if p.name not in ('main.o', 'ir_main.o'))
        assert core, 'missing actual core objects for unwind probe'
        linker, profile = 'cc', []
        cache = compiler.parent / 'CMakeCache.txt'
        if cache.is_file() and 'CINDER_SANITIZE:BOOL=ON' in cache.read_text().splitlines():
            linker = next(line.split('=', 1)[1] for line in cache.read_text().splitlines() if line.startswith('CMAKE_C_COMPILER:FILEPATH='))
            profile = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        run([linker, '-no-pie', *profile, probe_object, *core, '-o', probe], 'probe-link')
        artifacts.extend((probe, probe_object))
    else:
        probe = compiler.parent / 'cinder_unwind_probe'
        assert probe.is_file(), 'build the cinder_unwind_probe target first'
    sentinel = root / 'previous.o'
    probe_identity = digest(probe)
    output = run([probe, sentinel], 'probe-execute')
    assert output == 'Unwind output: twelve malformed section/relocation records rejected; prior output preserved\n'
    assert sentinel.read_bytes() == b'previous complete object\n\0'
    assert not list(root.glob('*.cinder-*'))
    artifacts.append(sentinel)
    assert digest(compiler) == identity and inputs == {name: digest(pathlib.Path(name)) for name in inputs}, 'unwind campaign input changed'
    assert digest(probe) == probe_identity
    assert all(digest(pathlib.Path(record['path'])) == record['sha256'] for record in tool_identities.values()), 'unwind tools changed during execution'
    if args.baseline: assert digest(args.baseline.resolve()) == baseline_identity
    report = dict(schema=1, compiler_sha256=identity, source_sha256=source_identity, inputs=inputs,
                  tools=tool_identities, baseline_sha256=baseline_identity,
                  api_probe=dict(path=str(probe), sha256=probe_identity),
                  native=native, execution_profile=execution_profile, target_execution=execute_target,
                  observations=observations, commands=commands, rejected_records=12,
                  artifacts={str(p.relative_to(root)): digest(p) for p in artifacts})
    (root / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Unwind: {len(observations)} original/assembled frame tables checked against instructions; native={native}; evidence={root}')


if __name__ == '__main__': main()
