#!/usr/bin/env python3
"""Execute bounded generated programs through references and original objects."""
import argparse
import concurrent.futures
import hashlib
import json
import os
import pathlib
import platform
import re
import resource
import shutil
import subprocess
import time

from defined_programs import program


def digest(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', action='append', required=True, help='stage-name=/absolute/compiler')
    parser.add_argument('--count', type=int, default=1000)
    parser.add_argument('--seed', type=int, default=0xC1D3)
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--profile', choices=('native', 'emulated'), default='native')
    parser.add_argument('--output', type=pathlib.Path, required=True)
    args = parser.parse_args()
    assert platform.system() == 'Linux' and platform.machine() == 'x86_64', 'generated native execution requires Linux x86-64'
    soft, hard = resource.getrlimit(resource.RLIMIT_STACK)
    stack_bytes = 64 * 1024 * 1024
    assert hard == resource.RLIM_INFINITY or hard >= stack_bytes, 'campaign requires the declared 64 MiB process stack'
    resource.setrlimit(resource.RLIMIT_STACK, (stack_bytes, hard))
    assert args.count > 0 and args.jobs > 0
    compilers = {}
    for value in args.compiler:
        name, path = value.split('=', 1)
        assert re.fullmatch(r'[a-z0-9_-]+', name) and name not in compilers and name not in ('gcc', 'clang')
        compilers[name] = pathlib.Path(path).resolve()
    references = {name: pathlib.Path(shutil.which(name)).resolve() for name in ('gcc', 'clang')}
    tools = {name: dict(path=str(path), sha256=digest(path), version=subprocess.run([str(path), '--version'], check=True, capture_output=True, text=True).stdout) for name, path in {**references, **compilers}.items()}
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    snapshots = root / 'tool-snapshots'
    snapshots.mkdir()
    for name, record in tools.items():
        shutil.copy2(record['path'], snapshots/name)
        record['artifact'] = 'tool-snapshots/'+name
        assert digest(snapshots/name) == record['sha256'], 'tool snapshot changed during capture'
    env = dict(os.environ, SOURCE_DATE_EPOCH='0', LC_ALL='C', TZ='UTC')
    inputs = {str(pathlib.Path(__file__).resolve()): digest(__file__), str(pathlib.Path(__file__).with_name('defined_programs.py').resolve()): digest(pathlib.Path(__file__).with_name('defined_programs.py'))}
    configuration = dict(schema=2, seed=args.seed, requested=args.count, jobs=args.jobs, profile=args.profile, stack_profile=dict(before_soft=soft, soft=stack_bytes, hard=hard), machine=platform.uname()._asdict(), environment={k: env[k] for k in ('SOURCE_DATE_EPOCH', 'LC_ALL', 'TZ')}, tools=tools, inputs=inputs)
    (root / 'configuration.json').write_text(json.dumps(configuration, indent=2) + '\n')

    def probe(index):
        case = program(args.seed, index)
        directory = root / f'case-{index:05d}'
        directory.mkdir()
        sources = []
        for name, text in case['units'].items():
            path = directory / name
            path.write_text(text)
            sources.append(path)
        ir_source = directory / 'interpreter.c'
        ir_source.write_text(case['interpreter'])
        row = dict(index=index, family=case['family'], expected=case['expected'], sources={p.name: digest(p) for p in sources}, interpreter_source_sha256=digest(ir_source), commands=[], objects={}, native=[], interpreted=[])

        def save():
            (directory / 'observations.json').write_text(json.dumps(row, indent=2) + '\n')

        def invoke(argv, role, timeout=120):
            command = [str(a) for a in argv]
            start = time.monotonic()
            try:
                result = subprocess.run(command, capture_output=True, text=True, timeout=timeout, env=env)
                record = dict(role=role, argv=command, exit=result.returncode, stdout=result.stdout, stderr=result.stderr, elapsed=time.monotonic()-start)
            except subprocess.TimeoutExpired as error:
                record = dict(role=role, argv=command, timeout=error.timeout, elapsed=time.monotonic()-start)
                row['commands'].append(record)
                save()
                raise
            row['commands'].append(record)
            save()
            return result

        for name, compiler in references.items():
            for level in ('-O0', '-O2'):
                executable = directory / (name + level)
                result = invoke([compiler, '-std=c17', level, *sources, '-o', executable], 'reference-compile')
                assert result.returncode == 0, (index, result)
                result = invoke([executable], 'reference-native', 10)
                row['native'].append(dict(compiler=name, level=level, executable_sha256=digest(executable), exit=result.returncode, stdout=result.stdout, stderr=result.stderr))
                save()
                assert result.returncode == case['expected'] and not result.stdout and not result.stderr, (index, name, level, result)
        for name, compiler in compilers.items():
            for level in ('-O0', '-O2'):
                objects = []
                for source in sources:
                    output = directory / (name + level + '-' + source.stem + '.o')
                    result = invoke([compiler, '-fverify-each', level, '-c', source, '-o', output], 'owned-compile')
                    assert result.returncode == 0, (index, name, result)
                    objects.append(output)
                    row['objects'][output.name] = digest(output)
                    save()
                executable = directory / (name + level + '.native')
                result = invoke([references['gcc'], '-no-pie', *objects, '-o', executable], 'owned-link')
                assert result.returncode == 0, (index, name, result)
                result = invoke([executable], 'owned-native', 10)
                row['native'].append(dict(compiler=name, level=level, executable_sha256=digest(executable), exit=result.returncode, stdout=result.stdout, stderr=result.stderr))
                save()
                assert result.returncode == case['expected'] and not result.stdout and not result.stderr, (index, name, level, result)
                result = invoke([compiler, '-fverify-each', level, '--interpret', ir_source], 'owned-interpreter', 30)
                match = re.fullmatch(r'interpret main => (-?\d+)\n', result.stdout)
                assert result.returncode == 0 and match and int(match[1]) == case['expected'] and not result.stderr, (index, name, level, result)
                row['interpreted'].append(dict(compiler=name, level=level, integer=int(match[1])))
                save()
        if set(compilers) == {'stage2', 'stage3'}:
            for level in ('-O0', '-O2'):
                for source in sources:
                    assert row['objects']['stage2' + level + '-' + source.stem + '.o'] == row['objects']['stage3' + level + '-' + source.stem + '.o'], (index, level, source, 'stage object mismatch')
        row['eligible'] = True
        save()
        return row

    rows = []
    started = time.monotonic()
    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            for row in pool.map(probe, range(args.count)):
                rows.append(row)
                if len(rows) % 32 == 0:
                    print('Generated native programs:', len(rows), '/', args.count, flush=True)
                    (root / 'progress.json').write_text(json.dumps(dict(completed=len(rows), requested=args.count, elapsed=time.monotonic()-started)) + '\n')
        unique = {hashlib.sha256(json.dumps(row['sources'], sort_keys=True).encode()).hexdigest() for row in rows}
        assert len(unique) == args.count, 'duplicate generated source program'
        assert all(digest(path) == value['sha256'] for name, value in tools.items() for path in [value['path']]), 'compiler changed during campaign'
        assert all(digest(path) == value for path, value in inputs.items()), 'generator changed during campaign'
        summary = dict(configuration, eligible_programs=len(rows), native_executions=sum(len(r['native']) for r in rows), interpreter_executions=sum(len(r['interpreted']) for r in rows), families={name: sum(r['family']==name for r in rows) for name in sorted({r['family'] for r in rows})}, reports={f'case-{r["index"]:05d}/observations.json': digest(root / f'case-{r["index"]:05d}/observations.json') for r in rows}, elapsed=time.monotonic()-started, status='pass')
        (root / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    except BaseException as error:
        (root / 'failure.json').write_text(json.dumps(dict(error=str(error), completed=len(rows), requested=args.count, elapsed=time.monotonic()-started), indent=2) + '\n')
        raise
    print('Generated native campaign:', len(rows), 'eligible programs,', summary['native_executions'], 'native executions,', summary['interpreter_executions'], 'interpreted executions.', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
