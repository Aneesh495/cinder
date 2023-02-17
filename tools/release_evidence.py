"""Read-only readers for the complete native release campaigns."""
import collections
import json
from pathlib import Path
import re
import shlex
import statistics
import sys

from evidence_integrity import EvidenceError, artifact_path, digest, inventory_digest, source_inputs
from native_evidence import elf, hashed, read, require, verify_generated


def inventory(base, declared, exclude):
    actual={str(p.relative_to(base)) for p in base.rglob('*') if p.is_file() and str(p.relative_to(base)) not in exclude}
    require(isinstance(declared,dict) and set(declared)==actual and actual, 'incomplete raw artifact inventory')
    for name,record in declared.items():
        path=hashed(base,name,record.get('sha256'))
        require(path.stat().st_size==record.get('bytes'), 'changed raw artifact extent: '+name)


def verify_fuzz(base, source, compiler, minimum=1000000):
    base,source,compiler=Path(base),Path(source),Path(compiler)
    summary=read(base,'summary.json');configuration=read(base,'configuration.json')
    require(summary.get('schema')==1 and summary.get('status')=='pass', 'incomplete fuzz campaign')
    require(summary.get('inputs')==source_inputs(source) and summary.get('source_sha256')==inventory_digest(source_inputs(source)), 'fuzz source changed')
    require(summary.get('compiler_sha256')==digest(compiler), 'fuzz seed compiler changed')
    require(all(summary.get(k)==v for k,v in configuration.items() if k!='stages'), 'changed fuzz configuration')
    require(set(summary.get('stages',{}))=={'pp','parser','ir','object'}, 'missing fuzz target')
    require(len(summary.get('source_seeds',{}))>=32 and len(summary.get('ir_seeds',{}))>=16, 'vacuous fuzz seed corpus')
    for name,row in summary['source_seeds'].items():
        require(digest(source/row['source'])==row['sha256'], 'changed authored fuzz seed')
        hashed(base,'seeds/'+name,row['sha256'])
    for name,value in summary['ir_seeds'].items():
        require(hashed(base,'seeds/'+name,value).read_bytes().startswith(b'cinder-ir 6 '), 'noncanonical IR seed')
    total=0
    for index,stage in enumerate(('pp','parser','ir','object')):
        record=read(base,stage+'/command.json')
        require(record==summary['stages'][stage] and record.get('exit')==0, 'failed fuzz target: '+stage)
        hashed(base,stage+'/cinder_fuzz_'+stage,record.get('target_sha256'))
        log=hashed(base,stage+'/fuzzer.log',record.get('log_sha256')).read_text(errors='replace')
        count=summary['requested']
        require(type(count) is int and count>=minimum and re.search(r'Done '+str(count)+r' runs in ',log), 'truncated fuzz execution log')
        coverage=re.findall(r'#(\d+)\s+DONE\s+cov: (\d+) ft: (\d+)',log)
        require(coverage and int(coverage[-1][0])>=count and int(coverage[-1][1])>100 and int(coverage[-1][2])>100, 'missing instrumented coverage')
        require('ERROR:' not in log and 'runtime error:' not in log and 'SUMMARY:' not in log, 'fuzz sanitizer failure')
        argv=record.get('command',[])
        require(len(argv)==9 and Path(argv[0]).name=='cinder_fuzz_'+stage and '-runs='+str(count) in argv and '-seed='+str(495+index) in argv and '-print_final_stats=1' in argv, 'wrong fuzz invocation')
        stats=read(base,stage+'/stats.json')
        require(stats==record.get('counters') and stats.get('stage')==index and stats.get('attempts')==count, 'inflated fuzz execution totals')
        require(stats.get('harness_io_errors')==stats.get('harness_extent_rejections')==0, 'fuzz harness skipped work')
        if index<2:
            require(stats.get('source_buffers',0)>100 and stats.get('lexed',0)>100, 'frontend corpus never reached lexer')
            if index==1:require(stats.get('parsed',0)>10 and stats.get('typed',0)>10, 'parser corpus never reached valid semantics')
        else:
            require(stats.get('verified_ir',0)>=16, 'IR corpus never reached verifier')
            if index==2:require(stats.get('roundtrips')==stats['verified_ir'], 'IR round trips were skipped')
            else:require(stats.get('encoded_functions',0)>=16 and stats.get('nonempty_objects',0)>=16 and stats['objects']<=stats['verified_ir'], 'object target never emitted code')
        require(not any((base/stage).glob('crash-*')) and not any((base/stage).glob('timeout-*')), 'unresolved fuzz finding')
        total+=count
    return dict(executions=total)


