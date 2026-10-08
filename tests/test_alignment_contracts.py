#!/usr/bin/env python3
"""Reject corrupt alignment contracts and preserve prior published output."""
import hashlib
import json
import pathlib
import subprocess
import sys

compiler = pathlib.Path(sys.argv[1]).resolve()
irtool = compiler.parent / 'cinderir'
identity = hashlib.sha256(compiler.read_bytes() + irtool.read_bytes()).hexdigest()
root = pathlib.Path('.agent-local/alignment-contracts') / identity
root.mkdir(parents=True, exist_ok=True)
records = []


def record(row):
    records.append(row)
    (root / 'observations.json').write_text(json.dumps(dict(tools_sha256=identity, cases=records), indent=2)+'\n')


def reject(tool, source, label):
    output = root / 'prior.o'
    output.write_bytes(b'prior complete output')
    argv = [str(tool), '-c', str(source), '-o', str(output)]
    result = subprocess.run(argv, capture_output=True, text=True, timeout=20)
    row = dict(label=label, argv=argv, input_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), exit=result.returncode, stdout=result.stdout, stderr=result.stderr)
    record(row)
    assert result.returncode > 0 and ('error:' in result.stderr or 'fatal:' in result.stderr), row
    assert 'Sanitizer' not in result.stderr and 'runtime error:' not in result.stderr, row
    assert output.read_bytes() == b'prior complete output', row


seeds = {}
for name in ('local_scalar', 'global_data', 'member_layout'):
    source = pathlib.Path('tests/alignment') / (name+'.c')
    output = root / (name+'.cir')
    subprocess.run([str(compiler), '--serialize-ir', '-O0', str(source), '-o', str(output)], capture_output=True, check=True, timeout=20)
    seeds[name] = output.read_text()

mutations = []
for seed, kind in (('local_scalar', 'local'), ('global_data', 'global'), ('member_layout', 'field')):
    lines = seeds[seed].splitlines()
    index = next(i for i, line in enumerate(lines) if line.split()[0] == kind and 'align 16' in line)
    for alignment in (1, 3, 32):
        changed = lines.copy()
        changed[index] = changed[index].replace('align 16', 'align '+str(alignment), 1)
        mutations.append((kind+'-'+str(alignment), '\n'.join(changed)+'\n'))

lines = seeds['member_layout'].splitlines()
index = next(i for i, line in enumerate(lines) if line.split()[:1] == ['type'] and line.split()[2] == 'struct')
for label, column, value in (('aggregate-alignment', 8, '8'), ('aggregate-extra-padding', 7, '48')):
    changed = lines.copy()
    words = changed[index].split(); words[column] = value; changed[index] = ' '.join(words)
    mutations.append((label, '\n'.join(changed)+'\n'))
mutations.append(('old-schema', seeds['local_scalar'].replace('cinder-ir 6', 'cinder-ir 2', 1)))
for label, content in mutations:
    fixture = root / (label+'.invalid.cir'); fixture.write_text(content)
    reject(irtool, fixture, label)

extended = pathlib.Path('tests/alignment/profile/extended_alignment.c')
reject(compiler, extended, 'unsupported-extended-alignment')
assert 'supported power-of-two' in records[-1]['stderr']

for depth in (8, 65, 1024):
    typename = 'char'
    for _ in range(depth): typename = 'struct { _Alignas('+typename+') char x; }'
    source = root / ('nested-'+str(depth)+'.c')
    source.write_text('int main(void) { _Alignas('+typename+') char x=3; return x!=3; }\n')
    if depth < 64:
        result = subprocess.run([str(compiler), '--interpret', str(source)], capture_output=True, text=True, timeout=20)
        row = dict(label='nested-'+str(depth), exit=result.returncode, stdout=result.stdout, stderr=result.stderr)
        record(row); assert result.returncode == 0 and 'interpret main => 0' in result.stdout, row
    else:
        reject(compiler, source, 'nested-'+str(depth))
        assert 'alignment specifier nesting exceeds the profile limit' in records[-1]['stderr']
assert hashlib.sha256(compiler.read_bytes()+irtool.read_bytes()).hexdigest() == identity
print(f'alignment contracts: {len(mutations)} malformed IR contracts and extended/nesting limits passed')
