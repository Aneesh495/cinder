#!/usr/bin/env python3
"""Independently decode source tables, DIEs and original address fixups."""
import argparse
import hashlib
import json
import os
import pathlib
import platform
import re
import struct
import subprocess
import tempfile

from test_unwind import digest, elf, leb, normalized, tool


def cstring(data, at):
    end = data.index(0, at)
    return data[at:end].decode(), end + 1


def line_table(data, relocations):
    length, version, header_length = struct.unpack_from('<IHI', data)
    assert length + 4 == len(data) and version == 4
    end_header = 10 + header_length
    minimum, operations, default, base, span, opcode_base = struct.unpack_from('<BBBbBB', data, 10)
    assert (minimum, operations, default, base, span, opcode_base) == (1, 1, 1, -5, 14, 13)
    assert data[16:28] == bytes((0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1))
    at = 28
    directories = []
    while data[at]:
        name, at = cstring(data, at); directories.append(name)
    at += 1; files = []
    while data[at]:
        name, at = cstring(data, at)
        directory, at = leb(data, at); modified, at = leb(data, at); size, at = leb(data, at)
        assert directory == modified == size == 0
        files.append(name)
    at += 1
    assert at == end_header and not directories and files
    fixups = {r['offset']: r for r in relocations}
    assert len(fixups) == len(relocations)
    used = set(); rows = []; sequences = []; symbol = None
    pc = 0; file = 1; line = 1; column = 0; statement = bool(default); prologue = False
    while at < len(data):
        opcode = data[at]; at += 1
        if opcode == 0:
            extent, at = leb(data, at); stop = at + extent
            assert extent > 0 and stop <= len(data)
            extended = data[at]; at += 1
            if extended == 2:
                assert extent == 9 and symbol is None and data[at:stop] == bytes(8)
                fix = fixups[at]; used.add(at)
                assert fix['kind'] == 1 and fix['addend'] == 0 and fix['symbol']['kind'] == 2
                symbol = fix['symbol']; pc = symbol['value']; at += 8
            elif extended == 1:
                assert extent == 1 and symbol and rows and pc == symbol['value'] + symbol['size']
                sequences.append(dict(symbol=symbol, rows=rows, end=pc))
                symbol = None; rows = []; pc = 0; file = 1; line = 1; column = 0
                statement = bool(default); prologue = False
            else: raise AssertionError(('unsupported extended line opcode', extended))
            assert at == stop
        elif opcode == 1:
            assert symbol and 1 <= file <= len(files) and line > 0 and column > 0
            assert symbol['value'] <= pc < symbol['value'] + symbol['size']
            assert not rows or pc > rows[-1]['pc']
            rows.append(dict(pc=pc, file=files[file - 1], line=line, column=column,
                             statement=statement, prologue=prologue))
            prologue = False
        elif opcode == 2:
            delta, at = leb(data, at); pc += delta * minimum
        elif opcode == 3:
            delta, at = leb(data, at, True); line += delta
        elif opcode == 4: file, at = leb(data, at)
        elif opcode == 5: column, at = leb(data, at)
        elif opcode == 6: statement = not statement
        elif opcode == 10: prologue = True
        else: raise AssertionError(('unsupported line opcode', opcode))
    assert symbol is None and used == set(fixups)
    return files, sequences


