"""Read real pass boundaries and observations without executing workloads."""
import argparse
import collections
import json
import math
import pathlib
import struct

from evidence_integrity import EvidenceError, artifact_path, digest

ORDER = ('mem2reg', 'constant-fold', 'cfg-simplify', 'sparse-constants',
         'value-numbering', 'strength-reduction', 'copy-cleanup', 'local-memory',
         'loop-motion', 'dead-code')
IDS = {name: number for number, name in enumerate((
    'constant-fold', 'cfg-simplify', 'mem2reg', 'sparse-constants', 'dead-code',
    'value-numbering', 'copy-cleanup', 'local-memory', 'loop-motion', 'strength-reduction'))}


def require(condition, message):
    if not condition:
        raise EvidenceError(message)


def input_files(source, cases):
    names = {'tests/test_pass_pipeline.py', 'tests/optimization/pipeline_cases.json',
             'CMakeLists.txt', 'tools/pass_evidence.py', 'tools/evidence_integrity.py',
             'tests/test_pass_evidence.py'}
    for directory in ('source', 'runtime'):
        names.update(str(p.relative_to(source)) for p in (source / directory).rglob('*') if p.is_file())
    names.update('tests/optimization/' + case['source'] for case in cases)
    return {name: digest(artifact_path(source, name)) for name in sorted(names)}


def representation(path):
    """Count operations and track stable value IDs, independently of counters."""
    functions = blocks = operations = 0
    opcodes = collections.Counter()
    positions = collections.defaultdict(dict)
    function = block = None
    lines = path.read_text().splitlines()
    require(lines and lines[0] == 'cinder-ir 6 lp64-le sysv-x86-64' and lines[-1] == 'end-module',
            'incomplete or wrong target IR: ' + str(path))
    for line in lines:
        words = line.split()
        if not words:
            continue
        if words[0] == 'function':
            function = words[1]
            functions += 1
        elif words[0] == 'block':
            block = int(words[1])
            blocks += 1
        elif words[0] == 'inst' and words[1] != 'nop':
            operations += 1
            opcodes[words[1]] += 1
            # Instructions without a destination do not carry a stable value ID.
            if words[5] != 'none':
                positions[words[1]][(function, words[5])] = block
    return dict(functions=functions, blocks=blocks, operations=operations,
                opcodes=opcodes, positions=positions)


def statistics(path, order):
    data = json.loads(path.read_text())
    require(data.get('schema') == 1 and data.get('timer') == 'process-cpu-clock', 'wrong pass statistics schema')
    records = data.get('passes', [])
    require([row.get('name') for row in records] == list(order), 'missing, repeated or reordered pass')
    epoch = 0
    for row in records:
        for name in ('id', 'epoch_before', 'epoch_after', 'functions_run', 'functions_changed',
                     'transformation_events', 'operations_before', 'operations_after', 'blocks_before',
                     'blocks_after', 'preserved_analyses', 'invalidated_analyses'):
            require(type(row.get(name)) is int and row[name] >= 0, 'invalid pass counter: ' + name)
        require(row['id'] == IDS[row['name']] and isinstance(row.get('preconditions'), str)
                and row['preconditions'], 'missing pass contract')
        require(row.get('verified_before') is True and row.get('verified_after') is True, 'unverified pass boundary')
        require(0 <= row['functions_changed'] <= row['functions_run'] and row['functions_run'] > 0,
                'empty or inflated function progress')
        changed = row['transformation_events'] > 0
        require(changed == (row['functions_changed'] > 0), 'inconsistent transformation progress')
        preserved = (0 if row['name'] in ('cfg-simplify', 'sparse-constants') else 7) if changed else 63
        invalidated = 63 & ~preserved
        require(row['preserved_analyses'] == preserved and row['invalidated_analyses'] == invalidated,
                'incorrect analysis invalidation contract')
        require(row['epoch_before'] == epoch, 'broken analysis epoch')
        epoch += invalidated != 0
        require(row['epoch_after'] == epoch, 'missing analysis invalidation')
        seconds = row.get('cpu_seconds')
        require(type(seconds) in (float, int) and math.isfinite(seconds) and seconds >= 0, 'missing actual CPU timing')
    require(data.get('analysis_epoch') == epoch and type(data.get('functions_changed')) is int,
            'wrong module pass summary')
    require(0 <= data['functions_changed'] <= max(row['functions_run'] for row in records),
            'inflated module function count')
    return data


