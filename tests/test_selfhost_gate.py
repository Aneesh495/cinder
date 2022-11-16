#!/usr/bin/env python3
"""Dispatch the complete bootstrap reader and reject inflated gate envelopes."""
import argparse
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
from evidence_integrity import EvidenceError, digest
from gate_registry import GATES, read_gate_report

parser = argparse.ArgumentParser()
parser.add_argument('artifacts', type=pathlib.Path)
parser.add_argument('--source', type=pathlib.Path, default=pathlib.Path('.'))
args = parser.parse_args()
raw, source = args.artifacts.resolve(), args.source.resolve()
compiler = raw / 'seed-compiler/cindercc'
base = raw.parent
report = dict(status='pass', unit=GATES['self-hosting']['unit'],
              observations=1000, raw_directory=raw.name)
before = {str(p.relative_to(raw)): (digest(p), p.stat().st_mtime_ns)
          for p in raw.rglob('*') if p.is_file()}
proof = read_gate_report('self-hosting', report, source, base, compiler)
assert proof['programs'] == 1000 and proof['native'] == 8000 and proof['interpreted'] == 4000
for key, value in (('status', 'skipped'), ('unit', 'native-object'), ('observations', 1001),
                   ('observations', True), ('raw_directory', '../escaped')):
    altered = dict(report); altered[key] = value
    try: read_gate_report('self-hosting', altered, source, base, compiler)
    except EvidenceError: pass
    else: raise AssertionError('accepted altered bootstrap gate: ' + key)
after = {str(p.relative_to(raw)): (digest(p), p.stat().st_mtime_ns)
         for p in raw.rglob('*') if p.is_file()}
assert before == after, 'gate reader changed bootstrap evidence'
print('Self-host gate: complete native stages reconstructed; five altered envelopes rejected; artifacts unchanged')
