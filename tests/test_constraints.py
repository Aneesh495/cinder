#!/usr/bin/env python3
"""Require authored invalid translation units to fail before output publication."""
import pathlib
import subprocess
import sys
import tempfile

compiler = str(pathlib.Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix='cinder-constraints-') as directory:
    output = pathlib.Path(directory) / 'prior.o'
    sources = sorted(pathlib.Path('tests/constraints').glob('*.c'))
    for source in sources:
        reference = subprocess.run(['cc', '-std=c17', '-pedantic-errors', '-c', str(source), '-o', str(output)], capture_output=True, timeout=10)
        assert reference.returncode != 0, (source, reference)
        output.write_bytes(b'prior complete output')
        result = subprocess.run([compiler, '-c', str(source), '-o', str(output)], capture_output=True, timeout=10)
        assert result.returncode != 0 and b'error:' in result.stderr, (source, result)
        assert output.read_bytes() == b'prior complete output', source
    assert sorted(pathlib.Path(directory).iterdir()) == [output]
print(f'constraints: {len(sources)} invalid source cases rejected')
