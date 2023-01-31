#!/usr/bin/env python3
"""Check storage addresses and bitfield roles against ELF bytes and execution."""
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
    roles = set(); origins = set(); bitfield_roles = set(); symbol_forms = Counter(); flags = 1 << 32
    for line in text.splitlines():
        if ' storage=' in line:
            match = re.search(r'machine-kind=4 origin=(\S+) dst=(\d+) fixed=([0-9a-f]+) clobbers=([0-9a-f]+).* load-form=([0-9a-f]+) store-form=([0-9a-f]+) storage=(\d+) store=(yes|no) address=(\d+) value=(\d+) frame-slot=(-?\d+) modrm=([0-9a-f]{2}) extended=(yes|no) relocation=(\d+) addend=(-?\d+)$', line)
            assert match, line
            origin, dst, fixed, clobbers, load, store, role, writes, address, value, slot, modrm, extended, relocation, addend = match.groups()
            dst, fixed, clobbers = int(dst), int(fixed, 16), int(clobbers, 16)
            role, address, value, slot, modrm, relocation, addend = int(role), int(address), int(value), int(slot), int(modrm, 16), int(relocation), int(addend)
            writes, extended = writes == 'yes', extended == 'yes'; load, store = bytes.fromhex(load), bytes.fromhex(store)
            assert role in (1, 2, 3) and value != 0xffffffff; roles.add(role); origins.add(origin)
            assert writes == (origin in ('local.store', 'local.init', 'global.store', 'memory.store', 'memory.init'))
            assert writes or value == dst
            if role == 1:
                assert origin.startswith('local.') and slot >= 0 and address == 0xffffffff
                assert modrm == 0x85 and not extended and relocation == addend == 0
            elif role == 2:
                assert origin.startswith('global.') and slot == -1 and address == 0xffffffff
                assert modrm == 5 and not extended and (relocation, addend) == (2, -4)
                symbol_forms[(store if writes else load) + b'\x05'] += 1
            else:
                assert origin.startswith('memory.') and slot == -1 and address != 0xffffffff
                assert modrm == 0x82 and extended and relocation == addend == 0
            floating = load.startswith(b'\xf2') or load.startswith(b'\xf3'); register = 1 << 16 if floating else 1
            assert fixed == ((32 if role == 1 else 0 if role == 2 else 1024) | (register if writes else 0))
            expected_clobbers = register | (1025 if extended else 0)
            assert clobbers == expected_clobbers or (not writes and not floating and clobbers == expected_clobbers | flags)
            prefix = store if writes else load
            if extended:
                # The selected dump also prints the ordinary-base forms; the
                # extended base adds or adjusts REX for the same operation.
                if prefix[:1] in (b'\x66',): prefix = prefix[:1] + (b'\x49' + prefix[2:] if prefix[1:2] == b'\x48' else b'\x41' + prefix[1:])
                elif prefix[:1] == b'\x48': prefix = b'\x49' + prefix[1:]
                elif prefix[:1] in (b'\xf2', b'\xf3'): prefix = prefix[:1] + b'\x41' + prefix[1:]
                else: prefix = b'\x41' + prefix
            assert prefix + bytes((modrm,)) in sections['.text'], (origin, prefix.hex())
        if ' bitfield-role=' in line:
            role = int(re.search(r' bitfield-role=(\d+)', line)[1]); origin = re.search(r' origin=(\S+)', line)[1]
            assert role == (1 if origin == 'bit.load' else 3 if origin == 'bit.convert' else 2)
            assert origin in ('bit.load', 'bit.store', 'bit.init', 'bit.convert'); bitfield_roles.add(role); origins.add(origin)
    actual = Counter()
    for relocation in relas.get('.text', []):
        if relocation['symbol']['name'].startswith('.LCF'): continue
        if relocation['kind'] != 2 or relocation['addend'] != -4: continue
        offset = relocation['offset']; code = sections['.text']
        candidates = [form for form in symbol_forms if code[offset-len(form):offset] == form]
        if candidates:
            assert len(candidates) == 1; actual[candidates[0]] += 1
    assert actual == symbol_forms, (actual, symbol_forms)
    return roles, origins, bitfield_roles


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('compiler', type=pathlib.Path)
    parser.add_argument('--baseline', type=pathlib.Path)
    parser.add_argument('--execution-profile', choices=('native', 'emulated', 'cross')); args = parser.parse_args()
    compiler = args.compiler.resolve(); identity = digest(compiler)
    linux = platform.system() == 'Linux' and platform.machine() == 'x86_64'
    profile = args.execution_profile or ('native' if linux else 'cross'); execute = profile in ('native', 'emulated'); assert not execute or linux
    files = sorted(pathlib.Path('source').glob('*.[ch]')) + sorted(pathlib.Path('runtime/include').rglob('*.h'))
    sources = sorted(pathlib.Path('tests/machine_memory').glob('*.c')) + [pathlib.Path('tests/bitfields') / name for name in ('compound_updates.c', 'assign_truncate.c', 'char_suffix.c')]
    ledger = {(pathlib.Path('tests/control') / row['source']).resolve(): row['exit'] for row in json.loads(pathlib.Path('tests/control/cases.json').read_text())}
    files += sources + [pathlib.Path(name) for name in ('tests/test_mir_storage.py', 'tests/mir_storage_probe.c', 'tests/test_unwind.py', 'tests/control/cases.json', 'CMakeLists.txt')]
    inputs = {str(path): digest(path) for path in files}; source_hash = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()
    base = pathlib.Path('.agent-local/mir-storage') / (source_hash + '-' + identity); base.mkdir(parents=True, exist_ok=True)
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
        probe, obj = root / 'mir-storage-probe', root / 'mir-storage-probe.o'
        run([compiler, '-O0', '-c', '-Isource', '-Iruntime/include', 'tests/mir_storage_probe.c', '-o', obj], 'probe-compile')
        core = [compiler.parent / 'libcinder_core.a'] if (compiler.parent / 'libcinder_core.a').is_file() else sorted(p for p in compiler.parent.glob('*.o') if p.name not in ('main.o', 'ir_main.o'))
        assert core
        core_inputs = {str(path): digest(path) for path in core}
        linker, flags = tools['cc'], []; cache = compiler.parent / 'CMakeCache.txt'
        if cache.is_file() and 'CINDER_SANITIZE:BOOL=ON' in cache.read_text().splitlines():
            linker = next(line.split('=', 1)[1] for line in cache.read_text().splitlines() if line.startswith('CMAKE_C_COMPILER:'))
            flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        run([linker, '-no-pie', *flags, obj, *core, '-o', probe], 'probe-link'); artifacts.update((probe, obj))
    else:
        probe = compiler.parent / 'cinder_mir_storage_probe'; assert probe.is_file()
        library = compiler.parent / 'libcinder_core.a'
        if library.is_file(): core_inputs[str(library)] = digest(library)
    cache = compiler.parent / 'CMakeCache.txt'
    if cache.is_file(): core_inputs[str(cache)] = digest(cache)
    probe_hash = digest(probe)
    for source in sources:
        probe_obj = root / (source.stem + '.probe.o')
        result = json.loads(run([probe, source, probe_obj], source.stem + '-probe'))
        assert result['rejected'] == 16 * (result['memory'] + result['bitfields']); artifacts.add(probe_obj)
        observations.append(dict(source=str(source), mutations=result))
        for level in ('-O0', '-O2'):
            stem = source.stem + level; obj, asm, assembled = [root / (stem + suffix) for suffix in ('.o', '.s', '.assembled.o')]
            run([compiler, level, '-fverify-each', '-c', source, '-o', obj], stem + '-compile')
            if level == '-O0': assert obj.read_bytes() == probe_obj.read_bytes()
            if args.baseline:
                baseline = root / (stem + '.baseline.o'); run([args.baseline, level, '-c', source, '-o', baseline], stem + '-baseline')
                assert obj.read_bytes() == baseline.read_bytes(); artifacts.add(baseline)
            dump = run([compiler, level, '--dump-mir', source], stem + '-dump')
            if level == '-O0':
                assert result['memory'] == len(re.findall(r'^  machine-kind=4 ', dump, re.M))
                assert result['bitfields'] == len(re.findall(r'^  machine-kind=7 ', dump, re.M))
                assert result['memory'] + result['bitfields'] > 0
            sections, _, relas = elf(obj); found, used, bulks = validate_dump(dump, sections, relas); roles |= found; origins |= used; bulk_roles |= bulks
            run([tools['llvm-objdump'], '-dr', obj], stem + '-disassemble')
            run([compiler, level, '-S', source, '-o', asm], stem + '-assembly')
            run([tools['llvm-mc'], '-triple=x86_64-linux-gnu', '-filetype=obj', asm, '-o', assembled], stem + '-assemble')
            reference, _, other = elf(assembled)
            assert sections['.text'] == reference['.text'] and normalized(relas.get('.text', [])) == normalized(other.get('.text', []))
            artifacts.update((obj, asm, assembled))
    assert roles == {1, 2, 3} and bulk_roles == {1, 2, 3}
    assert {'local.load', 'local.init', 'global.load', 'global.store', 'memory.load', 'memory.store', 'bit.load', 'bit.init', 'bit.store', 'bit.convert'} <= origins
    for source in sources:
        names, expected, label = (source.stem,), ledger[source.resolve()], source.stem
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
                  observations=observations, roles=sorted(roles), bitfield_roles=sorted(bulk_roles), origins=sorted(origins), commands=commands,
                  artifacts={str(path.relative_to(root)): digest(path) for path in sorted(artifacts)})
    (root / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Machine storage: three address families and bitfield roles, restored objects and fifteen reference programs passed; native={report["native"]}; evidence={root}')


if __name__ == '__main__': main()
