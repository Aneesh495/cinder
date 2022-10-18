#!/usr/bin/env python3
"""Check real instruction motion, natural-loop boundaries and zero-trip guards."""
import hashlib,json,pathlib,platform,subprocess,sys
compiler=pathlib.Path(sys.argv[1]).resolve();irtool=compiler.parent/'cinderir'
inputs=[pathlib.Path(__file__),pathlib.Path('tests/optimization/loop_cases.json')]+sorted(pathlib.Path('tests/optimization').glob('loop_*.c'))
h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();hashes={str(p):h(p) for p in inputs}
identity=hashlib.sha256(compiler.read_bytes()+irtool.read_bytes()+json.dumps(hashes,sort_keys=True).encode()).hexdigest()
root=pathlib.Path('.agent-local/loop-motion')/identity;root.mkdir(parents=True,exist_ok=True)
native=platform.system()=='Linux' and platform.machine()=='x86_64';rows=[]
def run(argv):
 r=subprocess.run([str(a) for a in argv],capture_output=True,text=True,timeout=30)
 assert r.returncode==0,(argv,r);return r
def locations(path,opcode):
 function=None;block=None;result={}
 for line in path.read_text().splitlines():
  words=line.split()
  if words[0]=='function':function=words[1]
  elif words[0]=='block':block=int(words[1])
  elif words[0]=='inst' and words[1]==opcode:result[(function,int(words[5]))]=block
 return result
def save(row):
 rows.append(row);(root/'observations.json').write_text(json.dumps(dict(inputs=hashes,compiler_sha256=h(compiler),irtool_sha256=h(irtool),native=native,cases=rows),indent=2)+'\n')
for case in json.loads(inputs[1].read_text()):
 source=pathlib.Path('tests/optimization')/case['source'];before=root/(source.stem+'.before.cir');after=root/(source.stem+'.after.cir')
 run([compiler,'-O0','-fverify-each','--serialize-ir',source,'-o',before]);run([irtool,'--pass=loop-motion',before,'-o',after])
 first=json.loads(run([irtool,'--classify',before]).stdout);second=json.loads(run([irtool,'--classify',after]).stdout)
 assert first==second and first['valid'] and first['integer']==case['expected'],(case,first,second)
 old=locations(before,case['opcode']);new=locations(after,case['opcode']);assert old and old.keys()==new.keys(),(case,old,new)
 moved=[dict(function=k[0],value=k[1],before=old[k],after=new[k]) for k in old if old[k]!=new[k]]
 assert bool(moved)==case['moves'],(case,moved)
 objects=[]
 for level in ('-O0','-O2','isolated'):
  obj=root/(source.stem+level+'.o');run([irtool,'-c',after,'-o',obj] if level=='isolated' else [compiler,level,'-fverify-each','-c',source,'-o',obj]);row=dict(level=level,object=str(obj),sha256=h(obj))
  if native:
   binary=obj.with_suffix('.native');run(['cc','-no-pie',obj,'-o',binary]);r=subprocess.run([str(binary.resolve())],capture_output=True,text=True,timeout=10)
   assert r.returncode==case['expected'] and r.stdout==r.stderr=='',(case,r);row.update(native_exit=r.returncode,executable_sha256=h(binary))
  objects.append(row)
 save(dict(case,source_sha256=h(source),before_sha256=h(before),after_sha256=h(after),moved=moved,interpreter=second,objects=objects))

# These hand-authored CIR fragments execute a zero-trip path or trap in the
# first iteration. Invariant operands alone never authorize speculation.
def instruction(op,dst,kind,left='none',right='none',literal=0,source='none'):
 return f'inst {op} {kind} {source} none {dst} {left} {right} {literal:016x} 0000000000000000 -1 0 - 0 noreturn 0 args 0 classes 0 incoming 0 loc 0 0 0 0 0'
