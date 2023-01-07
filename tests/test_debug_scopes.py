#!/usr/bin/env python3
"""Independently check lexical range trees and same-name declaration selection."""
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

from test_debug_lines import information, line_table
from test_debug_types import location_lists
from test_unwind import digest, elf, normalized, tool


MARKERS = {'before-shadow': ('value', 'int'), 'after-shadow': ('value', 'double'),
           'inner-shadow': ('value', 'unsigned char'), 'after-inner': ('value', 'double'),
           'after-block': ('value', 'int'), 'loop-value': ('loop_value', 'int')}


def range_lists(data, relocations):
    fixes = {entry['offset']: entry for entry in relocations}; used = set(); lists = {}; at = 0
    while at < len(data):
        start = at; assert struct.unpack_from('<Q', data, at)[0] == 0xffffffffffffffff
        fix = fixes[at + 8]; used.add(at + 8)
        assert data[at + 8:at + 16] == bytes(8) and fix['kind'] == 1 and fix['addend'] == 0 and fix['symbol']['kind'] == 2
        at += 16; ranges = []
        while data[at:at + 16] != bytes(16):
            begin, end = struct.unpack_from('<QQ', data, at); at += 16
            assert 0 <= begin < end <= fix['symbol']['size'] and (not ranges or ranges[-1][1] < begin)
            ranges.append((begin, end))
        assert data[at:at + 16] == bytes(16); at += 16
        lists[start] = dict(symbol=fix['symbol'], ranges=ranges)
    assert at == len(data) and used == set(fixes)
    return lists


def read(path):
    sections, _, relas = elf(path)
    files, sequences = line_table(sections['.debug_line'], relas['.debug_line'])
    _, _, nodes = information(sections['.debug_info'], sections['.debug_abbrev'], relas['.debug_info'], files, sequences)
    return dict(nodes=nodes, sequences=sequences, ranges=range_lists(sections['.debug_ranges'], relas.get('.debug_ranges', [])),
                locations=location_lists(sections['.debug_loc'], relas.get('.debug_loc', [])))


