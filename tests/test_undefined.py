#!/usr/bin/env python3
"""Classify undefined inputs without executing emitted native programs."""
import pathlib
import subprocess
import sys

compiler = str(pathlib.Path(sys.argv[1]).resolve())
sources = sorted(pathlib.Path('tests/undefined').glob('*.c'))
for source in sources:
    for level in ('-O0', '-O2'):
        result = subprocess.run([compiler, level, '--interpret', str(source)], capture_output=True, timeout=5)
        assert result.returncode != 0 and b'undefined' in result.stderr, (source, level, result)
        assert b'interpret main =>' not in result.stdout
print(f'undefined: {len(sources)} authored cases classified at both optimization levels')
