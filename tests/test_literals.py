#!/usr/bin/env python3
"""Check literal profile diagnostics, malformed encodings, and object contracts."""
import hashlib
import json
import pathlib
import subprocess
import sys

compiler = pathlib.Path(sys.argv[1]).resolve()
irtool = compiler.parent / 'cinderir'
identity = hashlib.sha256(compiler.read_bytes() + irtool.read_bytes()).hexdigest()
base = pathlib.Path('.agent-local/literals') / identity
base.mkdir(parents=True, exist_ok=True)
observations = []
invalid_bytes = {
    'nul': b'int main(void) { return 0; }\x00ignored\n',
    'overlong': b'/*\xc0\x80*/ int main(void) { return 0; }\n',
    'continuation': b'/*\x80*/ int main(void) { return 0; }\n',
    'truncated': b'/*\xe2\x82',
    'surrogate': b'/*\xed\xa0\x80*/ int main(void) { return 0; }\n',
    'out_of_range': b'/*\xf4\x90\x80\x80*/ int main(void) { return 0; }\n',
    'bad_continuation': b'/*\xe2A\x80*/ int main(void) { return 0; }\n',
}
inputs = list(pathlib.Path('tests/literals').glob('profile_*.invalid.c'))
for name, data in invalid_bytes.items():
    path = base / (name + '.c')
    path.write_bytes(data)
    inputs.append(path)
for path in inputs:
    prior = base / 'preserved.o'
    prior.write_bytes(b'prior complete output')
    argv = [str(compiler), '-c', str(path), '-o', str(prior)]
    result = subprocess.run(argv, capture_output=True, timeout=10)
    observations.append(dict(argv=argv, exit=result.returncode, stderr=result.stderr.decode(errors='replace'),
        source_sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    assert result.returncode > 0 and b'error:' in result.stderr and b'runtime error:' not in result.stderr and b'Sanitizer' not in result.stderr, (path, result)
    assert prior.read_bytes() == b'prior complete output'

seed = base / 'initializer.cir'
result = subprocess.run([str(compiler), '-O0', '--serialize-ir', 'tests/literals/local_const.c', '-o', str(seed)], capture_output=True, timeout=10)
assert result.returncode == 0, result
original = seed.read_text()
scalar = next(line.split()[2] for line in original.splitlines() if line.startswith('inst const '))
mutations = ({2: 'none'}, {2: scalar}, {5: '0'}, {6: 'none'}, {7: 'none'}, {6: '999999'}, {7: '999999'})
for index, edits in enumerate(mutations):
    lines = original.splitlines()
    for position, line in enumerate(lines):
        columns = line.split()
        if columns[:2] == ['inst', 'object.init']:
            for column, value in edits.items(): columns[column] = value
            lines[position] = ' '.join(columns)
            break
    else: raise AssertionError('missing object initializer')
    invalid = base / f'mutation-{index}.cir'
    invalid.write_text('\n'.join(lines) + '\n')
    prior.write_bytes(b'prior complete output')
    argv = [str(irtool), '-c', str(invalid), '-o', str(prior)]
    result = subprocess.run(argv, capture_output=True, timeout=10)
    assert result.returncode > 0 and (b'error:' in result.stderr or b'fatal:' in result.stderr) and b'runtime error:' not in result.stderr and b'Sanitizer' not in result.stderr, (edits, result)
    assert prior.read_bytes() == b'prior complete output'
    observations.append(dict(argv=argv, exit=result.returncode, stderr=result.stderr.decode(errors='replace'),
        ir_sha256=hashlib.sha256(invalid.read_bytes()).hexdigest()))
(base / 'observations.json').write_text(json.dumps(observations, indent=2) + '\n')
print(f'literals: {len(inputs)} profile/encoding rejections and {len(mutations)} object-contract mutations passed')
