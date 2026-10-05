#!/usr/bin/env python3
"""Check authored memory undefined behavior through the canonical IR interface."""
import hashlib
import json
import pathlib
import subprocess
import sys

compiler = pathlib.Path(sys.argv[1]).resolve()
irtool = compiler.parent / 'cinderir'
identity = hashlib.sha256(compiler.read_bytes() + irtool.read_bytes()).hexdigest()
base = pathlib.Path('.agent-local/memory') / identity
base.mkdir(parents=True, exist_ok=True)
observations = []
cases = json.loads(pathlib.Path('tests/memory/undefined.json').read_text())
for index, case in enumerate(cases):
    source = pathlib.Path('tests/memory') / case['source']
    for level in ('-O0', '-O2'):
        artifact = base / f'{index}{level}.cir'
        command = [str(compiler), '--serialize-ir', level, '-fverify-each', str(source), '-o', str(artifact)]
        result = subprocess.run(command, capture_output=True, timeout=10)
        assert result.returncode == 0, (command, result)
        result = subprocess.run([str(irtool), '--classify', str(artifact)], capture_output=True, timeout=10)
        assert result.returncode == 0, (artifact, result)
        actual = json.loads(result.stdout)
        assert actual['valid'] is False and actual['classification'] == case['classification'], (source, level, actual, result.stderr)
        observations.append(dict(case, level=level, actual=actual, source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), ir_sha256=hashlib.sha256(artifact.read_bytes()).hexdigest()))
(base / 'observations.json').write_text(json.dumps(observations, indent=2) + '\n')
print(f'memory: {len(cases)} authored undefined cases classified at both optimization levels')

# Change contracts on real memory instructions while preserving the rest of
# each canonical module. Every rejection must leave a previous output intact.
seeds = {}
for name in ('member_array', 'global_array', 'pointer_difference'):
    path = base / (name + '.cir')
    result = subprocess.run([str(compiler), '--serialize-ir', '-O0', 'tests/memory/' + name + '.c', '-o', str(path)], capture_output=True, timeout=10)
    assert result.returncode == 0, result
    seeds[name] = path.read_text()
mutations = [
    ('member_array', 'local.address', {10: '999999'}),
    ('member_array', 'local.begin', {6: '0'}),
    ('member_array', 'local.end', {10: '-1'}),
    ('member_array', 'memory.load', {6: 'none'}),
    ('member_array', 'memory.store', {5: '0'}),
    ('member_array', 'memory.store', {2: 'none'}),
    ('member_array', 'local.begin', {2: 'none'}),
    ('member_array', 'pointer.offset', {8: '0000000000000000'}),
    ('member_array', 'pointer.offset', {8: 'ffffffffffffffff'}),
    ('member_array', 'pointer.offset', {8: '0000000000000003'}),
    ('member_array', 'pointer.offset', {11: '0'}),
    ('member_array', 'pointer.offset', {11: '2'}),
    ('member_array', 'pointer.member', {8: '0000000000000001'}),
    ('member_array', 'pointer.member', {10: '9999'}),
    ('member_array', 'pointer.member', {10: '1'}),
    ('global_array', 'global.address', {12: 'x6d697373696e67'}),
    ('pointer_difference', 'pointer.diff', {8: '0000000000000000'}),
]
for index, (seed, opcode, edits) in enumerate(mutations):
    lines = seeds[seed].splitlines()
    found = False
    for line_index, line in enumerate(lines):
        columns = line.split()
        if columns[:2] == ['inst', opcode]:
            for column, value in edits.items(): columns[column] = value
            lines[line_index] = ' '.join(columns)
            found = True
            break
    assert found, (seed, opcode)
    invalid = base / f'mutation-{index}.cir'
    invalid.write_text('\n'.join(lines) + '\n')
    prior = base / 'preserved.o'
    prior.write_bytes(b'prior complete output')
    for mode in ('--verify', '-c'):
        command = [str(irtool), mode, str(invalid)]
        if mode == '-c': command += ['-o', str(prior)]
        result = subprocess.run(command, capture_output=True, timeout=10)
        assert result.returncode != 0 and (b'error:' in result.stderr or b'fatal:' in result.stderr), (seed, opcode, edits, mode, result)
        assert prior.read_bytes() == b'prior complete output'
print(f'memory verifier: {len(mutations)} malformed instruction contracts rejected')
