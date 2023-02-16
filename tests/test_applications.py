#!/usr/bin/env python3
"""Compare eight real normal-driver applications with frozen independent fixtures."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile

from native_profile import configure_stack
from test_unwind import digest, elf, tool


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('compiler', type=Path)
    parser.add_argument('--execution-profile', choices=('native','emulated','cross')); args = parser.parse_args()
    compiler = args.compiler.resolve(); compiler_hash = digest(compiler)
    linux = platform.system() == 'Linux' and platform.machine() == 'x86_64'
    profile = args.execution_profile or ('native' if linux else 'cross'); execute = profile in ('native','emulated'); assert not execute or linux
    repository = Path('.').resolve(); fixtures = repository/'examples/apps/fixtures'
    registry = json.loads((fixtures/'cases.json').read_text()); apps = registry['applications']
    assert registry['schema'] == 1 and len(apps) == 8 and len({a['name'] for a in apps}) == 8
    assert sum(len(a['units']) > 1 for a in apps) == 3 and all(a['cases'] and a['description'] and a['interpreter_exclusion'] for a in apps)
    assert {p for a in apps for p in a['units']} == {str(p) for p in Path('examples/apps').rglob('*.c')}
    files = sorted(p for folder in ('source','runtime/include','examples/apps') for p in Path(folder).rglob('*') if p.is_file())
    files += [Path(p) for p in ('CMakeLists.txt','tests/test_applications.py','tests/test_application_evidence.py','tests/test_unwind.py','tests/native_profile.py','tests/run_apps.sh','tools/application_fixtures.py','tools/application_evidence.py','tools/evidence_integrity.py','tools/pass_evidence.py','tools/native_evidence.py','tools/gate_registry.py')]
    inputs = {str(p): digest(p) for p in files}; source_hash = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()
    base = Path('.agent-local/applications')/(source_hash+'-'+compiler_hash); base.mkdir(parents=True,exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='run-',dir=base)).resolve(); commands = []; observations = []; artifacts = set()
    tools = dict(gcc=tool('gcc-15' if shutil.which('gcc-15') else 'gcc'),clang=tool('clang'),objdump=tool('llvm-objdump'),python=tool(sys.executable))
    if execute: tools['cc'] = tool('cc')
    identities = {name:dict(path=path,sha256=digest(Path(path))) for name,path in tools.items()}
    retained = root/'tools';retained.mkdir();shutil.copyfile(compiler,retained/'cindercc');artifacts.add(retained/'cindercc')
    for name,path in tools.items():
        copy=retained/name;shutil.copyfile(path,copy);identities[name]['artifact']='tools/'+name;artifacts.add(copy)
    stack = configure_stack(); environment = dict(os.environ,LC_ALL='C',TZ='UTC',SOURCE_DATE_EPOCH='0')
    def run(argv,label,cwd=repository,stdin=b'',env=None):
        argv = list(map(str,argv)); result = subprocess.run(argv,input=stdin,capture_output=True,cwd=cwd,env=env or environment,timeout=120)
        out,err = root/(label+'.stdout'),root/(label+'.stderr');out.write_bytes(result.stdout);err.write_bytes(result.stderr);artifacts.update((out,err))
        row = dict(argv=argv,cwd=str(cwd),exit=result.returncode,stdout=str(out.relative_to(root)),stderr=str(err.relative_to(root)),stdin_sha256=hashlib.sha256(stdin).hexdigest())
        commands.append(row);(root/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        return result,row
    def success(argv,label,**kwargs):
        result,row=run(argv,label,**kwargs);assert result.returncode==0,(row,result.stderr);return row
    success([tools['python'],'tools/application_fixtures.py','--check'],'fixture-oracles')
    for name,path in tools.items():success([path,'--version'],name+'-version')
    shims=root/'linker-shims';shims.mkdir();link_log=root/'linker.json';link_log.write_text('[]\n');saved=root/'link-inputs';saved.mkdir()
    if execute:
        wrapper = """#!/usr/bin/env python3
