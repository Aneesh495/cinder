#!/usr/bin/env python3
"""Check switch CFG round trips, source constraints, and bounded dispatch tables."""
import hashlib
import json
import pathlib
import platform
import subprocess
import sys
from native_profile import configure_stack

compiler = pathlib.Path(sys.argv[1]).resolve()
irtool = compiler.parent / 'cinderir'
identity = hashlib.sha256(compiler.read_bytes()+irtool.read_bytes()).hexdigest()
root = pathlib.Path('.agent-local/switch-contracts') / identity
root.mkdir(parents=True, exist_ok=True)
records = []
native = platform.system() == 'Linux' and platform.machine() == 'x86_64'
stack_profile = configure_stack()

def run(argv):
    return subprocess.run([str(arg) for arg in argv], capture_output=True, text=True, timeout=30)

def save(row):
    records.append(row)
    (root/'observations.json').write_text(json.dumps(records, indent=2)+'\n')

def reject(source, name, required):
    output=root/(name+'.prior.o'); sentinel=b'previous complete object'; output.write_bytes(sentinel)
    result=run([compiler, '-c', source, '-o', output])
    save(dict(kind='rejection', name=name, source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), exit=result.returncode, stdout=result.stdout, stderr=result.stderr))
    assert result.returncode > 0 and 'error:' in result.stderr and required in result.stderr, (name,result)
    assert 'Sanitizer' not in result.stderr and 'runtime error:' not in result.stderr, (name,result)
    assert output.read_bytes()==sentinel, name

sources=sorted(pathlib.Path('tests/switch').glob('*.c'))
for source in sources:
    for level in ('-O0', '-O2'):
        cir=root/(source.stem+level+'.cir'); canonical=cir.with_suffix('.again.cir')
        obj,parsed=cir.with_suffix('.o'),cir.with_suffix('.parsed.o')
        for argv in ([compiler, '--serialize-ir', level, '-fverify-each', source, '-o', cir], [irtool, cir, '-o', canonical], [compiler, '-c', level, source, '-o', obj], [irtool, '-c', cir, '-o', parsed]):
            result=run(argv); assert result.returncode==0, (argv,result)
        result=run([irtool, '--classify', cir]); assert result.returncode==0, result
        value=json.loads(result.stdout); assert value['valid'] and value['integer']==0, (source,level,value)
        assert cir.read_bytes()==canonical.read_bytes() and obj.read_bytes()==parsed.read_bytes(), (source,level)
        row=dict(kind='authored', source=str(source), source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), level=level, interpreter=value, ir_sha256=hashlib.sha256(cir.read_bytes()).hexdigest(), object_sha256=hashlib.sha256(obj.read_bytes()).hexdigest(), native=native, stack_profile=stack_profile)
        if native:
            executable=cir.with_suffix('.native');result=run(['cc', '-no-pie', parsed, '-o', executable]);assert result.returncode==0,result
            result=run([executable.resolve()]);assert result.returncode==0 and not result.stdout and not result.stderr,(source,level,result)
            row['native_exit']=result.returncode;row['executable_sha256']=hashlib.sha256(executable.read_bytes()).hexdigest()
        save(row)

# These generated boundary inputs are separate from the individually authored ledger.
for count in (1024,4096,4097):
    source=root/('case-count-'+str(count)+'.c')
    source.write_text('int main(void) { int n=0; switch('+str(count-1)+') { '+''.join('case '+str(i)+': n='+str(i)+'; break; ' for i in range(count))+'} return n!='+str(count-1)+'; }\n')
    if count==4097:
        reject(source,source.stem,'switch case table exceeds the profile limit')
    else:
        result=run([compiler, '-fsyntax-only', source]);assert result.returncode==0,result
        save(dict(kind='case-table-boundary', count=count, source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), exit=result.returncode))
        if count==1024:
            cir=root/'large-dispatch.cir';result=run([compiler, '--serialize-ir', '-O0', '-fverify-each', source, '-o', cir]);assert result.returncode==0,result
            result=run([irtool, '--classify', cir]);assert result.returncode==0,result
            value=json.loads(result.stdout);assert value['valid'] and value['integer']==0,value
            row=dict(kind='large-dispatch', count=count, interpreter=value, source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), ir_sha256=hashlib.sha256(cir.read_bytes()).hexdigest(), native=native)
            if native:
                obj=root/'large-dispatch.o'; executable=root/'large-dispatch.native'
                for argv in ([irtool, '-c', cir, '-o', obj], ['cc', '-no-pie', obj, '-o', executable]):
                    result=run(argv);assert result.returncode==0,result
                result=run([executable.resolve()]);assert result.returncode==0 and not result.stdout and not result.stderr,result
                row['native_exit']=result.returncode;row['object_sha256']=hashlib.sha256(obj.read_bytes()).hexdigest();row['executable_sha256']=hashlib.sha256(executable.read_bytes()).hexdigest()
            save(row)
for depth in (8,128,129,1024):
    source=root/('case-depth-'+str(depth)+'.c')
    source.write_text('int main(void) { switch(0) { '+''.join('case '+str(i)+': ' for i in range(depth))+'return 0; } return 1; }\n')
    if depth<=128:
        result=run([compiler, '--interpret', source]);assert result.returncode==0 and result.stdout=='interpret main => 0\n',result
        save(dict(kind='case-depth-boundary', depth=depth, exit=result.returncode, source_sha256=hashlib.sha256(source.read_bytes()).hexdigest()))
    else:reject(source,source.stem,'label nesting exceeds the profile limit')

assert hashlib.sha256(compiler.read_bytes()+irtool.read_bytes()).hexdigest()==identity
print(f'Switch contracts: {len(sources)*2} authored CIR executions and object identities, 1024-case executed dispatch, 4096-case semantic boundary, and case nesting boundaries passed; native={native}.')
