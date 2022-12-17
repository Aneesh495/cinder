#!/usr/bin/env python3
"""Exercise suspended call results and the exact interpreted depth boundary."""
import pathlib
import subprocess
import sys

compiler = str(pathlib.Path(sys.argv[1]).resolve())
for level in ('-O0', '-O2'):
    for depth in (0, 253, 254, 255, 300):
        command = [compiler, level, '-fverify-each', f'-DDEPTH={depth}', '--interpret',
                   'tests/interpreter/call_depth.c']
        result = subprocess.run(command, capture_output=True, timeout=10)
        if depth <= 254:
            assert result.returncode == 0 and result.stdout == f'interpret main => {depth}\n'.encode(), (command, result)
            assert result.stderr == b'', (command, result)
        else:
            assert result.returncode == 1 and b'call-depth limit exceeded' in result.stderr, (command, result)
            assert b'interpret main =>' not in result.stdout, (command, result)
    result = subprocess.run([compiler, level, '--interpret', 'tests/control/call_resume.c'], capture_output=True, timeout=10)
    assert result.returncode == 0 and result.stdout == b'interpret main => 56\n' and result.stderr == b'', result
print('Interpreter calls: exact depth boundary and suspended loop calls passed at O0/O2')
