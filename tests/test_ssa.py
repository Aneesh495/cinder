#!/usr/bin/env python3
"""Execute physical phi cycles in GPR, float32, and float64 locations."""
import hashlib
import json
import pathlib
import platform
import subprocess
import sys

probe = str(pathlib.Path(sys.argv[1]).resolve())
count = int(sys.argv[2]) if len(sys.argv) > 2 else 1000
native = platform.system() == 'Linux' and platform.machine() == 'x86_64'
base = pathlib.Path('.agent-local/ssa') / hashlib.sha256(pathlib.Path(probe).read_bytes()).hexdigest()
base.mkdir(parents=True, exist_ok=True)
with (base / 'observations.jsonl').open('w') as observations:
    for seed in range(count):
        obj = base / f'cycle-{seed}.o'
        result = subprocess.run([probe, str(seed), str(obj)], capture_output=True, timeout=10)
        assert result.returncode == 0, (seed, result)
        record = json.loads(result.stdout)
        assert record['interpreter'] == record['expected'] and obj.stat().st_size > 0
        record['object_sha256'] = hashlib.sha256(obj.read_bytes()).hexdigest()
        record['native'] = native
        if native:
            binary = base / f'cycle-{seed}'
            result = subprocess.run(['cc', '-no-pie', str(obj), '-o', str(binary)], capture_output=True, timeout=10)
            assert result.returncode == 0, (seed, result)
            result = subprocess.run([str(binary)], capture_output=True, timeout=5)
            assert result.returncode == record['expected'] % 256, (seed, record, result)
            record['native_exit'] = result.returncode
        observations.write(json.dumps(record, sort_keys=True) + '\n')
print(f'ssa: {count} simultaneous phi/critical-edge/physical-cycle cases passed; native={native}')
