#!/usr/bin/env python3
"""Observe selected scalar forms, malformed plans and original native output."""
import hashlib
import json
import pathlib
import platform
import subprocess
import sys

from test_objects import inspect

probe = pathlib.Path(sys.argv[1]).resolve()
inputs = sorted(pathlib.Path('source').glob('*.[ch]')) + [pathlib.Path('CMakeLists.txt'),
    pathlib.Path('tests/mir_probe.c'), pathlib.Path('tests/test_selected_mir.py'), pathlib.Path('tests/test_objects.py')]
hash_file = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
hashes = {str(p): hash_file(p) for p in inputs}
identity = hashlib.sha256(probe.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
base = pathlib.Path('.agent-local/selected-mir') / identity
base.mkdir(parents=True, exist_ok=True)
native = platform.system() == 'Linux' and platform.machine() == 'x86_64'
records = []
for number in range(43):
    obj = base / f'{number:02d}.o'
    command = [str(probe), str(number), str(obj)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    assert result.returncode == 0 and result.stderr == '', (command, result)
    row = json.loads(result.stdout)
    assert row['case'] == number and row['interpreter'] == row['expected'] and row['rejected'] == 7
    parsed = inspect(obj)
    assert parsed['sections']['.text']['size'] > 0
    row.update(command=command, exit=result.returncode, stdout=result.stdout, stderr=result.stderr,
               object=str(obj), object_sha256=hash_file(obj), inspected=parsed)
    if native:
        executable = obj.with_suffix('.native')
        command = ['cc', '-no-pie', str(obj), '-o', str(executable)]
        linked = subprocess.run(command, capture_output=True, text=True, timeout=30)
        assert linked.returncode == 0, linked
        observed = subprocess.run([str(executable.resolve())], capture_output=True, text=True, timeout=10)
        assert observed.returncode == row['expected'] % 256 and observed.stdout == observed.stderr == '', observed
        row['native'] = dict(link=command, executable_sha256=hash_file(executable), exit=observed.returncode,
                             stdout=observed.stdout, stderr=observed.stderr)
    records.append(row)
    (base / 'observations.json').write_text(json.dumps(dict(inputs=hashes, probe_sha256=hash_file(probe),
        native=native, cases=records), indent=2) + '\n')
assert hashes == {str(p): hash_file(p) for p in inputs}, 'selected machine inputs changed during verification'
assert identity == hashlib.sha256(probe.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
print(f'Selected MIR: 43 integer/floating forms, 301 rejected plans and original objects passed; native={native}')