def common(gate,base,source,compiler):
    data=read(base,'campaign.json')
    inputs=source_inputs(source)
    require(data.get('schema')==1 and data.get('status')=='pass' and data.get('gate')==gate, 'incomplete release campaign')
    require(data.get('inputs')==inputs and data.get('source_sha256')==inventory_digest(inputs), 'release campaign source mismatch')
    require(data.get('compiler_sha256')==digest(compiler), 'release compiler mismatch')
    hashed(base,'compiler',digest(compiler))
    require(data.get('native') is True and data.get('machine',{}).get('system')=='Linux' and data['machine'].get('machine')=='x86_64', 'native Linux evidence required')
    require(data.get('environment')==dict(SOURCE_DATE_EPOCH='0',LC_ALL='C',TZ='UTC'), 'undeclared campaign environment')
    inventory(base,data.get('artifacts'),{'campaign.json'})
    commands=data.get('commands')
    require(isinstance(commands,list) and commands, 'missing executed commands')
    for row in commands:
        require(row.get('exit')==row.get('expected') and 'timeout' not in row and row.get('elapsed',-1)>=0 and isinstance(row.get('argv'),list) and row['argv'], 'failed or truncated campaign command')
        require(isinstance(row.get('stdout'),str) and isinstance(row.get('stderr'),str), 'missing raw command output')
        require('ERROR: AddressSanitizer' not in row['stderr'] and 'runtime error:' not in row['stderr'], 'sanitizer campaign failure')
    return data