def information(data, abbreviations, relocations, files, sequences):
    length, version, abbreviation, address_size = struct.unpack_from('<IHI B', data)
    assert length + 4 == len(data) and (version, abbreviation, address_size) == (4, 0, 8)
    definitions = {}; at = 0
    while True:
        code, at = leb(abbreviations, at)
        if not code: break
        assert code not in definitions
        tag, at = leb(abbreviations, at); children = abbreviations[at]; at += 1
        attributes = []
        while True:
            name, at = leb(abbreviations, at); form, at = leb(abbreviations, at)
            if name == form == 0: break
            assert name and form and name not in [a[0] for a in attributes]
            attributes.append((name, form))
        definitions[code] = (tag, children, attributes)
    assert at == len(abbreviations)
    fixups = {r['offset']: r for r in relocations}; used = {6}
    assert fixups[6]['kind'] == 10 and fixups[6]['symbol']['section'] == '.debug_abbrev' and fixups[6]['addend'] == 0
    references = []
    def entry(at):
        code, at = leb(data, at)
        tag, children, attributes = definitions[code]; values = {}
        for name, form in attributes:
            offset = at
            if form == 8: value, at = cstring(data, at)
            elif form == 15: value, at = leb(data, at)
            elif form in (1, 7): value = struct.unpack_from('<Q', data, at)[0]; at += 8
            elif form == 5: value = struct.unpack_from('<H', data, at)[0]; at += 2
            elif form == 11: value = data[at]; at += 1
            elif form == 12: value = data[at]; at += 1
            elif form == 13: value, at = leb(data, at, True)
            elif form == 19:
                value = struct.unpack_from('<I', data, at)[0]; at += 4; references.append(value)
            elif form == 23: value = struct.unpack_from('<I', data, at)[0]; at += 4
            elif form == 24:
                extent, at = leb(data, at); value = data[at:at + extent]; at += extent
            else: raise AssertionError(('unsupported attribute form', form))
            if name in (0x10, 0x11):
                fix = fixups[offset]; used.add(offset)
                assert value == fix['addend'] == 0
                if name == 0x10: assert form == 23 and fix['kind'] == 10 and fix['symbol']['section'] == '.debug_line'
                else: assert form == 1 and fix['kind'] == 1 and fix['symbol']['kind'] == 2
                value = fix['symbol']
            elif name == 2 and form == 23:
                fix = fixups[offset]; used.add(offset)
                assert value == 0 and fix['kind'] == 10 and fix['symbol']['section'] == '.debug_loc' and fix['addend'] >= 0
                value = dict(section='.debug_loc', offset=fix['addend'])
            values[name] = value
        return at, tag, children, values
    at, tag, children, unit = entry(11)
    assert tag == 0x11 and children == 1 and unit[0x25].startswith('Cinder ') and unit[0x13] == 12
    assert unit[3] == files[0] and pathlib.PurePosixPath(unit[0x1b]).is_absolute()
    nodes = {}
    def children_at(at, depth=0):
        assert depth < 1024
        children = []
        while data[at]:
            offset = at; at, tag, has_children, values = entry(at)
            child_nodes = []
            if has_children: at, child_nodes = children_at(at, depth + 1)
            node = dict(offset=offset, tag=tag, attributes=values, children=child_nodes)
            assert offset not in nodes
            nodes[offset] = node; children.append(node)
        return at + 1, children
    at, entries = children_at(at)
    assert at == len(data) and all(reference in nodes for reference in references)
    functions = []
    for node in entries:
        if node['tag'] != 0x2e: continue
        function = node['attributes']
        assert function[0x40] == b'\x9c' and function[0x27] == 1
        assert function[0x3f] in (0, 1) and 1 <= function[0x3a] <= len(files)
        assert function[0x3b] > 0 and function[0x39] > 0
        symbol = function[0x11]
        assert function[3] == symbol['name'] and function[0x12] == symbol['size']
        parameters = []
        for child in node['children']:
            assert child['tag'] in (5, 0x18)
            if child['tag'] == 5:
                parameter = child['attributes']
                assert 2 not in parameter or parameter[2]['section'] == '.debug_loc'
                assert 1 <= parameter[0x3a] <= len(files) and parameter[0x3b] > 0 and parameter[0x39] > 0
                parameters.append(dict(name=parameter[3], type=parameter[0x49], location=parameter.get(2)))
        functions.append(dict(name=function[3], file=files[function[0x3a] - 1], line=function[0x3b],
                              column=function[0x39], external=bool(function[0x3f]), parameters=parameters))
    assert used == set(fixups)
    assert [f['name'] for f in functions] == [s['symbol']['name'] for s in sequences]
    return unit, functions, nodes


