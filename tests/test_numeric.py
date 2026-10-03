#!/usr/bin/env python3
import pathlib
import subprocess
import sys
import tempfile

compiler = str(pathlib.Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix='cinder-numeric-') as directory:
    output = pathlib.Path(directory) / 'prior.o'
    for source in sorted(pathlib.Path('tests/numeric').glob('invalid-*.c')):
        output.write_bytes(b'prior complete output')
        result = subprocess.run([compiler, '-c', str(source), '-o', str(output)], capture_output=True, timeout=5)
        assert result.returncode != 0 and b'invalid numeric constant' in result.stderr, (source, result)
        assert output.read_bytes() == b'prior complete output', source
    assert sorted(pathlib.Path(directory).iterdir()) == [output]
print('numeric: malformed literals rejected and prior outputs preserved')