def verify_campaign(gate,base,source,compiler):
    base,source,compiler=Path(base),Path(source),Path(compiler)
    data=common(gate,base,source,compiler);commands=data['commands']
    def roles(name):return [r for r in commands if r['role']==name]
    def command(name,expected=0):
        rows=roles(name);require(len(rows)==1 and rows[0]['exit']==expected, 'missing or duplicate command: '+name)
        return rows[0]
    def owned(row,flags):
        require(row['argv'][0]==data['compiler_path'] and all(flag in row['argv'] for flag in flags), 'host fallback or changed original command')
    if gate=='fresh-build':
        from release_campaigns import BUILD_PROFILES
        modules={str(p.relative_to(source)) for p in (source/'source').glob('*.c')}
        for name,host,kind,sanitize in BUILD_PROFILES:
            for action in ('version','configure','build','smoke','regressions','buffers'):command(name+'-'+action)
            rows=read(base,name+'/compile_commands.json');observed=set()
            for row in rows:
                path=Path(row['file']);relative='source/'+path.name
                if relative not in modules:continue
                argv=shlex.split(row['command'])
                require('-std=c17' in argv and '-Werror' in argv and '-c' in argv, 'non-strict host profile')
                if sanitize:require('-fsanitize=address,undefined' in argv, 'missing host sanitizers')
                observed.add(relative)
            require(observed==modules, 'host profile omitted a compiler source')
            cache=(base/name/'CMakeCache.txt').read_text()
            require('CMAKE_BUILD_TYPE:STRING='+kind in cache, 'wrong host build profile')
            for binary in ('cindercc','cinderir','cinder_source_buffer_probe'):elf(base/name/binary,(2,3))
        require(len(commands)==24,'extra host profile commands');return 4
    if gate=='original-pipeline':
        for name,n,expected in [('hello',1,42),('globals',1,42),('multi',2,9)]:
            require(len(roles(name+'-compile'))==n,'missing original translation unit')
            for row in roles(name+'-compile'):owned(row,['-O2','-c'])
            command(name+'-link');owned(command(name+'-driver'),['-O2'])
            command(name+'-native',expected);command(name+'-driver-native',expected)
            for index in range(n):elf(base/name/f'{index}.o',(1,))
            for binary in ('explicit','driver'):elf(base/name/binary,(2,))
        require(len(commands)==16,'extra native pipeline commands');return 1
    if gate=='feature-families':
        from release_campaigns import feature_sources
        specs=feature_sources(source);rows=data.get('families',[])
        require(len(rows)==len(specs)==48,'missing language family')
        for row,spec in zip(rows,specs):
            require(all(row.get(k)==v for k,v in spec.items()),'changed source-linked feature')
            name=spec['id'];expected=row['expected']
            require(len(roles(name+'-reference-compile'))==len(roles(name+'-reference'))==2,'missing feature references')
            for result in roles(name+'-reference'):require(result['exit']==expected and result['stdout']==result['stderr']=='','feature reference disagreement')
            require(len(roles(name+'-compile'))==len(roles(name+'-link'))==len(roles(name+'-native'))==len(roles(name+'-interpret'))==2,'incomplete feature levels')
            for result in roles(name+'-compile'):owned(result,['-c','-fverify-each'])
            for result in roles(name+'-native'):require(result['exit']==expected and not result['stdout'] and not result['stderr'],'feature native discrepancy')
            for result in roles(name+'-interpret'):require(result['stdout']==f'interpret main => {expected}\n','feature interpreter discrepancy')
            require(len(roles(name+'-negative'))==len(spec['negative']),'missing family rejection')
            for result in roles(name+'-negative'):
                owned(result,['-fsyntax-only']);require(result['exit']==1 and 'error:' in result['stderr'],'missing diagnostic')
            for level in ('-O0','-O2'):elf(base/name/(level+'.o'),(1,));elf(base/name/level,(2,))
        return len(rows)
    if gate=='authored-suite':
        owned_command=command('authored');require('tests/test_control.py' in owned_command['argv'] and owned_command['argv'][-1]==data['compiler_path'],'wrong authored producer')
        rows=read(base,'authored/observations.json');ledger=read(source,'tests/control/cases.json')
        require(len(rows)==len(ledger)>=1200,'incomplete authored suite')
        for index,(row,case) in enumerate(zip(rows,ledger)):
            require(all(row.get(k)==v for k,v in case.items()) and row.get('native') is True and row.get('source_sha256')==digest(source/'tests/control'/case['source']),'changed authored source')
            require(row.get('reference_verdict',{}).get('agreement') is True or row.get('reference_verdict',{}).get('eligible') is False,'untriaged reference disagreement')
            for level in ('-O0','-O2'):
                require(row.get(level+'_native_exit')==row.get(level+'_interpreter')==case['exit'],'authored native/IR discrepancy')
                elf(hashed(base,'authored/cinder-'+str(index)+level,row.get(level+'_object_sha256')),(2,))
        return len(rows)
    if gate=='differential-programs':
        command('differential')
        return verify_generated(base/'generated',source,20000,{'cinder':digest(compiler)})['programs']
    if gate=='ir-semantics':return verify_ir(base/'ir',source)
    if gate=='rewrite-validation':
        from rewrite_evidence import verify_rewrites
        return verify_rewrites(base/'rewrites',source,compiler)['defined']
    if gate=='abi':return verify_abi(base,source,compiler)
    if gate=='allocation':
        row=command('allocation');require(row['argv']==['tests/run_allocation.sh'],'wrong allocation producer')
        rows=[json.loads(line) for line in (base/'observations.jsonl').read_text().splitlines()]
        require(len(rows)>=10000 and '1000 rejected mutations passed' in row['stderr'],'missing allocation campaign or mutation controls')
        for index,row in enumerate(rows):
            require(row.get('seed')==index and row.get('checker') is True and row.get('blocks',0)>=4 and row.get('values',0)>8 and row.get('frame',0)>0,'invalid pressure graph outcome')
        require(sum(row.get('spills',0)>0 for row in rows)>0 and any(row.get('variadic') for row in rows),'vacuous allocation pressure')
        return len(rows)
    if gate=='object-output':return verify_objects(base/'objects',source,compiler,data['source_prefix'])
    if gate=='debugging':
        command('debug');sessions=0
        for folder in ('unwind','debug-lines','debug-types','debug-parameters','debug-scopes','debug-values'):
            report=verify_stage_report(base/folder,source,compiler)
            actual=sum(Path(r['argv'][0]).name=='gdb' and '-batch' in r['argv'] for r in report['commands'])
            require(actual==report.get('debugger_sessions',actual),'inflated debugger sessions');sessions+=actual
            for result in report['commands']:
                if Path(result['argv'][0]).name=='gdb' and '-batch' in result['argv']:
                    text=next((p.read_text(errors='replace') for p in (base/folder).glob('*.stdout') if digest(p)==result['stdout']),'')
                    require(text and ('Breakpoint' in text or '#0' in text),'empty debugger observation')
        require(sessions>=25,'missing actual debugger sessions');return sessions
    if gate=='failure-recovery':
        from release_campaigns import RECOVERY_KINDS
        rows=data.get('cases',[]);require(len(rows)>=1000,'incomplete failure recovery')
        for index,row in enumerate(rows):
            require(row.get('index')==index and row.get('kind')==RECOVERY_KINDS[index%len(RECOVERY_KINDS)],'missing recovery family')
            failed=command(str(index)+'-failure',1);recovered=command(str(index)+'-recovered')
            owned(failed,[]);owned(recovered,['-c'])
            require(failed['stderr'] and 'error' in failed['stderr'].lower(),'failure produced no diagnostic')
            elf(hashed(base,f'case-{index:04d}/result',row.get('object_sha256')),(1,))
        require(len(commands)==2*len(rows),'missing recovery command');return len(rows)
    if gate=='performance':
        from release_campaigns import BENCHMARKS
        measurements=data.get('measurements',[])
        require(len(measurements)==18,'incomplete native benchmark matrix')
        for name,text in BENCHMARKS.items():require((base/(name+'.c')).read_text()==text,'changed frozen benchmark source')
        for row in measurements:
            name,tool,level=row['workload'],row['tool'],row['level'];prefix=name+'-'+tool+level
            require(name in BENCHMARKS and tool in ('gcc','clang','cinder') and level in ('-O0','-O2'),'unknown benchmark cell')
            compile_row=command(prefix+'-compile');command(prefix+'-link')
            if tool=='cinder':owned(compile_row,[level,'-c'])
            samples=roles(prefix+'-sample')
            require(len(samples)==5 and all(r['exit']==row['expected'] and r['stdout']==r['stderr']=='' for r in samples),'benchmark output discrepancy')
            require(row['samples']==[r['elapsed'] for r in samples] and row['median_seconds']==statistics.median(row['samples']) and row['compile_seconds']==compile_row['elapsed'],'inflated benchmark measurements')
            elf(base/prefix,(2,));require((base/(prefix+'.o')).stat().st_size==row['object_bytes'],'wrong benchmark object size')
        return 1
    if gate=='documentation-product':
        owned(command('help'),['--help']);owned(command('version'),['--version']);command('driver');command('explorer')
        report=verify_stage_report(base/'explorer',source,compiler)
        require(len(report.get('observations',[]))==8 and report.get('rejected_publications')==2,'incomplete explorer profiles')
        for row in report['observations']:require(row.get('mappings',0)>0 and row.get('html_sha256') in report['artifacts'].values(),'missing source-to-code mapping')
        require('Cinder' in command('help')['stdout'] or '--explorer' in command('help')['stdout'],'empty CLI documentation')
        return 1
    if gate=='publication':
        remote=command('remote');require(remote['argv']==['git','ls-remote','origin','refs/heads/main'],'wrong publication query')
        require(remote['stdout'].strip()==data.get('revision','')+'\trefs/heads/main','published tip differs from tested source')
        rows=json.loads(command('hosted')['stdout']).get('workflow_runs',[])
        verified={row['name'] for row in rows if row.get('head_sha')==data['revision'] and row.get('conclusion')=='success' and row.get('status')=='completed'}
        require({'Cinder Linux validation','Cinder deterministic bootstrap'}<=verified,'tested tip lacks successful hosted validation and bootstrap')
        return 1
    raise EvidenceError('no raw campaign reader: '+gate)


