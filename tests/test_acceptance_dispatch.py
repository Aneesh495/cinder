#!/usr/bin/env python3
"""Exercise implemented gate generation while incomplete acceptance stays failed."""
import json
import pathlib
import platform
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path('tools').resolve()))
from evidence_integrity import digest, verify_bindings
from gate_registry import read_gate_report

root = pathlib.Path('.').resolve()
compiler = pathlib.Path(sys.argv[1]).resolve()
command = [sys.executable, '-B', 'tools/acceptance.py', 'run', str(compiler)]
result = subprocess.run(command, capture_output=True, text=True, timeout=1800)
assert result.returncode == 1 and 'Full acceptance is incomplete' in result.stderr, result
base = root / '.agent-local/evidence'
manifest = base / 'ACCEPTANCE.json'
data = json.loads(manifest.read_text())
assert data['status'] == 'incomplete' and 'nonvacuous-passes' not in data['open_campaign_readers']
assert data['open_campaign_readers'], 'this test requires remaining incomplete campaign readers'
verify_bindings(data, root, base, compiler)
native = platform.system() == 'Linux' and platform.machine() == 'x86_64'
gate = data['gates']['nonvacuous-passes']
if native:
    assert gate['status'] == 'pass' and gate['report'] == 'nonvacuous-passes.json', gate
    report = json.loads((base / gate['report']).read_text())
    proof = read_gate_report('nonvacuous-passes', report, root, base, compiler)
    assert proof['passes'] == 10 and proof['native_executions'] == 40
else:
    assert gate['status'] == 'unverified' and 'native Linux' in gate['reason'], gate
before = {str(p.relative_to(base)): (digest(p), p.stat().st_mtime_ns)
          for p in base.rglob('*') if p.is_file()}
result = subprocess.run([sys.executable, '-B', 'tools/verify_evidence.py', str(compiler)],
                        capture_output=True, text=True, timeout=60)
assert result.returncode == 1 and 'acceptance is incomplete' in result.stderr, result
after = {str(p.relative_to(base)): (digest(p), p.stat().st_mtime_ns)
         for p in base.rglob('*') if p.is_file()}
assert before == after, 'verification rewrote acceptance evidence'
print(f'Acceptance dispatch: native pass proof={native}; incomplete full acceptance rejected without mutation')
