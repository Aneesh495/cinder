#!/usr/bin/env python3
"""Require authored invalid translation units to fail before output publication."""
import pathlib
import hashlib
import json
import subprocess
import sys
import tempfile

compiler = str(pathlib.Path(sys.argv[1]).resolve())
policy = json.loads(pathlib.Path('tests/constraints/reference_policy.json').read_text())
records = []
base = pathlib.Path('.agent-local/constraints') / hashlib.sha256(pathlib.Path(compiler).read_bytes()).hexdigest()
base.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='cinder-constraints-') as directory:
    output = pathlib.Path(directory) / 'prior.o'
    sources = sorted(pathlib.Path('tests/constraints').glob('*.c'))
    for source in sources:
        reference = subprocess.run(['cc', '-std=c17', '-pedantic-errors', '-c', str(source), '-o', str(output)], capture_output=True, timeout=10)
        rule = policy.get(source.name, {'require_reference_rejection': True})
        assert reference.returncode >= 0, (source, reference)
        if rule['require_reference_rejection']: assert reference.returncode != 0, (source, reference)
        output.write_bytes(b'prior complete output')
        result = subprocess.run([compiler, '-c', str(source), '-o', str(output)], capture_output=True, timeout=10)
        assert result.returncode > 0 and b'error:' in result.stderr and b'runtime error:' not in result.stderr and b'Sanitizer' not in result.stderr, (source, result)
        assert output.read_bytes() == b'prior complete output', source
        records.append(dict(source=str(source), source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), policy=rule,
            reference=dict(exit=reference.returncode, stdout=reference.stdout.decode(errors='replace'), stderr=reference.stderr.decode(errors='replace')),
            cinder=dict(exit=result.returncode, stderr=result.stderr.decode(errors='replace'))))
    assert sorted(pathlib.Path(directory).iterdir()) == [output]
(base / 'observations.json').write_text(json.dumps(records, indent=2) + '\n')
print(f'constraints: {len(sources)} Cinder source rejections passed; reference semantic-rule differences retained')
