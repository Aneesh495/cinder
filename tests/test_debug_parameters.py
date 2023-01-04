#!/usr/bin/env python3
"""Check register exhaustion, incoming stack homes and debugger call transitions."""
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

from test_debug_types import read
from test_unwind import digest, elf, normalized, tool


def validate(nodes):
    function = next(node for node in nodes.values() if node['tag'] == 0x2e and node['attributes'].get(3) == 'debug_stack')
    parameters = {node['attributes'][3]: node for node in function['children']}
    registers = dict(zip(('a', 'b', 'c', 'd', 'e', 'f'), (5, 4, 1, 2, 8, 9)))
    registers.update({f'x{i}': 17 + i for i in range(8)})
    stack = dict(g=0, p=8, x8=16)
    assert set(parameters) == registers.keys() | stack.keys()
    extent = function['attributes'][0x12]; prologue = function['prologue']
    code = bytes.fromhex(function['code']); homes = set()
    moves = dict(a=b'\x48\x89\xf8', b=b'\x48\x89\xf0', c=b'\x48\x89\xd0',
                 d=b'\x48\x89\xc8', e=b'\x4c\x89\xc0', f=b'\x4c\x89\xc8')
    for name, parameter in parameters.items():
        entries = parameter['locations']
        assert all(entry['symbol']['name'] == 'debug_stack' for entry in entries)
        if name in stack:
            assert len(entries) == 1 and entries[0]['begin'] == 0 and entries[0]['end'] == extent
            assert entries[0]['cfa'] == stack[name] and 'reg' not in entries[0]
        else:
            assert len(entries) == 2
            assert (entries[0]['begin'], entries[0]['end'], entries[0]['reg']) == (0, prologue, registers[name])
            assert entries[1]['begin'] == prologue and entries[1]['end'] == extent and 'reg' not in entries[1]
            home = entries[1]['cfa']; assert home < -16 and home not in homes; homes.add(home)
            displacement = struct.pack('<i', home + 16)
            if name.startswith('x'):
                store = b'\xf2\x0f\x11' + bytes((0x85 + 8 * int(name[1:]),)) + displacement
            else: store = moves[name] + b'\x48\x89\x85' + displacement
            assert store in code, (name, store.hex(), code.hex())
    return dict(parameters=len(parameters), register_parameters=len(registers), stack_parameters=stack)


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('compiler', type=pathlib.Path)
    parser.add_argument('--execution-profile', choices=('native', 'emulated', 'cross')); args = parser.parse_args()
    compiler = args.compiler.resolve(); identity = digest(compiler)
    linux = platform.system() == 'Linux' and platform.machine() == 'x86_64'
    profile = args.execution_profile or ('native' if linux else 'cross'); execute = profile in ('native', 'emulated')
    assert not execute or linux
    sources = [pathlib.Path('tests/debug') / name for name in ('parameters.c', 'parameters_main.c')]
    files = sorted(pathlib.Path('source').glob('*.[ch]')) + sorted(pathlib.Path('runtime/include').rglob('*.h'))
    files += sources + [pathlib.Path(name) for name in ('tests/debug/parameters.h', 'tests/test_debug_parameters.py', 'tests/test_debug_types.py', 'tests/test_debug_lines.py', 'tests/test_unwind.py')]
    inputs = {str(path): digest(path) for path in files}; source_hash = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()
    base = pathlib.Path('.agent-local/debug-parameters') / (source_hash + '-' + identity); base.mkdir(parents=True, exist_ok=True)
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
    for level in ('-O0', '-O2'):
        objects = []
        for source in sources:
            stem = source.stem + level; obj, asm, assembled = [root / (stem + suffix) for suffix in ('.o', '.s', '.assembled.o')]
            run([compiler, level, '-g', '-fverify-each', '-c', source, '-o', obj], stem + '-compile')
            verification = run([tools['llvm-dwarfdump'], '--verify', obj], stem + '-verify')
            assert 'warning:' not in verification and 'No errors.' in verification
            run([compiler, level, '-g', '-S', source, '-o', asm], stem + '-assembly')
            run([tools['llvm-mc'], '-triple=x86_64-linux-gnu', '-filetype=obj', asm, '-o', assembled], stem + '-assemble')
            original, _, relas = elf(obj); reference, _, other = elf(assembled)
            for name in {'.text'} | {name for name in original if name.startswith('.debug_')}:
                assert original[name] == reference[name] and normalized(relas.get(name, [])) == normalized(other.get(name, [])), name
            artifacts.update((obj, asm, assembled)); objects.append(obj)
        observation = validate(read(objects[0])); observation.update(level=level, object_sha256=digest(objects[0])); observations.append(observation)
        for reference in ('gcc', 'clang'):
            executable = root / (reference + level)
            run([tools[reference], '-std=c17', level, *sources, '-o', executable], reference + level + '-compile')
            run([executable], reference + level + '-execute', 81); artifacts.add(executable)
            if execute:
                reference_objects = []
                for source in sources:
                    obj = root / (reference + source.stem + level + '.o')
                    run([tools[reference], '-std=c17', level, '-c', source, '-o', obj], obj.stem + '-compile')
                    artifacts.add(obj); reference_objects.append(obj)
                for direction in range(2):
                    mixed = root / (reference + level + '-mixed-' + str(direction))
                    pair = (objects[0], reference_objects[1]) if direction == 0 else (reference_objects[0], objects[1])
                    run([tools['cc'], '-no-pie', *pair, '-o', mixed], mixed.name + '-link'); run([mixed], mixed.name + '-execute', 81); artifacts.add(mixed)
        if execute:
            executable = root / ('parameters' + level)
            run([tools['cc'], '-no-pie', *objects, '-o', executable], level + '-link'); run([executable], level + '-execute', 81)
            argv = [tools['gdb'], '-nx', '-batch']; expressions = ['set pagination off', 'set disable-randomization off', 'break debug_stack', 'run', 'echo CINDER_BEFORE_CALL\n', 'info args',
                'print *p', 'break debug_stack_call', 'continue', 'bt', 'finish', 'echo CINDER_AFTER_CALL\n', 'info args', 'print *p', 'print g', 'print x8']
            for expression in expressions: argv.extend(('-ex', expression))
            recording = run([*argv, executable], level + '-gdb')
            assert 'Breakpoint 1, debug_stack' in recording and 'Breakpoint 2, debug_stack_call' in recording
            assert re.search(r'#1\s+.*debug_stack', recording) and re.search(r'#2\s+.*main', recording)
            before, after = recording.split('CINDER_AFTER_CALL\n')
            before = before.split('CINDER_BEFORE_CALL\n')[1].split('Breakpoint 2,')[0]
            for segment in (before, after):
                for name, value in dict(a=1, b=2, c=3, d=4, e=5, f=6, g=7, **{f'x{i}': i + 1 for i in range(9)}).items():
                    assert re.search(r'^' + name + r' = ' + str(value) + r'\b', segment, re.M), (name, segment)
            pointers = [re.search(r'^p = (0x[0-9a-f]+)\b', segment, re.M).group(1) for segment in (before, after)]
            assert pointers[0] == pointers[1] and pointers[0] != '0x0'
            assert len(re.findall(r'\$\d+ = 8\b', recording)) == 2 and re.search(r'\$\d+ = 7\b', recording) and re.search(r'\$\d+ = 9\b', recording)
            assert '<optimized out>' not in recording and 'Cannot access memory' not in recording
            artifacts.add(executable)
    rejected = []
    for label in ('register', 'stack-offset', 'prologue', 'extent', 'home', 'symbol'):
        nodes = copy.deepcopy(read(root / 'parameters-O0.o'))
        function = next(node for node in nodes.values() if node['tag'] == 0x2e)
        parameters = {node['attributes'][3]: node for node in function['children']}
        if label == 'register': parameters['a']['locations'][0]['reg'] = 4
        elif label == 'stack-offset': parameters['p']['locations'][0]['cfa'] = 0
        elif label == 'prologue': parameters['x0']['locations'][1]['begin'] += 1
        elif label == 'extent': parameters['g']['locations'][0]['end'] -= 1
        elif label == 'home': parameters['f']['locations'][1]['cfa'] = parameters['e']['locations'][1]['cfa']
        else: parameters['g']['locations'][0]['symbol']['name'] = 'main'
        try: validate(nodes)
        except AssertionError: rejected.append(label)
        else: raise AssertionError('independent reader accepted ' + label)
    assert digest(compiler) == identity and inputs == {name: digest(pathlib.Path(name)) for name in inputs}
    assert all(digest(pathlib.Path(value['path'])) == value['sha256'] for value in tools_before.values())
    report = dict(schema=1, compiler_sha256=identity, source_sha256=source_hash, inputs=inputs, tools=tools_before,
                  execution_profile=profile, native=profile == 'native', target_execution=execute, debugger_sessions=2 if execute else 0,
                  observations=observations, rejected_metadata=rejected, commands=commands,
                  artifacts={str(path.relative_to(root)): digest(path) for path in sorted(artifacts)})
    (root / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Debug parameters: 17 arguments across register/stack exhaustion at O0/O2; six corruptions rejected; native={report["native"]}; evidence={root}')


if __name__ == '__main__': main()
