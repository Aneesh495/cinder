#!/usr/bin/env python3
"""Cross-check authored initializer observations and original object assembly."""
import hashlib
import concurrent.futures
import json
import pathlib
import platform
import shutil
import subprocess
import struct
import sys
from test_objects import inspect
from reference_policy import identify, adjudicate
from native_profile import configure_stack

compiler = pathlib.Path(sys.argv[1]).resolve()
identity = hashlib.sha256(compiler.read_bytes()).hexdigest()
group = sys.argv[2] if len(sys.argv) > 2 else 'initializers'
assert group in ('initializers', 'aggregates', 'aggregate_abi', 'variadic', 'compound_literals', 'static_assertions', 'generic', 'alignment', 'noreturn', 'block_storage', 'goto', 'switch', 'register', 'offset')
base = pathlib.Path('.agent-local') / group / identity
base.mkdir(parents=True, exist_ok=True)
native = platform.system() == 'Linux' and platform.machine() == 'x86_64'
stack_profile = configure_stack()
references = [shutil.which('gcc-15') or shutil.which('gcc'), shutil.which('clang')]
assert all(references), 'GCC and Clang references are required'
records = []
versions = {}
identities = {}
for reference in references:
    identities[reference] = identify(reference)
    versions[reference] = identities[reference]['version']
by_source = {pathlib.Path('tests/control', case['source']).resolve(): case for case in json.loads(pathlib.Path('tests/control/cases.json').read_text())}
sources = sorted(pathlib.Path('tests', group).glob('*.c'))
assert sources and all(path.resolve() in by_source for path in sources), 'every initializer source needs an authored observation'
cases = [by_source[path.resolve()] for path in sources]
def probe(item):
    index, case = item
    source = pathlib.Path('tests/control') / case['source']
    row = dict(source=str(source.resolve()), source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), expected=case['exit'], references=[], objects=[])
    for reference in references:
        for level in ('-O0', '-O2'):
            executable = base / f'reference-{index}-{pathlib.Path(reference).name}{level}'
            argv = [reference, '-std=c17', level, str(source), '-o', str(executable)]
            result = subprocess.run(argv, capture_output=True, timeout=30)
            assert result.returncode == 0, (argv, result)
            observed = subprocess.run([str(executable.resolve())], capture_output=True, timeout=5)
            assert observed.stdout == b'' and observed.stderr == b'', (source, reference, level, observed)
            reference_row = dict(argv=argv, exit=observed.returncode, executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(), compile_stderr=result.stderr.decode(errors='replace'))
            row['references'].append(reference_row)
            (base / f'reference-observations-{index}.json').write_text(json.dumps(row, indent=2)+'\n')
            reference_row['verdict'] = adjudicate(source, case['exit'], observed.returncode, identities[reference])
    row['differential_eligible'] = all(reference['verdict']['eligible'] for reference in row['references'])
    for level in ('-O0', '-O2'):
        obj = base / f'owned-{index}{level}.o'
        assembly = base / f'owned-{index}{level}.s'
        oracle = base / f'assembled-{index}{level}.o'
        for mode, output in (('-c', obj), ('-S', assembly)):
            argv = [str(compiler), level, '-fverify-each', mode, str(source), '-o', str(output)]
            result = subprocess.run(argv, capture_output=True, timeout=20)
            assert result.returncode == 0, (argv, result)
        result = subprocess.run([references[1], '-target', 'x86_64-linux-gnu', '-c', str(assembly), '-o', str(oracle)], capture_output=True, timeout=20)
        assert result.returncode == 0, result
        actual = inspect(obj)
        assembled = inspect(oracle, strict=False)
        for section in ('.text', '.data', '.rodata', '.bss'):
            assert actual['sections'][section] == assembled['sections'][section], (source, level, section, 'section size/alignment disagrees')
        def relocations(record):
            values = []
            sections = list(record['sections'])
            for r in record['relocations']:
                if r['symbol_binding'] == 0 and r['symbol_section'] != 0:
                    target = ('section', sections[r['symbol_section']], r['symbol_value'] + r['addend'])
                else:
                    target = ('symbol', r['symbol'], r['addend'])
                values.append((r['section'], r['offset'], r['type'], target))
            return sorted(values)
        def contents(path):
            data = path.read_bytes()
            header = struct.unpack_from('<16sHHIQQQIHHHHHH', data)
            table = [struct.unpack_from('<IIQQQQIIQQ', data, header[6] + index * header[11]) for index in range(header[12])]
            names = table[header[13]]; strings = data[names[4]:names[4]+names[5]]
            result = {}
            for section in table:
                name = strings[section[0]:strings.index(0, section[0])].decode()
                if name in ('.text', '.data', '.rodata'):
                    result[name] = data[section[4]:section[4]+section[5]]
            return result
        assert contents(obj) == contents(oracle), (source, level, 'encoded section bytes disagree')
        assert relocations(actual) == relocations(assembled), (source, level, actual, assembled)
        object_row = dict(level=level, owned=actual, assembled=assembled, assembly_sha256=hashlib.sha256(assembly.read_bytes()).hexdigest(), native_exits=[])
        if native:
            for kind, path in (('owned', obj), ('assembled', oracle)):
                executable = base / f'{kind}-{index}{level}'
                result = subprocess.run(['cc', '-no-pie', str(path), '-o', str(executable)], capture_output=True, timeout=20)
                assert result.returncode == 0, result
                observed = subprocess.run([str(executable.resolve())], capture_output=True, timeout=5)
                assert observed.returncode == case['exit'] and observed.stdout == b'' and observed.stderr == b'', (source, level, kind, observed)
                object_row['native_exits'].append(dict(kind=kind, exit=observed.returncode))
        row['objects'].append(object_row)
    return row

with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    for row in pool.map(probe, enumerate(cases)):
        records.append(row)
        (base / 'observations.json').write_text(json.dumps(dict(compiler_sha256=identity, reference_versions=versions, native=native, stack_profile=stack_profile, cases=records), indent=2)+'\n')
assert hashlib.sha256(compiler.read_bytes()).hexdigest() == identity, 'compiler changed during initializer checks'
disagreements = sum(not reference['verdict']['agreement'] for row in records for reference in row['references'])
eligible = sum(row['differential_eligible'] for row in records)
print(f'{group}: {len(cases)} authored cases and object/assembly pairs passed; {eligible} reference-agreement cases, {disagreements} triaged reference discrepancies; native={native}')
