#!/usr/bin/env python3
"""Reject corrupted scalar plans and execute their unchanged original objects."""
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
sources = set(pathlib.Path('tests/bitfields').glob('*.c')) | set(pathlib.Path('tests/machine_memory').glob('*.c'))
sources |= {p for p in pathlib.Path('tests/control').glob('*.c')
            if p.name.startswith(('convert_', 'float_', 'literal_float'))}
sources = sorted(sources)
inputs = sorted(pathlib.Path('source').glob('*.[ch]')) + [pathlib.Path('CMakeLists.txt'),
    pathlib.Path('tests/mir_scalar_probe.c'), pathlib.Path('tests/test_mir_scalar.py'),
    pathlib.Path('tests/test_objects.py'), pathlib.Path('tests/control/cases.json')] + sources
inputs += [p for p in sorted(pathlib.Path('runtime').rglob('*')) if p.is_file()]
hashes = {str(p): hash_file(p) for p in inputs}
identity = hashlib.sha256(probe.read_bytes() + compiler.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
base = pathlib.Path('.agent-local/mir-scalar') / identity
base.mkdir(parents=True, exist_ok=True)
native = platform.system() == 'Linux' and platform.machine() == 'x86_64'
ledger = {pathlib.Path('tests/control', row['source']).resolve(): row['exit']
          for row in json.loads(pathlib.Path('tests/control/cases.json').read_text())}
rows = []
memory_profile = conversion_profile = addresses = 0
for index, source in enumerate(sources):
    obj, original = base / f'{index:03d}.o', base / f'{index:03d}.source.o'
    command = [str(probe), str(source), str(obj)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    assert result.returncode == 0 and result.stderr == '', (source, result)
    row = json.loads(result.stdout)
    expected_mutations = 6 * row['memory'] + 11 * row['bitfield'] + 2 * row['conversion'] + 2 * row['unary'] + 3 * row['values']
    assert row['values'] > 0 and row['rejected'] == expected_mutations, row
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
    memory_profile |= row['memory_profile']; conversion_profile |= row['conversion_profile']; addresses |= row['addresses']
    rows.append(row)
    (base / 'observations.json').write_text(json.dumps(dict(inputs=hashes, probe_sha256=hash_file(probe),
        compiler_sha256=hash_file(compiler), native=native, cases=rows), indent=2) + '\n')
assert memory_profile == 0xfff and conversion_profile == 0x7f and addresses == 7, (memory_profile, conversion_profile, addresses)
assert hashes == {str(p): hash_file(p) for p in inputs}, 'scalar selection inputs changed during verification'
assert identity == hashlib.sha256(probe.read_bytes() + compiler.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
print(f'Machine scalar plans: {len(rows)} source cases, {sum(r["rejected"] for r in rows)} rejected plans; 12 storage profiles, 7 conversion modes and original object identity passed; native={native}')
