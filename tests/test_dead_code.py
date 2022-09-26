#!/usr/bin/env python3
"""Compare real CFG transformations, semantic results, and native objects."""
import hashlib
import json
import pathlib
import platform
import subprocess
import sys

compiler = pathlib.Path(sys.argv[1]).resolve()
irtool = compiler.parent / 'cinderir'
inputs = [pathlib.Path(__file__), pathlib.Path('tests/optimization/dead_cases.json')] + sorted(pathlib.Path('tests/optimization').glob('dead_*.c'))
hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
identity = hashlib.sha256(compiler.read_bytes() + irtool.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
root = pathlib.Path('.agent-local/dead-code') / identity
root.mkdir(parents=True, exist_ok=True)
native = platform.system() == 'Linux' and platform.machine() == 'x86_64'
rows = []

def run(argv):
    result = subprocess.run([str(a) for a in argv], capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, (argv, result)
    return result

def save(row):
    rows.append(row)
    (root / 'observations.json').write_text(json.dumps(dict(inputs=hashes, compiler_sha256=hashlib.sha256(compiler.read_bytes()).hexdigest(), irtool_sha256=hashlib.sha256(irtool.read_bytes()).hexdigest(), native=native, cases=rows), indent=2) + '\n')

for case in json.loads(inputs[1].read_text()):
    source = pathlib.Path('tests/optimization') / case['source']
    before, after = root / (source.stem + '.before.cir'), root / (source.stem + '.after.cir')
    run([compiler, '-O0', '-fverify-each', '--serialize-ir', source, '-o', before])
    run([irtool, '-O2', before, '-o', after])
    before_value = json.loads(run([irtool, '--classify', before]).stdout)
    after_value = json.loads(run([irtool, '--classify', after]).stdout)
    assert before_value == after_value and after_value['valid'] and after_value['integer'] == case['expected'], (case, before_value, after_value)
    prefix='inst '+case['opcode']+' '
    blocks_before=sum(line.startswith(prefix) for line in before.read_text().splitlines())
    blocks_after=sum(line.startswith(prefix) for line in after.read_text().splitlines())
    assert (blocks_after<blocks_before)==case['changes'],(case,blocks_before,blocks_after)
    objects = []
    for level in ('-O0', '-O2'):
        obj = root / (source.stem + level + '.o')
        run([compiler, level, '-fverify-each', '-c', source, '-o', obj])
        row = dict(level=level, object=str(obj), sha256=hashlib.sha256(obj.read_bytes()).hexdigest())
        if native:
            binary = obj.with_suffix('.native')
            run(['cc', '-no-pie', obj, '-o', binary])
            result = subprocess.run([str(binary.resolve())], capture_output=True, text=True, timeout=10)
            assert result.returncode == case['expected'] and result.stdout == result.stderr == '', (case, result)
            row.update(native_exit=result.returncode, executable_sha256=hashlib.sha256(binary.read_bytes()).hexdigest())
        objects.append(row)
    save(dict(case, source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), before_sha256=hashlib.sha256(before.read_bytes()).hexdigest(), after_sha256=hashlib.sha256(after.read_bytes()).hexdigest(), blocks_before=blocks_before, blocks_after=blocks_after, interpreter=after_value, objects=objects))

assert hashes == {str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
assert identity == hashlib.sha256(compiler.read_bytes()+irtool.read_bytes()+json.dumps(hashes,sort_keys=True).encode()).hexdigest()
print(f'Dead code: {len(rows)} positive/negative sources preserve values, definedness, calls, memory, and floating operations; native={native}')