def verify_stage_report(base,source,compiler):
    report=read(base,'report.json')
    require(report.get('native') is True and report.get('execution_profile')=='native' and report.get('compiler_sha256')==digest(compiler),'nonnative or changed stage report')
    for name,value in report.get('inputs',{}).items():
        path=Path(name)
        if path.is_absolute():
            parts=path.parts;start=next((i for i,p in enumerate(parts) if p in ('source','runtime','tests','tools','examples')),None)
            require(start is not None,'unrecognized stage source');path=Path(*parts[start:])
        require(digest(source/path)==value,'changed stage report input')
    artifacts=report.get('artifacts',{});require(artifacts,'empty stage artifacts')
    hashes=set()
    for name,value in artifacts.items():hashed(base,name,value);hashes.add(value)
    for row in report.get('commands',[]):
        require(type(row.get('exit')) is int and row.get('stdout') in hashes and row.get('stderr') in hashes,'missing raw stage command output')
        if row['exit']==0 and Path(row['argv'][0]).name=='gdb':require('-batch' in row['argv'],'nonbatch debugger session')
    return report


def verify_ir(base,source):
    data=read(base,'summary.json');count=data.get('executed')
    require(type(count) is int and count==data.get('requested') and count>=100000,'incomplete IR semantic campaign')
    for name,value in data.get('inputs',{}).items():require(digest(source/name)==value,'changed IR campaign source')
    for name,value in data.get('artifacts',{}).items():hashed(base,Path(name).name,value)
    families=collections.Counter();classes=collections.Counter();offset=0
    with (base/'cases.cir').open('rb') as archive,(base/'observations.jsonl').open() as records:
        for index in range(count):
            row=json.loads(next(records));family=0 if index<65536 else 1+index%9
            require(row.get('case')==index and row.get('family')==family and row.get('archive_offset')==offset and row.get('roundtrip') is True and row.get('optimized_match') is True,'wrong IR case record')
            size=row['archive_bytes'];text=archive.read(size)
            require(len(text)==size and text.startswith(b'cinder-ir 6 lp64-le sysv-x86-64\n') and text.endswith(b'end-module\n'),'truncated typed IR case')
            offset+=size;families[family]+=1;classes[row['classification']]+=1
        require(not archive.read(1) and not records.read(),'extra or missing IR cases')
    require(data.get('families')=={str(k):v for k,v in families.items()} and data.get('classifications')=={str(k):v for k,v in classes.items()},'inflated IR family totals')
    require(set(families)==set(range(10)) and {0,1,2,3,4,5,7}<=set(classes),'missing defined or undefined IR classifications')
    return count


