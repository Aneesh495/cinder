#!/usr/bin/env python3
"""Check target layouts, recursive type references and unavailable parameters."""
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
from test_unwind import digest, elf, leb, normalized, tool


def location_lists(data, relocations):
    fixes = {entry['offset']: entry for entry in relocations}; used = set(); lists = {}; at = 0
    while at < len(data):
        start = at; entries = []
        assert struct.unpack_from('<Q', data, at)[0] == 0xffffffffffffffff and data[at + 8:at + 16] == bytes(8)
        base = fixes[at + 8]; used.add(at + 8)
        assert base['kind'] == 1 and base['addend'] == 0 and base['symbol']['kind'] == 2
        at += 16
        while data[at:at + 16] != bytes(16):
            begin, end = struct.unpack_from('<QQ', data, at)
            assert 0 <= begin < end <= base['symbol']['size']
            at += 16; length = struct.unpack_from('<H', data, at)[0]; at += 2
            expression = data[at:at + length]; at += length
            if len(expression) == 1 and 0x50 <= expression[0] <= 0x6f: location = dict(reg=expression[0] - 0x50)
            else:
                assert expression[0] == 0x91
                offset, stop = leb(expression, 1, True); assert stop == len(expression); location = dict(cfa=offset)
            entries.append(dict(begin=begin, end=end, symbol=base['symbol'], **location))
        assert data[at:at + 16] == bytes(16)
        at += 16; lists[start] = entries
    assert at == len(data) and used == set(fixes)
    return lists


def read(path):
    sections, _, relas = elf(path)
    files, sequences = line_table(sections['.debug_line'], relas['.debug_line'])
    _, _, nodes = information(sections['.debug_info'], sections['.debug_abbrev'], relas['.debug_info'], files, sequences)
    lists = location_lists(sections['.debug_loc'], relas.get('.debug_loc', [])); used = set()
    for function in [node for node in nodes.values() if node['tag'] == 0x2e]:
        symbol = function['attributes'][0x11]
        sequence = next(sequence for sequence in sequences if sequence['symbol'] == symbol)
        prologue = next(row['pc'] - symbol['value'] for row in sequence['rows'] if row['prologue'])
        function['prologue'] = prologue; function['code'] = sections['.text'][symbol['value']:symbol['value'] + prologue].hex()
        pending = list(function['children'])
        while pending:
            parameter = pending.pop(); pending.extend(parameter['children'])
            if parameter['tag'] not in (5, 0x34) or 2 not in parameter['attributes']: continue
            offset = parameter['attributes'][2]['offset']; used.add(offset)
            parameter['locations'] = lists[offset]
            assert all(entry['symbol'] == symbol for entry in lists[offset])
    assert used == set(lists)
    return nodes


