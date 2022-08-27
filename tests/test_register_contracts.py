#!/usr/bin/env python3
"""Check register storage and variadic eligibility through canonical IR and native objects."""
import hashlib
import json
import pathlib
import platform
import subprocess
import sys

compiler = pathlib.Path(sys.argv[1]).resolve()
irtool = compiler.parent / 'cinderir'
identity = hashlib.sha256(compiler.read_bytes()+irtool.read_bytes()).hexdigest()
root = pathlib.Path('.agent-local/register-contracts') / identity
root.mkdir(parents=True, exist_ok=True)
records = []
native = platform.system() == "Linux" and platform.machine() == "x86_64"

def run(argv):
    return subprocess.run([str(arg) for arg in argv], capture_output=True, text=True, timeout=20)

def save(row):
    records.append(row); (root/'observations.json').write_text(json.dumps(records, indent=2)+'\n')

for source in sorted(pathlib.Path('tests/register').glob('*.c')):
    for level in ('-O0', '-O2'):
        cir = root/(source.stem+level+'.cir'); canonical = cir.with_suffix('.again.cir')
        obj, parsed = cir.with_suffix('.o'), cir.with_suffix('.parsed.o')
        for argv in ([compiler, '--serialize-ir', level, '-fverify-each', source, '-o', cir], [irtool, cir, '-o', canonical], [compiler, '-c', level, source, '-o', obj], [irtool, '-c', cir, '-o', parsed]):
            result = run(argv); assert result.returncode == 0, (argv, result)
        value = json.loads(run([irtool, '--classify', cir]).stdout)
        assert value['valid'] and value['integer'] == 0, (source, level, value)
        assert cir.read_bytes() == canonical.read_bytes() and obj.read_bytes() == parsed.read_bytes(), (source, level)
        if native:
            executable=cir.with_suffix('.native');result=run(['cc','-no-pie',parsed,'-o',executable]);assert result.returncode==0,result
            result=run([executable.resolve()]);assert result.returncode==0 and not result.stdout and not result.stderr,(source,level,result)
        save(dict(kind='authored', native=native, native_exit=result.returncode if native else None, source=str(source), source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), level=level, interpreter=value, ir_sha256=hashlib.sha256(cir.read_bytes()).hexdigest(), object_sha256=hashlib.sha256(obj.read_bytes()).hexdigest()))

assert hashlib.sha256(compiler.read_bytes()+irtool.read_bytes()).hexdigest()==identity
print(f"Register contracts: {len(records)} authored CIR executions and object identities passed; native={native}.")
