"""Reconstruct application progress from retained objects, commands and golden bytes."""
import json
from pathlib import Path, PurePosixPath
import struct

from application_fixtures import fixtures
from evidence_integrity import EvidenceError, artifact_path, digest
from native_evidence import elf as target_elf
from pass_evidence import elf as object_elf


def require(condition, message):
    if not condition: raise EvidenceError(message)


def input_files(source):
    files = sorted(p for folder in ('source','runtime/include','examples/apps') for p in (source/folder).rglob('*') if p.is_file())
    files += [source/p for p in ('CMakeLists.txt','tests/test_applications.py','tests/test_application_evidence.py','tests/test_unwind.py','tests/native_profile.py','tests/run_apps.sh','tools/application_fixtures.py','tools/application_evidence.py','tools/evidence_integrity.py','tools/pass_evidence.py','tools/native_evidence.py','tools/gate_registry.py')]
    return {str(p.relative_to(source)):digest(p) for p in files}


def text_size(path):
    object_elf(path,1);data=path.read_bytes();at=struct.unpack_from('<Q',data,40)[0]
    width,count,names=struct.unpack_from('<HHH',data,58)
    entries=[struct.unpack_from('<IIQQQQIIQQ',data,at+i*width) for i in range(count)]
    strings=data[entries[names][4]:entries[names][4]+entries[names][5]]
    sections={strings[e[0]:strings.index(0,e[0])]:e for e in entries}
    require(b'.text' in sections and sections[b'.text'][5]>0,'empty application object')
    return sections[b'.text'][5]


