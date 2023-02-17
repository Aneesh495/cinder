#!/usr/bin/env python3
"""Exercise successful full verification and reject altered gate envelopes."""
import copy
import json
from pathlib import Path
import sys

sys.path.insert(0,str(Path('tools').resolve()))
from evidence_integrity import EvidenceError, digest
from gate_registry import GATES, read_gate_report
from verify_evidence import verify

root=Path('.').resolve();compiler=Path(sys.argv[1]).resolve();base=root/'.agent-local/evidence'
manifest=base/'ACCEPTANCE.json';data=verify(root,manifest,compiler)
before={str(p.relative_to(base)):(p.stat().st_size,p.stat().st_mtime_ns) for p in base.rglob('*') if p.is_file()}
for key,spec in GATES.items():
    original=json.loads((base/data['gates'][key]['report']).read_text())
    for field,value in [('observations',original['observations']+1),('status','skipped'),('unit','unknown')]:
        altered=copy.deepcopy(original);altered[field]=value
        try:read_gate_report(key,altered,root,base,compiler)
        except EvidenceError:pass
        else:raise AssertionError('accepted altered '+key+' '+field)
assert before=={str(p.relative_to(base)):(p.stat().st_size,p.stat().st_mtime_ns) for p in base.rglob('*') if p.is_file()}
assert digest(compiler)==data['compiler_sha256']
print('Complete acceptance: every raw gate verified; sixty altered envelopes rejected; artifacts unchanged')