def validate(path, disassembly):
    sections, symbols, relas = elf(path)
    files, sequences = line_table(sections['.debug_line'], relas.get('.debug_line', []))
    unit, functions, _ = information(sections['.debug_info'], sections['.debug_abbrev'], relas['.debug_info'], files, sequences)
    boundaries = {int(m[1], 16) for m in re.finditer(r'^\s*([0-9a-f]+):\s+[a-z]', disassembly, re.M)}
    expected = [s for s in symbols if s['kind'] == 2 and s['section'] == '.text']
    assert [s['symbol'] for s in sequences] == expected
    for sequence in sequences:
        rows = sequence['rows']; symbol = sequence['symbol']
        assert rows[0]['pc'] == symbol['value']
        assert all(row['pc'] in boundaries for row in rows)
        assert sum(row['prologue'] for row in rows) == 1
        prologue = next(row for row in rows if row['prologue'])
        assert prologue['statement'] and prologue['pc'] > symbol['value'] + 4
    return dict(unit_name=unit[3], directory=unit[0x1b], files=files, functions=functions, sequences=sequences)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('compiler', type=pathlib.Path)
    parser.add_argument('--execution-profile', choices=('native', 'emulated', 'cross'))
    args = parser.parse_args(); compiler = args.compiler.resolve(); compiler_hash = digest(compiler)
    linux = platform.system() == 'Linux' and platform.machine() == 'x86_64'
    profile = args.execution_profile or ('native' if linux else 'cross')
    execute = profile in ('native', 'emulated')
    assert not execute or linux, 'target execution requires Linux x86-64'
    names = ('unwind.c', 'unwind_provider.c', 'unwind_caller.c', 'line-origins.c', 'line-empty.c', 'line-minimal.c')
    sources = [pathlib.Path('tests/debug') / name for name in names]
    input_files = sorted(pathlib.Path('source').glob('*.[ch]')) + sorted(pathlib.Path('runtime/include').rglob('*.h'))
    input_files += sorted(pathlib.Path('tests/debug').glob('*.[ch]'))
    input_files += [pathlib.Path(__file__), pathlib.Path('tests/test_unwind.py'), pathlib.Path('tests/debug_line_probe.c'), pathlib.Path('CMakeLists.txt')]
    inputs = {str(path): digest(path) for path in input_files}
    source_hash = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()
    base = pathlib.Path('.agent-local/debug-lines') / (source_hash + '-' + compiler_hash)
    base.mkdir(parents=True, exist_ok=True); root = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=base)).resolve()
    tools = {name: tool(name) for name in ('llvm-dwarfdump', 'llvm-objdump', 'llvm-mc')}
    if execute: tools.update({name: tool(name) for name in ('cc', 'gdb')})
    identities = {name: dict(path=path, sha256=digest(pathlib.Path(path))) for name, path in tools.items()}
    commands = []; observations = []; artifacts = set()
    environment = dict(os.environ, SOURCE_DATE_EPOCH='0')
    def run(argv, stem, expected=0, cwd=None):
        result = subprocess.run(list(map(str, argv)), cwd=cwd, env=environment, capture_output=True, timeout=60)
        out, err = root / (stem + '.stdout'), root / (stem + '.stderr')
        out.write_bytes(result.stdout); err.write_bytes(result.stderr); artifacts.update((out, err))
        commands.append(dict(argv=list(map(str, argv)), cwd=str(cwd or pathlib.Path.cwd()), exit=result.returncode,
                             stdout=digest(out), stderr=digest(err)))
        (root / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        assert result.returncode == expected, (argv, result)
        return result.stdout.decode(errors='replace')
    def check(obj, stem):
        disassembly = run([tools['llvm-objdump'], '--no-show-raw-insn', '-d', obj], stem + '-disassembly')
        run([tools['llvm-dwarfdump'], '--verify', obj], stem + '-verify')
        run([tools['llvm-dwarfdump'], '--debug-info', '--debug-line', obj], stem + '-dump')
        return validate(obj, disassembly)
    for name, path in tools.items(): run([path, '--version'], name + '-version')
    objects = {}
    for level in ('-O0', '-O2'):
        for source in sources:
            stem = source.stem + level
            obj, asm, assembled, plain = [root / (stem + suffix) for suffix in ('.o', '.s', '.assembled.o', '.plain.o')]
            run([compiler, level, '-g', '-fverify-each', '-c', source, '-o', obj], stem + '-compile')
            observation = check(obj, stem); observation.update(source=str(source), level=level, object_sha256=digest(obj))
            run([compiler, level, '-g', '-S', source, '-o', asm], stem + '-assembly')
            run([tools['llvm-mc'], '-triple=x86_64-linux-gnu', '-filetype=obj', asm, '-o', assembled], stem + '-assemble')
            original, _, relas = elf(obj); reference, _, other_relas = elf(assembled)
            for name in {'.text'} | {name for name in original if name.startswith('.debug_')}:
                assert original[name] == reference[name], name
                assert normalized(relas.get(name, [])) == normalized(other_relas.get(name, [])), name
            run([compiler, level, '-c', source, '-o', plain], stem + '-plain')
            without, _, _ = elf(plain)
            assert not any(name.startswith('.debug_') for name in without) and without['.text'] == original['.text']
            if source.name == 'line-origins.c':
                assert [(f['name'], f['file'], f['line']) for f in observation['functions']] == [
                    ('header_value', 'logical-header.c', 90), ('adjusted', 'logical-source.c', 70), ('main', 'other-source.c', 8)]
                assert [f['external'] for f in observation['functions']] == [False, False, True]
                for sequence, file, line in zip(observation['sequences'], ('logical-header.c', 'logical-source.c', 'other-source.c'), (91, 71, 9)):
                    assert any(row['file'] == file and row['line'] == line for row in sequence['rows'])
                    assert all(row['file'] == file for row in sequence['rows'])
            if source.name == 'line-empty.c': assert not observation['functions'] and not observation['sequences']
            observations.append(observation); artifacts.update((obj, asm, assembled, plain)); objects[(level, source.name)] = obj
        if execute:
            for label, files, expected, target, expected_file, expected_line in (
                ('single', ['unwind.c'], 24, 'unwind.c:3', 'unwind.c', 4),
                ('multi', ['unwind_provider.c', 'unwind_caller.c'], 11, 'unwind_provider.c:2', 'unwind_provider.c', 3),
                ('origins', ['line-origins.c'], 10, 'logical-source.c:71', 'logical-source.c', 72)):
                stem = label + level; executable = root / stem
                run([tools['cc'], '-no-pie', *(objects[(level, name)] for name in files), '-o', executable], stem + '-link')
                run([executable], stem + '-execute', expected)
                recording = run([tools['gdb'], '-nx', '-batch', '-ex', 'set pagination off', '-ex', 'set disable-randomization off',
                    '-ex', 'break ' + target, '-ex', 'run', '-ex', 'bt', '-ex', 'next', '-ex', 'frame', '-ex', 'info line', executable], stem + '-gdb')
                assert 'Breakpoint 1,' in recording and re.search(r'#\d+.*\bmain\b', recording), recording
                assert re.search(re.escape(expected_file) + ':' + str(expected_line) + r'\b', recording), recording
                artifacts.add(executable)
    # Compile the same absolute source names from different checkout directories.
    # Both CU comp_dir and file table must obey file mapping; dependencies stay physical.
    trees = []
    for index in (1, 2):
        tree = root / ('checkout ' + str(index)); tree.mkdir(); trees.append(tree)
        header, source = tree / 'origin.h', tree / 'unit.c'
        header.write_text('static int header(int x) { return x + 2; }\n')
        source.write_text('#include "origin.h"\nint main(void) { return header(5); }\n')
        artifacts.update((header, source))
        for level in ('-O0', '-O2'):
            stem = f'mapped-{index}' + level; obj = tree / (level + '.o')
            run([compiler, level, '-g', '-c', f'-ffile-prefix-map={tree}=/src', source, '-o', obj], stem + '-compile', cwd=tree)
            observation = check(obj, stem)
            assert observation['directory'] == '/src' and observation['unit_name'] == '/src/unit.c'
            assert set(observation['files']) == {'/src/unit.c', '/src/origin.h'}
            assert str(tree).encode() not in obj.read_bytes()
            artifacts.add(obj); observations.append(dict(mapping=stem, **observation))
        dependencies = run([compiler, '-M', f'-ffile-prefix-map={tree}=/src', source], f'dependencies-{index}', cwd=tree)
        assert '/src/' not in dependencies and str(source).replace(' ', '\\ ') in dependencies and str(header).replace(' ', '\\ ') in dependencies
    for level in ('-O0', '-O2'): assert (trees[0] / (level + '.o')).read_bytes() == (trees[1] / (level + '.o')).read_bytes()
    for label, flags, mapped in (
        ('macro', [f'-fmacro-prefix-map={trees[0]}=/macro'], str(trees[0])),
        ('last-file', [f'-ffile-prefix-map={trees[0]}=/first', f'-ffile-prefix-map={trees[0]}=/last'], '/last'),
        ('file-macro', [f'-ffile-prefix-map={trees[0]}=/file', f'-fmacro-prefix-map={trees[0]}=/macro'], '/file')):
        obj = root / (label + '.o')
        run([compiler, '-O0', '-g', '-c', *flags, trees[0] / 'unit.c', '-o', obj], label + '-compile', cwd=trees[0])
        observation = check(obj, label)
        assert observation['directory'] == mapped and observation['unit_name'] == mapped + '/unit.c'
        assert set(observation['files']) == {mapped + '/unit.c', mapped + '/origin.h'}
        artifacts.add(obj); observations.append(dict(mapping=label, **observation))
    if execute:
        probe, probe_object = root / 'debug-line-probe', root / 'debug-line-probe.o'
        run([compiler, '-O0', '-c', '-Isource', '-Iruntime/include', 'tests/debug_line_probe.c', '-o', probe_object], 'probe-compile')
        core = [compiler.parent / 'libcinder_core.a'] if (compiler.parent / 'libcinder_core.a').is_file() else sorted(p for p in compiler.parent.glob('*.o') if p.name not in ('main.o', 'ir_main.o'))
        assert core, 'missing actual core objects for source-map probe'
        linker, flags = tools['cc'], []
        cache = compiler.parent / 'CMakeCache.txt'
        if cache.is_file() and 'CINDER_SANITIZE:BOOL=ON' in cache.read_text().splitlines():
            linker = next(line.split('=', 1)[1] for line in cache.read_text().splitlines() if line.startswith('CMAKE_C_COMPILER:'))
            flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        run([linker, '-no-pie', *flags, probe_object, *core, '-o', probe], 'probe-link')
        artifacts.update((probe, probe_object))
    else:
        probe = compiler.parent / 'cinder_debug_line_probe'
        assert probe.is_file(), 'build the cinder_debug_line_probe target first'
    probe_hash = digest(probe)
    recording = run([probe, sources[0]], 'probe-execute')
    assert recording == 'Source debug: twenty-two malformed maps and repeated finalization rejected before mutation\n'
    assert digest(probe) == probe_hash
    assert digest(compiler) == compiler_hash and inputs == {name: digest(pathlib.Path(name)) for name in inputs}
    assert all(digest(pathlib.Path(value['path'])) == value['sha256'] for value in identities.values())
    report = dict(schema=1, compiler_sha256=compiler_hash, source_sha256=source_hash, inputs=inputs, tools=identities,
                  execution_profile=profile, native=profile == 'native', target_execution=execute, source_date_epoch=0,
                  observations=observations, commands=commands, debugger_sessions=6 if execute else 0,
                  api_probe=dict(path=str(probe), sha256=probe_hash), rejected_maps=22, rejected_repeated_finalization=True,
                  artifacts={str(path.relative_to(root)): digest(path) for path in sorted(artifacts)})
    (root / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Source debug: {len(observations)} tables; debugger sessions={report["debugger_sessions"]}; native={report["native"]}; evidence={root}')


if __name__ == '__main__': main()
