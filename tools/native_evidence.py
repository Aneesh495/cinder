"""Read retained native bootstrap/generated artifacts without running code."""
import collections
import json
import pathlib
import re
import shlex
import struct

from evidence_integrity import EvidenceError, artifact_path, digest
from defined_programs import program


def require(condition, message):
    if not condition:
        raise EvidenceError(message)


def read(base, name):
    path = artifact_path(base, name)
    require(path.is_file(), 'missing report: '+name)
    try:
        return json.loads(path.read_text())
    except (ValueError, UnicodeError) as error:
        raise EvidenceError('invalid report: '+name) from error


def hashed(base, name, expected):
    path = artifact_path(base, name)
    require(path.is_file() and re.fullmatch('[0-9a-f]{64}', str(expected)) and digest(path)==expected, 'missing or changed artifact: '+name)
    return path


def elf(path, kinds):
    with path.open('rb') as stream:
        header=stream.read(64)
    require(len(header)==64 and header[:7]==b'\x7fELF\x02\x01\x01' and struct.unpack_from('<HH',header,16)[0] in kinds and struct.unpack_from('<H',header,18)[0]==62, 'artifact is not target ELF64 x86-64: '+str(path))


def profile(data, key):
    require(data.get(key)=='native' and data.get('machine', {}).get('system')=='Linux' and data['machine'].get('machine')=='x86_64', 'evidence requires native Linux x86-64')
    require(data.get('environment')==dict(SOURCE_DATE_EPOCH='0', LC_ALL='C', TZ='UTC'), 'undeclared deterministic environment')
    require(data.get('stack_profile', {}).get('soft')==64*1024*1024, 'missing declared stack profile')