def validate(nodes):
    def attributes(node): return node['attributes']
    def named(tag, name): return [node for node in nodes.values() if node['tag'] == tag and attributes(node).get(3) == name]
    def one(tag, name):
        found = named(tag, name); assert len(found) == 1, (tag, name, found)
        return found[0]
    def target(node): return nodes[attributes(node)[0x49]]
    def members(node): return {attributes(child)[3]: child for child in node['children'] if child['tag'] != 0x0b}
    function = one(0x2e, 'debug_types'); params = members(function)
    assert set(params) == {'node', 'state', 'callback', 'byte', 'fraction', 'tail'}
    assert attributes(target(function)) == {3: 'double', 0x0b: 8, 0x3e: 4}
    # Inspect the actual declaration token coordinates, including a split prototype.
    source = pathlib.Path('tests/debug/types.c').read_text().splitlines()
    lines = dict(node=19, state=19, callback=20, byte=20, fraction=20, tail=21)
    for name, parameter in params.items():
        attr = attributes(parameter); line = lines[name]
        assert attr[0x3b] == line and attr[0x39] == re.search(r'\b' + name + r'\b', source[line - 1]).start() + 1
    assert 2 not in attributes(params['byte']), 'assigned parameter must not retain its original ABI value'
    registers = dict(node=5, state=4, callback=1, fraction=17, tail=8)
    moves = dict(node=b'\x48\x89\xf8', state=b'\x48\x89\xf0', callback=b'\x48\x89\xd0', tail=b'\x4c\x89\xc0')
    code = bytes.fromhex(function['code'])
    for name, reg in registers.items():
        entries = params[name]['locations']; assert len(entries) == 2
        assert (entries[0]['begin'], entries[0]['end'], entries[0]['reg']) == (0, function['prologue'], reg)
        assert entries[1]['begin'] == function['prologue'] and entries[1]['end'] == attributes(function)[0x12]
        displacement = struct.pack('<i', entries[1]['cfa'] + 16)
        store = b'\xf2\x0f\x11\x85' + displacement if name == 'fraction' else moves[name] + b'\x48\x89\x85' + displacement
        assert store in code, (name, store.hex(), code.hex())
    pointer = target(params['node']); assert pointer['tag'] == 0x37
    pointer = target(pointer); assert pointer['tag'] == 0xf and attributes(pointer)[0xb] == 8
    qualified = target(pointer); assert qualified['tag'] == 0x35
    node = target(qualified); assert node['tag'] == 0x13 and attributes(node)[3] == 'Node'
    for record in named(0x13, 'Node'):
        assert attributes(record)[0xb] == 56
        assert {name: attributes(member)[0x38] for name, member in members(record).items()} == {'payload': 0, 'next': 48}
    recursive = target(target(members(node)['next']))
    assert target(target(members(recursive)['next']))['offset'] == recursive['offset']
    payload = one(0x13, 'Payload'); assert attributes(payload)[0xb] == 48
    fields = members(payload)
    assert set(fields) == {'tag', 'item', 'flag', 'code', 'values', 'next', 'opaque'}
    assert {name: attributes(fields[name])[0x38] for name in ('tag', 'item', 'values', 'next', 'opaque')} == dict(tag=0, item=8, values=20, next=32, opaque=40)
    assert (attributes(fields['flag'])[0x6b], attributes(fields['flag'])[0x0d]) == (128, 3)
    assert (attributes(fields['code'])[0x6b], attributes(fields['code'])[0x0d]) == (131, 5)
    assert attributes(target(fields['flag']))[0x3e] == 7 and attributes(target(fields['code']))[0x3e] == 5
    array = target(fields['values']); assert array['tag'] == 1 and len(array['children']) == 1
    assert attributes(array['children'][0])[0x37] == 3 and attributes(target(array))[3] == 'int'
    assert attributes(target(array['children'][0])) == {3: 'unsigned long', 0xb: 8, 0x3e: 7}
    union = one(0x17, 'Bits'); assert attributes(union)[0xb] == 8
    assert {name: attributes(member)[0x38] for name, member in members(union).items()} == {'real': 0, 'word': 0}
    opaque = one(0x13, 'Opaque'); assert attributes(opaque) == {3: 'Opaque', 0x3c: 1} and not opaque['children']
    state = one(4, 'State'); assert attributes(state)[0xb] == 4
    assert {attributes(child)[3]: attributes(child)[0x1c] for child in state['children']} == dict(STATE_NEGATIVE=-4, STATE_READY=7, STATE_ALIAS=7)
    callback = target(target(params['callback'])); assert callback['tag'] == 0x15 and attributes(callback)[0x27] == 1
    assert [child['tag'] for child in callback['children']] == [5, 5, 0x18]
    constant = target(target(callback['children'][0])); assert constant['tag'] == 0xf
    assert target(constant)['tag'] == 0x26 and attributes(target(target(constant)))[3] == 'Node'
    assert attributes(target(params['byte'])) == {3: 'unsigned char', 0xb: 1, 0x3e: 8}
    assert attributes(target(params['fraction'])) == {3: 'float', 0xb: 4, 0x3e: 4}
    tail = one(0x13, 'Tail'); assert attributes(tail)[0xb] == 4
    flexible = target(members(tail)['bytes']); assert flexible['tag'] == 1
    assert len(flexible['children']) == 1 and flexible['children'][0]['tag'] == 0x21 and 0x37 not in attributes(flexible['children'][0])
    assert attributes(target(flexible['children'][0])) == {3: 'unsigned long', 0xb: 8, 0x3e: 7}
    return dict(entries=len(nodes), parameters=len(params), record_bytes=dict(Node=56, Payload=48, Tail=4, Bits=8))


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('compiler', type=pathlib.Path)
    parser.add_argument('--execution-profile', choices=('native', 'emulated', 'cross')); args = parser.parse_args()
    compiler = args.compiler.resolve(); identity = digest(compiler)
    linux = platform.system() == 'Linux' and platform.machine() == 'x86_64'
    profile = args.execution_profile or ('native' if linux else 'cross'); execute = profile in ('native', 'emulated')
    assert not execute or linux
    files = sorted(pathlib.Path('source').glob('*.[ch]')) + sorted(pathlib.Path('runtime/include').rglob('*.h'))
    files += [pathlib.Path(name) for name in ('tests/debug/types.c', 'tests/debug_type_probe.c', 'tests/test_debug_types.py', 'tests/test_debug_lines.py', 'tests/test_unwind.py', 'CMakeLists.txt')]
    inputs = {str(path): digest(path) for path in files}; source_hash = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()
    base = pathlib.Path('.agent-local/debug-types') / (source_hash + '-' + identity); base.mkdir(parents=True, exist_ok=True)
    root = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=base)).resolve()
    tools = {name: tool(name) for name in ('llvm-dwarfdump', 'llvm-mc')}
    tools['gcc'] = tool('gcc-15' if shutil.which('gcc-15') else 'gcc'); tools['clang'] = tool('clang')
    if execute: tools.update({name: tool(name) for name in ('cc', 'gdb')})
    tools_before = {name: dict(path=path, sha256=digest(pathlib.Path(path))) for name, path in tools.items()}
    artifacts = set(); commands = []; observations = []
    def run(argv, label, expected=0):
        result = subprocess.run(list(map(str, argv)), capture_output=True, timeout=60)
        out, err = root / (label + '.stdout'), root / (label + '.stderr'); out.write_bytes(result.stdout); err.write_bytes(result.stderr)
        artifacts.update((out, err)); commands.append(dict(argv=list(map(str, argv)), exit=result.returncode, stdout=digest(out), stderr=digest(err)))
        (root / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        assert result.returncode == expected, (argv, result)
        return result.stdout.decode(errors='replace')
    for name, path in tools.items(): run([path, '--version'], name + '-version')
    source = pathlib.Path('tests/debug/types.c')
    for level in ('-O0', '-O2'):
        for reference in ('gcc', 'clang'):
            executable = root / (reference + level)
            run([tools[reference], '-std=c17', level, source, '-o', executable], reference + level + '-compile')
            run([executable], reference + level + '-execute', 15); artifacts.add(executable)
        obj, asm, assembled = [root / ('types' + level + suffix) for suffix in ('.o', '.s', '.assembled.o')]
        run([compiler, level, '-g', '-fverify-each', '-c', source, '-o', obj], level + '-compile')
        verification = run([tools['llvm-dwarfdump'], '--verify', obj], level + '-verify')
        assert 'warning:' not in verification and 'No errors.' in verification
        run([tools['llvm-dwarfdump'], '--debug-info', obj], level + '-types')
        observation = validate(read(obj)); observation.update(level=level, object_sha256=digest(obj)); observations.append(observation)
        run([compiler, level, '-g', '-S', source, '-o', asm], level + '-assembly')
        run([tools['llvm-mc'], '-triple=x86_64-linux-gnu', '-filetype=obj', asm, '-o', assembled], level + '-assemble')
        original, _, relas = elf(obj); reference, _, other = elf(assembled)
        for name in {'.text'} | {name for name in original if name.startswith('.debug_')}:
            assert original[name] == reference[name] and normalized(relas.get(name, [])) == normalized(other.get(name, [])), name
        artifacts.update((obj, asm, assembled))
        if execute:
            executable = root / ('types' + level)
            run([tools['cc'], '-no-pie', obj, '-o', executable], level + '-link'); run([executable], level + '-execute', 15)
            recording = run([tools['gdb'], '-nx', '-batch', '-ex', 'set pagination off', '-ex', 'set disable-randomization off',
                '-ex', 'break debug_types', '-ex', 'run', '-ex', 'bt', '-ex', 'ptype struct Node', '-ex', 'ptype struct Payload', '-ex', 'ptype union Bits',
                '-ex', 'ptype enum State', '-ex', 'ptype debug_types', '-ex', 'info args', '-ex', 'print state', '-ex', 'print tail',
                '-ex', 'print node->payload.values[1]', '-ex', 'print node->payload.item.real', executable], level + '-gdb')
            assert 'Breakpoint 1, debug_types' in recording and re.search(r'#\d+.*\bmain\b', recording)
            assert all(text in recording for text in ('STATE_NEGATIVE = -4', 'STATE_READY = 7', 'STATE_ALIAS = 7', 'values[3]', 'volatile struct Node'))
            assert 'byte = <optimized out>' in recording and 'state = STATE_READY' in recording and 'fraction = 0.5' in recording
            assert re.search(r'node = 0x[0-9a-f]+', recording) and 'tail = 0x0' in recording and 'callback = 0x0' in recording
            assert re.search(r'\$\d+ = STATE_READY', recording) and re.search(r'\$\d+ = \(struct Tail \*\) 0x0', recording)
            assert re.search(r'\$\d+ = 3\b', recording) and re.search(r'\$\d+ = 1\.5\b', recording)
            assert 'Cannot access memory' not in recording, recording
            artifacts.add(executable)
    # Demonstrate that the independent layout assertions reject wrong metadata.
    good = read(root / 'types-O0.o'); mutations = []
    for label in ('member-offset', 'bit-offset', 'enumerator', 'array-count', 'unsigned-encoding', 'callback-variadic'):
        changed = copy.deepcopy(good)
        named = lambda name: next(node for node in changed.values() if node['attributes'].get(3) == name)
        if label == 'member-offset': named('item')['attributes'][0x38] = 9
        elif label == 'bit-offset': named('flag')['attributes'][0x6b] = 129
        elif label == 'enumerator': named('STATE_NEGATIVE')['attributes'][0x1c] = 4
        elif label == 'array-count': next(node for node in changed.values() if node['tag'] == 0x21 and 0x37 in node['attributes'])['attributes'][0x37] = 4
        elif label == 'unsigned-encoding': named('unsigned char')['attributes'][0x3e] = 6
        else: next(node for node in changed.values() if node['tag'] == 0x15)['children'].pop()
        try: validate(changed)
        except AssertionError: mutations.append(label)
        else: raise AssertionError('independent reader accepted ' + label)
    if execute:
        probe, probe_obj = root / 'debug-type-probe', root / 'debug-type-probe.o'
        run([compiler, '-O0', '-c', '-Isource', '-Iruntime/include', 'tests/debug_type_probe.c', '-o', probe_obj], 'probe-compile')
        core = [compiler.parent / 'libcinder_core.a'] if (compiler.parent / 'libcinder_core.a').is_file() else sorted(p for p in compiler.parent.glob('*.o') if p.name not in ('main.o', 'ir_main.o'))
        assert core
        linker, flags = tools['cc'], []; cache = compiler.parent / 'CMakeCache.txt'
        if cache.is_file() and 'CINDER_SANITIZE:BOOL=ON' in cache.read_text().splitlines():
            linker = next(line.split('=', 1)[1] for line in cache.read_text().splitlines() if line.startswith('CMAKE_C_COMPILER:'))
            flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        run([linker, '-no-pie', *flags, probe_obj, *core, '-o', probe], 'probe-link'); artifacts.update((probe, probe_obj))
    else:
        probe = compiler.parent / 'cinder_debug_type_probe'; assert probe.is_file(), 'build cinder_debug_type_probe first'
    probe_hash = digest(probe); retired = root / 'retired-types.o'
    recording = run([probe, source, retired], 'probe-execute')
    assert recording == 'Debug types: AST and HIR retired before construction; IR, types and sources retired before ELF publication\n'
    run([tools['llvm-dwarfdump'], '--verify', retired], 'probe-verify'); validate(read(retired)); artifacts.add(retired)
    assert digest(probe) == probe_hash and digest(compiler) == identity and inputs == {name: digest(pathlib.Path(name)) for name in inputs}
    assert all(digest(pathlib.Path(value['path'])) == value['sha256'] for value in tools_before.values())
    report = dict(schema=1, compiler_sha256=identity, source_sha256=source_hash, inputs=inputs, tools=tools_before,
                  execution_profile=profile, native=profile == 'native', target_execution=execute, debugger_sessions=2 if execute else 0,
                  observations=observations, rejected_metadata=mutations, api_probe=dict(path=str(probe), sha256=probe_hash),
                  commands=commands, artifacts={str(path.relative_to(root)): digest(path) for path in sorted(artifacts)})
    (root / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Debug types: target layouts and recursive types checked at O0/O2; six metadata corruptions rejected; native={report["native"]}; evidence={root}')


if __name__ == '__main__': main()