header='cinder-ir 6 lp64-le sysv-x86-64\ntypes 3\ntype 0 function 0 1 0 0 0 1 none 0 1 0 - params 0 fields 0 identity 18\ntype 1 int 0 1 0 0 4 4 none 0 none 0 - params 0 fields 0 identity 9\ntype 2 int 0 1 1 0 4 4 none 0 none 0 - params 0 fields 0 identity 10\nglobals 0\nfunctions 1\n'
def block(number,body,preds,succs,term):
 return f'block {number} x62{48+number:02x} instructions {len(body)} predecessors {len(preds)}'+''.join(' '+str(p) for p in preds)+f' successors {len(succs)}'+''.join(' '+str(p) for p in succs)+'\n'+'\n'.join(body)+('\n' if body else '')+'term '+term+' loc 0 0 0 0 0\nend-block\n'
cases=[('unsigned-zero-trip','mul',2,3,7,0,0,True),
 ('division-zero-trip','div.u',2,23,0,0,0,False),('division-active','div.u',2,23,0,1,2,False),
 ('signed-overflow-zero-trip','add',1,2147483647,1,0,0,False),('signed-overflow-active','add',1,2147483647,1,1,1,False),
 ('indeterminate-zero-trip','copy',2,None,7,0,0,False),('indeterminate-active','copy',2,None,7,1,4,False),
 ('overshift-zero-trip','shl',2,3,32,0,0,False),('overshift-active','shl',2,3,32,1,3,False),
 ('multiple-entry-kept','mul',2,3,7,0,0,False)]
for name,op,kind,left,right,condition,classification,moves in cases:
 entry=[instruction('undef' if left is None else 'const',0,kind,literal=left or 0),instruction('const',1,kind,literal=right),instruction('const',2,1,literal=condition),instruction('const',3,1,literal=13)]
 body=[instruction(op,4,kind,left=0,right='none' if op=='copy' else 1,source='none' if op=='copy' else kind)]
 if name=='multiple-entry-kept':
  text=header+'function x6d61696e 0 1 noreturn 0 5 0 0 params 0 blocks 6\n'+block(0,entry,[],[1,2],'branch none none 1 2 2')+block(1,[],[0],[3],'jump none 3 none none none')+block(2,[],[0],[3],'jump none 3 none none none')+block(3,[],[1,2,4],[4,5],'branch none none 4 5 2')+block(4,body,[3],[3],'jump none 3 none none none')+block(5,[],[3],[],'return 3 none none none none')
 else:
  text=header+'function x6d61696e 0 1 noreturn 0 5 0 0 params 0 blocks 4\n'+block(0,entry,[],[1],'jump none 1 none none none')+block(1,[],[0,2],[2,3],'branch none none 2 3 2')+block(2,body,[1],[1],'jump none 1 none none none')+block(3,[],[1],[],'return 3 none none none none')
 text+='end-function\nend-module\n';before=root/(name+'.before.cir');after=root/(name+'.after.cir');before.write_text(text)
 run([irtool,'--verify',before]);run([irtool,'--pass=loop-motion',before,'-o',after]);first=json.loads(run([irtool,'--classify',before]).stdout);second=json.loads(run([irtool,'--classify',after]).stdout)
 assert first==second and first['classification']==classification and (classification!=0 or first['integer']==13),(name,first,second)
 old=locations(before,op);new=locations(after,op);assert old.keys()==new.keys() and (old!=new)==moves,(name,old,new)
 objects=[]
 if classification==0:
  for version,path in (('before',before),('after',after)):
   obj=root/(name+'.'+version+'.o');run([irtool,'-c',path,'-o',obj]);row=dict(level=version,object=str(obj),sha256=h(obj))
   if native:
    binary=obj.with_suffix('.native');run(['cc','-no-pie',obj,'-o',binary]);r=subprocess.run([str(binary.resolve())],capture_output=True,text=True,timeout=10);assert r.returncode==13 and r.stdout==r.stderr=='',(name,version,r);row.update(native_exit=13,executable_sha256=h(binary))
   objects.append(row)
 save(dict(kind='typed-guard',name=name,expected=13 if classification==0 else None,classification=classification,before_sha256=h(before),after_sha256=h(after),interpreter=second,objects=objects))
assert hashes=={str(p):h(p) for p in inputs}
assert identity==hashlib.sha256(compiler.read_bytes()+irtool.read_bytes()+json.dumps(hashes,sort_keys=True).encode()).hexdigest()
print(f'Loop motion: 18 isolated sources and 10 typed zero-trip/preheader guards passed; native={native}')