def verify_generated(base, source_root, minimum, compilers):
    """Validate every eligible program, reference execution and original object."""
    base, source_root = pathlib.Path(base).resolve(), pathlib.Path(source_root).resolve()
    summary=read(base, 'summary.json')
    require(summary.get('schema')==2 and summary.get('status')=='pass', 'incomplete native program report')
    profile(summary, 'profile')
    count=summary.get('requested')
    require(type(count) is int and count>=minimum and summary.get('eligible_programs')==count, 'insufficient eligible native programs')
    configuration=read(base, 'configuration.json')
    require(all(summary.get(k)==v for k,v in configuration.items()), 'campaign configuration changed')
    tools=summary.get('tools', {})
    require(set(tools)=={'gcc', 'clang', *compilers}, 'wrong native compiler set')
    for name, record in tools.items():
        require(isinstance(record.get('version'), str) and record['version'], 'missing tool version')
        elf(hashed(base, record.get('artifact', ''), record.get('sha256')), (2,3))
        if name in compilers:
            require(record['sha256']==compilers[name], 'original compiler binary mismatch')
    inputs=summary.get('inputs', {})
    require(len(inputs)==2 and {pathlib.PurePosixPath(name).name for name in inputs}=={'defined_programs.py','run_native_programs.py'}, 'wrong generator input inventory')
    for name, expected in inputs.items():
        basename=pathlib.PurePosixPath(name).name
        require(basename in ('defined_programs.py','run_native_programs.py'), 'unexpected generator input')
        require(digest(source_root/'tools'/basename)==expected, 'changed generator source')
    reports=summary.get('reports', {})
    require(set(reports)=={f'case-{i:05d}/observations.json' for i in range(count)}, 'missing, duplicate or extra eligible program reports')
    native_count, interpreted_count=0,0
    source_programs=set(); families=collections.Counter()
    for index in range(count):
        name=f'case-{index:05d}/observations.json';path=hashed(base,name,reports[name]);case_root=path.parent;row=read(case_root,'observations.json')
        spec=program(summary['seed'],index)
        require(row.get('index')==index and row.get('eligible') is True and row.get('expected')==spec['expected'] and row.get('family')==spec['family'], 'wrong generated source oracle')
        require(set(row.get('sources', {}))==set(spec['units']), 'wrong program unit inventory')
        for name, text in spec['units'].items():
            source=hashed(case_root,name,row['sources'][name])
            require(source.read_text()==text, 'changed bounded generated source')
        require(hashed(case_root,'interpreter.c',row.get('interpreter_source_sha256')).read_text()==spec['interpreter'], 'wrong interpreter source view')
        identity=tuple(sorted(row['sources'].items()))
        require(identity not in source_programs, 'duplicate eligible generated source')
        source_programs.add(identity);families[row['family']]+=1
        expected_native={(name,level) for name in tools for level in ('-O0','-O2')}
        observed_native=set()
        for result in row.get('native', []):
            key=(result.get('compiler'),result.get('level'))
            require(key in expected_native and key not in observed_native and result.get('exit')==spec['expected'] and result.get('stdout')==result.get('stderr')=='', 'missing or failed native execution')
            observed_native.add(key)
            filename=key[0]+key[1]+('.native' if key[0] in compilers else '')
            elf(hashed(case_root,filename,result.get('executable_sha256')), (2,) if key[0] in compilers else (2,3))
        require(observed_native==expected_native, 'incomplete reference or original native levels')
        expected_interpreted={(name,level) for name in compilers for level in ('-O0','-O2')}
        interpreted=row.get('interpreted', [])
        require(len(interpreted)==len(expected_interpreted) and {(r.get('compiler'),r.get('level')) for r in interpreted}==expected_interpreted and all(r.get('integer')==spec['expected'] for r in interpreted), 'incomplete independent interpreter comparisons')
        expected_objects={name+level+'-'+pathlib.PurePosixPath(unit).stem+'.o' for name in compilers for level in ('-O0','-O2') for unit in spec['units']}
        require(set(row.get('objects', {}))==expected_objects, 'missing or extra original unit objects')
        for name,expected in row['objects'].items():
            elf(hashed(case_root,name,expected), (1,))
        if set(compilers)=={'stage2','stage3'}:
            for unit in spec['units']:
                for level in ('-O0','-O2'):
                    suffix=level+'-'+pathlib.PurePosixPath(unit).stem+'.o'
                    require(row['objects']['stage2'+suffix]==row['objects']['stage3'+suffix], 'stage generated objects differ')
        commands=row.get('commands', [])
        expected_roles=collections.Counter({'reference-compile':4,'reference-native':4,'owned-compile':len(compilers)*2*len(spec['units']),'owned-link':len(compilers)*2,'owned-native':len(compilers)*2,'owned-interpreter':len(compilers)*2})
        require(collections.Counter(c.get('role') for c in commands)==expected_roles, 'missing native command roles')
        for command in commands:
            role=command['role'];exit_code=spec['expected'] if role.endswith('-native') else 0
            require(command.get('exit')==exit_code and 'timeout' not in command and isinstance(command.get('argv'),list) and command['argv'], 'failed or truncated native command')
            if role=='owned-compile':
                require(command['argv'][0] in {tools[n]['path'] for n in compilers} and '-c' in command['argv'] and '-fverify-each' in command['argv'], 'host fallback or unverified original compilation')
            if role.endswith('-native'):
                require(command.get('stdout')==command.get('stderr')=='', 'unexpected native output')
            if role=='owned-interpreter':
                require(command.get('stdout')==f"interpret main => {spec['expected']}\n" and command.get('stderr')=='', 'interpreter command disagrees with result')
        native_count+=len(observed_native);interpreted_count+=len(interpreted)
    require(summary.get('native_executions')==native_count and summary.get('interpreter_executions')==interpreted_count and summary.get('families')==dict(families), 'inflated native campaign totals')
    return dict(programs=count,native=native_count,interpreted=interpreted_count,summary_sha256=digest(base/'summary.json'))


AUTHORED_COMMANDS = frozenset((
    'tests/run_smoke.sh','tests/run_frontend.sh','tests/run_globals.sh','tests/run_multi.sh','tests/run_preprocessor.sh','tests/run_ir.sh',
    'tests/test_preprocessor.py','tests/test_control.py','tests/test_constraints.py','tests/test_undefined.py','tests/test_memory.py','tests/test_literals.py','tests/test_linkage.py','tests/test_static_addresses.py','tests/test_numeric.py','tests/test_ir_text.py',
    'tests/test_alignment_contracts.py','tests/test_noreturn_contracts.py','tests/test_block_storage_contracts.py','tests/test_goto_contracts.py','tests/test_switch_contracts.py','tests/test_register_contracts.py','tests/test_offset_contracts.py','tests/test_runtime_headers.py','tests/test_flexible_contracts.py','tests/test_anonymous_contracts.py', 'tests/test_bitfield_contracts.py',
    'tests/run_float.sh','tests/run_varargs.sh','tests/run_apps.sh','tests/run_abi.sh','tests/test_abi_boundary.py','tests/run_output.py',
))
AUTHORED_GROUPS = frozenset(('initializers','aggregates','aggregate_abi','variadic','compound_literals','static_assertions','generic','alignment','noreturn','block_storage','goto','switch','register','offset','flexible','anonymous','bitfields'))


