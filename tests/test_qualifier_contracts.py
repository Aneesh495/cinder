#!/usr/bin/env python3
"""Check qualifier-origin type graphs, object identities, and mutation rejection."""
import hashlib
import json
import pathlib
import platform
import subprocess
import sys

compiler = pathlib.Path(sys.argv[1]).resolve()
irtool = compiler.parent / 'cinderir'
inputs = sorted(pathlib.Path('tests/qualifiers').glob('*.c')) + [pathlib.Path(__file__)]
hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
identity = hashlib.sha256(compiler.read_bytes() + irtool.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
root = pathlib.Path('.agent-local/qualifier-contracts') / identity
root.mkdir(parents=True, exist_ok=True)
native = platform.system() == 'Linux' and platform.machine() == 'x86_64'
rows = []

def run(argv):
    return subprocess.run([str(a) for a in argv], capture_output=True, text=True, timeout=30)

def invoke(argv):
    result = run(argv)
    assert result.returncode == 0, (argv, result)
    return result

def save(row):
    rows.append(row)
    (root / 'observations.json').write_text(json.dumps(dict(inputs=hashes, compiler_sha256=hashlib.sha256(compiler.read_bytes()).hexdigest(), irtool_sha256=hashlib.sha256(irtool.read_bytes()).hexdigest(), native=native, cases=rows), indent=2) + '\n')

for source in inputs[:-1]:
    for level in ('-O0', '-O2'):
        prefix = root / (source.stem + level)
        cir, again = prefix.with_suffix('.cir'), prefix.with_suffix('.again.cir')
        obj, parsed = prefix.with_suffix('.o'), prefix.with_suffix('.parsed.o')
        invoke([compiler, level, '-fverify-each', '--serialize-ir', source, '-o', cir])
        invoke([compiler, level, '-fverify-each', '-c', source, '-o', obj])
        invoke([irtool, cir, '-o', again])
        invoke([irtool, '-c', cir, '-o', parsed])
        value = json.loads(invoke([irtool, '--classify', cir]).stdout)
        assert value['valid'] and value['integer'] == 0, (source, level, value)
        assert cir.read_bytes() == again.read_bytes() and obj.read_bytes() == parsed.read_bytes(), (source, level)
        row = dict(kind='authored', source=str(source), level=level, ir_sha256=hashlib.sha256(cir.read_bytes()).hexdigest(), object_sha256=hashlib.sha256(obj.read_bytes()).hexdigest(), interpreter=value)
        if native:
            output = prefix.with_suffix('.native')
            invoke(['cc', '-no-pie', parsed, '-o', output])
            result = invoke([output.resolve()])
            assert not result.stdout and not result.stderr, result
            row.update(native_exit=result.returncode, executable_sha256=hashlib.sha256(output.read_bytes()).hexdigest())
        save(row)

# Explicit member origins distinguish const union alternatives even when
# their byte domains coincide. Only actual declared const storage is immutable.
seeds={name:(root/(name+'-O0.cir')).read_text() for name in ('union_static_mutable_pointer','union_static_nested_pointer','union_static_bit_pointer')}
mutations={}
def origin(name,seed,fields):
    lines=seeds[seed].splitlines();index=next(i for i,line in enumerate(lines) if ' address ' in line);columns=lines[index].split();mark=columns.index('origin');end=mark+2+int(columns[mark+1])
    columns=columns[:mark]+['origin',str(len(fields)),*[str(x) for x in fields]]+columns[end:];lines[index]=' '.join(columns);mutations[name]='\n'.join(lines)+'\n'
origin('unknown-member','union_static_mutable_pointer',[999])
origin('member-below-scalar','union_static_mutable_pointer',[1,0])
origin('nested-below-scalar','union_static_nested_pointer',[1,1,0])
origin('nonaddressable-bitfield','union_static_bit_pointer',[0])
seed=seeds['union_static_mutable_pointer']
mutations['old-schema']=seed.replace('cinder-ir 6 ','cinder-ir 5 ',1)
mutations['excess-origin-depth']=seed.replace('origin 1 1','origin 257 1',1)
mutations['truncated-origin']=seed.replace('origin 1 1','origin 2 1',1)
mutations['negative-origin-field']=seed.replace('origin 1 1','origin 1 -1',1)
for name,text in mutations.items():
    assert text!=seed or name in ('nested-below-scalar','nonaddressable-bitfield'),name
    path=root/(name+'.invalid.cir');path.write_text(text);output=root/'prior.o';output.write_bytes(b'previous complete output');commands=[]
    for mode in ('--verify','-c'):
        argv=[irtool,mode,path]
        if mode=='-c':argv+=['-o',output]
        result=run(argv)
        assert result.returncode>0 and ('error:' in result.stderr or 'fatal:' in result.stderr),(name,result)
        assert 'Sanitizer' not in result.stderr and 'runtime error:' not in result.stderr and output.read_bytes()==b'previous complete output',(name,result)
        commands.append(dict(argv=[str(a) for a in argv],exit=result.returncode,stderr=result.stderr))
    save(dict(kind='mutation',name=name,ir_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),commands=commands))
assert hashes=={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
assert identity==hashlib.sha256(compiler.read_bytes()+irtool.read_bytes()+json.dumps(hashes,sort_keys=True).encode()).hexdigest()
print(f'Qualifier contracts: {len(inputs)-1} authored cases at both levels and {len(mutations)} malformed origins passed; native={native}.')
