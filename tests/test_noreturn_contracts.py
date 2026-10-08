#!/usr/bin/env python3
"""Check return contracts, diagnostics, canonical metadata, and defined exits."""
import hashlib
import json
import pathlib
import platform
import shutil
import subprocess
import struct
import sys
from native_profile import configure_stack
from test_objects import inspect

compiler = pathlib.Path(sys.argv[1]).resolve()
irtool = compiler.parent / 'cinderir'
identity = hashlib.sha256(compiler.read_bytes() + irtool.read_bytes()).hexdigest()
base = pathlib.Path('.agent-local/noreturn-contracts') / identity
base.mkdir(parents=True, exist_ok=True)
native = platform.system() == 'Linux' and platform.machine() == 'x86_64'
stack_profile = configure_stack()
rows = []


def contents(path):
    data = path.read_bytes()
    header = struct.unpack_from('<16sHHIQQQIHHHHHH', data)
    table = [struct.unpack_from('<IIQQQQIIQQ', data, header[6]+i*header[11]) for i in range(header[12])]
    names = data[table[header[13]][4]:table[header[13]][4]+table[header[13]][5]]
    sections = {names[row[0]:names.index(0,row[0])].decode(): row for row in table}
    return {name: data[row[4]:row[4]+row[5]] for name,row in sections.items() if name in ('.text','.data','.rodata')}


def relocation_contract(record):
    sections = list(record['sections'])
    values = []
    for row in record['relocations']:
        if row['symbol_binding'] == 0 and row['symbol_section'] != 0:
            target = ('section', sections[row['symbol_section']], row['symbol_value']+row['addend'])
        else:
            target = ('symbol', row['symbol'], row['addend'])
        values.append((row['section'], row['offset'], row['type'], target))
    return sorted(values)


def invoke(argv, timeout=20):
    result = subprocess.run([str(x) for x in argv], capture_output=True, timeout=timeout)
    assert b'Sanitizer' not in result.stderr and b'runtime error:' not in result.stderr, (argv, result)
    return result


def save(row):
    rows.append(row)
    (base / 'observations.json').write_text(json.dumps(dict(tools_sha256=identity, native=native, stack_profile=stack_profile, cases=rows), indent=2)+'\n')


# A recommended return diagnostic is a warning, not an invalid declaration.
for name, body, warning in (
    ('explicit-return', 'return;', True),
    ('fallthrough', '', True),
    ('conditional-return', 'if (n) return; for (;;) {}', True),
    ('loop-break', 'for (;;) { if (n) break; }', True),
    ('endless-for', 'for (;;) {}', False),
    ('endless-while', 'while (1) {}', False),
    ('endless-do', 'do {} while (1);', False),
    ('dead-return', 'if (0) return; for (;;) {}', False),
    ('nonreturn-call', 'stop();', False),
):
    source = base / (name+'.c')
    source.write_text('_Noreturn void stop(void); _Noreturn void f(int n) {'+body+'} int main(void) { return 0; }\n')
    for level in ('-O0', '-O2'):
        output = base / (name+level+'.o')
        argv = [compiler, level, '-c', source, '-o', output]
        result = invoke(argv)
        assert result.returncode == 0, (argv, result)
        assert (b'appears capable of returning' in result.stderr) == warning, (name, level, result)
        save(dict(kind='diagnostic', name=name, level=level, argv=[str(x) for x in argv], warning=warning, stderr=result.stderr.decode(), object_sha256=hashlib.sha256(output.read_bytes()).hexdigest()))

