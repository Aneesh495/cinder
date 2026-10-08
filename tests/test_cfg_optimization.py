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
inputs = [pathlib.Path(__file__), pathlib.Path('tests/optimization/cfg_cases.json')] + sorted(pathlib.Path('tests/optimization').glob('cfg_*.c'))
hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
identity = hashlib.sha256(compiler.read_bytes() + irtool.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
root = pathlib.Path('.agent-local/cfg-optimization') / identity
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
    blocks_before = sum(line.startswith('block ') for line in before.read_text().splitlines())
    blocks_after = sum(line.startswith('block ') for line in after.read_text().splitlines())
    assert (blocks_after < blocks_before) == case['cfg_changes'], (case, blocks_before, blocks_after)
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

# Typed IR constants use their declared width even when the literal storage
# contains additional high bits. These are independent IR boundary cases.
def inst(op,kind,dst,left='none',right='none',integer=0,source='none'):
    return f'inst {op} {kind} {source} none {dst} {left} {right} {integer & ((1<<64)-1):016x} 0000000000000000 -1 0 - 0 noreturn 0 args 0 classes 0 incoming 0 loc 0 0 0 0 0'
header='cinder-ir 6 lp64-le sysv-x86-64\ntypes 6\n'+\
 'type 0 function 0 1 0 0 0 1 none 0 1 0 - params 0 fields 0 identity 18\n'+\
 'type 1 int 0 1 0 0 4 4 none 0 none 0 - params 0 fields 0 identity 9\n'+\
 'type 2 char 0 1 1 0 1 1 none 0 none 0 - params 0 fields 0 identity 5\n'+\
 'type 3 char 0 1 0 0 1 1 none 0 none 0 - params 0 fields 0 identity 3\n'+\
 'type 4 bool 0 1 0 0 1 1 none 0 none 0 - params 0 fields 0 identity 2\n'+\
 'type 5 pointer 0 1 0 0 8 8 1 0 none 0 - params 0 fields 0 identity 19\nglobals 0\nfunctions 1\n'
typed=[]
for name,kind,left,right,op,expected,classification in [
    ('byte-wrap-compare',2,256,0,'cmp.ne',0,0),
    ('signed-word-compare',1,4294967295,0,'cmp.lt.s',1,0),
    ('signed-byte-compare',3,255,0,'cmp.lt.s',1,0),
    ('boolean-normalization',4,2,1,'cmp.ne',0,0),
    ('invalid-pointer-not-folded',5,1,0,'cmp.ne',0,11),
]:
    instructions=[inst('const',kind,0,integer=left),inst('const',kind,1,integer=right),inst(op,1,2,0,1,source=kind)]
    text=header+'function x6d61696e 0 1 noreturn 0 3 0 0 params 0 blocks 1\nblock 0 x656e747279 instructions 3 predecessors 0 successors 0\n'+'\n'.join(instructions)+'\nterm return 2 none none none none loc 0 0 0 0 0\nend-block\nend-function\nend-module\n'
    typed.append((name,text,expected,classification))
for name,op,integer,classification in [('byte-wrap-branch','const',256,0),('same-edge-uninitialized','undef',0,4)]:
    same=op=='undef';blocks=2 if same else 3
    instructions=[inst(op,1 if same else 2,0,integer=integer),inst('const',1,1,integer=77),inst('const',1,2,integer=0)]
    text=header+f'function x6d61696e 0 1 noreturn 0 3 0 0 params 0 blocks {blocks}\nblock 0 x656e747279 instructions 3 predecessors 0 successors '+('1 1' if same else '2 1 2')+'\n'+'\n'.join(instructions)+f'\nterm branch none none 1 {1 if same else 2} 0 loc 0 0 0 0 0\nend-block\nblock 1 x796573 instructions 0 predecessors 1 0 successors 0\nterm return 1 none none none none loc 0 0 0 0 0\nend-block\n'
    if not same:text+='block 2 x6e6f instructions 0 predecessors 1 0 successors 0\nterm return 2 none none none none loc 0 0 0 0 0\nend-block\n'
    text+='end-function\nend-module\n';typed.append((name,text,0,classification))
for name,text,expected,classification in typed:
    before=root/(name+'.before.cir');after=root/(name+'.after.cir');before.write_text(text)
    run([irtool,'--verify',before]);run([irtool,'-O2',before,'-o',after])
    first=json.loads(run([irtool,'--classify',before]).stdout);second=json.loads(run([irtool,'--classify',after]).stdout)
    assert first==second and first['classification']==classification and first['integer']==expected,(name,first,second)
    save(dict(kind='typed-ir-boundary',name=name,before_sha256=hashlib.sha256(before.read_bytes()).hexdigest(),after_sha256=hashlib.sha256(after.read_bytes()).hexdigest(),interpreter=second))

assert hashes == {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
assert identity == hashlib.sha256(compiler.read_bytes() + irtool.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
print(f'CFG optimization: 12 positive/negative sources and {len(typed)} typed width/definedness boundaries preserved results; native={native}')
