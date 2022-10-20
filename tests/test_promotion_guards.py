#!/usr/bin/env python3
"""Exercise independently parsed slot lifetimes, invalid writes and phi ownership."""
import hashlib,json,pathlib,platform,subprocess,sys
compiler=pathlib.Path(sys.argv[1]).resolve();irtool=compiler.parent/'cinderir';h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs=[pathlib.Path(__file__)]+[p for p in sorted(pathlib.Path('source').glob('*')) if p.is_file()]
hashes={str(p):h(p) for p in inputs};identity=hashlib.sha256(compiler.read_bytes()+irtool.read_bytes()+json.dumps(hashes,sort_keys=True).encode()).hexdigest()
root=pathlib.Path('.agent-local/promotion-guards')/identity;root.mkdir(parents=True,exist_ok=True);rows=[]
native=platform.system()=='Linux' and platform.machine()=='x86_64'
def run(argv):
 r=subprocess.run([str(a) for a in argv],capture_output=True,text=True,timeout=30);assert r.returncode==0,(argv,r);return r
def inst(op,dst='none',left='none',right='none',slot=-1,kind=1,literal=0,args=(),incoming=()):
 return f'inst {op} {kind} none none {dst} {left} {right} {literal:016x} 0000000000000000 {slot} 0 - 0 noreturn 0 args {len(args)}'+''.join(' '+str(a) for a in args)+f' classes 0 incoming {len(incoming)}'+''.join(' '+str(a) for a in incoming)+' loc 0 0 0 0 0'
header='cinder-ir 6 lp64-le sysv-x86-64\ntypes 5\ntype 0 function 0 1 0 0 0 1 none 0 1 0 - params 0 fields 0 identity 18\ntype 1 int 0 1 0 0 4 4 none 0 none 0 - params 0 fields 0 identity 9\ntype 2 pointer 0 1 0 0 8 8 1 0 none 0 - params 0 fields 0 identity 19\ntype 3 int 1 1 0 0 4 4 none 0 none 0 - params 0 fields 0 identity 9\ntype 4 int 2 1 0 0 4 4 none 0 none 0 - params 0 fields 0 identity 9\nglobals 0\nfunctions 1\n'
def block(number,body,preds,succs,term):
 return f'block {number} x62{48+number:02x} instructions {len(body)} predecessors {len(preds)}'+''.join(' '+str(v) for v in preds)+f' successors {len(succs)}'+''.join(' '+str(v) for v in succs)+'\n'+'\n'.join(body)+('\n' if body else '')+'term '+term+' loc 0 0 0 0 0\nend-block\n'
initial=[inst('const',0,literal=13),inst('local.init',left=0,slot=0)]
cases=[('initialized-slot',1,initial+[inst('local.load',1,slot=0)],1,0,13,True),
 ('uninitialized-load',1,[inst('local.load',0,slot=0)],0,4,None,True),
 ('uninitialized-store',1,[inst('undef',0),inst('const',1,literal=13),inst('local.store',left=0,slot=0)],1,4,None,False),
 ('retired-load',1,initial+[inst('local.end',slot=0),inst('local.load',1,slot=0)],1,10,None,False),
 ('retired-store',1,initial+[inst('local.end',slot=0),inst('local.store',left=0,slot=0)],0,10,None,False),
 ('retired-reset',1,initial+[inst('local.end',slot=0),inst('local.reset',slot=0)],0,10,None,False),
 ('renewed-lifetime',1,initial+[inst('local.end',slot=0),inst('local.begin',slot=0),inst('local.init',left=0,slot=0),inst('local.load',1,slot=0)],1,0,13,True),
 ('const-initialization',3,initial+[inst('local.load',1,slot=0)],1,0,13,True),
 ('volatile-storage',4,initial+[inst('local.load',1,slot=0)],1,0,13,False)]
texts=[]
for name,slot,body,returned,classification,value,changes in cases:
 count=max(int(line.split()[5]) for line in body if line.split()[5]!='none')+1
 text=header+f'function x6d61696e 0 1 noreturn 0 {count} 1 0 params 0 blocks 1\nlocal {slot} align 0\n'+block(0,body,[],[],f'return {returned} none none none none')+'end-function\nend-module\n'
 texts.append((name,text,classification,value,changes,None))
for condition in (0,1):
 entry=initial+[inst('const',1,literal=condition)]
 text=header+'function x6d61696e 0 1 noreturn 0 3 1 0 params 0 blocks 4\nlocal 1 align 0\n'+block(0,entry,[],[1,2],'branch none none 1 2 1')+block(1,[inst('local.end',slot=0)],[0],[3],'jump none 3 none none none')+block(2,[],[0],[3],'jump none 3 none none none')+block(3,[inst('local.load',2,slot=0)],[1,2],[],'return 2 none none none none')+'end-function\nend-module\n'
 texts.append(('possibly-retired-'+str(condition),text,10 if condition else 0,None if condition else 13,False,None))
