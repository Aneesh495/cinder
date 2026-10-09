#!/usr/bin/env python3
"""Check local integer load/store elimination and alias/effect limits."""
import hashlib
import json
import pathlib
import platform
import subprocess
import sys

compiler = pathlib.Path(sys.argv[1]).resolve()
irtool = compiler.parent / 'cinderir'
inputs = [pathlib.Path(__file__), pathlib.Path('tests/optimization/memory_cases.json')] + sorted(pathlib.Path('tests/optimization').glob('memory_*.c'))
hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
identity = hashlib.sha256(compiler.read_bytes() + irtool.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
root = pathlib.Path('.agent-local/local-memory') / identity
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
    run([irtool, '--pass=local-memory', before, '-o', after])
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



# A first ordinary store can guard a duplicate. An initializer or read cannot
# establish permission to write const storage. Lifetime effects revoke facts.
def inst(op,dst='none',kind=1,left='none',right='none',slot=-1,literal=0,source='none'):
    return f'inst {op} {kind} {source} none {dst} {left} {right} {literal:016x} 0000000000000000 {slot} 0 - 0 noreturn 0 args 0 classes 0 incoming 0 loc 0 0 0 0 0'
header='cinder-ir 6 lp64-le sysv-x86-64\ntypes 7\ntype 0 function 0 1 0 0 0 1 none 0 1 0 - params 0 fields 0 identity 18\ntype 1 int 0 1 0 0 4 4 none 0 none 0 - params 0 fields 0 identity 9\ntype 2 pointer 0 1 0 0 8 8 1 0 none 0 - params 0 fields 0 identity 19\ntype 3 pointer 0 1 0 0 8 8 4 0 none 0 - params 0 fields 0 identity 20\ntype 4 int 1 1 0 0 4 4 none 0 none 0 - params 0 fields 0 identity 9\ntype 5 struct 0 1 0 0 4 4 none 0 none 0 x53 params 0 fields 1 identity 30\nfield x78 1 0 0 0 align 0 bits 0\ntype 6 pointer 0 1 0 0 8 8 5 0 none 0 - params 0 fields 0 identity 31\nglobals 0\nfunctions 1\n'
initial=[inst('local.address',0,2,slot=0),inst('const',1,literal=13),inst('memory.store',left=0,right=1)]
typed=[('duplicate-regular-store',1,initial+[inst('memory.store',left=0,right=1),inst('memory.load',2,left=0)],2,0,13,'memory.store',True,None)]
const_initial=[inst('local.address',0,3,slot=0),inst('const',1,literal=13),inst('convert',2,2,left=0,source=3),inst('memory.init',left=2,right=1)]
typed += [('const-initializer-does-not-permit-store',4,const_initial+[inst('memory.store',left=2,right=1),inst('const',3)],3,12,0,'memory.store',False,None),
          ('const-read-does-not-permit-store',4,const_initial+[inst('memory.load',3,left=2),inst('memory.store',left=2,right=3),inst('const',4)],4,12,0,'memory.store',False,None)]
for op,classification in [('local.end',10),('local.reset',4)]:
    typed.append((op+'-revokes-load',1,initial+[inst('memory.load',2,left=0),inst(op,slot=0),inst('memory.load',3,left=0)],3,classification,0,'memory.load',True,3))
freeze_initial=[inst('local.address',0,6,slot=0),inst('pointer.member',1,2,left=0,slot=0),inst('const',2,literal=13),inst('memory.store',left=1,right=2)]
typed.append(('freeze-revokes-store',5,freeze_initial+[inst('local.freeze',kind=5,slot=0),inst('memory.store',left=1,right=2)],2,12,0,'memory.store',False,None))
for name,local_type,body,returned,classification,value,opcode,changes,protected in typed:
    count=max(int(line.split()[5]) for line in body if line.split()[5]!='none')+1
    text=header+f'function x6d61696e 0 1 noreturn 0 {count} 1 0 params 0 blocks 1\nlocal {local_type} align 0\nblock 0 x656e747279 instructions {len(body)} predecessors 0 successors 0\n'+'\n'.join(body)+f'\nterm return {returned} none none none none loc 0 0 0 0 0\nend-block\nend-function\nend-module\n'
    before=root/(name+'.before.cir');after=root/(name+'.after.cir');before.write_text(text)
    run([irtool,'--verify',before]);run([irtool,'--pass=local-memory',before,'-o',after])
    first=json.loads(run([irtool,'--classify',before]).stdout);second=json.loads(run([irtool,'--classify',after]).stdout)
    assert first==second and first['classification']==classification and (classification!=0 or first['integer']==value),(name,first,second)
    prefix='inst '+opcode+' ';old=sum(line.startswith(prefix) for line in before.read_text().splitlines());new=sum(line.startswith(prefix) for line in after.read_text().splitlines())
    assert (new<old)==changes,(name,old,new)
    if protected is not None:
        assert any(line.startswith('inst memory.load ') and int(line.split()[5])==protected for line in after.read_text().splitlines())
    save(dict(kind='typed-guard',name=name,classification=classification,before_sha256=hashlib.sha256(before.read_bytes()).hexdigest(),after_sha256=hashlib.sha256(after.read_bytes()).hexdigest(),interpreter=second))

assert hashes == {str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
assert identity == hashlib.sha256(compiler.read_bytes()+irtool.read_bytes()+json.dumps(hashes,sort_keys=True).encode()).hexdigest()
print(f'Local memory: 17 isolated positive/negative sources and 6 typed initialization/lifetime guards passed; native={native}')