import hashlib,json,os,pathlib,shutil,sys
args=sys.argv[1:];log=pathlib.Path(os.environ['CINDER_APP_LINK_LOG']);rows=json.loads(log.read_text());directory=pathlib.Path(os.environ['CINDER_APP_OBJECTS'])
assert args[0]=='-no-pie' and args[-2]=='-o' and len(args)>3
objects=[]
for i,name in enumerate(args[1:-2]):
 p=pathlib.Path(name);assert p.suffix=='.o' and p.read_bytes()[:7]==b'\\x7fELF\\x02\\x01\\x01'
 copy=directory/(str(len(rows))+'-'+str(i)+'.o');shutil.copyfile(p,copy);objects.append(dict(path=str(p),copy=str(copy),sha256=hashlib.sha256(copy.read_bytes()).hexdigest()))
rows.append(dict(argv=args,objects=objects));log.write_text(json.dumps(rows,indent=2)+'\\n');real=os.environ['CINDER_APP_REAL_CC'];os.execv(real,[real,*args])
"""
        (shims/'cc').write_text(wrapper);(shims/'cc').chmod(0o755)
    for name in ('gcc','clang','as','cpp'):
        p=shims/name;p.write_text('#!/usr/bin/env python3\nimport sys\nsys.stderr.write("unexpected delegated tool: '+name+'\\n")\nsys.exit(97)\n');p.chmod(0o755)
    compiler_environment=dict(environment,PATH=str(shims)+os.pathsep+environment['PATH'])
    if execute:compiler_environment.update(CINDER_APP_LINK_LOG=str(link_log),CINDER_APP_OBJECTS=str(saved),CINDER_APP_REAL_CC=tools['cc'])
    native_runs=reference_runs=sanitizer_runs=object_count=0
    for app in apps:
        units=[repository/p for p in app['units']]
        assert all(p.is_file() for p in units)
        app_record=dict(name=app['name'],units=app['units'],objects=[],executions=[],interpreter=dict(eligible=False,reason=app['interpreter_exclusion']))
        for level in ('-O0','-O2'):
            stem=app['name']+level; objects=[]
            for i,unit in enumerate(units):
                obj=root/(stem+'-'+str(i)+'.o');success([compiler,level,'-fverify-each','-I'+str(repository/'runtime/include'),'-c',unit,'-o',obj],stem+'-compile-'+str(i),env=compiler_environment)
                sections,symbols,_=elf(obj);assert sections['.text'] and any(s['kind']==2 and s['size']>0 for s in symbols)
                success([tools['objdump'],'-dr',obj],stem+'-disassemble-'+str(i));artifacts.add(obj);objects.append(obj);object_count+=1
                app_record['objects'].append(dict(level=level,unit=str(unit.relative_to(repository)),path=str(obj.relative_to(root)),sha256=digest(obj),text_bytes=len(sections['.text'])))
            binaries=[]
            for reference in ('gcc','clang'):
                binary=root/(stem+'-'+reference);success([tools[reference],'-std=c17',level,*units,'-o',binary],stem+'-'+reference+'-build');artifacts.add(binary);binaries.append((reference,binary))
            if execute:
                binary=root/(stem+'-cinder');before=len(json.loads(link_log.read_text()))
                success([compiler,level,'-fverify-each','-I'+str(repository/'runtime/include'),*units,'-o',binary],stem+'-normal-driver-build',env=compiler_environment)
                links=json.loads(link_log.read_text());assert len(links)==before+1 and [o['sha256'] for o in links[-1]['objects']]==[digest(p) for p in objects]
                binaries.append(('cinder',binary));artifacts.add(binary)
            if level=='-O2':
                binary=root/(stem+'-reference-sanitized');success([tools['clang'],'-std=c17','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer',*units,'-o',binary],stem+'-reference-sanitized-build');artifacts.add(binary);binaries.append(('reference-sanitized',binary))
            for implementation,binary in binaries:
                for case in app['cases']:
                    label=stem+'-'+implementation+'-'+case['name'];work=root/label;work.mkdir()
                    for name,fixture in case.get('previous_files',{}).items():(work/name).write_bytes((fixtures/fixture).read_bytes())
                    arguments=[a.replace('$FIXTURES',str(fixtures)).replace('$WORK',str(work)) for a in case['argv']]
                    result,row=run([binary,*arguments],label,cwd=work,stdin=(fixtures/case['stdin']).read_bytes())
                    assert result.returncode==case['exit'] and result.stdout==(fixtures/case['stdout']).read_bytes() and result.stderr==(fixtures/case['stderr']).read_bytes(),(label,row,result)
                    assert {p.name for p in work.iterdir()}==set(case['files']),(label,'unexpected/missing output file')
                    for name,fixture in case['files'].items():assert (work/name).read_bytes()==(fixtures/fixture).read_bytes();artifacts.add(work/name)
                    app_record['executions'].append(dict(level=level,implementation=implementation,case=case['name'],command=len(commands)-1,files={name:digest(work/name) for name in case['files']}))
                    if implementation=='cinder':native_runs+=1
                    elif implementation=='reference-sanitized':sanitizer_runs+=1
                    else:reference_runs+=1
        observations.append(app_record);(root/'progress.json').write_text(json.dumps(observations,indent=2)+'\n')
        print('Application:',app['name'],'passed; target_execution='+str(execute),flush=True)
    case_count=sum(len(a['cases']) for a in apps)
    assert reference_runs==case_count*4 and sanitizer_runs==case_count and native_runs==(case_count*2 if execute else 0)
    assert object_count==2*sum(len(a['units']) for a in apps)
    assert digest(compiler)==compiler_hash and inputs=={name:digest(Path(name)) for name in inputs}
    assert all(digest(Path(row['path']))==row['sha256'] for row in identities.values())
    links=json.loads(link_log.read_text());assert len(links)==(16 if execute else 0)
    for entry in links:
        for obj in entry['objects']:assert digest(Path(obj['copy']))==obj['sha256'];artifacts.add(Path(obj['copy']))
    artifacts.add(link_log)
    artifacts.add(root/'commands.json')
    report=dict(schema=2,compiler_sha256=compiler_hash,compiler_path=str(compiler),compiler_artifact='tools/cindercc',artifact_prefix=str(root),source_prefix=str(repository),machine=dict(system=platform.system(),machine=platform.machine()),environment={k:environment[k] for k in ('LC_ALL','TZ','SOURCE_DATE_EPOCH')},inputs=inputs,tools=identities,execution_profile=profile,native=profile=='native',target_execution=execute,stack_profile=stack,applications=8,multi_translation_unit=3,cases=case_count,compiled_original_objects=object_count,reference_executions=reference_runs,reference_sanitizer_executions=sanitizer_runs,target_executions=native_runs,observations=observations,commands=commands,linker_invocations=links,artifacts={str(p.relative_to(root)):digest(p) for p in sorted(artifacts)})
    valid=sum(c['exit']==0 for a in apps for c in a['cases']);rejected=case_count-valid
    report.update(valid_inputs=valid,rejected_inputs=rejected,target_valid_executions=valid*2 if execute else 0,target_rejected_inputs=rejected*2 if execute else 0,reference_valid_executions=valid*4,reference_rejected_inputs=rejected*4)
    (root/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    sys.path.insert(0,str(repository/'tools'))
    from application_evidence import verify_applications
    verify_applications(root,repository,compiler,require_native=profile=='native')
    print(f'Applications: eight fixtures, three multi-TU, {case_count} inputs, {reference_runs} reference, {sanitizer_runs} sanitized reference and {native_runs} original target executions; evidence={root}')


if __name__=='__main__':main()
