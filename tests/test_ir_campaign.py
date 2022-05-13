#!/usr/bin/env python3
"""Retain and independently inventory every executed typed IR case."""
import collections
import hashlib
import json
import pathlib
import platform
import subprocess
import sys

def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(chunk)
    return value.hexdigest()

probe = pathlib.Path(sys.argv[1]).resolve()
count = int(sys.argv[2]) if len(sys.argv) > 2 else 100000
assert 0 < count <= 1000000
inputs = sorted(pathlib.Path('source').glob('*.[ch]')) + [pathlib.Path('CMakeLists.txt'), pathlib.Path('tests/ir_campaign.c'), pathlib.Path(__file__)]
hashes = {str(path): digest(path) for path in inputs}
source_hash = hashlib.sha256(json.dumps(hashes, sort_keys=True).encode()).hexdigest()
identity = digest(probe)
base = pathlib.Path('.agent-local/ir-campaign') / (source_hash + '-' + identity)
base.mkdir(parents=True, exist_ok=True)
archive = base / 'cases.cir'
records = base / 'observations.jsonl'
log = base / 'execution.log'
summary = base / 'summary.json'
summary.unlink(missing_ok=True)
command = [str(probe), str(count), str(archive)]
with records.open('wb') as out, log.open('wb') as err:
    result = subprocess.run(command, stdout=out, stderr=err, timeout=1800)
assert result.returncode == 0, (result.returncode, str(log))
assert digest(probe) == identity and hashes == {str(path): digest(path) for path in inputs}, 'campaign inputs changed during execution'
families = collections.Counter()
classes = collections.Counter()
observed = 0
offset = 0
with records.open() as stream, archive.open('rb') as modules:
    for observed, line in enumerate(stream, start=1):
        record = json.loads(line)
        assert record['case'] == observed - 1 and record['archive_offset'] == offset
        assert record['roundtrip'] is True and record['optimized_match'] is True
        extent = record['archive_bytes']
        assert 0 < extent <= 64 * 1024 * 1024
        text = modules.read(extent)
        assert len(text) == extent and text.startswith(b'cinder-ir 1 lp64-le sysv-x86-64\n') and text.endswith(b'end-module\n')
        offset += extent
        families[record['family']] += 1
        classes[record['classification']] += 1
    assert modules.read(1) == b''
assert observed == count and offset == archive.stat().st_size
if count >= 100000:
    assert set(families) == set(range(10)) and families[0] == 65536
    assert all(families[family] >= 3800 for family in range(1, 10))
    assert {0, 1, 2, 3, 4, 5, 7} <= set(classes)
evidence = {'schema': 1, 'source_sha256': source_hash, 'inputs': hashes, 'probe_sha256': identity,
            'command': command, 'platform': platform.platform(), 'requested': count, 'executed': observed,
            'defined': classes[0], 'classified': observed - classes[0], 'families': dict(families), 'classifications': dict(classes),
            'artifacts': {str(path): digest(path) for path in (archive, records, log)}}
summary.write_text(json.dumps(evidence, indent=2, sort_keys=True) + '\n')
print(f'IR campaign: {observed} executed round trips and optimizer comparisons; defined={classes[0]} classified={observed - classes[0]}; evidence={summary}')
