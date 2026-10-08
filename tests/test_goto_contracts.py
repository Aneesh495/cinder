#!/usr/bin/env python3
"""Check authored jumps, declaration reset metadata, and bounded label parsing."""
import hashlib
import json
import pathlib
import subprocess
import sys

compiler = pathlib.Path(sys.argv[1]).resolve()
irtool = compiler.parent / 'cinderir'
identity = hashlib.sha256(compiler.read_bytes()+irtool.read_bytes()).hexdigest()
root = pathlib.Path('.agent-local/goto-contracts') / identity
root.mkdir(parents=True, exist_ok=True)
records = []

def run(argv):
    return subprocess.run([str(arg) for arg in argv], capture_output=True, text=True, timeout=20)

def save(row):
    records.append(row); (root/'observations.json').write_text(json.dumps(records, indent=2)+'\n')

for source in sorted(pathlib.Path('tests/goto').glob('*.c')):
    for level in ('-O0', '-O2'):
        cir = root/(source.stem+level+'.cir'); canonical = cir.with_suffix('.again.cir')
        obj, parsed = cir.with_suffix('.o'), cir.with_suffix('.parsed.o')
        for argv in ([compiler, '--serialize-ir', level, '-fverify-each', source, '-o', cir], [irtool, cir, '-o', canonical], [compiler, '-c', level, source, '-o', obj], [irtool, '-c', cir, '-o', parsed]):
            result = run(argv); assert result.returncode == 0, (argv, result)
        value = json.loads(run([irtool, '--classify', cir]).stdout)
        assert value['valid'] and value['integer'] == 0, (source, level, value)
        assert cir.read_bytes() == canonical.read_bytes() and obj.read_bytes() == parsed.read_bytes(), (source, level)
        save(dict(kind='authored', source=str(source), source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), level=level, interpreter=value, ir_sha256=hashlib.sha256(cir.read_bytes()).hexdigest(), object_sha256=hashlib.sha256(obj.read_bytes()).hexdigest()))

def reject(tool, source, name, required=None):
    output=root/(name+'.prior.o'); sentinel=b'previous complete object'; output.write_bytes(sentinel)
    result=run([tool, '-c', source, '-o', output])
    save(dict(kind='rejection', name=name, source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), exit=result.returncode, stdout=result.stdout, stderr=result.stderr))
    assert result.returncode > 0 and ('error:' in result.stderr or 'fatal:' in result.stderr) and 'Sanitizer' not in result.stderr and 'runtime error:' not in result.stderr, (name,result)
    if required: assert required in result.stderr, (name,result)
    assert output.read_bytes()==sentinel, name

reset_source=root/'reset-storage.c';reset_source.write_text('int main(void) { int n=0; double x=1.0; int *p=&n; double *q=&x; return *p+(int)*q-1; }\n')
seed=root/'reset-storage.cir'; result=run([compiler,'--serialize-ir','-O0',reset_source,'-o',seed]);assert result.returncode==0,result
value=json.loads(run([irtool,'--classify',seed]).stdout);assert value['valid'] and value['integer']==0,value
text=seed.read_text(); lines=text.splitlines()
index=next(i for i,line in enumerate(lines) if line.startswith('inst local.reset '))
double_id=next(line.split()[1] for line in lines if line.startswith('type ') and line.split()[2]=='double')
parts=lines[index].split()
for name,slot,value in [('reset-slot',10,'99999'),('reset-type',2,double_id),('reset-result',5,'0'),('reset-operand',6,'0')]:
    changed=parts.copy();changed[slot]=value;mutated=lines.copy();mutated[index]=' '.join(changed)
    artifact=root/(name+'.cir');artifact.write_text('\n'.join(mutated)+'\n');reject(irtool,artifact,name)

for depth in (8,128,129,1024):
    source=root/('label-depth-'+str(depth)+'.c'); source.write_text('int main(void) { goto L'+str(depth-1)+'; '+''.join('L'+str(i)+': ' for i in range(depth))+'return 0; }\n')
    if depth<=128:
        result=run([compiler, '--interpret', source]);assert result.returncode==0 and result.stdout=='interpret main => 0\n',result
        save(dict(kind='label-boundary', depth=depth, exit=result.returncode, source_sha256=hashlib.sha256(source.read_bytes()).hexdigest()))
    else:reject(compiler,source,'label-depth-'+str(depth),'label nesting exceeds the profile limit')
source=root/'statement-depth.c';source.write_text('int main(void) { '+ 'if(1) '*1024 +'return 0; }\n')
reject(compiler,source,'statement-depth','statement nesting exceeds the profile limit')

assert hashlib.sha256(compiler.read_bytes()+irtool.read_bytes()).hexdigest()==identity
print('Goto contracts: 80 authored CIR executions and object identities, 4 malformed reset contracts, label and statement nesting boundaries passed.')
