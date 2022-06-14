#!/usr/bin/env python3
"""Observe declaration coalescing at the actual ELF and canonical IR boundaries."""
import hashlib
import json
import pathlib
import subprocess
import sys
from test_objects import inspect

compiler = pathlib.Path(sys.argv[1]).resolve()
irtool = compiler.parent / 'cinderir'
base = pathlib.Path('.agent-local/linkage') / hashlib.sha256(compiler.read_bytes() + irtool.read_bytes()).hexdigest()
base.mkdir(parents=True, exist_ok=True)
cases = (
    ('extern_then_definition', 'value', 1, 1, 4),
    ('repeated_tentative', 'value', 1, 1, 4),
    ('extern_initializer', 'value', 1, 1, 4),
    ('static_then_extern', 'value', 0, 1, 4),
    ('function_static_inheritance', 'value', 0, 2, None),
    ('incomplete_tentative_array', 'data', 1, 1, 4),
    ('prior_bound_string_definition', 'text', 1, 1, 17),
    ('pointer_array_composite', 'p', 1, 1, 8),
)
records = []
for source, symbol, binding, kind, size in cases:
    for level in ('-O0', '-O2'):
        output = base / (source + level + '.o')
        argv = [str(compiler), level, '-fverify-each', '-c', 'tests/linkage/' + source + '.c', '-o', str(output)]
        result = subprocess.run(argv, capture_output=True, timeout=10)
        assert result.returncode == 0, (argv, result)
        actual = inspect(output)
        matches = [item for item in actual['symbols'] if item['name'] == symbol]
        assert len(matches) == 1 and matches[0]['binding'] == binding and matches[0]['kind'] == kind, matches
        if size is not None: assert matches[0]['size'] == size, matches
        records.append(dict(source=source, level=level, argv=argv, exit=result.returncode, object=actual))

seed = base / 'one-global.cir'
result = subprocess.run([str(compiler), '--serialize-ir', 'tests/linkage/repeated_tentative.c', '-o', str(seed)], capture_output=True, timeout=10)
assert result.returncode == 0, result
lines = seed.read_text().splitlines()
global_line = next(line for line in lines if line.startswith('global '))
assert 'globals 1' in lines
lines[lines.index('globals 1')] = 'globals 2'
lines.insert(lines.index(global_line), global_line)
invalid = base / 'duplicate-global.cir'
invalid.write_text('\n'.join(lines) + '\n')
prior = base / 'preserved.o'
for mode in ('--verify', '-c'):
    prior.write_bytes(b'prior complete output')
    argv = [str(irtool), mode, str(invalid)]
    if mode == '-c': argv += ['-o', str(prior)]
    result = subprocess.run(argv, capture_output=True, timeout=10)
    assert result.returncode > 0 and b'duplicate definitions' in result.stderr and b'Sanitizer' not in result.stderr and b'runtime error:' not in result.stderr, result
    assert prior.read_bytes() == b'prior complete output'
    records.append(dict(argv=argv, exit=result.returncode, stderr=result.stderr.decode()))
(base / 'observations.json').write_text(json.dumps(records, indent=2) + '\n')
print('linkage: 16 owned objects have unique, correctly bound symbols; duplicate IR globals rejected')
