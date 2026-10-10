#!/usr/bin/env python3
"""Build every actual compiler module through Cinder and test both stages."""
import argparse
import hashlib
import json
import os
import pathlib
import platform
import re
import resource
import shutil
import subprocess
import sys
import time


def digest(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('compiler', type=pathlib.Path)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--profile', choices=('native', 'emulated'), default='native')
    args = parser.parse_args()
    assert platform.system() == 'Linux' and platform.machine() == 'x86_64', 'self-host execution requires the declared Linux x86-64 profile'
    soft, hard = resource.getrlimit(resource.RLIMIT_STACK)
    stack_bytes = 64 * 1024 * 1024
    assert hard == resource.RLIM_INFINITY or hard >= stack_bytes, 'bootstrap requires the declared 64 MiB process stack'
    resource.setrlimit(resource.RLIMIT_STACK, (stack_bytes, hard))
    compiler = args.compiler.resolve()
    irtool = compiler.parent / 'cinderir'
    assert compiler.exists() and irtool.exists()
    repo = pathlib.Path.cwd().resolve()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    inputs = sorted(p for directory in ('source', 'runtime', 'tests', 'tools', 'examples') for p in (repo / directory).rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix != '.pyc') + [repo/'CMakeLists.txt', repo/'Makefile']
    hashes = {str(p.relative_to(repo)): digest(p) for p in inputs}
    snapshot = root / 'input-snapshot'
    for path in inputs:
        output = snapshot / path.relative_to(repo)
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, output)
    seed = root / 'seed-compiler'
    seed.mkdir()
    shutil.copy2(compiler, seed/'cindercc')
    shutil.copy2(irtool, seed/'cinderir')
    stage1 = root / 'stage1'
    modules = re.search(r'set\(CINDER_SOURCES\s*(.*?)\n\)', (snapshot/'CMakeLists.txt').read_text(), re.S).group(1).split() + ['source/ir_main.c']
    assert set(modules) == {str(p.relative_to(snapshot)) for p in (snapshot/'source').glob('*.c')}, 'CMake module inventory excludes an actual production source'
    env = dict(os.environ, SOURCE_DATE_EPOCH='0', LC_ALL='C', TZ='UTC')
    linker = pathlib.Path(shutil.which('gcc')).resolve()
    dependencies = {}
    for name in ('crt1.o', 'crti.o', 'crtbegin.o', 'crtend.o', 'crtn.o', 'libc.so'):
        result = subprocess.run([str(linker), '-print-file-name='+name], check=True, capture_output=True, text=True)
        path = pathlib.Path(result.stdout.strip()).resolve()
        assert path.exists(), (name, path)
        dependencies[name] = dict(path=str(path), sha256=digest(path))
    for name in ('cc1', 'as', 'ld'):
        result = subprocess.run([str(linker), '-print-prog-name='+name], check=True, capture_output=True, text=True)
        command = result.stdout.strip()
        path = pathlib.Path(command if '/' in command else shutil.which(command)).resolve()
        assert path.exists(), (name, path)
        dependencies[name] = dict(path=str(path), sha256=digest(path))
    linked = subprocess.run(['ldd', str(compiler)], check=True, capture_output=True, text=True)
    for name in re.findall(r'(/[^\s()]+)', linked.stdout):
        path = pathlib.Path(name).resolve()
        if path.is_file():
            dependencies[str(path)] = dict(path=str(path), sha256=digest(path))
    dependencies['driver'] = dict(path=str(linker), sha256=digest(linker))
    dependency_snapshots = root/'dependency-snapshots'
    dependency_snapshots.mkdir()
    for index, (name, record) in enumerate(dependencies.items()):
        path = dependency_snapshots/str(index)
        shutil.copy2(record['path'], path)
        record['artifact'] = str(path.relative_to(root))
        assert digest(path) == record['sha256'], 'dependency changed during capture'
    build_configuration = {}
    for name in ('CMakeCache.txt', 'compile_commands.json', 'build.ninja'):
        path = compiler.parent/name
        if path.exists():
            shutil.copy2(path, seed/name)
            build_configuration[name] = digest(path)
    manifest = dict(schema=2, status='running', inputs=hashes, modules=modules, seed_compiler={name: digest(seed/name) for name in ('cindercc', 'cinderir')}, seed_configuration=build_configuration, environment={k:env[k] for k in ('SOURCE_DATE_EPOCH', 'LC_ALL', 'TZ')}, source_prefix=str(snapshot), execution_profile=args.profile, stack_profile=dict(before_soft=soft, soft=stack_bytes, hard=hard), machine=platform.uname()._asdict(), linker=dict(path=str(linker), sha256=digest(linker), version=subprocess.run([str(linker), '--version'], check=True, capture_output=True, text=True).stdout), dependencies=dependencies, commands=[], stages={})

    def save():
        (root/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')

    def invoke(argv, role, cwd=snapshot, timeout=300):
        command = [str(a) for a in argv]
        start = time.monotonic()
        record = dict(role=role, argv=command, cwd=str(cwd))
        manifest['active_command'] = record
        save()
        print('Running', role, command[0], flush=True)
        try:
            result = subprocess.run(command, cwd=cwd, env=env, capture_output=True, text=True, timeout=timeout)
            record.update(exit=result.returncode, stdout=result.stdout, stderr=result.stderr, elapsed=time.monotonic()-start)
        except subprocess.TimeoutExpired as error:
            record.update(timeout=error.timeout, elapsed=time.monotonic()-start)
            manifest['commands'].append(record)
            save()
            raise
        manifest['commands'].append(record)
        manifest.pop('active_command', None)
        save()
        assert result.returncode == 0, (role, command, result)
        return result

    save()
    try:
        # The host compiler builds stage 1 only. Every stage 2/3 module below
        # is compiled by Cinder and is linked solely from its original objects.
        invoke(['cmake', '-S', snapshot, '-B', stage1, '-DCMAKE_C_COMPILER='+str(linker), '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'], 'stage1-configure', cwd=root)
        invoke(['cmake', '--build', stage1, '--parallel', '4'], 'stage1-host-build', cwd=root, timeout=1800)
        manifest['stage1'] = {name:digest(stage1/name) for name in ('cindercc', 'cinderir')}
        manifest['stage1_configuration'] = {name:digest(stage1/name) for name in ('CMakeCache.txt', 'compile_commands.json')}
        save()
        previous = stage1
        for stage in ('stage2', 'stage3'):
            directory = root/stage
            directory.mkdir()
            objects = {}
            for module in modules:
                output = directory/(pathlib.Path(module).stem+'.o')
                command = [previous/'cindercc', '-O0', '-fverify-each', '-c', '-I'+str(snapshot/'source'), '-I'+str(snapshot/'runtime/include'), '-DCINDER_VERSION="0.1.0"', '-DCINDER_RUNTIME_INCLUDE="'+str(snapshot/'runtime/include')+'"', snapshot/module, '-o', output]
                invoke(command, stage+'-compile')
                assert output.read_bytes().startswith(b'\x7fELF'), (stage, module)
                objects[output.name] = digest(output)
                print(stage, module, 'compiled', flush=True)
            executables = {}
            for name, exclude in (('cindercc', 'ir_main.o'), ('cinderir', 'main.o')):
                output = directory/name
                invoke([linker, '-no-pie', *[directory/p for p in sorted(objects) if p != exclude], '-o', output], stage+'-link')
                executables[name] = digest(output)
            manifest['stages'][stage] = dict(objects=objects, executables=executables)
            save()
            previous = directory
        assert manifest['stages']['stage2'] == manifest['stages']['stage3'], 'stage 2/3 object or executable mismatch; raw artifacts retained'
        # Each stage receives a separate authored-suite workspace and evidence
        # tree even when the compiler bytes are identical.
        commands = [
            ['tests/run_smoke.sh'], ['tests/run_frontend.sh'], ['tests/run_globals.sh'], ['tests/run_multi.sh'], ['tests/run_preprocessor.sh'], ['tests/run_ir.sh'],
            ['python3', 'tests/test_preprocessor.py'], ['python3', 'tests/test_control.py'], ['python3', 'tests/test_constraints.py'], ['python3', 'tests/test_undefined.py'], ['python3', 'tests/test_memory.py'], ['python3', 'tests/test_literals.py'], ['python3', 'tests/test_linkage.py'], ['python3', 'tests/test_static_addresses.py'], ['python3', 'tests/test_numeric.py'], ['python3', 'tests/test_ir_text.py'],
            ['python3', 'tests/test_alignment_contracts.py'], ['python3', 'tests/test_noreturn_contracts.py'], ['python3', 'tests/test_block_storage_contracts.py'], ['python3', 'tests/test_goto_contracts.py'], ['python3', 'tests/test_switch_contracts.py'], ['python3', 'tests/test_register_contracts.py'], ['python3', 'tests/test_offset_contracts.py'], ['python3', 'tests/test_runtime_headers.py'], ['python3', 'tests/test_flexible_contracts.py'], ['python3', 'tests/test_anonymous_contracts.py'], ['python3', 'tests/test_bitfield_contracts.py'], ['python3', 'tests/test_qualifier_contracts.py'], ['python3', 'tests/test_cfg_optimization.py'], ['python3', 'tests/test_sparse_optimization.py'], ['python3', 'tests/test_value_numbering.py'], ['python3', 'tests/test_dead_code.py'], ['python3', 'tests/test_strength_reduction.py'], ['python3', 'tests/test_copy_cleanup.py'], ['python3', 'tests/test_local_memory.py'], ['python3', 'tests/test_loop_motion.py'], ['python3', 'tests/test_pass_pipeline.py'], ['python3', 'tests/test_promotion_guards.py'], ['python3', 'tests/test_pass_evidence.py'], ['python3', 'tests/test_pointer_word_contracts.py'],
            ['tests/run_float.sh'], ['tests/run_varargs.sh'], ['tests/run_apps.sh'], ['tests/run_abi.sh'], ['python3', 'tests/test_abi_boundary.py'], ['python3', 'tests/run_output.py'],
        ]
        for stage in ('stage2', 'stage3'):
            workspace = root/(stage+'-authored')
            shutil.copytree(snapshot, workspace)
            cc = root/stage/'cindercc'
            for command in commands:
                invoke([*command, cc], stage+'-authored', cwd=workspace, timeout=1800)
                print(stage, command[-1], 'passed', flush=True)
            for group in ('initializers', 'aggregates', 'aggregate_abi', 'variadic', 'compound_literals', 'static_assertions', 'generic', 'alignment', 'noreturn', 'block_storage', 'goto', 'switch', 'register', 'offset', 'flexible', 'anonymous', 'bitfields', 'qualifiers', 'pointer_words'):
                invoke(['python3', 'tests/test_initializers.py', cc, group], stage+'-authored-group', cwd=workspace, timeout=1800)
                print(stage, group, 'passed', flush=True)
            invoke(['python3', 'tests/test_runtime_headers.py', cc, 'flexible_allocated'], stage+'-allocated', cwd=workspace, timeout=300)
            invoke(['python3', 'tests/test_block_storage_contracts.py', cc, 'flexible'], stage+'-flexible-abi', cwd=workspace, timeout=300)
            invoke(['python3', 'tests/test_block_storage_contracts.py', cc, 'anonymous'], stage+'-anonymous-abi', cwd=workspace, timeout=300)
            invoke(['python3', 'tests/test_block_storage_contracts.py', cc, 'bitfields'], stage+'-bitfield-abi', cwd=workspace, timeout=300)
        invoke([sys.executable, snapshot/'tools/run_native_programs.py', '--compiler', 'stage2='+str(root/'stage2/cindercc'), '--compiler', 'stage3='+str(root/'stage3/cindercc'), '--count', '1000', '--profile', args.profile, '--output', root/'generated'], 'stage-native-subset', timeout=7200)
        generated = json.loads((root/'generated/summary.json').read_text())
        assert generated['eligible_programs'] == 1000 and generated['native_executions'] == 8000 and generated['interpreter_executions'] == 4000
        assert hashes == {str(p.relative_to(repo)): digest(p) for p in inputs}, 'source changed during bootstrap'
        assert manifest['stage1'] == {name:digest(stage1/name) for name in ('cindercc', 'cinderir')}, 'stage 1 changed during bootstrap'
        for stage, outputs in manifest['stages'].items():
            assert all(digest(root/stage/name)==value for category in ('objects', 'executables') for name, value in outputs[category].items()), 'stage artifact changed during execution'
        assert digest(linker) == manifest['linker']['sha256'] and all(digest(v['path'])==v['sha256'] for v in dependencies.values()), 'link dependencies changed during bootstrap'
        manifest.update(status='pass', generated_summary_sha256=digest(root/'generated/summary.json'))
        save()
        from native_evidence import verify_bootstrap
        verified = verify_bootstrap(root, repo, compiler)
        assert verified['programs'] == 1000 and verified['native'] == 8000 and verified['interpreted'] == 4000
        print('Read-only bootstrap evidence verification passed.', flush=True)
    except BaseException as error:
        manifest.update(status='fail', error=str(error))
        save()
        raise
    print('Self-hosting:', len(modules), 'actual modules per stage; matching objects/executables; both authored suites and 1000 generated native programs passed.', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