for slot in (0,):
 entry=[inst('const',0,literal=9),inst('local.init',left=0,slot=0),inst('const',1,literal=13),inst('const',2,literal=17),inst('const',3)]
 phi=inst('phi',6,slot=slot,args=(1,2),incoming=(1,2))
 text=header+'function x6d61696e 0 1 noreturn 0 7 1 0 params 0 blocks 4\nlocal 1 align 0\n'+block(0,entry,[],[1,2],'branch none none 1 2 3')+block(1,[inst('local.load',4,slot=0)],[0],[3],'jump none 3 none none none')+block(2,[inst('local.load',5,slot=0)],[0],[3],'jump none 3 none none none')+block(3,[phi],[1,2],[],'return 6 none none none none')+'end-function\nend-module\n'
 texts.append(('existing-phi-'+str(slot),text,0,17,True,('1','2')))
for name,text,classification,value,changes,phi_values in texts:
 before=root/(name+'.before.cir');after=root/(name+'.after.cir');again=root/(name+'.again.cir');stats=root/(name+'.stats.json');second_stats=root/(name+'.again.json');before.write_text(text)
 first=json.loads(run([irtool,'--classify',before]).stdout)
 run([irtool,'--pass=mem2reg','--pass-stats',stats,before,'-o',after]);second=json.loads(run([irtool,'--classify',after]).stdout)
 assert first==second and second['classification']==classification and (classification!=0 or second['integer']==value),(name,first,second)
 data=json.loads(stats.read_text());assert len(data['passes'])==1 and data['passes'][0]['name']=='mem2reg' and (data['passes'][0]['transformation_events']>0)==changes,(name,data)
 run([irtool,'--pass=mem2reg','--pass-stats',second_stats,after,'-o',again]);assert after.read_bytes()==again.read_bytes(),name
 assert json.loads(second_stats.read_text())['passes'][0]['transformation_events']==0,name
 if phi_values is not None:
  line=next(line for line in after.read_text().splitlines() if line.startswith('inst phi '));words=line.split();position=words.index('args');assert tuple(words[position+2:position+4])==phi_values,(name,line)
 objects=[]
 if classification==0:
  for level,path in (('before',before),('after',after)):
   obj=root/(name+'.'+level+'.o');run([irtool,'-c',path,'-o',obj]);record=dict(level=level,object=str(obj),sha256=h(obj))
   if native:
    binary=obj.with_suffix('.native');run(['cc','-no-pie',obj,'-o',binary]);r=subprocess.run([str(binary.resolve())],capture_output=True,text=True,timeout=10);assert r.returncode==value and r.stdout==r.stderr=='',(name,level,r);record.update(native_exit=r.returncode,executable_sha256=h(binary))
   objects.append(record)
 rows.append(dict(kind='typed-guard',name=name,expected=value,classification=classification,before_sha256=h(before),after_sha256=h(after),stats_sha256=h(stats),interpreter=second,objects=objects))
 (root/'observations.json').write_text(json.dumps(dict(inputs=hashes,compiler_sha256=h(compiler),irtool_sha256=h(irtool),native=native,cases=rows),indent=2)+'\n')
# Invalid const slot writes are rejected before any transformation/publication.
invalid=root/'const-store.invalid.cir';invalid.write_text(header+'function x6d61696e 0 1 noreturn 0 1 1 0 params 0 blocks 1\nlocal 3 align 0\n'+block(0,[inst('const',0,literal=13),inst('local.store',left=0,slot=0)],[],[],'return 0 none none none none')+'end-function\nend-module\n')
output=root/'preserved.cir';output.write_bytes(b'prior complete output\n');r=subprocess.run([str(irtool),'--pass=mem2reg',str(invalid),'-o',str(output)],capture_output=True,text=True,timeout=30)
assert r.returncode!=0 and 'const-qualified' in r.stderr and output.read_bytes()==b'prior complete output\n'
bad_phi=root/'missing-phi-slot.invalid.cir';bad_phi.write_text(text.replace('inst phi 1 none none 6 none none 0000000000000000 0000000000000000 0 ', 'inst phi 1 none none 6 none none 0000000000000000 0000000000000000 -1 '))
r=subprocess.run([str(irtool),'--pass=mem2reg',str(bad_phi),'-o',str(output)],capture_output=True,text=True,timeout=30)
assert r.returncode!=0 and 'slot is outside' in r.stderr and output.read_bytes()==b'prior complete output\n'
assert hashes=={str(p):h(p) for p in inputs} and identity==hashlib.sha256(compiler.read_bytes()+irtool.read_bytes()+json.dumps(hashes,sort_keys=True).encode()).hexdigest()
print(f'Promotion guards: {len(texts)} slot lifetime/definedness/phi cases, repeated-pass identity and const-store/invalid-phi rejection passed; native={native}')
