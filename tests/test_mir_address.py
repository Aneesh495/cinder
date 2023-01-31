#!/usr/bin/env python3
"""Check selected address programs, restored plans and cross-unit relocations."""
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


def validate_dump(text, code):
    roles = set(); origins = set()
    for line in text.splitlines():
        if not line.startswith('  machine-kind=8 '): continue
        match = re.search(r'machine-kind=8 origin=(\S+).* fixed=([0-9a-f]+) clobbers=([0-9a-f]+).* address=(\d+) slot=(-?\d+) scale=(-?\d+) displacement=(-?\d+) relocation=(\d+) addend=(-?\d+) before=([0-9a-f]+) after=([0-9a-f]*)$', line)
        assert match, line
        origin, fixed, clobbers, role, slot, scale, displacement, relocation, addend, before, after = match.groups()
        fixed, clobbers = int(fixed, 16), int(clobbers, 16)
        role, slot, scale, displacement, relocation, addend = map(int, (role, slot, scale, displacement, relocation, addend))
        before, after = bytes.fromhex(before), bytes.fromhex(after); flags = 1 << 32
        if role == 1:
            assert origin == 'local.address' and slot >= 0 and scale == displacement == relocation == addend == 0
            assert before == b'\x48\x8d\x85' and after == b'' and (fixed, clobbers) == (32, 1)
        elif role == 2:
            assert origin in ('global.address', 'function.address') and slot == -1 and scale == displacement == 0 and (relocation, addend) == (2, -4)
            assert before == b'\x48\x8d\x05' and after == b'' and (fixed, clobbers) == (0, 1)
        elif role == 3:
            assert origin == 'pointer.member' and slot == -1 and scale == relocation == addend == 0 and 0 <= displacement <= 0x7fffffff
            assert before == b'\x48\x05' + struct.pack('<i', displacement) and after == b'' and (fixed, clobbers) == (1, flags | 1)
        elif role == 4:
            assert origin == 'pointer.offset' and slot == -1 and scale > 0 and displacement == relocation == addend == 0
            assert before == b'\x49\xba' + struct.pack('<Q', scale) + b'\x49\x0f\xaf\xc2'
            assert after in (b'\x4c\x01\xd0', b'\x48\xf7\xd8\x4c\x01\xd0') and (fixed, clobbers) == (0x401, flags | 0x401)
        else:
            assert role == 5 and origin == 'pointer.diff' and slot == -1 and scale > 0 and displacement == relocation == addend == 0
            assert before == b'\x4c\x29\xd0\x48\x99\x49\xba' + struct.pack('<Q', scale) + b'\x49\xf7\xfa'
            assert after == b'' and (fixed, clobbers) == (0x401, flags | 0x405)
        assert before in code and (not after or after in code), (origin, before.hex(), after.hex())
        roles.add(role); origins.add(origin)
    return roles, origins


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('compiler', type=pathlib.Path)
    parser.add_argument('--baseline', type=pathlib.Path)
    parser.add_argument('--execution-profile', choices=('native', 'emulated', 'cross')); args = parser.parse_args()
    compiler = args.compiler.resolve(); identity = digest(compiler)
    linux = platform.system() == 'Linux' and platform.machine() == 'x86_64'
    profile = args.execution_profile or ('native' if linux else 'cross'); execute = profile in ('native', 'emulated'); assert not execute or linux
    files = sorted(pathlib.Path('source').glob('*.[ch]')) + sorted(pathlib.Path('runtime/include').rglob('*.h'))
    sources = sorted(pathlib.Path('tests/machine_address').glob('*.c'))
    files += sources + [pathlib.Path(name) for name in ('tests/test_mir_address.py', 'tests/mir_address_probe.c', 'tests/test_unwind.py', 'CMakeLists.txt')]
    inputs = {str(path): digest(path) for path in files}; source_hash = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()
    base = pathlib.Path('.agent-local/mir-address') / (source_hash + '-' + identity); base.mkdir(parents=True, exist_ok=True)
    root = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=base)).resolve()
    tools = {name: tool(name) for name in ('llvm-objdump', 'llvm-mc')}
    tools['gcc'] = tool('gcc-15' if shutil.which('gcc-15') else 'gcc'); tools['clang'] = tool('clang')
    if execute: tools['cc'] = tool('cc')
    identities = {name: dict(path=path, sha256=digest(pathlib.Path(path))) for name, path in tools.items()}
    if args.baseline: args.baseline = args.baseline.resolve(); baseline_hash = digest(args.baseline)
    artifacts = set(); commands = []; observations = []; roles = set(); origins = set()
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
        probe, obj = root / 'mir-address-probe', root / 'mir-address-probe.o'
        run([compiler, '-O0', '-c', '-Isource', '-Iruntime/include', 'tests/mir_address_probe.c', '-o', obj], 'probe-compile')
        core = [compiler.parent / 'libcinder_core.a'] if (compiler.parent / 'libcinder_core.a').is_file() else sorted(p for p in compiler.parent.glob('*.o') if p.name not in ('main.o', 'ir_main.o'))
        assert core
        core_inputs = {str(path): digest(path) for path in core}
        linker, flags = tools['cc'], []; cache = compiler.parent / 'CMakeCache.txt'
        if cache.is_file() and 'CINDER_SANITIZE:BOOL=ON' in cache.read_text().splitlines():
            linker = next(line.split('=', 1)[1] for line in cache.read_text().splitlines() if line.startswith('CMAKE_C_COMPILER:'))
            flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        run([linker, '-no-pie', *flags, obj, *core, '-o', probe], 'probe-link'); artifacts.update((probe, obj))
    else:
        probe = compiler.parent / 'cinder_mir_address_probe'; assert probe.is_file()
        library = compiler.parent / 'libcinder_core.a'
        if library.is_file(): core_inputs[str(library)] = digest(library)
    cache = compiler.parent / 'CMakeCache.txt'
    if cache.is_file(): core_inputs[str(cache)] = digest(cache)
    probe_hash = digest(probe)
    for source in sources:
        probe_obj = root / (source.stem + '.probe.o')
        result = json.loads(run([probe, source, probe_obj], source.stem + '-probe'))
        assert result['rejected'] == 16 * result['addresses']; artifacts.add(probe_obj)
        observations.append(dict(source=str(source), mutations=result))
        for level in ('-O0', '-O2'):
            stem = source.stem + level; obj, asm, assembled = [root / (stem + suffix) for suffix in ('.o', '.s', '.assembled.o')]
            run([compiler, level, '-fverify-each', '-c', source, '-o', obj], stem + '-compile')
            if level == '-O0': assert obj.read_bytes() == probe_obj.read_bytes()
            if args.baseline:
                baseline = root / (stem + '.baseline.o'); run([args.baseline, level, '-c', source, '-o', baseline], stem + '-baseline')
                assert obj.read_bytes() == baseline.read_bytes(); artifacts.add(baseline)
            dump = run([compiler, level, '--dump-mir', source], stem + '-dump')
            sections, _, relas = elf(obj); found, used = validate_dump(dump, sections['.text']); roles |= found; origins |= used
            run([tools['llvm-objdump'], '-dr', obj], stem + '-disassemble')
            run([compiler, level, '-S', source, '-o', asm], stem + '-assembly')
            run([tools['llvm-mc'], '-triple=x86_64-linux-gnu', '-filetype=obj', asm, '-o', assembled], stem + '-assemble')
            reference, _, other = elf(assembled)
            assert sections['.text'] == reference['.text'] and normalized(relas.get('.text', [])) == normalized(other.get('.text', []))
            artifacts.update((obj, asm, assembled))
            if source.stem == 'pressure':
                allocation = run([compiler, level, '--dump-regalloc', source], stem + '-allocation')
                assert any(int(value) > 0 for value in re.findall(r'spills=(\d+)', allocation)), 'pointer pressure fixture must actually spill'
    assert roles == {1, 2, 3, 4, 5} and origins == {'local.address', 'global.address', 'function.address', 'pointer.member', 'pointer.offset', 'pointer.diff'}
    for names, expected in ((('layout',), 131), (('pressure',), 133), (('provider', 'caller'), 15)):
        units = [pathlib.Path('tests/machine_address') / (name + '.c') for name in names]; label = names[0]
        joined = root / (label + '-joined.c'); joined.write_bytes(b'\n'.join(unit.read_bytes() for unit in units)); artifacts.add(joined)
        for level in ('-O0', '-O2'):
            assert run([compiler, level, '--interpret', joined], label + level + '-interpret') == f'interpret main => {expected}\n'
            for reference in ('gcc', 'clang'):
                executable = root / (reference + label + level)
                run([tools[reference], '-std=c17', level, *units, '-o', executable], executable.name + '-compile')
                run([executable], executable.name + '-execute', expected); artifacts.add(executable)
                if execute and len(names) == 2:
                    pair = []
                    for unit in units:
                        ref_obj = root / (reference + unit.stem + level + '.o')
                        run([tools[reference], '-std=c17', level, '-c', unit, '-o', ref_obj], ref_obj.stem + '-compile'); artifacts.add(ref_obj); pair.append(ref_obj)
                    for direction in range(2):
                        objects = [root / ('provider' + level + '.o'), pair[1]] if direction == 0 else [pair[0], root / ('caller' + level + '.o')]
                        mixed = root / (executable.name + '-mixed-' + str(direction))
                        run([tools['cc'], '-no-pie', *objects, '-o', mixed], mixed.name + '-link'); run([mixed], mixed.name + '-execute', expected); artifacts.add(mixed)
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
                  observations=observations, roles=sorted(roles), origins=sorted(origins), commands=commands,
                  artifacts={str(path.relative_to(root)): digest(path) for path in sorted(artifacts)})
    (root / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Machine addresses: five selected roles, six typed origins, restored object identity and three reference programs passed; native={report["native"]}; evidence={root}')


if __name__ == '__main__': main()
