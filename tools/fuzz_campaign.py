#!/usr/bin/env python3
"""Run all four instrumented targets and retain coverage, inputs and outcomes."""
import argparse
import concurrent.futures
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess

from evidence_integrity import digest, inventory_digest, source_inputs

STAGES = ('pp', 'parser', 'ir', 'object')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('build', type=pathlib.Path)
    parser.add_argument('--compiler', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--runs', type=int, default=1000000)
    args = parser.parse_args()
    assert args.runs > 0
    root = pathlib.Path(__file__).resolve().parents[1]
    inputs = source_inputs(root)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    build, compiler = args.build.resolve(), args.compiler.resolve()
    profile = output/'build-profile'
    profile.mkdir()
    shutil.copy2(build/'CMakeCache.txt', profile/'CMakeCache.txt')
    shutil.copy2(build/'CMakeFiles/cinder_core.dir/flags.make', profile/'core-flags.make')
    candidates = [root/'examples/hello.c', root/'examples/macro_args.c']
    candidates += sorted((root/'tests/explorer').glob('*.c'))
    candidates += sorted((root/'tests/control').glob('*.c'))[:64]
    candidates += sorted((root/'tests/fuzz/regressions').glob('*.c'))
    seeds = output/'seeds'
    seeds.mkdir()
    source_seeds, ir_seeds = {}, {}
    for index, path in enumerate(candidates):
        name = f'{index:03d}.c'
        (seeds/name).write_bytes(path.read_bytes())
        source_seeds[name] = dict(source=str(path.relative_to(root)), sha256=digest(path))
        result = subprocess.run([str(compiler), '--serialize-ir', str(path)], capture_output=True, timeout=30)
        if result.returncode == 0 and len(result.stdout) <= 65536:
            name = f'{index:03d}.cir'
            assert result.stdout.startswith(b'cinder-ir 6 ')
            (seeds/name).write_bytes(result.stdout)
            ir_seeds[name] = digest(seeds/name)
    assert len(source_seeds) >= 32 and len(ir_seeds) >= 16
    configuration = dict(schema=1, source_sha256=inventory_digest(inputs), inputs=inputs,
                         compiler_sha256=digest(compiler), machine=platform.uname()._asdict(),
                         requested=args.runs, source_seeds=source_seeds, ir_seeds=ir_seeds, stages={},
                         build_profile={p.name:digest(p) for p in profile.iterdir()})
    (output/'configuration.json').write_text(json.dumps(configuration, indent=2)+'\n')
    # Darwin's libFuzzer leaves its detached RSS monitor allocated at exit.
    # Suppress that runtime-owned thread only; compiler allocations remain checked.
    suppression = output/'runtime-leaks.supp'
    if platform.system() == 'Darwin':
        suppression.write_text('leak:fuzzer::StartRssThread\n')

    def run(index):
        stage = STAGES[index]
        directory = output/stage
        directory.mkdir()
        corpus = directory/'corpus'
        corpus.mkdir()
        for name in source_seeds if index < 2 else ir_seeds:
            shutil.copy2(seeds/name, corpus/name)
        target = directory/('cinder_fuzz_'+stage)
        shutil.copy2(build/target.name, target)
        stats = directory/'stats.json'
        command = [str(target), str(corpus), '-runs='+str(args.runs), '-seed='+str(495+index),
                   '-max_len=65536', '-timeout=10', '-rss_limit_mb=2048', '-print_final_stats=1',
                   '-artifact_prefix='+str(directory)+'/']
        environment = dict(os.environ, CINDER_FUZZ_STATS=str(stats), CINDER_FUZZ_WORK=str(directory),
                           ASAN_OPTIONS='detect_leaks=1:abort_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        if platform.system() == 'Darwin':
            environment['LSAN_OPTIONS'] = 'suppressions='+str(suppression)+':print_suppressions=0'
        with (directory/'fuzzer.log').open('wb') as log:
            result = subprocess.run(command, stdout=log, stderr=log, env=environment, timeout=7200)
        text = (directory/'fuzzer.log').read_text(errors='replace')
        record = dict(command=command, exit=result.returncode, target_sha256=digest(target),
                      log_sha256=digest(directory/'fuzzer.log'))
        if stats.exists():
            record['counters'] = json.loads(stats.read_text())
        (directory/'command.json').write_text(json.dumps(record, indent=2)+'\n')
        assert result.returncode == 0 and re.search(r'Done '+str(args.runs)+r' runs in ', text), (stage, directory)
        assert not any(directory.glob('crash-*')) and not any(directory.glob('timeout-*'))
        assert source_inputs(root) == inputs
        print('Fuzz target:', stage, args.runs, 'executions passed', flush=True)
        return stage, record

    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        configuration['stages'] = dict(pool.map(run, range(4)))
    configuration['status'] = 'pass'
    (output/'summary.json').write_text(json.dumps(configuration, indent=2)+'\n')
    from release_evidence import verify_fuzz
    verify_fuzz(output, root, compiler, minimum=args.runs)


if __name__ == '__main__':
    main()
