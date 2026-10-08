#!/usr/bin/env python3
"""Check bitfield type graphs, object identities, and mutation rejection."""
import hashlib
import json
import pathlib
import platform
import subprocess
import sys

compiler = pathlib.Path(sys.argv[1]).resolve()
irtool = compiler.parent / 'cinderir'
inputs = sorted(pathlib.Path('tests/bitfields').glob('*.c')) + [pathlib.Path(__file__)]
hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
identity = hashlib.sha256(compiler.read_bytes() + irtool.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
root = pathlib.Path('.agent-local/bitfield-contracts') / identity
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

# Independent readers reject malformed packing and executable bit contracts.
seed = (root / 'unsigned_limits-O0.cir').read_text()
lines = seed.splitlines()
field = next(i for i,line in enumerate(lines) if line.startswith('field '))
mutations = {'previous-schema': seed.replace('cinder-ir 5 ', 'cinder-ir 4 ', 1)}
def change(name, index, edits):
    altered = lines.copy(); columns = altered[index].split()
    for column,value in edits.items(): columns[column] = value
    altered[index] = ' '.join(columns)
    mutations[name] = '\n'.join(altered) + '\n'
change('named-zero-width',field,{5:'0'})
change('oversize-field',field,{5:'33'})
change('wrong-packed-offset',field+1,{4:'4'})
change('ordinary-field-with-bits',field,{9:'0'})
change('aligned-bitfield',field,{7:'16'})
change('unnamed-ordinary-scalar',field,{1:'-',4:'0',5:'0',9:'0'})
scalar_id = lines[field].split()[2]
scalar = next(i for i,line in enumerate(lines) if line.startswith('type '+scalar_id+' '))
for label,column,value in [('zero-size',7,'0'),('byte-size',7,'1'),('wide-size',7,'8'),('zero-align',8,'0'),('weak-align',8,'1'),('excess-align',8,'16'),('incomplete',4,'0')]:
    change('bitfield-scalar-'+label,scalar,{column:value})
for opcode in ('bit.load','bit.init'):
    index = next(i for i,line in enumerate(lines) if line.startswith('inst '+opcode+' '))
    for name,edits in [('zero-width',{8:'0000000000000000'}),('oversize-width',{8:'0000000000000021'}),('past-unit',{11:'32'}),('negative-offset',{11:'-1'}),('bad-slot',{10:'0'}),('source-type',{3:'1'}),('missing-address',{6:'none'})]:
        change(opcode+'-'+name,index,edits)
    if opcode=='bit.init': change('bit.init-missing-value',index,{7:'none'})
    else: change('bit.load-extra-value',index,{7:'0'})
# Conversions and stores occur in a separate assignment seed.
assignment = (root / 'assign_truncate-O0.cir').read_text()
saved_lines = lines; lines = assignment.splitlines()
for opcode in ('bit.store','bit.convert'):
    index = next(i for i,line in enumerate(lines) if line.startswith('inst '+opcode+' '))
    for name,edits in [('zero-width',{8:'0000000000000000'}),('oversize-width',{8:'0000000000000021'}),('source-type',{3:'1'}),('bad-slot',{10:'0'})]: change(opcode+'-'+name,index,edits)
    if opcode=='bit.store': change('bit.store-missing-value',index,{7:'none'})
    else: change('bit.convert-offset',index,{11:'1'})
lines = saved_lines
for name,text in mutations.items():
    path = root / (name+'.invalid.cir'); path.write_text(text)
    output = root/'prior.o'; output.write_bytes(b'previous complete output'); commands=[]
    for mode in ('--verify','-c'):
        argv=[irtool,mode,path]
        if mode=='-c': argv += ['-o',output]
        result=run(argv)
        assert result.returncode>0 and ('error:' in result.stderr or 'fatal:' in result.stderr),(name,result)
        assert 'Sanitizer' not in result.stderr and 'runtime error:' not in result.stderr and output.read_bytes()==b'previous complete output',(name,result)
        commands.append(dict(argv=[str(a) for a in argv],exit=result.returncode,stderr=result.stderr))
    save(dict(kind='mutation',name=name,ir_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),commands=commands))
assert hashes == {str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
assert identity == hashlib.sha256(compiler.read_bytes()+irtool.read_bytes()+json.dumps(hashes,sort_keys=True).encode()).hexdigest()
print(f'Bitfield contracts: {len(inputs)-1} authored cases at both levels and {len(mutations)} malformed type and operation contracts passed; native={native}.')
