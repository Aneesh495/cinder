#!/usr/bin/env python3
"""Observe each real pass, its verification boundaries and retained guards."""
import hashlib
import json
import pathlib
import platform
import shutil
import subprocess
import sys

compiler = pathlib.Path(sys.argv[1]).resolve()
irtool = compiler.parent / 'cinderir'
registry = pathlib.Path('tests/optimization/pipeline_cases.json')
cases = json.loads(registry.read_text())
order = ['mem2reg', 'constant-fold', 'cfg-simplify', 'sparse-constants', 'value-numbering', 'strength-reduction', 'copy-cleanup', 'local-memory', 'loop-motion', 'dead-code']
inputs = [pathlib.Path('tests/test_pass_pipeline.py'), registry, pathlib.Path('CMakeLists.txt'),
          pathlib.Path('tools/pass_evidence.py'), pathlib.Path('tools/evidence_integrity.py'),
          pathlib.Path('tests/test_pass_evidence.py')]
inputs += [p for directory in ('source', 'runtime') for p in sorted(pathlib.Path(directory).rglob('*')) if p.is_file()]
inputs += sorted({pathlib.Path('tests/optimization') / row['source'] for row in cases})
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
hashes = {str(p): digest(p) for p in inputs}
identity = hashlib.sha256(compiler.read_bytes() + irtool.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
root = pathlib.Path('.agent-local/pass-pipeline') / identity
root.mkdir(parents=True, exist_ok=True)
native = platform.system() == 'Linux' and platform.machine() == 'x86_64'
rows = []
commands = []
tools = root / 'tools'
tools.mkdir(exist_ok=True)
shutil.copy2(compiler, tools / 'cindercc')
shutil.copy2(irtool, tools / 'cinderir')
linker = pathlib.Path(shutil.which('cc')).resolve() if native else None
if linker is not None:
    shutil.copy2(linker, tools / 'cc')


def run(argv, expected=0, role='compiler'):
    result = subprocess.run([str(a) for a in argv], capture_output=True, text=True, timeout=30)
    commands.append(dict(role=role, argv=[str(a) for a in argv], exit=result.returncode, stdout=result.stdout, stderr=result.stderr))
    (root / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    assert result.returncode == expected, (argv, result)
    return result


def dimensions(path):
    lines = path.read_text().splitlines()
    return sum(s.startswith('inst ') and not s.startswith('inst nop ') for s in lines), sum(s.startswith('block ') for s in lines)


def records(path, expected):
    data = json.loads(path.read_text())
    assert data['schema'] == 1 and data['timer'] == 'process-cpu-clock'
    assert [row['name'] for row in data['passes']] == expected
    epoch = 0
    for row in data['passes']:
        assert row['verified_before'] and row['verified_after'] and row['functions_run'] > 0
        assert row['preconditions'] and isinstance(row['cpu_seconds'], (int, float)) and row['cpu_seconds'] >= 0
        assert row['epoch_before'] == epoch and row['preserved_analyses'] | row['invalidated_analyses'] == 63
        assert row['preserved_analyses'] & row['invalidated_analyses'] == 0
        assert (row['transformation_events'] > 0) == (row['functions_changed'] > 0)
        epoch += row['invalidated_analyses'] != 0
        assert row['epoch_after'] == epoch
    assert data['analysis_epoch'] == epoch
    return data


def metric(path, kind):
    if kind == 'blocks':
        return dimensions(path)[1]
    opcode = kind.removeprefix('motion:')
    function = None
    block = None
    result = {}
    for line in path.read_text().splitlines():
        words = line.split()
        if words[0] == 'function':
            function = words[1]
        elif words[0] == 'block':
            block = int(words[1])
        elif words[0] == 'inst' and words[1] == opcode:
            result[(function, words[5])] = block
    return result if kind.startswith('motion:') else len(result)


def save(row):
    rows.append(row)
    publish()


def publish():
    artifacts = {str(p.relative_to(root)): dict(sha256=digest(p), bytes=p.stat().st_size)
                 for p in sorted(root.rglob('*')) if p.is_file() and p.name != 'observations.json'}
    (root / 'observations.json').write_text(json.dumps(dict(schema=2, inputs=hashes,
        compiler_sha256=digest(compiler), irtool_sha256=digest(irtool),
        compiler_path=str(compiler), irtool_path=str(irtool), artifact_prefix=str(root),
        native=native, machine=dict(system=platform.system(), machine=platform.machine()),
        linker_sha256=digest(linker) if linker else None, artifacts=artifacts, cases=rows), indent=2) + '\n')


def relative(path):
    return str(path.relative_to(root))


for number, case in enumerate(cases):
    source = pathlib.Path('tests/optimization') / case['source']
    directory = root / f'case-{number:02d}'
    baseline = directory / 'baseline'
    trace = directory / 'isolated'
    baseline.mkdir(parents=True, exist_ok=True)
    trace.mkdir(exist_ok=True)
    source_ir = directory / 'source.cir'
    run([compiler, '-O2', '--pass-trace', baseline, '--serialize-ir', source, '-o', source_ir])
    baseline_stats = records(baseline / '10-pipeline.json', order)
    position = order.index(case['pass'])
    before = baseline / f"{position:02d}-{case['pass']}.before.cir"
    after = directory / 'after.cir'
    stats = directory / 'stats.json'
    run([irtool, '--pass=' + case['pass'], '--pass-trace', trace, '--pass-stats', stats, before, '-o', after])
    data = records(stats, [case['pass']])
    assert data == records(trace / '01-pipeline.json', [case['pass']])
    entry = data['passes'][0]
    assert before.read_bytes() == (trace / ('00-' + case['pass'] + '.before.cir')).read_bytes()
    assert after.read_bytes() == (trace / ('00-' + case['pass'] + '.after.cir')).read_bytes()
    assert dimensions(before) == (entry['operations_before'], entry['blocks_before'])
    assert dimensions(after) == (entry['operations_after'], entry['blocks_after'])
    first = json.loads(run([irtool, '--classify', before]).stdout)
    second = json.loads(run([irtool, '--classify', after]).stdout)
    assert first == second and second['valid'] and second['integer'] == case['expected'], (case, first, second)
    old, new = metric(before, case['metric']), metric(after, case['metric'])
    changed = old != new if case['metric'].startswith('motion:') else new < old
    assert old and changed == case['changes'], (case, old, new)
    if case['changes']:
        assert entry['transformation_events'] > 0 and before.read_bytes() != after.read_bytes()
    objects = []
    for version, path in (('before', before), ('after', after)):
        obj = directory / (version + '.o')
        run([irtool, '-c', path, '-o', obj])
        record = dict(level=version, object=relative(obj), sha256=digest(obj))
        if native:
            binary = obj.with_suffix('.native')
            run(['cc', '-no-pie', obj, '-o', binary])
            result = run([binary.resolve()], expected=case['expected'], role='native')
            assert result.returncode == case['expected'] and result.stdout == result.stderr == '', (case, version, result)
            record.update(native_exit=result.returncode, executable=relative(binary), executable_sha256=digest(binary))
        objects.append(record)
    save(dict(case, source_sha256=digest(source), before=relative(before), after=relative(after), before_sha256=digest(before), after_sha256=digest(after), stats=relative(stats), stats_sha256=digest(stats), interpreter=second, objects=objects))

# Full source compilation must actually traverse all ten recorded boundaries.
directory = root / 'complete-pipeline'
directory.mkdir(exist_ok=True)
stats = root / 'complete.json'
run([compiler, '-O2', '--pass-stats', stats, '--pass-trace', directory, '--serialize-ir', 'tests/optimization/pipeline_mem2reg.c', '-o', root / 'complete.cir'])
data = records(stats, order)
assert data == records(directory / '10-pipeline.json', order)
for index, row in enumerate(data['passes']):
    before = directory / f"{index:02d}-{row['name']}.before.cir"
    after = directory / f"{index:02d}-{row['name']}.after.cir"
    assert dimensions(before) == (row['operations_before'], row['blocks_before'])
    assert dimensions(after) == (row['operations_after'], row['blocks_after'])
    run([irtool, '--verify', before])
    run([irtool, '--verify', after])
    if index:
        prior = directory / f"{index - 1:02d}-{data['passes'][index - 1]['name']}.after.cir"
        assert prior.read_bytes() == before.read_bytes()

assert hashes == {str(p): digest(p) for p in inputs}, 'pass inputs changed during verification'
assert identity == hashlib.sha256(compiler.read_bytes() + irtool.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
publish()
sys.path.insert(0, str(pathlib.Path('tools').resolve()))
from pass_evidence import verify_passes
proof = verify_passes(root, pathlib.Path('.'), compiler, require_native=native)
assert proof['passes'] == 10 and proof['pairs'] == 20
print(f'Pass pipeline: ten actual isolated positive/negative pairs, stage counters, verifier results, timing and invalidation passed; native={native}')
