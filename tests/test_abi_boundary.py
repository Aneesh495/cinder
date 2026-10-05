#!/usr/bin/env python3
"""Observe poisoned scalar ABI bits against actual Linux reference toolchains."""
import hashlib
import json
import pathlib
import platform
import shutil
import subprocess
import sys

compiler = pathlib.Path(sys.argv[1]).resolve()
identity = hashlib.sha256(compiler.read_bytes()).hexdigest()
base = pathlib.Path('.agent-local/abi-boundary') / identity
base.mkdir(parents=True, exist_ok=True)
native = platform.system() == 'Linux' and platform.machine() == 'x86_64'
observations = []

def run(arguments):
    result = subprocess.run([str(a) for a in arguments], capture_output=True, timeout=30)
    observations.append(dict(argv=[str(a) for a in arguments], exit=result.returncode,
                             stdout=result.stdout.decode(), stderr=result.stderr.decode()))
    (base / 'commands.json').write_text(json.dumps(observations, indent=2) + '\n')
    assert result.returncode == 0, (arguments, result)
    return result

source = pathlib.Path('tests/abi/bool_boundary.c')
for level in ('-O0', '-O2'):
    output = base / ('cinder' + level + '.o')
    run([compiler, level, '-fverify-each', '-c', source, '-o', output])
    if native:
        for name in ('gcc', 'clang'):
            host = shutil.which(name)
            assert host, name
            executable = base / (name + level)
            run([host, '-std=c17', '-O2', '-no-pie', 'tests/abi/bool_boundary_host.c',
                 'tests/abi/bool_boundary.S', output, '-o', executable])
            result = run([executable])
            assert result.stdout == b'arguments=1 results=10\n', result
            reference = base / ('reference-' + name + level)
            run([host, '-std=c17', level, '-no-pie', 'tests/abi/bool_boundary_host.c',
                 'tests/abi/bool_boundary.S', source, '-o', reference])
            assert run([reference]).stdout == result.stdout

(base / 'summary.json').write_text(json.dumps(dict(native=native, compiler_sha256=identity,
    source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
    native_executions=8 if native else 0), indent=2) + '\n')
print(f'ABI boundary: two owned objects emitted; native={native}')
