#!/usr/bin/env python3
"""Check flexible-member type graphs, object identities, and mutation rejection."""
import hashlib
import json
import pathlib
import platform
import subprocess
import sys

compiler = pathlib.Path(sys.argv[1]).resolve()
irtool = compiler.parent / 'cinderir'
inputs = sorted(pathlib.Path('tests/flexible').glob('*.c')) + [pathlib.Path(__file__)]
hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
identity = hashlib.sha256(compiler.read_bytes() + irtool.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
root = pathlib.Path('.agent-local/flexible-contracts') / identity
root.mkdir(parents=True, exist_ok=True)
native = platform.system() == 'Linux' and platform.machine() == 'x86_64'
rows = []

def run(argv):
    return subprocess.run([str(a) for a in argv], capture_output=True, text=True, timeout=30)

def invoke(argv):
    result = run(argv)
    assert result.returncode == 0, (argv, result)
    return result

def save(row):
    rows.append(row)
    (root / 'observations.json').write_text(json.dumps(dict(inputs=hashes, compiler_sha256=hashlib.sha256(compiler.read_bytes()).hexdigest(), irtool_sha256=hashlib.sha256(irtool.read_bytes()).hexdigest(), native=native, cases=rows), indent=2) + '\n')

for source in inputs[:-1]:
    for level in ('-O0', '-O2'):
        prefix = root / (source.stem + level)
        cir, again = prefix.with_suffix('.cir'), prefix.with_suffix('.again.cir')
        obj, parsed = prefix.with_suffix('.o'), prefix.with_suffix('.parsed.o')
        invoke([compiler, level, '-fverify-each', '--serialize-ir', source, '-o', cir])
        invoke([compiler, level, '-fverify-each', '-c', source, '-o', obj])
        invoke([irtool, cir, '-o', again])
        invoke([irtool, '-c', cir, '-o', parsed])
        value = json.loads(invoke([irtool, '--classify', cir]).stdout)
        assert value['valid'] and value['integer'] == 0, (source, level, value)
        assert cir.read_bytes() == again.read_bytes() and obj.read_bytes() == parsed.read_bytes(), (source, level)
        row = dict(kind='authored', source=str(source), level=level, ir_sha256=hashlib.sha256(cir.read_bytes()).hexdigest(), object_sha256=hashlib.sha256(obj.read_bytes()).hexdigest(), interpreter=value)
        if native:
            output = prefix.with_suffix('.native')
            invoke(['cc', '-no-pie', parsed, '-o', output])
            result = invoke([output.resolve()])
            assert not result.stdout and not result.stderr, result
            row.update(native_exit=result.returncode, executable_sha256=hashlib.sha256(output.read_bytes()).hexdigest())
        save(row)

seed = (root / 'layout_byte-O0.cir').read_text()
lines = seed.splitlines()
header_index = next(i for i, line in enumerate(lines) if line.startswith('type ') and line.split()[2] == 'struct')
tail_index = next(i for i, line in enumerate(lines) if line.startswith('type ') and line.split()[2] == 'array' and line.split()[4] == '0')
header_id, tail_id = lines[header_index].split()[1], lines[tail_index].split()[1]
mutations = {}

def changed(name, index, edits):
    value = lines.copy()
    columns = value[index].split()
    for column, replacement in edits.items():
        columns[column] = replacement
    value[index] = ' '.join(columns)
    mutations[name] = '\n'.join(value) + '\n'

changed('union-flexible-array', header_index, {2: 'union'})
changed('unnamed-prefix', header_index + 1, {1: '-'})
changed('unnamed-flexible-member', header_index + 2, {1: '-'})
changed('nonzero-incomplete-size', tail_index, {7: '1'})
changed('nonzero-incomplete-length', tail_index, {10: '1'})
changed('incomplete-element', tail_index, {9: header_id})
changed('wrong-flexible-offset', header_index + 2, {3: '3'})
changed('invalid-flexible-alignment', tail_index, {8: '2'})
swapped = lines.copy()
swapped[header_index + 1], swapped[header_index + 2] = swapped[header_index + 2], swapped[header_index + 1]
mutations['flexible-not-final'] = '\n'.join(swapped) + '\n'
count = int(lines[1].split()[1])
extra = f'type {count} array 0 1 0 0 4 4 {header_id} 1 none 0 - params 0 fields 0 identity 999999\n'
mutations['array-of-flexible-header'] = seed.replace(f'types {count}\n', f'types {count + 1}\n', 1).replace('globals 0\n', extra + 'globals 0\n', 1)
extra = f'type {count} struct 0 1 0 0 4 4 none 0 none 0 x456d626564 params 0 fields 1 identity 999999\nfield x68656164 {header_id} 0 0 0 align 0 bits 0\n'
mutations['embedded-flexible-header'] = seed.replace(f'types {count}\n', f'types {count + 1}\n', 1).replace('globals 0\n', extra + 'globals 0\n', 1)
for name, text in mutations.items():
    assert text != seed, name
    path = root / (name + '.invalid.cir')
    path.write_text(text)
    output = root / 'prior.o'
    output.write_bytes(b'previous complete output')
    commands = []
    for mode in ('--verify', '-c'):
        argv = [irtool, mode, path]
        if mode == '-c':
            argv += ['-o', output]
        result = run(argv)
        assert result.returncode > 0 and ('error:' in result.stderr or 'fatal:' in result.stderr), (name, result)
        assert 'Sanitizer' not in result.stderr and 'runtime error:' not in result.stderr and output.read_bytes() == b'previous complete output', (name, result)
        commands.append(dict(argv=[str(a) for a in argv], exit=result.returncode, stderr=result.stderr))
    save(dict(kind='mutation', name=name, ir_sha256=hashlib.sha256(path.read_bytes()).hexdigest(), commands=commands))

assert hashes == {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
assert identity == hashlib.sha256(compiler.read_bytes() + irtool.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
print(f'Flexible contracts: {len(inputs)-1} authored cases at both levels and {len(mutations)} malformed type graphs passed; native={native}.')
