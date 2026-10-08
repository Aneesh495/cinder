#!/usr/bin/env python3
"""Require authored invalid translation units to fail before output publication."""
import pathlib
import hashlib
import json
import subprocess
import sys
import tempfile
import shutil
from reference_policy import identify

compiler = str(pathlib.Path(sys.argv[1]).resolve())
policy = json.loads(pathlib.Path('tests/constraints/reference_policy.json').read_text())
messages = json.loads(pathlib.Path('tests/constraints/diagnostics.json').read_text())
records = []
references = [shutil.which('gcc-15') or shutil.which('gcc'), shutil.which('clang')]
assert all(references), 'GCC and Clang diagnostic references are required'
identities = {reference: identify(reference) for reference in references}
base = pathlib.Path('.agent-local/constraints') / hashlib.sha256(pathlib.Path(compiler).read_bytes()).hexdigest()
base.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='cinder-constraints-') as directory:
    output = pathlib.Path(directory) / 'prior.o'
    sources = sorted(pathlib.Path('tests/constraints').glob('*.c'))
    for source in sources:
        rule = policy.get(source.name, {'require_reference_rejection': True})
        if not rule['require_reference_rejection'] or rule.get('allowed_reference_acceptance'):
            assert hashlib.sha256(source.read_bytes()).hexdigest() == rule['source_sha256'], (source, 'changed adjudicated diagnostic input')
        reference_rows = []
        for command in references:
            reference = subprocess.run([command, '-std=c17', '-pedantic-errors', '-c', str(source), '-o', str(output)], capture_output=True, timeout=10)
            assert reference.returncode >= 0, (source, reference)
            family = identities[command]['family']
            discrepancy = rule['require_reference_rejection'] and reference.returncode == 0
            if discrepancy:
                assert family in rule.get('allowed_reference_acceptance', []), (source, reference, 'unadjudicated reference diagnostic discrepancy')
            reference_rows.append(dict(**identities[command], exit=reference.returncode, stdout=reference.stdout.decode(errors='replace'), stderr=reference.stderr.decode(errors='replace'), mandatory_constraint=rule['require_reference_rejection'], adjudicated_discrepancy=discrepancy))
        output.write_bytes(b'prior complete output')
        result = subprocess.run([compiler, '-c', str(source), '-o', str(output)], capture_output=True, timeout=10)
        assert result.returncode > 0 and b'error:' in result.stderr and b'runtime error:' not in result.stderr and b'Sanitizer' not in result.stderr, (source, result)
        for message in messages.get(source.name, []):
            assert message.encode() in result.stderr, (source, message, result.stderr)
        assert output.read_bytes() == b'prior complete output', source
        records.append(dict(source=str(source), source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), policy=rule,
            reference=reference_rows[-1], references=reference_rows,
            cinder=dict(exit=result.returncode, stderr=result.stderr.decode(errors='replace'))))
    assert sorted(pathlib.Path(directory).iterdir()) == [output]
(base / 'observations.json').write_text(json.dumps(records, indent=2) + '\n')
print(f'constraints: {len(sources)} Cinder source rejections passed; reference semantic-rule differences retained')