def bootstrap_inputs(root):
    root=pathlib.Path(root).resolve()
    paths=sorted(p for name in ('source','runtime','tests','tools','examples') for p in (root/name).rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix!='.pyc')+[root/'CMakeLists.txt',root/'Makefile']
    require(paths and all(not p.is_symlink() for p in paths), 'empty or indirect bootstrap source inventory')
    return {str(p.relative_to(root)):digest(p) for p in paths}


def verify_bootstrap(base, source_root, seed_compiler=None):
    """Require original stage compilation, complete suites, and native subset."""
    base,source_root=pathlib.Path(base).resolve(),pathlib.Path(source_root).resolve()
    manifest=read(base,'manifest.json')
    require(manifest.get('schema')==2 and manifest.get('status')=='pass' and 'active_command' not in manifest, 'incomplete bootstrap')
    profile(manifest,'execution_profile')
    require(manifest.get('inputs')==bootstrap_inputs(source_root), 'bootstrap source or configuration bytes changed')
    for name,expected in manifest['inputs'].items():
        hashed(base,'input-snapshot/'+name,expected)
    modules={'source/'+p.name for p in (source_root/'source').glob('*.c')}
    require(set(manifest.get('modules', []))==modules and len(manifest['modules'])==len(modules), 'missing or duplicate actual compiler module')
    stages=manifest.get('stages', {})
    require(set(stages)=={'stage2','stage3'} and stages['stage2']==stages['stage3'], 'stage objects or executable hashes differ')
    object_names={pathlib.PurePosixPath(name).stem+'.o' for name in modules}
    for stage in stages:
        require(set(stages[stage].get('objects', {}))==object_names and set(stages[stage].get('executables', {}))=={'cindercc','cinderir'}, 'missing stage objects or entry point')
        for category in ('objects','executables'):
            for name,expected in stages[stage][category].items():
                elf(hashed(base,stage+'/'+name,expected), (1,) if category=='objects' else (2,))
    for group, directory in (('seed_compiler','seed-compiler'),('stage1','stage1')):
        require(set(manifest.get(group, {}))=={'cindercc','cinderir'}, 'missing actual compiler pair')
        for name,expected in manifest[group].items():
            hashed(base,directory+'/'+name,expected)
    if seed_compiler is not None:
        require(digest(pathlib.Path(seed_compiler))==manifest['seed_compiler']['cindercc'], 'seed compiler does not match acceptance binary')
    require(set(manifest.get('stage1_configuration', {}))=={'CMakeCache.txt','compile_commands.json'}, 'missing stage 1 build configuration')
    for name,expected in manifest['stage1_configuration'].items():
        hashed(base,'stage1/'+name,expected)
    dependencies=manifest.get('dependencies', {})
    require({'crt1.o','crti.o','crtbegin.o','crtend.o','crtn.o','libc.so','cc1','as','ld','driver'}<=set(dependencies), 'missing host/startup/link dependencies')
    for record in dependencies.values():
        hashed(base,record.get('artifact',''),record.get('sha256'))
    require(manifest.get('linker', {}).get('version') and manifest['linker'].get('sha256'), 'missing link driver identity')
    require(manifest['linker']['sha256']==dependencies['driver']['sha256'] and manifest['linker']['path']==dependencies['driver']['path'], 'link driver snapshot disagrees')
    commands=manifest.get('commands', [])
    require(commands and all(c.get('exit')==0 and 'timeout' not in c and isinstance(c.get('argv'),list) and c['argv'] for c in commands), 'failed or incomplete bootstrap command')
    roles=collections.Counter(c.get('role') for c in commands)
    expected_roles={'stage1-configure':1,'stage1-host-build':1,'stage-native-subset':1}
    old_root=pathlib.PurePosixPath(manifest.get('source_prefix','')).parent
    require(str(old_root).startswith('/') and old_root.name, 'missing bootstrap source prefix')
    host_compilations=read(base,'stage1/compile_commands.json')
    observed_host=set()
    for command in host_compilations:
        file=command.get('file','')
        if not file.startswith(manifest['source_prefix']+'/source/'):
            continue
        name=str(pathlib.PurePosixPath(file).relative_to(pathlib.PurePosixPath(manifest['source_prefix'])))
        argv=shlex.split(command.get('command',''))
        require(name in modules and name not in observed_host and argv and argv[0]==manifest['linker']['path'] and '-std=c17' in argv and '-Werror' in argv and '-O3' in argv and '-c' in argv and file in argv, 'stage 1 is missing a strict fresh host compilation')
        observed_host.add(name)
    require(observed_host==modules, 'stage 1 actual compiler module inventory is incomplete')
    for stage,previous in [('stage2','stage1'),('stage3','stage2')]:
        expected_roles.update({stage+'-compile':len(modules),stage+'-link':2,stage+'-authored':len(AUTHORED_COMMANDS),stage+'-authored-group':len(AUTHORED_GROUPS),stage+'-allocated':1,stage+'-flexible-abi':1,stage+'-anonymous-abi':1,stage+'-bitfield-abi':1})
        compilations=[c for c in commands if c['role']==stage+'-compile']
        observed=set()
        for command in compilations:
            argv=command['argv'];source_names=[str(pathlib.PurePosixPath(a).relative_to(pathlib.PurePosixPath(manifest['source_prefix']))) for a in argv if a.startswith(manifest['source_prefix']+'/source/') and a.endswith('.c')]
            require(len(source_names)==1 and source_names[0] in modules and source_names[0] not in observed, 'missing or duplicate compiled source module')
            observed.add(source_names[0]);output=str(old_root/stage/(pathlib.PurePosixPath(source_names[0]).stem+'.o'))
            require(argv[0]==str(old_root/previous/'cindercc') and '-O0' in argv and '-fverify-each' in argv and '-c' in argv and argv[-2:]==['-o',output], 'module used a fallback compiler or changed stage options')
        require(observed==modules, 'actual stage module was not compiled')
        for command in (c for c in commands if c['role']==stage+'-link'):
            argv=command['argv'];name=pathlib.PurePosixPath(argv[-1]).name
            require(name in ('cindercc','cinderir') and argv[0]==manifest['linker']['path'] and '-no-pie' in argv, 'wrong stage linking contract')
            exclude='ir_main.o' if name=='cindercc' else 'main.o'
            require({a for a in argv if a.endswith('.o')}=={str(old_root/stage/n) for n in object_names if n!=exclude}, 'stage link contains missing, host, or unrelated objects')
        authored=[c for c in commands if c['role']==stage+'-authored']
        require({a for c in authored for a in c['argv'] if a in AUTHORED_COMMANDS}==AUTHORED_COMMANDS and all(c['argv'][-1]==str(old_root/stage/'cindercc') for c in authored), 'missing full authored stage command')
        groups=[c for c in commands if c['role']==stage+'-authored-group']
        require({c['argv'][-1] for c in groups}==AUTHORED_GROUPS and all(c['argv'][-2]==str(old_root/stage/'cindercc') for c in groups), 'missing full authored initializer group')
        verify_stage_control(base,stage,source_root,stages[stage]['executables']['cindercc'])
    require(roles==collections.Counter(expected_roles), 'missing or extra bootstrap command roles')
    result=verify_generated(base/'generated',source_root,1000,{stage:stages[stage]['executables']['cindercc'] for stage in stages})
    require(result['summary_sha256']==manifest.get('generated_summary_sha256'), 'changed generated bootstrap subset')
    return dict(modules=len(modules),**result,manifest_sha256=digest(base/'manifest.json'))


def verify_stage_control(base,stage,source_root,compiler_hash):
    """Bind both full authored execution levels to every current source case."""
    workspace=base/(stage+'-authored');name='.agent-local/control/'+compiler_hash
    rows=read(workspace,name+'/observations.json')
    ledger=read(source_root,'tests/control/cases.json')
    require(len(rows)==len(ledger), 'missing authored stage cases')
    for index,(row,case) in enumerate(zip(rows,ledger)):
        source=source_root/'tests/control'/case['source']
        require(row.get('source')==case['source'] and row.get('exit')==case['exit'] and row.get('source_sha256')==digest(source) and row.get('native') is True, 'wrong or nonnative authored source observation')
        for level in ('-O0','-O2'):
            require(row.get(level+'_native_exit')==row.get(level+'_interpreter')==case['exit'], 'authored stage native/interpreter mismatch')
            # The driver's object-hash field names the linked executable when
            # native linking is enabled. Hash the actual retained executable.
            hashed(workspace,name+'/cinder-'+str(index)+level,row.get(level+'_object_sha256'))
