#!/usr/bin/env python3
"""Check sealed ABI plans and their original-object behavior on real source."""
import hashlib
import json
import pathlib
import platform
import subprocess
import sys

from test_objects import inspect

probe = pathlib.Path(sys.argv[1]).resolve()
compiler = probe.parent / 'cindercc'
hash_file = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
inputs = sorted(pathlib.Path('source').glob('*.[ch]')) + [pathlib.Path('CMakeLists.txt'),
    pathlib.Path('tests/mir_abi_probe.c'), pathlib.Path('tests/test_mir_abi.py'),
    pathlib.Path('tests/test_objects.py'), pathlib.Path('tests/control/cases.json')]
sources = sorted({p for group in ('aggregate_abi', 'variadic', 'pointer_words')
                  for p in pathlib.Path('tests', group).glob('*.c')})
inputs += sources + [p for p in sorted(pathlib.Path('runtime').rglob('*')) if p.is_file()]
hashes = {str(p): hash_file(p) for p in inputs}
identity = hashlib.sha256(probe.read_bytes() + compiler.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
base = pathlib.Path('.agent-local/mir-abi') / identity
base.mkdir(parents=True, exist_ok=True)
native = platform.system() == 'Linux' and platform.machine() == 'x86_64'
ledger = {pathlib.Path('tests/control', row['source']).resolve(): row['exit']
          for row in json.loads(pathlib.Path('tests/control/cases.json').read_text())}
rows = []
for index, source in enumerate(sources):
    obj, original = base / f'{index:02d}.o', base / f'{index:02d}.source.o'
    command = [str(probe), str(source), str(obj)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    assert result.returncode == 0 and result.stderr == '', (source, result)
    row = json.loads(result.stdout)
    assert row['functions'] > 0 and row['rejected'] == row['functions'] * 6 + row['calls'] * 7 + row['variadic'] * 3, row
    result = subprocess.run([str(compiler), '-O0', '-c', str(source), '-o', str(original)], capture_output=True, text=True, timeout=30)
    assert result.returncode == 0 and obj.read_bytes() == original.read_bytes(), (source, result)
    layout = inspect(obj)
    assert layout['sections']['.text']['size'] > 0
    row.update(source=str(source), source_sha256=hash_file(source), command=command, object=str(obj),
               object_sha256=hash_file(obj), expected=ledger[source.resolve()], layout=layout)
    if native:
        executable = obj.with_suffix('.native')
        result = subprocess.run(['cc', '-no-pie', str(obj), '-o', str(executable)], capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result
        result = subprocess.run([str(executable.resolve())], capture_output=True, text=True, timeout=10)
        assert result.returncode == row['expected'] and result.stdout == result.stderr == '', (source, result)
        row['native'] = dict(exit=result.returncode, stdout=result.stdout, stderr=result.stderr, executable_sha256=hash_file(executable))
    rows.append(row)
    (base / 'observations.json').write_text(json.dumps(dict(inputs=hashes, probe_sha256=hash_file(probe),
        compiler_sha256=hash_file(compiler), native=native, cases=rows), indent=2) + '\n')
assert sum(r['calls'] for r in rows) > 0 and sum(r['variadic'] for r in rows) > 0
assert hashes == {str(p): hash_file(p) for p in inputs}, 'machine ABI inputs changed during verification'
assert identity == hashlib.sha256(probe.read_bytes() + compiler.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
print(f'Machine ABI plans: {len(rows)} source cases, {sum(r["rejected"] for r in rows)} rejected plans/signatures and original object identity passed; native={native}')
