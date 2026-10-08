#!/usr/bin/env python3
"""Check anonymous-member type graphs, object identities, and mutation rejection."""
import hashlib
import json
import pathlib
import platform
import subprocess
import sys

compiler = pathlib.Path(sys.argv[1]).resolve()
irtool = compiler.parent / 'cinderir'
inputs = sorted(pathlib.Path('tests/anonymous').glob('*.c')) + [pathlib.Path(__file__)]
hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
identity = hashlib.sha256(compiler.read_bytes() + irtool.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
root = pathlib.Path('.agent-local/anonymous-contracts') / identity
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

# Preserve physical nesting: changing a promoted name or anonymous container
# must not yield a different silently accepted canonical type graph.
seed = (root / 'struct_members-O0.cir').read_text()
lines = seed.splitlines()
# Tags occur before the params section; locate the exact outer S graph.
outer_index = next(i for i,line in enumerate(lines) if line.startswith('type ') and line.split()[2]=='struct' and ' x53 ' in line)
outer_id=lines[outer_index].split()[1]
anonymous_field=next(i for i in range(outer_index+1,len(lines)) if lines[i].startswith('field - '))
child_id=lines[anonymous_field].split()[2]
child_index=next(i for i,line in enumerate(lines) if line.startswith('type '+child_id+' '))
scalar_field=next(i for i in range(child_index+1,len(lines)) if lines[i].startswith('field ') and lines[i].split()[1]!='-')
mutations={}
def change(name,index,column,value):
 altered=lines.copy(); columns=altered[index].split(); columns[column]=value; altered[index]=' '.join(columns)
 mutations[name]='\n'.join(altered)+'\n'
change('anonymous-scalar',anonymous_field,2,lines[scalar_field].split()[2])
change('anonymous-self-cycle',anonymous_field,2,outer_id)
change('hidden-tagged-container',child_index,13,'x48696464656e')
change('overlapping-offset',anonymous_field,3,'1')
change('duplicate-promoted-name',scalar_field,1,lines[outer_index+2].split()[1])
for name,text in mutations.items():
 assert text!=seed,name
 path=root/(name+'.invalid.cir');path.write_text(text)
 output=root/'prior.o';output.write_bytes(b'previous complete output');commands=[]
 for mode in ('--verify','-c'):
  argv=[irtool,mode,path]
  if mode=='-c':argv+=['-o',output]
  result=run(argv)
  assert result.returncode>0 and ('error:' in result.stderr or 'fatal:' in result.stderr),(name,result)
  assert 'Sanitizer' not in result.stderr and 'runtime error:' not in result.stderr and output.read_bytes()==b'previous complete output',(name,result)
  commands.append(dict(argv=[str(a) for a in argv],exit=result.returncode,stderr=result.stderr))
 save(dict(kind='mutation',name=name,ir_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),commands=commands))

# C17 requires at least 63 aggregate levels. The declared profile allows 64.
# Boundary sources remain distinct from the individually authored ledger.
for depth in (63,64,65,1024):
 source=root/('depth-'+str(depth)+'.c')
 source.write_text('struct Outer {'+'struct {'*(depth-1)+'int n;'+'};'*depth+'int main(void){struct Outer v={.n=29};return v.n!=29;}\n')
 if depth<=64:
  for level in ('-O0','-O2'):
   cir=root/(source.stem+level+'.cir');obj=cir.with_suffix('.o')
   invoke([compiler,level,'-fverify-each','--serialize-ir',source,'-o',cir]);invoke([irtool,'-c',cir,'-o',obj])
   value=json.loads(invoke([irtool,'--classify',cir]).stdout);assert value['valid'] and value['integer']==0,value
   row=dict(kind='depth-boundary',depth=depth,level=level,source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),ir_sha256=hashlib.sha256(cir.read_bytes()).hexdigest(),object_sha256=hashlib.sha256(obj.read_bytes()).hexdigest(),interpreter=value)
   if native:
    executable=cir.with_suffix('.native');invoke(['cc','-no-pie',obj,'-o',executable]);result=invoke([executable.resolve()]);assert not result.stdout and not result.stderr,result
    row.update(native_exit=result.returncode,executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest())
   save(row)
 else:
  output=root/'prior.o';output.write_bytes(b'previous complete output');result=run([compiler,'-c',source,'-o',output])
  assert result.returncode>0 and 'aggregate definition nesting exceeds the profile limit' in result.stderr,(depth,result)
  assert 'Sanitizer' not in result.stderr and 'runtime error:' not in result.stderr and output.read_bytes()==b'previous complete output',(depth,result)
  save(dict(kind='depth-rejection',depth=depth,source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),exit=result.returncode,stderr=result.stderr))
assert hashes == {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
assert identity == hashlib.sha256(compiler.read_bytes() + irtool.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
print(f'Anonymous contracts: {len(inputs)-1} authored cases at both levels, {len(mutations)} malformed type graphs, and 63/64/65/1024 aggregate boundaries passed; native={native}.')