def verify_abi(base,source,compiler):
    sys.path.insert(0,str(source/'tests'))
    import test_abi_callbacks as scalar
    import test_abi_aggregates as aggregate
    count=0
    for folder,module,variadic in [('abi-callbacks',scalar,False),('abi-aggregates',aggregate,False),('abi-variadic',aggregate,True)]:
        root=base/folder;summary=read(root,'summary.json');rows=summary.get('observations',[])
        require(summary.get('native') is True and summary.get('compiler_sha256')==digest(compiler) and len(rows)==summary.get('cases') and len(rows)>=512,'incomplete native ABI signatures')
        require(summary.get('source_inputs')==source_inputs(source),'changed ABI source')
        for index,row in enumerate(rows):
            require(row.get('seed')==index,'missing ABI signature')
            work=root/Path(row['report']).parent
            record=json.loads(hashed(root,row['report'],row['report_sha256']).read_text())
            spec=module.specification(index)
            require(record.get('specification')==spec and record.get('native') is True,'changed ABI specification')
            generated=module.sources(spec)
            if variadic:
                from test_abi_variadic import sources as variadic_sources
                generated=variadic_sources(spec)
            for name,text in generated.items():require((work/name).read_text()==text,'changed generated ABI source')
            for name,artifact in record.get('artifacts',{}).items():
                path=hashed(work,name,artifact['sha256']);require(path.stat().st_size==artifact['bytes'],'changed ABI artifact extent')
            observed=collections.Counter()
            for command in record.get('commands',[]):
                require(command.get('exit')==0 and 'timeout' not in command,'failed ABI command')
                observed[command['role']]+=1
                if command['role'] in ('reference','native'):require(command['stdout']==(spec['expected']+'\n')*(4 if variadic else 2) and not command['stderr'],'ABI returned wrong scalar members')
                elif '-c' in command['argv']:
                    require(Path(command['argv'][0]).name=='cindercc' and '-fverify-each' in command['argv'],'host fallback in ABI object')
            references=8 if module is aggregate else 4
            native=16 if module is aggregate else 8
            require(observed['native']==native and observed['reference']==references and row.get('native_executions')==native and row.get('reference_executions')==references,'missing bidirectional ABI comparisons')
        count+=len(rows)
    return count


def verify_objects(base,source,compiler,source_prefix):
    sys.path.insert(0,str(source/'tests'))
    from test_objects import inspect_fixture
    paths=sorted(base.glob('case-*/observations.json'));require(len(paths)>=1000,'incomplete object campaign')
    for index,path in enumerate(paths):
        row=json.loads(path.read_text());work=path.parent
        require(row.get('seed')==index and row.get('native') is True and row.get('compiler_sha256')==digest(compiler),'wrong native object probe')
        require(row.get('source_sha256')==[digest(work/'a.c'),digest(work/'b.c')],'changed object sources')
        require(len(row.get('objects',[]))==8,'missing original or independently assembled object')
        for obj in row['objects']:
            suffix='-asm.o' if obj['source'].endswith('-assembly') else '.o'
            name=Path(obj['source'].removesuffix('-assembly')).stem+obj['level']+suffix
            actual=inspect_fixture(work/name,strict=suffix=='.o')
            require(all(obj.get(k)==v for k,v in actual.items()),'object reader disagrees with recorded structure')
        commands=row.get('execution',[])
        require(len(commands)==22 and all(r.get('exit')==0 for r in commands),'incomplete object assembly/link/execution comparisons')
        require(sum(len(r['command'])==1 for r in commands)==5,'missing original, assembly or reference native execution')
        for name in ('object-O0','object-O2','assembly-O0','assembly-O2','reference'):elf(work/name,(2,3))
    return len(paths)