def boundary(row, before, after):
    old, new = representation(before), representation(after)
    require(old['functions'] == new['functions'] == row['functions_run'], 'wrong function dimensions')
    for version, parsed in (('before', old), ('after', new)):
        require(parsed['operations'] == row['operations_' + version]
                and parsed['blocks'] == row['blocks_' + version], 'fabricated pass dimensions')
    if row['transformation_events'] == 0:
        require(before.read_bytes() == after.read_bytes(), 'unrecorded transformation')
    return old, new


def elf(path, kind):
    data = path.read_bytes()
    require(len(data) >= 64 and data[:7] == b'\x7fELF\x02\x01\x01'
            and struct.unpack_from('<H', data, 16)[0] == kind
            and struct.unpack_from('<H', data, 18)[0] == 62, 'wrong target ELF artifact')
    if kind == 1:
        offset = struct.unpack_from('<Q', data, 40)[0]
        size, count = struct.unpack_from('<HH', data, 58)
        require(size == 64 and count > 1 and offset + size * count <= len(data), 'missing ELF sections')
        executable = 0
        for index in range(count):
            at = offset + index * size
            section_type = struct.unpack_from('<I', data, at + 4)[0]
            flags = struct.unpack_from('<Q', data, at + 8)[0]
            start, length = struct.unpack_from('<QQ', data, at + 24)
            require(section_type == 8 or start + length <= len(data), 'truncated object section')
            if section_type == 1 and flags & 4:
                executable += length
        require(executable > 0, 'empty original machine code')


