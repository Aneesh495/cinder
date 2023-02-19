#!/usr/bin/env python3
"""Retain every discovered malformed input as a bounded driver regression."""
import pathlib
import subprocess
import sys

compiler = pathlib.Path(sys.argv[1]).resolve()
for path in sorted(pathlib.Path('tests/fuzz/regressions').glob('*.c')):
    result = subprocess.run([str(compiler), '-fsyntax-only', str(path)],
                            capture_output=True, timeout=5)
    assert result.returncode == 1 and b'error:' in result.stderr, (path, result)
    assert b'Sanitizer' not in result.stderr and b'runtime error:' not in result.stderr
print('Fuzz regressions: malformed inputs terminate with diagnostics')
