#!/usr/bin/env python3
"""Recompute private census data and reject count or inventory substitution."""
import copy
import json
import pathlib
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path('tools').resolve()))
from evidence_integrity import EvidenceError, digest
from gate_registry import GATES, read_gate_report
from source_census import census

root = pathlib.Path('.').resolve()
compiler = pathlib.Path(sys.argv[1]).resolve()
original = census(root)
before = {name: (digest(root / name), (root / name).stat().st_mtime_ns)
          for name in (row['path'] for row in original['files'])}
with tempfile.TemporaryDirectory(prefix='cinder-census-reader-') as directory:
    base = pathlib.Path(directory)
    raw = base / 'census.json'
    raw.write_text(json.dumps(original) + '\n')
    report = dict(status='pass', unit=GATES['private-size']['unit'],
                  observations=original['total_substantive_lines'], raw_report='census.json')
    observed = read_gate_report('private-size', report, root, base, compiler)
    assert observed == original
    for change in ('count', 'missing-file', 'changed-sha', 'duplicate-file', 'changed-rules', 'changed-schema', 'extent'):
        altered = copy.deepcopy(original)
        if change == 'count': altered['total_substantive_lines'] += 1
        elif change == 'missing-file': altered['files'].pop()
        elif change == 'changed-sha': altered['files'][0]['sha256'] = '0' * 64
        elif change == 'duplicate-file': altered['files'].append(altered['files'][0])
        elif change == 'changed-rules': altered['rules']['excluded'] = ''
        elif change == 'changed-schema': altered['schema'] += 1
        else: altered['files'][0]['substantive_lines'] += 1
        raw.write_text(json.dumps(altered) + '\n')
        try: read_gate_report('private-size', report, root, base, compiler)
        except EvidenceError: pass
        else: raise AssertionError('accepted altered census: ' + change)
    raw.write_text(json.dumps(original) + '\n')
    for key, value in (('status', 'skipped'), ('unit', 'file-count'),
                       ('observations', original['total_substantive_lines'] + 1),
                       ('raw_report', '../escaped.json')):
        altered = dict(report); altered[key] = value
        try: read_gate_report('private-size', altered, root, base, compiler)
        except EvidenceError: pass
        else: raise AssertionError('accepted altered census envelope: ' + key)
    assert json.loads(raw.read_text()) == original, 'reader rewrote census inputs'
after = {name: (digest(root / name), (root / name).stat().st_mtime_ns) for name in before}
assert before == after, 'reader changed production source'
print('Private census gate: complete inventory recomputed; eleven substitutions rejected; source unchanged')
