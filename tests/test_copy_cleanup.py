#!/usr/bin/env python3
"""Check isolated copy/phi cleanup and its effect/indeterminate boundaries."""
import hashlib
import json
import pathlib
import platform
import subprocess
import sys

compiler = pathlib.Path(sys.argv[1]).resolve()
irtool = compiler.parent / 'cinderir'
inputs = [pathlib.Path(__file__), pathlib.Path('tests/optimization/copy_cases.json')] + sorted(pathlib.Path('tests/optimization').glob('copy_*.c'))
hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
identity = hashlib.sha256(compiler.read_bytes() + irtool.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
root = pathlib.Path('.agent-local/copy-cleanup') / identity
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
    run([irtool, '--pass=copy-cleanup', before, '-o', after])
    before_value = json.loads(run([irtool, '--classify', before]).stdout)
    after_value = json.loads(run([irtool, '--classify', after]).stdout)
    assert before_value == after_value and after_value['valid'] and after_value['integer'] == case['expected'], (case, before_value, after_value)
    prefix='inst '+case['opcode']+' '
    blocks_before=sum(line.startswith(prefix) for line in before.read_text().splitlines())
    blocks_after=sum(line.startswith(prefix) for line in after.read_text().splitlines())
    assert (blocks_after<blocks_before)==case['changes'],(case,blocks_before,blocks_after)
    objects = []
    for level in ('-O0', '-O2', 'isolated'):
        obj = root / (source.stem + level + '.o')
        run([irtool, '-c', after, '-o', obj] if level == 'isolated' else [compiler, level, '-fverify-each', '-c', source, '-o', obj])
        row = dict(level=level, object=str(obj), sha256=hashlib.sha256(obj.read_bytes()).hexdigest())
        if native:
            binary = obj.with_suffix('.native')
            run(['cc', '-no-pie', obj, '-o', binary])
            result = subprocess.run([str(binary.resolve())], capture_output=True, text=True, timeout=10)
            assert result.returncode == case['expected'] and result.stdout == result.stderr == '', (case, result)
            row.update(native_exit=result.returncode, executable_sha256=hashlib.sha256(binary.read_bytes()).hexdigest())
        objects.append(row)
    save(dict(case, source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), before_sha256=hashlib.sha256(before.read_bytes()).hexdigest(), after_sha256=hashlib.sha256(after.read_bytes()).hexdigest(), blocks_before=blocks_before, blocks_after=blocks_after, interpreter=after_value, objects=objects))


# Phi transfers do not read indeterminate inputs. Removing them must not
# introduce an eager COPY, while an existing COPY must retain its read.
def inst(op,dst,left='none',literal=0,slot=-1,args=(),incoming=()):
    return f'inst {op} 1 none none {dst} {left} none {literal:016x} 0000000000000000 {slot} 0 - 0 noreturn 0 args {len(args)}'+''.join(' '+str(v) for v in args)+f' classes 0 incoming {len(incoming)}'+''.join(' '+str(v) for v in incoming)+' loc 0 0 0 0 0'
header='cinder-ir 6 lp64-le sysv-x86-64\ntypes 2\ntype 0 function 0 1 0 0 0 1 none 0 1 0 - params 0 fields 0 identity 18\ntype 1 int 0 1 0 0 4 4 none 0 none 0 - params 0 fields 0 identity 9\nglobals 0\nfunctions 1\n'
typed=[]
for name,used,equal,classification in [('unused-indeterminate-phi',False,True,0),('used-indeterminate-phi',True,True,4),('unequal-indeterminate-phi',True,False,4)]:
    text=header+'function x6d61696e 0 1 noreturn 0 4 1 0 params 0 blocks 4\nlocal 1 align 0\nblock 0 x656e747279 instructions 3 predecessors 0 successors 2 1 2\n'+inst('undef',0)+'\n'+inst('const',1)+'\n'+inst('const',2,literal=1)+'\nterm branch none none 1 2 2 loc 0 0 0 0 0\nend-block\n'
    for b in (1,2):text+=f'block {b} x65646765 instructions 0 predecessors 1 0 successors 1 3\nterm jump none 3 none none none loc 0 0 0 0 0\nend-block\n'
    text+='block 3 x6a6f696e instructions 1 predecessors 2 1 2 successors 0\n'+inst('phi',3,slot=0,args=(0,0 if equal else 1),incoming=(1,2))+f'\nterm return {3 if used else 1} none none none none loc 0 0 0 0 0\nend-block\nend-function\nend-module\n'
    typed.append((name,text,classification,'phi',equal))
text=header+'function x6d61696e 0 1 noreturn 0 3 1 0 params 0 blocks 1\nlocal 1 align 0\nblock 0 x656e747279 instructions 3 predecessors 0 successors 0\n'+inst('undef',0)+'\n'+inst('const',1)+'\n'+inst('copy',2,left=0)+'\nterm return 1 none none none none loc 0 0 0 0 0\nend-block\nend-function\nend-module\n'
typed.append(('unused-indeterminate-copy',text,4,'copy',False))
for name,text,classification,opcode,changes in typed:
    before=root/(name+'.before.cir');after=root/(name+'.after.cir');before.write_text(text)
    run([irtool,'--verify',before]);run([irtool,'--pass=copy-cleanup',before,'-o',after])
    first=json.loads(run([irtool,'--classify',before]).stdout);second=json.loads(run([irtool,'--classify',after]).stdout)
    assert first==second and first['classification']==classification,(name,first,second)
    prefix='inst '+opcode+' ';old=sum(line.startswith(prefix) for line in before.read_text().splitlines());new=sum(line.startswith(prefix) for line in after.read_text().splitlines())
    assert (new<old)==changes,(name,old,new)
    save(dict(kind='typed-indeterminate',name=name,classification=classification,before_sha256=hashlib.sha256(before.read_bytes()).hexdigest(),after_sha256=hashlib.sha256(after.read_bytes()).hexdigest(),interpreter=second))
entry=root/'invalid-entry-phi.cir'
entry.write_text(header+'function x6d61696e 0 1 noreturn 0 1 1 0 params 0 blocks 1\nlocal 1 align 0\nblock 0 x656e747279 instructions 1 predecessors 0 successors 0\n'+inst('phi',0,slot=0)+'\nterm return 0 none none none none loc 0 0 0 0 0\nend-block\nend-function\nend-module\n')
result=subprocess.run([str(irtool),'--verify',str(entry)],capture_output=True,text=True,timeout=10)
assert result.returncode!=0 and 'entry block cannot contain a phi' in result.stderr
for options in (['--pass=missing'],['-O2','--pass=copy-cleanup']):
    result=subprocess.run([str(irtool),*options,str(after)],capture_output=True,text=True,timeout=10)
    assert result.returncode!=0 and result.stdout==''

assert hashes == {str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
assert identity == hashlib.sha256(compiler.read_bytes()+irtool.read_bytes()+json.dumps(hashes,sort_keys=True).encode()).hexdigest()
print(f'Copy cleanup: 11 isolated positive/negative sources, 4 indeterminate IR cases, and entry-phi rejection passed; native={native}')
