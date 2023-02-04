#!/usr/bin/env python3
"""Check selected ABI programs and allocated transfers against ELF bytes and execution."""
import argparse
import copy
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
from read_mir_program import validate_dump




def main():
    parser = argparse.ArgumentParser(); parser.add_argument('compiler', type=pathlib.Path)
    parser.add_argument('--baseline', type=pathlib.Path)
    parser.add_argument('--execution-profile', choices=('native', 'emulated', 'cross')); args = parser.parse_args()
    compiler = args.compiler.resolve(); identity = digest(compiler)
    linux = platform.system() == 'Linux' and platform.machine() == 'x86_64'
    profile = args.execution_profile or ('native' if linux else 'cross'); execute = profile in ('native', 'emulated'); assert not execute or linux
    files = sorted(pathlib.Path('source').glob('*.[ch]')) + sorted(pathlib.Path('runtime/include').rglob('*.h'))
    sources = sorted(pathlib.Path('tests/aggregate_abi').glob('*.c')) + sorted(pathlib.Path('tests/variadic').glob('*.c')) + sorted(pathlib.Path('tests/machine_material').glob('*.c'))
    ledger = {(pathlib.Path('tests/control') / row['source']).resolve(): row['exit'] for row in json.loads(pathlib.Path('tests/control/cases.json').read_text())}
    files += sources + [pathlib.Path(name) for name in ('tests/test_mir_program.py', 'tests/mir_program_probe.c', 'tests/test_unwind.py', 'tests/read_mir_program.py', 'tests/control/cases.json', 'CMakeLists.txt')]
    inputs = {str(path): digest(path) for path in files}; source_hash = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()
    base = pathlib.Path('.agent-local/mir-program') / (source_hash + '-' + identity); base.mkdir(parents=True, exist_ok=True)
    root = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=base)).resolve()
    tools = {name: tool(name) for name in ('llvm-objdump', 'llvm-mc')}
    tools['gcc'] = tool('gcc-15' if shutil.which('gcc-15') else 'gcc'); tools['clang'] = tool('clang')
    if execute: tools['cc'] = tool('cc')
    identities = {name: dict(path=path, sha256=digest(pathlib.Path(path))) for name, path in tools.items()}
    if args.baseline: args.baseline = args.baseline.resolve(); baseline_hash = digest(args.baseline)
    artifacts = set(); commands = []; observations = []; roles = set(); origins = set(); bulk_roles = set(); reader_rejections = 0
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
        probe, obj = root / 'mir-program-probe', root / 'mir-program-probe.o'
        run([compiler, '-O0', '-c', '-Isource', '-Iruntime/include', 'tests/mir_program_probe.c', '-o', obj], 'probe-compile')
        core = [compiler.parent / 'libcinder_core.a'] if (compiler.parent / 'libcinder_core.a').is_file() else sorted(p for p in compiler.parent.glob('*.o') if p.name not in ('main.o', 'ir_main.o'))
        assert core
        core_inputs = {str(path): digest(path) for path in core}
        linker, flags = tools['cc'], []; cache = compiler.parent / 'CMakeCache.txt'
        if cache.is_file() and 'CINDER_SANITIZE:BOOL=ON' in cache.read_text().splitlines():
            linker = next(line.split('=', 1)[1] for line in cache.read_text().splitlines() if line.startswith('CMAKE_C_COMPILER:'))
            flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        run([linker, '-no-pie', *flags, obj, *core, '-o', probe], 'probe-link'); artifacts.update((probe, obj))
    else:
        probe = compiler.parent / 'cinder_mir_program_probe'; assert probe.is_file()
        library = compiler.parent / 'libcinder_core.a'
        if library.is_file(): core_inputs[str(library)] = digest(library)
    cache = compiler.parent / 'CMakeCache.txt'
    if cache.is_file(): core_inputs[str(cache)] = digest(cache)
    probe_hash = digest(probe)
    for source in sources:
        probe_obj = root / (source.stem + '.probe.o')
        result = json.loads(run([probe, source, probe_obj], source.stem + '-probe'))
        assert result['programs'] > 0 and result['rejected'] == 4 * result['programs'] + 6 * result['steps'] + 2 * result['byte_steps'] + 4 * result['patches'] + 6 * result['fixups'] + result['patch_lists'] + result['fixup_lists']; artifacts.add(probe_obj)
        observations.append(dict(source=str(source), mutations=result))
        for level in ('-O0', '-O2'):
            stem = source.stem + level; obj, asm, assembled = [root / (stem + suffix) for suffix in ('.o', '.s', '.assembled.o')]
            run([compiler, level, '-fverify-each', '-c', source, '-o', obj], stem + '-compile')
            if level == '-O0': assert obj.read_bytes() == probe_obj.read_bytes()
            if args.baseline:
                baseline = root / (stem + '.baseline.o'); run([args.baseline, level, '-c', source, '-o', baseline], stem + '-baseline')
                assert obj.read_bytes() == baseline.read_bytes(); artifacts.add(baseline)
            dump = run([compiler, level, '--dump-mir', '--dump-regalloc', source], stem + '-dump')
            sections, symbols, relas = elf(obj)
            disassembly = run([tools['llvm-objdump'], '--no-show-raw-insn', '-dr', obj], stem + '-disassemble')
            found, steps, patches, programs = validate_dump(dump, sections, symbols, relas, disassembly); roles |= found; origins |= steps; bulk_roles |= patches
            if level == '-O0':
                assert result['programs'] == len(programs) and result['steps'] == sum(p['steps'] for p in programs)
                assert result['patches'] == sum(p['patches'] for p in programs) and result['fixups'] == sum(p['fixups'] for p in programs)
                observations[-1]['decoded'] = programs
                if source == sources[0]:
                    corrupted = dict(sections); corrupted['.text'] = bytes(len(sections['.text']))
                    wrong_targets = copy.deepcopy(relas)
                    target = next(r for r in wrong_targets['.text'] if r['kind'] == 4); target['symbol']['name'] += '.wrong'
                    wrong_role = re.sub(r'(    protocol role=)\d+', r'\g<1>0', dump, count=1)
                    for bad_dump, bad_sections, bad_relocations in ((dump, corrupted, relas), (wrong_role, sections, relas), (dump, sections, wrong_targets)):
                        try: validate_dump(bad_dump, bad_sections, symbols, bad_relocations, disassembly)
                        except (AssertionError, KeyError): reader_rejections += 1
                        else: raise AssertionError('independent program reader accepted corrupted evidence')
            run([compiler, level, '-S', source, '-o', asm], stem + '-assembly')
            run([tools['llvm-mc'], '-triple=x86_64-linux-gnu', '-filetype=obj', asm, '-o', assembled], stem + '-assemble')
            reference, _, other = elf(assembled)
            assert sections['.text'] == reference['.text'] and normalized(relas.get('.text', [])) == normalized(other.get('.text', []))
            artifacts.update((obj, asm, assembled))
    assert roles == set(range(1, 8)) and origins == set(range(5)) and bulk_roles == set(range(4))
    assert reader_rejections == 3
    for source in sources:
        names, expected, label = (source.stem,), (0 if source.parent.name == 'machine_material' else ledger[source.resolve()]), source.stem
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
                  observations=observations, reader_rejections=reader_rejections, roles=sorted(roles), patch_roles=sorted(bulk_roles), step_roles=sorted(origins), commands=commands,
                  artifacts={str(path.relative_to(root)): digest(path) for path in sorted(artifacts)})
    (root / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Machine ABI programs: seven roles, allocated transfer bytes, frame patches and original object identity passed; native={report["native"]}; evidence={root}')


if __name__ == '__main__': main()
