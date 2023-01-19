#!/usr/bin/env python3
"""Read actual branch/frame encodings and execute scalar, cyclic and variadic CFGs."""
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


def controls(text):
    roles = set()
    for line in text.splitlines():
        if not line.startswith('  control='): continue
        match = re.fullmatch(r'  control=(\d+) bank=(\d+) value=(\d+) fixed=([0-9a-f]+) clobbers=([0-9a-f]+) prefix=([0-9a-f]*)(.*)', line)
        assert match, line
        kind, bank, value = map(int, match.groups()[:3]); fixed, clobbers = map(lambda s: int(s, 16), match.groups()[3:5])
        prefix = bytes.fromhex(match[6]); edges = re.findall(r' edge(\d+)=(\d+):([0-9a-f]+)', match[7]); roles.add(kind)
        if kind == 0: assert (bank, value, fixed, clobbers, prefix, edges) == (0, 0xffffffff, 0, 0, b'\x0f\x0b', [])
        elif kind == 1:
            assert not edges and clobbers == 0x430 | (1 if bank == 1 else 1 << 16 if bank == 2 else 0) and prefix in (b'', b'\xf2\x0f\x5a\xc0')
            assert bank in (0, 1, 2) and fixed == 0x30 | (1 if bank == 1 else 1 << 16 if bank == 2 else 0)
            assert (value == 0xffffffff) == (bank == 0)
        elif kind == 2: assert (bank, value, fixed, clobbers, prefix) == (0, 0xffffffff, 0, 0, b'') and len(edges) == 1 and edges[0][::2] == ('0', 'e9')
        else:
            assert kind == 3 and bank == 1 and value != 0xffffffff and fixed == 1 and clobbers == (1 << 32) | 1 and prefix == b'\x48\x83\xf8\x00'
            assert len(edges) == 2 and edges[0][::2] == ('0', '0f85') and edges[1][::2] == ('1', 'e9')
    return roles


