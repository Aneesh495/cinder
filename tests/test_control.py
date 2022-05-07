#!/usr/bin/env python3
"""Check defined source effects through reference, IR, and native execution."""
import hashlib
import json
import pathlib
import platform
import re
import subprocess
import sys


def main():
    compiler = str(pathlib.Path(sys.argv[1]).resolve())
    root = pathlib.Path(__file__).resolve().parent / 'control'
    native = platform.system() == 'Linux' and platform.machine() == 'x86_64'
    cases = json.loads((root / 'cases.json').read_text())
    observations = []
    compiler_hash = hashlib.sha256(pathlib.Path(compiler).read_bytes()).hexdigest()
    evidence = pathlib.Path('.agent-local/control')
    temporary = evidence / compiler_hash
    temporary.mkdir(parents=True, exist_ok=True)
    for index, case in enumerate(cases):
        path = root / case['source']
        reference = temporary / f'reference-{index}'
        result = subprocess.run(['cc', '-std=c17', '-O0', str(path), '-o', str(reference)], capture_output=True, timeout=20)
        assert result.returncode == 0, (path, result.stderr)
        result = subprocess.run([str(reference)], capture_output=True, timeout=5)
        assert result.returncode == case['exit'], (path, result)
        record = dict(case, source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(), reference_exit=result.returncode, native=native)
        for level in ('-O0', '-O2'):
            result = subprocess.run([compiler, level, '-fverify-each', '--interpret', str(path)], capture_output=True, text=True, timeout=10)
            match = re.search(r'interpret main => (-?\d+)', result.stdout)
            assert result.returncode == 0 and match and int(match[1]) == case['exit'], (path, level, result)
            record[level + '_interpreter'] = int(match[1])
            output = temporary / f'cinder-{index}{level}'
            arguments = [compiler, level, '-fverify-each', str(path), '-o', str(output)]
            if not native: arguments.insert(1, '-c')
            result = subprocess.run(arguments, capture_output=True, timeout=10)
            assert result.returncode == 0, (path, level, result.stderr)
            record[level + '_object_sha256'] = hashlib.sha256(output.read_bytes()).hexdigest()
            if native:
                result = subprocess.run([str(output)], capture_output=True, timeout=5)
                assert result.returncode == case['exit'], (path, level, result)
                record[level + '_native_exit'] = result.returncode
        observations.append(record)
        (temporary / 'observations.json').write_text(json.dumps(observations, indent=2) + '\n')
    (evidence / 'observations.json').write_text(json.dumps(observations, indent=2) + '\n')
    print(f'control: {len(cases)} reference/interpreter/object cases passed; native={native}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
