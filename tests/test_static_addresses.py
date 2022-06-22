#!/usr/bin/env python3
"""Observe owned absolute data relocations, assembly equivalence, and native links."""
import hashlib
import json
import pathlib
import platform
import subprocess
import sys
from test_objects import inspect

compiler = pathlib.Path(sys.argv[1]).resolve()
irtool = compiler.parent / 'cinderir'
identity = hashlib.sha256(compiler.read_bytes() + irtool.read_bytes()).hexdigest()
base = pathlib.Path('.agent-local/static-addresses') / identity
base.mkdir(parents=True, exist_ok=True)
native = platform.system() == 'Linux' and platform.machine() == 'x86_64'
records = []

def run(argv, expected=0):
    result = subprocess.run([str(a) for a in argv], capture_output=True, timeout=30)
    records.append(dict(argv=[str(a) for a in argv], exit=result.returncode,
        stdout=result.stdout.decode(errors='replace'), stderr=result.stderr.decode(errors='replace')))
    (base / 'commands.json').write_text(json.dumps(records, indent=2) + '\n')
    assert result.returncode == expected, (argv, result)
    return result

cases = (
    ('global_object_pointer', '.data', 'value', 0, 37),
    ('global_const_pointer', '.rodata', 'value', 0, 41),
    ('global_array_element', '.data', 'data', 8, 59),
    ('global_member_pointer', '.data', 'value', 8, 73),
    ('global_function_pointer', '.data', 'value', 0, 89),
    ('global_function_const_pointer', '.rodata', 'value', 0, 103),
)
objects = []
for source, section, symbol, addend, expected in cases:
    path = pathlib.Path('tests/static_addresses') / (source + '.c')
    for level in ('-O0', '-O2'):
        obj = base / (source + level + '.o')
        asm = base / (source + level + '.s')
        assembled = base / (source + level + '-asm.o')
        run([compiler, level, '-fverify-each', '-c', path, '-o', obj])
        actual = inspect(obj)
        found = [r for r in actual['relocations'] if r['section'] == section and r['symbol'] == symbol and r['type'] == 1]
        assert len(found) == 1 and found[0]['addend'] == addend, (source, actual)
        run([compiler, level, '-S', path, '-o', asm])
        run(['clang', '-target', 'x86_64-linux-gnu', '-c', asm, '-o', assembled])
        oracle = inspect(assembled, strict=False)
        def address_relocations(record):
            return sorted((r['section'], r['offset'], r['symbol'], r['type'], r['addend']) for r in record['relocations'] if r['type'] == 1)
        assert address_relocations(actual) == address_relocations(oracle), (source, actual, oracle)
        objects.append(dict(source=source, level=level, object=actual, assembly_object=oracle))
        if native:
            for kind, object_path in (('object', obj), ('assembly', assembled)):
                executable = base / (source + level + '-' + kind)
                run(['cc', '-no-pie', object_path, '-o', executable])
                run([executable], expected)

# Both inputs go through Cinder. The host only links the owned objects.
provider = base / 'provider.c'
consumer = base / 'consumer.c'
provider.write_text('int exported=137; int value(int x) { return x+2; }\n')
consumer.write_text('extern int exported; int value(int); int *p=&exported; int (*callback)(int)=value; int main(void) { return *p+callback(10); }\n')
for level in ('-O0', '-O2'):
    outputs = []
    for path in (provider, consumer):
        obj = base / (path.stem + level + '.o')
        run([compiler, level, '-c', path, '-o', obj]); outputs.append(obj)
        inspect(obj)
    if native:
        executable = base / ('multi' + level)
        run(['cc', '-no-pie', *outputs, '-o', executable]); run([executable], 149)

seed = base / 'address.cir'
run([compiler, '--serialize-ir', 'tests/static_addresses/global_object_pointer.c', '-o', seed])
original = seed.read_text()
edits = ({1: '1'}, {1: '999999'}, {2: '-'}, {2: 'x6d697373696e67'}, {4: 'none'}, {5: 'none'}, {6: '5'}, {7: '999999'}, {8: '1'})
for index, mutation in enumerate(edits):
    lines = original.splitlines()
    for position, line in enumerate(lines):
        words = line.split()
        if words and words[0] == 'global' and 'address' in words:
            start = words.index('address')
            for column, value in mutation.items(): words[start + column] = value
            lines[position] = ' '.join(words); break
    else: raise AssertionError('missing address record')
    invalid = base / f'invalid-{index}.cir'
    invalid.write_text('\n'.join(lines) + '\n')
    prior = base / 'preserved.o'; prior.write_bytes(b'prior complete output')
    result = subprocess.run([str(irtool), '-c', str(invalid), '-o', str(prior)], capture_output=True, timeout=10)
    assert result.returncode > 0 and (b'error:' in result.stderr or b'fatal:' in result.stderr) and b'runtime error:' not in result.stderr and b'Sanitizer' not in result.stderr, (mutation, result)
    assert prior.read_bytes() == b'prior complete output'
    records.append(dict(mutation=mutation, exit=result.returncode, stderr=result.stderr.decode(errors='replace')))
(base / 'objects.json').write_text(json.dumps(objects, indent=2) + '\n')
(base / 'commands.json').write_text(json.dumps(records, indent=2) + '\n')
(base / 'summary.json').write_text(json.dumps(dict(native=native, compiler_sha256=identity,
    object_pairs=len(objects), malformed_records=len(edits), native_executions=26 if native else 0), indent=2) + '\n')
print(f'static addresses: {len(objects)} object/assembly pairs, two-unit relocations, {len(edits)} malformed records rejected; native={native}')