def validate(record, boundaries):
    nodes, sequences, ranges, locations = [record[name] for name in ('nodes', 'sequences', 'ranges', 'locations')]
    used_ranges = set(); used_locations = set(); parents = {}
    for node in nodes.values():
        for child in node['children']: parents[child['offset']] = node
    for node in nodes.values():
        if node['tag'] == 0x0b:
            offset = node['attributes'][0x55]['offset']; used_ranges.add(offset)
            value = ranges[offset]; symbol = value['symbol']; own = value['ranges']
            assert all(symbol['value'] + begin in boundaries and symbol['value'] + end in boundaries for begin, end in own)
            parent = parents[node['offset']]
            if parent['tag'] == 0x0b:
                outer = ranges[parent['attributes'][0x55]['offset']]
                assert outer['symbol'] == symbol and all(any(a <= begin < end <= b for a, b in outer['ranges']) for begin, end in own)
            else: assert parent['tag'] == 0x2e and parent['attributes'][0x11] == symbol
        elif node['tag'] in (5, 0x34) and 2 in node['attributes']:
            offset = node['attributes'][2]['offset']; used_locations.add(offset)
            if node['tag'] == 0x34:
                assert node['offset'] in parents
                assert parents[node['offset']]['tag'] == 0x0b and locations[offset] == []
                assert node['attributes'][0x3b] > 0 and node['attributes'][0x39] > 0
    assert used_ranges == set(ranges) and used_locations == set(locations)
    function = next(node for node in nodes.values() if node['tag'] == 0x2e and node['attributes'].get(3) == 'scope_probe')
    sequence = next(sequence for sequence in sequences if sequence['symbol'] == function['attributes'][0x11])
    source = pathlib.Path('tests/debug/scopes.c').read_text().splitlines()
    observations = []
    for marker, (name, typename) in MARKERS.items():
        line = next(i + 1 for i, text in enumerate(source) if '/* ' + marker + ' */' in text)
        rows = [row for row in sequence['rows'] if row['line'] == line]; assert rows, marker
        for row in rows:
            pc = row['pc'] - sequence['symbol']['value']; current = function
            while True:
                children = [child for child in current['children'] if child['tag'] == 0x0b and any(a <= pc < b for a, b in ranges[child['attributes'][0x55]['offset']]['ranges'])]
                assert len(children) <= 1, (marker, children)
                if not children: break
                current = children[0]
            found = None
            while current is not None:
                candidates = [child for child in current['children'] if child['tag'] == 0x34 and child['attributes'][3] == name]
                assert len(candidates) <= 1
                if candidates: found = candidates[0]; break
                current = parents.get(current['offset'])
            assert found and nodes[found['attributes'][0x49]]['attributes'][3] == typename, (marker, found)
        observations.append(dict(marker=marker, line=line, variable=name, type=typename, rows=len(rows)))
    return observations


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('compiler', type=pathlib.Path)
    parser.add_argument('--execution-profile', choices=('native', 'emulated', 'cross')); args = parser.parse_args()
    compiler = args.compiler.resolve(); identity = digest(compiler)
    linux = platform.system() == 'Linux' and platform.machine() == 'x86_64'
    profile = args.execution_profile or ('native' if linux else 'cross'); execute = profile in ('native', 'emulated'); assert not execute or linux
    source = pathlib.Path('tests/debug/scopes.c')
    files = sorted(pathlib.Path('source').glob('*.[ch]')) + sorted(pathlib.Path('runtime/include').rglob('*.h'))
    files += [source] + [pathlib.Path(name) for name in ('tests/test_debug_scopes.py', 'tests/test_debug_types.py', 'tests/test_debug_lines.py', 'tests/test_unwind.py')]
    inputs = {str(path): digest(path) for path in files}; source_hash = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()
    base = pathlib.Path('.agent-local/debug-scopes') / (source_hash + '-' + identity); base.mkdir(parents=True, exist_ok=True)
    root = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=base)).resolve()
    tools = {name: tool(name) for name in ('llvm-dwarfdump', 'llvm-objdump', 'llvm-mc')}
    tools['gcc'] = tool('gcc-15' if shutil.which('gcc-15') else 'gcc'); tools['clang'] = tool('clang')
    if execute: tools.update({name: tool(name) for name in ('cc', 'gdb')})
    identities = {name: dict(path=path, sha256=digest(pathlib.Path(path))) for name, path in tools.items()}
    artifacts = set(); commands = []; observations = []; boundaries = {}
    def run(argv, label, expected=0):
        result = subprocess.run(list(map(str, argv)), capture_output=True, timeout=60)
        out, err = root / (label + '.stdout'), root / (label + '.stderr'); out.write_bytes(result.stdout); err.write_bytes(result.stderr)
        artifacts.update((out, err)); commands.append(dict(argv=list(map(str, argv)), exit=result.returncode, stdout=digest(out), stderr=digest(err)))
        (root / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        assert result.returncode == expected, (argv, result)
        return result.stdout.decode(errors='replace')
    for name, path in tools.items(): run([path, '--version'], name + '-version')
    for level in ('-O0', '-O2'):
        obj, asm, assembled = [root / ('scopes' + level + suffix) for suffix in ('.o', '.s', '.assembled.o')]
        run([compiler, level, '-g', '-fverify-each', '-c', source, '-o', obj], level + '-compile')
        verification = run([tools['llvm-dwarfdump'], '--verify', obj], level + '-verify'); assert 'warning:' not in verification and 'No errors.' in verification
        disassembly = run([tools['llvm-objdump'], '--no-show-raw-insn', '-d', obj], level + '-disassemble')
        boundaries[level] = {int(m[1], 16) for m in re.finditer(r'^\s*([0-9a-f]+):\s+[a-z]', disassembly, re.M)}
        _, symbols, _ = elf(obj); boundaries[level].update(symbol['value'] + symbol['size'] for symbol in symbols if symbol['kind'] == 2)
        observed = validate(read(obj), boundaries[level]); observations.append(dict(level=level, object_sha256=digest(obj), markers=observed))
        run([compiler, level, '-g', '-S', source, '-o', asm], level + '-assembly')
        run([tools['llvm-mc'], '-triple=x86_64-linux-gnu', '-filetype=obj', asm, '-o', assembled], level + '-assemble')
        original, _, relas = elf(obj); reference, _, other = elf(assembled)
        for name in {'.text'} | {name for name in original if name.startswith('.debug_')}:
            assert original[name] == reference[name] and normalized(relas.get(name, [])) == normalized(other.get(name, [])), name
        artifacts.update((obj, asm, assembled))
        for reference in ('gcc', 'clang'):
            executable = root / (reference + level)
            run([tools[reference], '-std=c17', level, source, '-o', executable], reference + level + '-compile'); run([executable], reference + level + '-execute', 94); artifacts.add(executable)
        if execute:
            executable = root / ('scopes' + level)
            run([tools['cc'], '-no-pie', obj, '-o', executable], level + '-link'); run([executable], level + '-execute', 94); artifacts.add(executable)
            for marker in observed:
                argv = [tools['gdb'], '-nx', '-batch']; expressions = ['set pagination off', 'set disable-randomization off',
                    f'break {source.resolve()}:{marker["line"]}', 'run', 'bt', 'whatis ' + marker['variable'], 'print ' + marker['variable']]
                if marker['marker'] == 'after-shadow': expressions += ['step', 'bt', 'finish']
                for expression in expressions: argv.extend(('-ex', expression))
                recording = run([*argv, executable], level + '-' + marker['marker'] + '-gdb')
                assert 'Breakpoint 1, scope_probe' in recording and re.search(r'#\d+.*\bmain\b', recording)
                assert 'type = ' + marker['type'] + '\n' in recording and re.search(r'\$\d+ = <optimized out>', recording)
                assert 'Cannot access memory' not in recording
                if marker['marker'] == 'after-shadow':
                    assert re.search(r'#0\s+scope_call \(input=2\)', recording)
                    assert re.search(r'#1\s+.*scope_probe', recording) and re.search(r'#2\s+.*main', recording)
                    assert re.search(r'Value returned is \$\d+ = 2\b', recording)
    rejected = []
    good = read(root / 'scopes-O0.o')
    for label in ('scope-extent', 'shadow-type', 'variable-scope', 'stale-location'):
        record = copy.deepcopy(good); nodes = record['nodes']
        locals_ = [node for node in nodes.values() if node['tag'] == 0x34]
        shadow = next(node for node in locals_ if node['attributes'][3] == 'value' and nodes[node['attributes'][0x49]]['attributes'][3] == 'double')
        if label == 'scope-extent':
            owner = next(node for node in nodes.values() if shadow in node['children']); record['ranges'][owner['attributes'][0x55]['offset']]['ranges'] = []
        elif label == 'shadow-type': shadow['attributes'][0x49] = next(node['offset'] for node in nodes.values() if node['tag'] == 0x24 and node['attributes'][3] == 'int')
        elif label == 'variable-scope': next(node for node in nodes.values() if shadow in node['children'])['children'].remove(shadow)
        else: record['locations'][shadow['attributes'][2]['offset']] = [dict(begin=0, end=1, cfa=-24)]
        try: validate(record, boundaries['-O0'])
        except AssertionError: rejected.append(label)
        else: raise AssertionError('reader accepted ' + label)
    assert digest(compiler) == identity and inputs == {name: digest(pathlib.Path(name)) for name in inputs}
    assert all(digest(pathlib.Path(value['path'])) == value['sha256'] for value in identities.values())
    report = dict(schema=1, compiler_sha256=identity, source_sha256=source_hash, inputs=inputs, tools=identities,
                  execution_profile=profile, native=profile == 'native', target_execution=execute, debugger_sessions=12 if execute else 0,
                  observations=observations, rejected_metadata=rejected, commands=commands,
                  artifacts={str(path.relative_to(root)): digest(path) for path in sorted(artifacts)})
    (root / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Debug scopes: six shadow/loop boundaries at O0/O2; four corruptions rejected; native={report["native"]}; evidence={root}')


if __name__ == '__main__': main()