references = [shutil.which('gcc-15') or shutil.which('gcc'), shutil.which('clang')]
assert all(references)
for index, case in enumerate(json.loads(pathlib.Path('tests/noreturn/native/cases.json').read_text())):
    source = pathlib.Path('tests/noreturn/native') / case['source']
    for level in ('-O0', '-O2'):
        for reference in references:
            executable = base / ('reference-'+str(index)+level+'-'+pathlib.Path(reference).name)
            argv = [reference, '-std=c17', level, source, '-o', executable]
            result = invoke(argv); assert result.returncode == 0, (argv, result)
            observed = invoke([executable.resolve()])
            assert observed.returncode == case['exit'] and observed.stdout == b'' and observed.stderr == b'', (case, reference, level, observed)
            save(dict(kind='reference-exit', source=str(source), level=level, argv=[str(x) for x in argv], exit=observed.returncode, expected=case['exit'], stdout=observed.stdout.decode(), stderr=observed.stderr.decode()))
        obj = base / ('owned-'+str(index)+level+'.o')
        assembly = base / ('owned-'+str(index)+level+'.s')
        oracle = base / ('assembled-'+str(index)+level+'.o')
        for mode, output in (('-c', obj), ('-S', assembly)):
            result = invoke([compiler, level, mode, source, '-o', output]); assert result.returncode == 0, result
        result = invoke([references[1], '-target', 'x86_64-linux-gnu', '-c', assembly, '-o', oracle]); assert result.returncode == 0, result
        actual, assembled = inspect(obj), inspect(oracle, strict=False)
        for section in ('.text', '.data', '.rodata', '.bss'):
            assert actual['sections'][section] == assembled['sections'][section], (source, level, section)
        assert contents(obj) == contents(oracle), (source, level, 'section bytes differ')
        assert relocation_contract(actual) == relocation_contract(assembled), (source, level, 'relocations differ')
        cir = obj.with_suffix('.cir'); canonical = obj.with_suffix('.again.cir'); parsed = obj.with_suffix('.parsed.o')
        for argv in ([compiler, '--serialize-ir', level, source, '-o', cir], [irtool, cir, '-o', canonical], [irtool, '-c', cir, '-o', parsed]):
            result = invoke(argv); assert result.returncode == 0, result
        assert cir.read_bytes() == canonical.read_bytes() and obj.read_bytes() == parsed.read_bytes()
        text = cir.read_text()
        assert 'noreturn 1' in text
        # External process termination is not yet an interpreter service.
        classified = invoke([irtool, '--classify', cir]); assert classified.returncode == 0, classified
        value = json.loads(classified.stdout); assert value['valid'] is False and value['class'] == 'unsupported', value
        row = dict(kind='owned-exit', source=str(source), source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), level=level, object_sha256=hashlib.sha256(obj.read_bytes()).hexdigest(), expected=case['exit'], native=native, interpreter=value)
        if native:
            executable = obj.with_suffix('.native')
            result = invoke(['cc', '-no-pie', obj, '-o', executable]); assert result.returncode == 0, result
            observed = invoke([executable.resolve()])
            assert observed.returncode == case['exit'] and observed.stdout == b'' and observed.stderr == b'', (case, level, observed)
            row.update(exit=observed.returncode, stdout=observed.stdout.decode(), stderr=observed.stderr.decode())
        save(row)

# Inspect exact trap boundaries on undefined return fixtures without running
# the native program. The independent interpreter owns the UB observation.
for name in ('void_return', 'integer_return', 'aggregate_return', 'void_fallthrough', 'integer_fallthrough', 'aggregate_fallthrough', 'late_redeclaration', 'indirect_return', 'block_declaration'):
    source = pathlib.Path('tests/memory') / ('noreturn_'+name+'.undefined.c')
    for level in ('-O0', '-O2'):
        obj = base / ('undefined-'+name+level+'.o')
        cir = obj.with_suffix('.cir')
        result = invoke([compiler, level, '-c', source, '-o', obj]); assert result.returncode == 0, result
        result = invoke([compiler, level, '--serialize-ir', source, '-o', cir]); assert result.returncode == 0, result
        observed = invoke([irtool, '--classify', cir]); value = json.loads(observed.stdout)
        assert value['valid'] is False and value['class'] == 'noreturn_return', (name, level, value)
        record = inspect(obj); code = contents(obj)['.text']
        symbol = next(x for x in record['symbols'] if x['name'] == 'f')
        assert code[symbol['value']+symbol['size']-2:symbol['value']+symbol['size']] == bytes.fromhex('0f0b'), (name, level, 'marked return lacks UD2')
        for relocation in record['relocations']:
            if relocation['section'] == '.text' and relocation['symbol'] == 'f' and relocation['type'] == 4:
                start = relocation['offset']+4
                assert code[start:start+2] == bytes.fromhex('0f0b'), (name, level, 'marked call resumes without UD2')
        save(dict(kind='undefined-return', source=str(source), level=level, interpreter=value, native_executed=False, object_sha256=record['sha256']))

seed = base / 'owned-0-O0.cir'
text = seed.read_text()
lines = text.splitlines()
noncall = next(i for i, line in enumerate(lines) if line.startswith('inst const '))
bad = lines.copy(); bad[noncall] = bad[noncall].replace('noreturn 0', 'noreturn 1')
mutations = {
    'noncall-contract': '\n'.join(bad)+'\n',
    'invalid-boolean': text.replace('noreturn 1', 'noreturn 2', 1),
    'missing-contract': text.replace('noreturn 1 ', '', 1),
    'previous-schema': text.replace('cinder-ir 6', 'cinder-ir 3', 1),
}
for name, content in mutations.items():
    fixture = base / (name+'.invalid.cir'); fixture.write_text(content)
    output = base / 'prior.o'; output.write_bytes(b'prior complete output')
    result = invoke([irtool, '-c', fixture, '-o', output])
    assert result.returncode > 0 and (b'error:' in result.stderr or b'fatal:' in result.stderr), (name, result)
    assert output.read_bytes() == b'prior complete output'
    save(dict(kind='invalid-contract', name=name, exit=result.returncode, stderr=result.stderr.decode()))
assert hashlib.sha256(compiler.read_bytes()+irtool.read_bytes()).hexdigest() == identity
print(f'noreturn contracts: 9 diagnostic paths, 6 defined exits, 9 trap inspections, 4 malformed contracts passed; native={native}')