def verify_passes(base, source, compiler=None, *, require_native=True):
    base, source = pathlib.Path(base).resolve(), pathlib.Path(source).resolve()
    data = json.loads((base / 'observations.json').read_text())
    require(data.get('schema') == 2, 'incomplete pass evidence schema')
    cases = json.loads((source / 'tests/optimization/pipeline_cases.json').read_text())
    require(len(cases) == 20 and collections.Counter(c['pass'] for c in cases) == {name: 2 for name in ORDER}
            and all({c['changes'] for c in cases if c['pass'] == name} == {True, False} for name in ORDER),
            'missing independent positive/negative pass pair')
    require(data.get('inputs') == input_files(source, cases), 'changed or missing pass input binding')
    require(type(data.get('native')) is bool, 'undeclared execution profile')
    if require_native:
        require(data['native'] and data.get('machine') == dict(system='Linux', machine='x86_64'),
                'pass gate requires native Linux x86-64 execution')
    inventory = data.get('artifacts', {})
    actual = {str(p.relative_to(base)) for p in base.rglob('*') if p.is_file() and p.name != 'observations.json'}
    require(set(inventory) == actual and inventory, 'missing or extra pass artifact')
    for name, record in inventory.items():
        path = artifact_path(base, name)
        require(type(record.get('bytes')) is int and path.stat().st_size == record['bytes']
                and digest(path) == record.get('sha256'), 'changed pass artifact: ' + name)
    for tool, key in (('cindercc', 'compiler_sha256'), ('cinderir', 'irtool_sha256')):
        require(digest(artifact_path(base, 'tools/' + tool)) == data.get(key), 'changed recorded compiler tool')
    if compiler is not None:
        compiler = pathlib.Path(compiler)
        require(digest(compiler) == data['compiler_sha256']
                and digest(compiler.parent / 'cinderir') == data['irtool_sha256'], 'changed active compiler pair')
    if data['native']:
        require(digest(artifact_path(base, 'tools/cc')) == data.get('linker_sha256'), 'missing linker binding')
    prefix = data.get('artifact_prefix')
    require(isinstance(prefix, str) and prefix and '..' not in pathlib.PurePosixPath(prefix).parts, 'wrong original artifact prefix')
    command_records = json.loads((base / 'commands.json').read_text())
    require(isinstance(command_records, list) and command_records, 'missing actual command observations')
    expected_commands = []

    def argument(name):
        return str(pathlib.Path(prefix) / name)

    def command(argv, *, expected=0, role='compiler', observed=None, absolute=False):
        expected_commands.append((list(map(str, argv)), expected, role, observed, absolute))

    def trace(directory, order):
        report = statistics(artifact_path(base, directory + f'/{len(order):02d}-pipeline.json'), order)
        previous = None
        for number, row in enumerate(report['passes']):
            stem = directory + f"/{number:02d}-{row['name']}"
            before, after = artifact_path(base, stem + '.before.cir'), artifact_path(base, stem + '.after.cir')
            boundary(row, before, after)
            require(previous is None or previous.read_bytes() == before.read_bytes(), 'broken actual pass chain')
            previous = after
        return report

    observations = data.get('cases', [])
    require(len(observations) == len(cases), 'missing selected pass case')
    native_executions = 0
    for number, (case, row) in enumerate(zip(cases, observations)):
        require(all(row.get(key) == value for key, value in case.items()), 'changed pass fixture contract')
        source_name = 'tests/optimization/' + case['source']
        require(digest(source / source_name) == row.get('source_sha256'), 'changed pass fixture source')
        directory = f'case-{number:02d}'
        baseline = trace(directory + '/baseline', ORDER)
        position = ORDER.index(case['pass'])
        before_name = directory + f"/baseline/{position:02d}-{case['pass']}.before.cir"
        after_name, stats_name = directory + '/after.cir', directory + '/stats.json'
        require((row.get('before'), row.get('after'), row.get('stats')) == (before_name, after_name, stats_name),
                'substituted isolated pass boundary')
        before, after = artifact_path(base, before_name), artifact_path(base, after_name)
        require(digest(before) == row.get('before_sha256') and digest(after) == row.get('after_sha256')
                and digest(base / stats_name) == row.get('stats_sha256'), 'changed selected pass boundary')
        isolated = trace(directory + '/isolated', (case['pass'],))
        require(isolated == statistics(base / stats_name, (case['pass'],)), 'changed isolated statistics')
        stem = directory + '/isolated/00-' + case['pass']
        require(before.read_bytes() == (base / (stem + '.before.cir')).read_bytes()
                and after.read_bytes() == (base / (stem + '.after.cir')).read_bytes(), 'wrong isolated snapshots')
        normal_after = directory + f"/baseline/{position:02d}-{case['pass']}.after.cir"
        require(after.read_bytes() == (base / normal_after).read_bytes(), 'isolated pass differs from normal pipeline')
        old, new = boundary(isolated['passes'][0], before, after)
        metric = case['metric']
        if metric == 'blocks':
            a, b = old['blocks'], new['blocks']
        elif metric.startswith('motion:'):
            a, b = old['positions'][metric[7:]], new['positions'][metric[7:]]
        else:
            a, b = old['opcodes'][metric], new['opcodes'][metric]
        changed = a != b if metric.startswith('motion:') else b < a
        require(bool(a) and changed == case['changes'], 'vacuous or incorrect selected transformation')
        if case['changes']:
            require(isolated['passes'][0]['transformation_events'] > 0 and before.read_bytes() != after.read_bytes(),
                    'claimed transformation without actual progress')
        outcome = row.get('interpreter', {})
        require(outcome.get('valid') is True and outcome.get('classification') == 0
                and outcome.get('integer') == case['expected'], 'incorrect interpreter observation')
        command([data['compiler_path'], '-O2', '--pass-trace', argument(directory + '/baseline'),
                 '--serialize-ir', source_name, '-o', argument(directory + '/source.cir')])
        require((base / (directory + '/source.cir')).read_bytes() ==
                (base / (directory + '/baseline/09-dead-code.after.cir')).read_bytes(), 'wrong final baseline output')
        command([data['irtool_path'], '--pass=' + case['pass'], '--pass-trace', argument(directory + '/isolated'),
                 '--pass-stats', argument(stats_name), argument(before_name), '-o', argument(after_name)])
        command([data['irtool_path'], '--classify', argument(before_name)], observed=outcome)
        command([data['irtool_path'], '--classify', argument(after_name)], observed=outcome)
        objects = row.get('objects', [])
        require([item.get('level') for item in objects] == ['before', 'after'], 'missing original before/after object')
        for item, ir_name in zip(objects, (before_name, after_name)):
            name = directory + '/' + item['level'] + '.o'
            require(item.get('object') == name and digest(base / name) == item.get('sha256'), 'changed original object')
            elf(base / name, 1)
            command([data['irtool_path'], '-c', argument(ir_name), '-o', argument(name)])
            if data['native']:
                binary = directory + '/' + item['level'] + '.native'
                require(item.get('executable') == binary and digest(base / binary) == item.get('executable_sha256')
                        and item.get('native_exit') == case['expected'], 'missing or wrong native pass observation')
                elf(base / binary, 2)
                command(['cc', '-no-pie', argument(name), '-o', argument(binary)])
                command([argument(binary)], expected=case['expected'], role='native', absolute=True)
                native_executions += 1
    complete = trace('complete-pipeline', ORDER)
    require(complete == statistics(base / 'complete.json', ORDER), 'changed complete pipeline statistics')
    command([data['compiler_path'], '-O2', '--pass-stats', argument('complete.json'),
             '--pass-trace', argument('complete-pipeline'), '--serialize-ir',
             'tests/optimization/pipeline_mem2reg.c', '-o', argument('complete.cir')])
    require((base / 'complete.cir').read_bytes() == (base / 'complete-pipeline/09-dead-code.after.cir').read_bytes(),
            'wrong complete pipeline output')
    for number, name in enumerate(ORDER):
        for version in ('before', 'after'):
            command([data['irtool_path'], '--verify', argument(f'complete-pipeline/{number:02d}-{name}.{version}.cir')])
    require(len(command_records) == len(expected_commands), 'missing or extra actual pass command')
    for record, (argv, expected, role, observed, absolute) in zip(command_records, expected_commands):
        actual_argv = record.get('argv')
        if absolute:
            # Native argv was resolved in its original checkout. Relocation of
            # the evidence directory must not change the executable identity.
            require(isinstance(actual_argv, list) and len(actual_argv) == 1
                    and pathlib.PurePosixPath(actual_argv[0]).is_absolute()
                    and actual_argv[0].endswith('/' + argv[0]), 'wrong native executable command')
        else:
            require(actual_argv == argv, 'substituted pass command or hidden fallback')
        require(record.get('exit') == expected and record.get('role') == role and record.get('stderr') == '',
                'failed or misclassified actual command')
        if observed is None:
            output = 'IR verified\n' if '--verify' in argv else ''
            require(record.get('stdout') == output, 'unexpected command output')
        else:
            require(json.loads(record.get('stdout', '')) == observed, 'altered interpreter output')
    return dict(passes=10, pairs=20, objects=40, native_executions=native_executions,
                commands=len(command_records), observations_sha256=digest(base / 'observations.json'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('evidence', type=pathlib.Path)
    parser.add_argument('--source', type=pathlib.Path, default=pathlib.Path('.'))
    args = parser.parse_args()
    print(json.dumps(verify_passes(args.evidence, args.source), sort_keys=True))