def verify_applications(base,source,compiler=None,*,require_native=True):
    base,source=Path(base).resolve(),Path(source).resolve()
    data=json.loads((base/'report.json').read_text())
    require(data.get('schema')==2 and data.get('inputs')==input_files(source),'stale or incomplete application source binding')
    expected=fixtures();directory=source/'examples/apps/fixtures'
    require({p.name for p in directory.iterdir()}==set(expected) and all((directory/name).read_bytes()==value for name,value in expected.items()),'application golden observations differ from independent oracles')
    apps=json.loads(expected['cases.json'])['applications']
    require(len(apps)==8 and sum(len(a['units'])>1 for a in apps)==3,'wrong application integration inventory')
    require({p for a in apps for p in a['units']}=={str(p.relative_to(source)) for p in (source/'examples/apps').rglob('*.c')},'unregistered or missing application source')
    profile=data.get('execution_profile');execute=profile in ('native','emulated')
    require(profile in ('native','emulated','cross') and data.get('native') is (profile=='native') and data.get('target_execution') is execute,'inconsistent application execution profile')
    if require_native:require(profile=='native','application gate requires native Linux x86-64 execution')
    if execute:
        require(data.get('machine')==dict(system='Linux',machine='x86_64') and data.get('stack_profile',{}).get('soft_after')==64*1024*1024,'wrong application target or stack profile')
    require(data.get('environment')==dict(LC_ALL='C',TZ='UTC',SOURCE_DATE_EPOCH='0'),'undeclared application environment')
    original_base=PurePosixPath(data.get('artifact_prefix',''));original_source=PurePosixPath(data.get('source_prefix',''))
    require(original_base.is_absolute() and original_source.is_absolute() and '..' not in original_base.parts+original_source.parts,'unsafe original application roots')
    inventory=data.get('artifacts',{});require(isinstance(inventory,dict) and inventory,'empty application artifact inventory');used=set()
    def artifact(name):
        require(name in inventory,'unlisted application artifact: '+str(name));path=artifact_path(base,name)
        require(path.is_file() and digest(path)==inventory[name],'missing or changed application artifact: '+name);used.add(name);return path
    # Validate every digest, including artifacts whose semantic role is checked later.
    for name in inventory:artifact(name)
    used.clear()
    require(data.get('compiler_artifact')=='tools/cindercc','missing retained original compiler')
    require(digest(artifact('tools/cindercc'))==data.get('compiler_sha256'),'substituted original application compiler')
    if compiler is not None:require(digest(Path(compiler))==data.get('compiler_sha256'),'application compiler does not match requested compiler')
    compiler_path=data.get('compiler_path');require(isinstance(compiler_path,str) and PurePosixPath(compiler_path).is_absolute(),'missing original compiler path')
    tools=data.get('tools',{});require(set(tools)=={'gcc','clang','objdump','python'}|({'cc'} if execute else set()),'incomplete application reference tools')
    for name,record in tools.items():
        require(record.get('artifact')=='tools/'+name and digest(artifact(record['artifact']))==record.get('sha256') and PurePosixPath(record.get('path','')).is_absolute(),'substituted application tool: '+name)
    commands=data.get('commands',[]);require(commands==json.loads(artifact('commands.json').read_text()),'changed application command log')
    cursor=0
    def consume(argv,label,*,cwd=original_source,exit=0,stdin=b'',stdout=b'',stderr=b''):
        nonlocal cursor
        require(cursor<len(commands),'missing application command: '+label);row=commands[cursor];cursor+=1
        import hashlib
        require(row.get('argv')==list(map(str,argv)) and row.get('cwd')==str(cwd) and row.get('exit')==exit and row.get('stdin_sha256')==hashlib.sha256(stdin).hexdigest(),'wrong command, input or exit: '+label)
        require(row.get('stdout')==label+'.stdout' and row.get('stderr')==label+'.stderr','wrong command output binding: '+label)
        out,err=artifact(row['stdout']).read_bytes(),artifact(row['stderr']).read_bytes()
        require((out==stdout if stdout is not None else bool(out)) and err==stderr,'wrong raw observation: '+label)
        return cursor-1
    consume([tools['python']['path'],'tools/application_fixtures.py','--check'],'fixture-oracles',stdout=f'Application fixtures: {len(expected)} frozen input/output records match independent oracles\n'.encode())
    for name in ('gcc','clang','objdump','python',*(['cc'] if execute else [])):
        consume([tools[name]['path'],'--version'],name+'-version',stdout=None)
    links=json.loads(artifact('linker.json').read_text());require(links==data.get('linker_invocations'),'changed original linker invocation log')
    link_cursor=objects=references=sanitized=targets=0
    observations=data.get('observations',[]);require(len(observations)==len(apps),'missing or duplicated application')
    for app,observed in zip(apps,observations):
        require(observed.get('name')==app['name'] and observed.get('units')==app['units'] and observed.get('interpreter')==dict(eligible=False,reason=app['interpreter_exclusion']),'changed application or interpreter classification')
        units=[original_source/p for p in app['units']];expected_objects=[];expected_executions=[]
        for level in ('-O0','-O2'):
            stem=app['name']+level;unit_hashes=[]
            for i,unit in enumerate(units):
                name=stem+'-'+str(i)+'.o';obj=artifact(name);size=text_size(obj);objects+=1;unit_hashes.append(digest(obj))
                consume([compiler_path,level,'-fverify-each','-I'+str(original_source/'runtime/include'),'-c',unit,'-o',original_base/name],stem+'-compile-'+str(i))
                consume([tools['objdump']['path'],'-dr',original_base/name],stem+'-disassemble-'+str(i),stdout=None)
                expected_objects.append(dict(level=level,unit=app['units'][i],path=name,sha256=digest(obj),text_bytes=size))
            implementations=[]
            for reference in ('gcc','clang'):
                name=stem+'-'+reference;binary=artifact(name)
                if execute:target_elf(binary,(2,3))
                consume([tools[reference]['path'],'-std=c17',level,*units,'-o',original_base/name],stem+'-'+reference+'-build')
                implementations.append((reference,name))
            if execute:
                name=stem+'-cinder';target_elf(artifact(name),(2,))
                consume([compiler_path,level,'-fverify-each','-I'+str(original_source/'runtime/include'),*units,'-o',original_base/name],stem+'-normal-driver-build')
                require(link_cursor<len(links),'missing normal driver linker call');link=links[link_cursor]
                argv=link.get('argv',[]);items=link.get('objects',[])
                require(len(argv)==len(units)+3 and argv[0]=='-no-pie' and argv[-2]=='-o' and len(items)==len(units),'wrong normal driver link shape')
                require(argv[1:-2]==[o.get('path') for o in items] and all(PurePosixPath(p).suffix=='.o' for p in argv[1:-2]),'host compiler received user C or missing original objects')
                for i,item in enumerate(items):
                    copy='link-inputs/'+str(link_cursor)+'-'+str(i)+'.o'
                    require(item.get('copy')==str(original_base/copy) and item.get('sha256')==unit_hashes[i] and digest(artifact(copy))==unit_hashes[i],'normal driver objects differ from standalone original emission')
                    text_size(base/copy)
                link_cursor+=1;implementations.append(('cinder',name))
            if level=='-O2':
                name=stem+'-reference-sanitized';binary=artifact(name)
                if execute:target_elf(binary,(2,3))
                consume([tools['clang']['path'],'-std=c17','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer',*units,'-o',original_base/name],stem+'-reference-sanitized-build')
                implementations.append(('reference-sanitized',name))
            for implementation,name in implementations:
                for case in app['cases']:
                    label=stem+'-'+implementation+'-'+case['name'];work=original_base/label
                    arguments=[a.replace('$FIXTURES',str(original_source/'examples/apps/fixtures')).replace('$WORK',str(work)) for a in case['argv']]
                    index=consume([original_base/name,*arguments],label,cwd=work,exit=case['exit'],stdin=expected[case['stdin']],stdout=expected[case['stdout']],stderr=expected[case['stderr']])
                    files={}
                    for filename,golden in case['files'].items():
                        output=artifact(label+'/'+filename);require(output.read_bytes()==expected[golden],'wrong application output file');files[filename]=digest(output)
                    expected_executions.append(dict(level=level,implementation=implementation,case=case['name'],command=index,files=files))
                    if implementation=='cinder':targets+=1
                    elif implementation=='reference-sanitized':sanitized+=1
                    else:references+=1
        require(observed.get('objects')==expected_objects and observed.get('executions')==expected_executions,'missing, repeated or inflated application observations')
    require(cursor==len(commands) and link_cursor==len(links) and used==set(inventory),'unconsumed command, link or artifact')
    counts=dict(applications=len(apps),multi_translation_unit=sum(len(a['units'])>1 for a in apps),cases=sum(len(a['cases']) for a in apps),compiled_original_objects=objects,reference_executions=references,reference_sanitizer_executions=sanitized,target_executions=targets)
    valid=sum(c['exit']==0 for a in apps for c in a['cases']);rejected=counts['cases']-valid
    counts.update(valid_inputs=valid,rejected_inputs=rejected,target_valid_executions=valid*2 if execute else 0,target_rejected_inputs=rejected*2 if execute else 0,reference_valid_executions=valid*4,reference_rejected_inputs=rejected*4)
    require(all(type(data.get(k)) is int and data[k]==v for k,v in counts.items()),'self-reported application progress differs from raw results')
    return dict(**counts,execution_profile=profile,compiler_sha256=data['compiler_sha256'])