def frames(text, path, disassembly):
    sections, symbols, _ = elf(path); code = sections['.text']
    functions = {s['name']: s for s in symbols if s['kind'] == 2 and s['section'] == '.text'}
    instructions = {int(m[1], 16): m[2] for m in re.finditer(r'^\s*([0-9a-f]+):\s+([a-z][^\n]*)', disassembly, re.M)}
    rows = []; seen = set()
    for part in re.split(r'(?=^allocation function=)', text, flags=re.M):
        if not part.startswith('allocation function='): continue
        header = re.match(r'allocation function=(\S+) frame=(\d+) spills=(\d+)', part); assert header
        name, size, spills = header[1], int(header[2]), int(header[3]); symbol = functions[name]; begin, end = symbol['value'], symbol['value'] + symbol['size']; seen.add(name)
        frame = re.search(r'frame-program size=(\d+) incoming-base=(\d+) variadic=(-?\d+) prologue=([0-9a-f]+) epilogue=([0-9a-f]+)', part); assert frame
        extent, incoming, variadic = map(int, frame.groups()[:3]); prologue, epilogue = map(bytes.fromhex, frame.groups()[3:])
        assert extent == size and size >= 112 and size % 16 == 0 and incoming * 8 + 112 <= size
        assert variadic in (0, -size) and (variadic == 0 or incoming * 8 + 288 <= size)
        assert prologue == b'\x55\x48\x89\xe5\x48\x81\xec' + struct.pack('<I', size) and epilogue == b'\xc9\xc3'
        assert code[begin:begin + len(prologue)] == prologue
        saves = []; restores = []; offsets = []
        for match in re.finditer(r'preserve reg=(\d+) offset=(-?\d+) save=([0-9a-f]+) restore=([0-9a-f]+)', part):
            reg, offset = int(match[1]), int(match[2]); save, restore = bytes.fromhex(match[3]), bytes.fromhex(match[4])
            assert reg in range(12, 16) and -size <= offset < 0 and offset % 8 == 0
            assert save == bytes((0x4c, 0x89, 0xc0 | ((reg - 8) << 3), 0x48, 0x89, 0x85)) + struct.pack('<i', offset)
            assert restore == b'\x4c\x8b\x95' + struct.pack('<i', offset) + bytes((0x4d, 0x89, 0xd0 | (reg - 8)))
            saves.append(save); restores.append(restore); offsets.append(offset)
        assert len(set(offsets)) == len(offsets) and offsets == sorted(offsets, reverse=True)
        saved = b''.join(saves); assert code[begin + 11:begin + 11 + len(saved)] == saved
        returns = branches = 0
        for pc, instruction in instructions.items():
            if not begin <= pc < end: continue
            if instruction.startswith('ret'):
                suffix = b''.join(restores) + epilogue
                assert code[pc + 1 - len(suffix):pc + 1] == suffix; returns += 1
            if re.match(r'j(?:mp|ne)\s', instruction):
                width = 2 if code[pc:pc + 2] == b'\x0f\x85' else 1
                assert code[pc:pc + width] in (b'\xe9', b'\x0f\x85')
                target = pc + width + 4 + struct.unpack_from('<i', code, pc + width)[0]
                assert begin <= target < end and target in instructions, (name, pc, target); branches += 1
        rows.append(dict(function=name, frame=size, saved=len(saves), variadic=variadic != 0, spills=spills, returns=returns, branches=branches))
    assert seen == set(functions)
    return rows


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('compiler', type=pathlib.Path); parser.add_argument('--baseline', type=pathlib.Path)
    parser.add_argument('--execution-profile', choices=('native', 'emulated', 'cross')); args = parser.parse_args()
    compiler = args.compiler.resolve(); identity = digest(compiler); linux = platform.system() == 'Linux' and platform.machine() == 'x86_64'
    profile = args.execution_profile or ('native' if linux else 'cross'); execute = profile in ('native', 'emulated'); assert not execute or linux
    sources = sorted(pathlib.Path('tests/machine_control').glob('*.c')) + [pathlib.Path('tests/machine_address/pressure.c')]
    files = sorted(pathlib.Path('source').glob('*.[ch]')) + sorted(pathlib.Path('runtime/include').rglob('*.h')) + sources
    files += [pathlib.Path(p) for p in ('tests/test_mir_control.py', 'tests/mir_control_probe.c', 'tests/test_unwind.py', 'CMakeLists.txt')]
    inputs = {str(p): digest(p) for p in files}; source_hash = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()
    base = pathlib.Path('.agent-local/mir-control') / (source_hash + '-' + identity); base.mkdir(parents=True, exist_ok=True)
    root = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=base)).resolve(); artifacts = set(); commands = []; observations = []; roles = set()
    paths = {name: tool(name) for name in ('llvm-objdump', 'llvm-mc', 'clang')}; paths['gcc'] = tool('gcc-15' if shutil.which('gcc-15') else 'gcc')
    if execute: paths['cc'] = tool('cc')
    tools = {name: dict(path=path, sha256=digest(pathlib.Path(path))) for name, path in paths.items()}
    def run(argv, label, expected=0):
        argv = list(map(str, argv)); result = subprocess.run(argv, capture_output=True, timeout=120)
        out, err = root / (label + '.stdout'), root / (label + '.stderr'); out.write_bytes(result.stdout); err.write_bytes(result.stderr); artifacts.update((out, err))
        commands.append(dict(argv=argv, exit=result.returncode, stdout=digest(out), stderr=digest(err))); (root / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        assert result.returncode == expected, (argv, result); return result.stdout.decode(errors='replace')
    for name, path in paths.items(): run([path, '--version'], name + '-version')
    core_inputs = {}
    if execute:
        probe, obj = root / 'mir-control-probe', root / 'mir-control-probe.o'
        run([compiler, '-O0', '-c', '-Isource', '-Iruntime/include', 'tests/mir_control_probe.c', '-o', obj], 'probe-compile')
        core = [compiler.parent / 'libcinder_core.a'] if (compiler.parent / 'libcinder_core.a').is_file() else sorted(p for p in compiler.parent.glob('*.o') if p.name not in ('main.o', 'ir_main.o'))
        assert core; core_inputs = {str(p): digest(p) for p in core}; linker, flags = paths['cc'], []; cache = compiler.parent / 'CMakeCache.txt'
        if cache.is_file() and 'CINDER_SANITIZE:BOOL=ON' in cache.read_text().splitlines():
            linker = next(line.split('=', 1)[1] for line in cache.read_text().splitlines() if line.startswith('CMAKE_C_COMPILER:')); flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        run([linker, '-no-pie', *flags, obj, *core, '-o', probe], 'probe-link'); artifacts.update((probe, obj))
    else:
        probe = compiler.parent / 'cinder_mir_control_probe'; assert probe.is_file()
        library = compiler.parent / 'libcinder_core.a'
        if library.is_file(): core_inputs[str(library)] = digest(library)
    cache = compiler.parent / 'CMakeCache.txt'
    if cache.is_file(): core_inputs[str(cache)] = digest(cache)
    probe_hash = digest(probe); baseline_hash = digest(args.baseline.resolve()) if args.baseline else None
    for source in sources:
        restored = root / (source.stem + '.restored.o'); result = json.loads(run([probe, source, restored], source.stem + '-probe')); artifacts.add(restored)
        assert result['rejected'] == 12 * result['controls'] + 18 * result['frames'] and result['frames'] > 0
        observations.append(dict(source=str(source), mutations=result))
        for level in ('-O0', '-O2'):
            stem = source.stem + level; obj, asm, assembled = [root / (stem + suffix) for suffix in ('.o', '.s', '.assembled.o')]
            run([compiler, level, '-fverify-each', '-c', source, '-o', obj], stem + '-compile')
            if level == '-O0': assert obj.read_bytes() == restored.read_bytes()
            if args.baseline:
                baseline = root / (stem + '.baseline.o'); run([args.baseline.resolve(), level, '-c', source, '-o', baseline], stem + '-baseline'); artifacts.add(baseline); assert obj.read_bytes() == baseline.read_bytes()
            roles |= controls(run([compiler, level, '--dump-mir', source], stem + '-mir'))
            disassembly = run([paths['llvm-objdump'], '-d', '--no-show-raw-insn', obj], stem + '-disassemble')
            allocation = run([compiler, level, '--dump-regalloc', source], stem + '-allocation')
            observations.append(dict(source=str(source), level=level, frames=frames(allocation, obj, disassembly)))
            run([compiler, level, '-S', source, '-o', asm], stem + '-assembly'); run([paths['llvm-mc'], '-triple=x86_64-linux-gnu', '-filetype=obj', asm, '-o', assembled], stem + '-assemble')
            original, _, relas = elf(obj); other, _, relocs = elf(assembled)
            assert original['.text'] == other['.text'] and normalized(relas.get('.text', [])) == normalized(relocs.get('.text', [])); artifacts.update((obj, asm, assembled))
    assert roles == {0, 1, 2, 3}
    all_frames = [frame for row in observations for frame in row.get('frames', [])]
    assert any(f['spills'] for f in all_frames) and any(f['variadic'] for f in all_frames) and any(f['frame'] > 16384 for f in all_frames) and any(f['returns'] > 1 for f in all_frames)
    for label, units, expected in (('control', [sources[0]], 0), ('trap', [p for p in sources if p.stem in ('trap', 'trap_driver')], 0), ('pressure', [sources[-1]], 133)):
        assert (label == 'control' and units[0].stem == 'control') or label != 'control'
        for level in ('-O0', '-O2'):
            if label != 'trap': assert run([compiler, level, '--interpret', units[0]], label + level + '-interpret') == f'interpret main => {expected}\n'
            for reference in ('gcc', 'clang', *(['cinder'] if execute else [])):
                executable = root / (reference + label + level)
                if reference == 'cinder': run([paths['cc'], '-no-pie', *[root / (p.stem + level + '.o') for p in units], '-o', executable], executable.name + '-link')
                else: run([paths[reference], '-std=c17', level, *units, '-o', executable], executable.name + '-compile')
                run([executable], executable.name + '-execute', expected); artifacts.add(executable)
                if label == 'trap': run([executable, 'halt'], executable.name + '-noreturn', 77)
    assert digest(compiler) == identity and digest(probe) == probe_hash and inputs == {name: digest(pathlib.Path(name)) for name in inputs}
    assert all(digest(pathlib.Path(row['path'])) == row['sha256'] for row in tools.values()) and all(digest(pathlib.Path(name)) == value for name, value in core_inputs.items())
    if args.baseline: assert digest(args.baseline.resolve()) == baseline_hash
    report = dict(schema=1, compiler_sha256=identity, source_sha256=source_hash, inputs=inputs, core_inputs=core_inputs, tools=tools, probe_sha256=probe_hash, baseline_sha256=baseline_hash,
                  execution_profile=profile, native=profile == 'native', target_execution=execute, roles=sorted(roles), observations=observations, commands=commands,
                  artifacts={str(p.relative_to(root)): digest(p) for p in sorted(artifacts)})
    (root / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Machine control: four roles, physical frames, malformed-plan rejection and reference programs passed; native={report["native"]}; evidence={root}')


if __name__ == '__main__': main()
