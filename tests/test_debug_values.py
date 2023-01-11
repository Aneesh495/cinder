#!/usr/bin/env python3
"""Check initialized local storage, CFG joins and loop reentry independently."""
import argparse
import copy
import hashlib
import json
import pathlib
import platform
import re
import shutil
import struct
import subprocess
import tempfile

from test_debug_lines import line_table
from test_debug_types import read
from test_unwind import digest, elf, normalized, tool


CASES = {'before-init': ('debug_initialize', 'pending', False), 'after-init': ('debug_initialize', 'pending', True),
         'partial-join': ('debug_partial', 'partial', False), 'complete-join': ('debug_complete', 'complete', True),
         'loop-before': ('debug_loop', 'fresh', False), 'loop-after': ('debug_loop', 'fresh', True),
         'escaped-address': ('debug_escaped', 'escaped', False), 'pointer-value': ('debug_pointer', 'pointer', True)}


def record(path):
    sections, _, relas = elf(path); _, sequences = line_table(sections['.debug_line'], relas['.debug_line'])
    return dict(nodes=read(path), sequences=sequences, text=sections['.text'])


def validate(data, level):
    functions = {node['attributes'][3]: node for node in data['nodes'].values() if node['tag'] == 0x2e}
    locals_ = {}
    for function in functions.values():
        pending = list(function['children'])
        while pending:
            node = pending.pop(); pending.extend(node['children'])
            if node['tag'] == 0x34: locals_[node['attributes'][3]] = (function, node)
    source = pathlib.Path('tests/debug/values.c').read_text().splitlines(); observations = []
    for marker, (name, variable, supported) in CASES.items():
        function, local = locals_[variable]; symbol = function['attributes'][0x11]
        assert function is functions[name]
        sequence = next(sequence for sequence in data['sequences'] if sequence['symbol'] == symbol)
        line = next(i + 1 for i, text in enumerate(source) if '/* ' + marker + ' */' in text)
        pcs = [row['pc'] - symbol['value'] for row in sequence['rows'] if row['line'] == line]; assert pcs, marker
        available = supported and level == '-O0'; entries = local['locations']
        assert all(any(entry['begin'] <= pc < entry['end'] for entry in entries) == available for pc in pcs), (marker, entries, pcs)
        if available:
            home = entries[0]['cfa']; assert home <= -24 and all(entry['cfa'] == home for entry in entries)
            code = data['text'][symbol['value']:symbol['value'] + symbol['size']]
            # Every first range must start after a real store into that exact
            # allocated local home, rather than at scope entry or a prior load.
            prefix = b'\x48\x89\x85' if variable == 'pointer' else b'\x89\x85'
            store = prefix + struct.pack('<i', home + 16)
            ends = {m.end() for m in re.finditer(re.escape(store), code) if m.start() >= function['prologue']}
            assert entries[0]['begin'] in ends, (variable, entries, ends)
        observations.append(dict(marker=marker, function=name, variable=variable, line=line, available=available, pcs=pcs))
    return observations


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('compiler', type=pathlib.Path)
    parser.add_argument('--execution-profile', choices=('native', 'emulated', 'cross')); args = parser.parse_args()
    compiler = args.compiler.resolve(); identity = digest(compiler)
    linux = platform.system() == 'Linux' and platform.machine() == 'x86_64'
    profile = args.execution_profile or ('native' if linux else 'cross'); execute = profile in ('native', 'emulated'); assert not execute or linux
    source = pathlib.Path('tests/debug/values.c')
    files = sorted(pathlib.Path('source').glob('*.[ch]')) + sorted(pathlib.Path('runtime/include').rglob('*.h'))
    files += [source] + [pathlib.Path(name) for name in ('tests/test_debug_values.py', 'tests/test_debug_types.py', 'tests/test_debug_lines.py', 'tests/test_unwind.py')]
    inputs = {str(path): digest(path) for path in files}; source_hash = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()
    base = pathlib.Path('.agent-local/debug-values') / (source_hash + '-' + identity); base.mkdir(parents=True, exist_ok=True)
    root = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=base)).resolve()
    tools = {name: tool(name) for name in ('llvm-dwarfdump', 'llvm-mc')}
    tools['gcc'] = tool('gcc-15' if shutil.which('gcc-15') else 'gcc'); tools['clang'] = tool('clang')
    if execute: tools.update({name: tool(name) for name in ('cc', 'gdb')})
    identities = {name: dict(path=path, sha256=digest(pathlib.Path(path))) for name, path in tools.items()}
    artifacts = set(); commands = []; observations = []
    def run(argv, label, expected=0):
        result = subprocess.run(list(map(str, argv)), capture_output=True, timeout=60)
        out, err = root / (label + '.stdout'), root / (label + '.stderr'); out.write_bytes(result.stdout); err.write_bytes(result.stderr)
        artifacts.update((out, err)); commands.append(dict(argv=list(map(str, argv)), exit=result.returncode, stdout=digest(out), stderr=digest(err)))
        (root / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        assert result.returncode == expected, (argv, result)
        return result.stdout.decode(errors='replace')
    def gdb(executable, expressions, label):
        argv = [tools['gdb'], '-nx', '-batch', '-ex', 'set pagination off', '-ex', 'set disable-randomization off']
        for expression in expressions: argv.extend(('-ex', expression))
        text = run([*argv, executable], label)
        assert re.search(r'#\d+.*\bmain\b', text) and 'Cannot access memory' not in text
        return text
    for name, path in tools.items(): run([path, '--version'], name + '-version')
    for level in ('-O0', '-O2'):
        obj, asm, assembled = [root / ('values' + level + suffix) for suffix in ('.o', '.s', '.assembled.o')]
        run([compiler, level, '-g', '-fverify-each', '-c', source, '-o', obj], level + '-compile')
        assert run([compiler, level, '-g', '--interpret', source], level + '-interpret') == 'interpret main => 31\n'
        verification = run([tools['llvm-dwarfdump'], '--verify', obj], level + '-verify'); assert 'warning:' not in verification and 'No errors.' in verification
        observed = validate(record(obj), level); observations.append(dict(level=level, object_sha256=digest(obj), markers=observed))
        run([compiler, level, '-g', '-S', source, '-o', asm], level + '-assembly')
        run([tools['llvm-mc'], '-triple=x86_64-linux-gnu', '-filetype=obj', asm, '-o', assembled], level + '-assemble')
        original, _, relas = elf(obj); reference, _, other = elf(assembled)
        for name in {'.text'} | {name for name in original if name.startswith('.debug_')}:
            assert original[name] == reference[name] and normalized(relas.get(name, [])) == normalized(other.get(name, [])), name
        artifacts.update((obj, asm, assembled))
        for reference in ('gcc', 'clang'):
            executable = root / (reference + level)
            run([tools[reference], '-std=c17', level, source, '-o', executable], reference + level + '-compile'); run([executable], reference + level + '-execute', 31); artifacts.add(executable)
        if execute:
            executable = root / ('values' + level)
            run([tools['cc'], '-no-pie', obj, '-o', executable], level + '-link'); run([executable], level + '-execute', 31); artifacts.add(executable)
            for marker in [marker for marker in observed if marker['marker'] not in ('loop-before', 'loop-after')]:
                variable = marker['variable']; expressions = [f'break {source.resolve()}:{marker["line"]}', 'run', 'bt', 'print ' + variable]
                if marker['marker'] == 'pointer-value' and marker['available']: expressions += ['print *pointer']
                if marker['marker'] == 'complete-join': expressions += ['continue', 'bt', 'print complete']
                text = gdb(executable, expressions, level + '-' + marker['marker'] + '-gdb')
                assert 'Breakpoint 1, ' + marker['function'] in text
                values = re.findall(r'\$\d+ = ([^\n]+)', text)
                if not marker['available']:
                    assert values == ['<optimized out>'] * (2 if marker['marker'] == 'complete-join' else 1), (marker, values)
                elif marker['marker'] == 'pointer-value': assert len(values) == 2 and '0x' in values[0] and values[1] == '11'
                else: assert values == (['7'] if marker['marker'] == 'after-init' else ['3', '4']), (marker, values)
            loop = {marker['marker']: marker for marker in observed if marker['marker'].startswith('loop-')}
            expressions = [f'break {source.resolve()}:{loop["loop-before"]["line"]}', f'break {source.resolve()}:{loop["loop-after"]["line"]}', 'run', 'bt', 'print fresh']
            for _ in range(3): expressions += ['continue', 'bt', 'print fresh']
            text = gdb(executable, expressions, level + '-loop-gdb')
            values = re.findall(r'\$\d+ = ([^\n]+)', text)
            assert values == (['<optimized out>', '1', '<optimized out>', '2'] if level == '-O0' else ['<optimized out>'] * 4), values
    rejected = []
    for label in ('before-store', 'wrong-home', 'partial-join', 'escaped-home', 'stale-loop'):
        data = copy.deepcopy(record(root / 'values-O0.o')); nodes = data['nodes']
        local = lambda name: next(node for node in nodes.values() if node['tag'] == 0x34 and node['attributes'][3] == name)
        if label == 'before-store': local('pending')['locations'][0]['begin'] -= 1
        elif label == 'wrong-home':
            for entry in local('pending')['locations']: entry['cfa'] -= 8
        else:
            name = {'partial-join': 'partial', 'escaped-home': 'escaped', 'stale-loop': 'fresh'}[label]
            function = next(node for node in nodes.values() if node['tag'] == 0x2e and node['attributes'][3] == {'partial': 'debug_partial', 'escaped': 'debug_escaped', 'fresh': 'debug_loop'}[name])
            local(name)['locations'] = [dict(begin=0, end=function['attributes'][0x12], cfa=-32)]
        try: validate(data, '-O0')
        except AssertionError: rejected.append(label)
        else: raise AssertionError('reader accepted ' + label)
    assert digest(compiler) == identity and inputs == {name: digest(pathlib.Path(name)) for name in inputs}
    assert all(digest(pathlib.Path(value['path'])) == value['sha256'] for value in identities.values())
    report = dict(schema=1, compiler_sha256=identity, source_sha256=source_hash, inputs=inputs, tools=identities,
                  execution_profile=profile, native=profile == 'native', target_execution=execute, debugger_sessions=14 if execute else 0,
                  observations=observations, rejected_metadata=rejected, commands=commands,
                  artifacts={str(path.relative_to(root)): digest(path) for path in sorted(artifacts)})
    (root / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Debug values: initialization, both joins, loop reentry, escaped storage and pointers at O0/O2; five corruptions rejected; native={report["native"]}; evidence={root}')


if __name__ == '__main__': main()
