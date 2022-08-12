#!/usr/bin/env python3
"""Check canonical IR through independent process boundaries and the backend."""
import hashlib
import json
import pathlib
import platform
import subprocess
import sys
from native_profile import configure_stack

compiler = pathlib.Path(sys.argv[1]).resolve()
irtool = compiler.parent / 'cinderir'
identity = hashlib.sha256(compiler.read_bytes() + irtool.read_bytes()).hexdigest()
base = pathlib.Path('.agent-local/ir-text') / identity
base.mkdir(parents=True, exist_ok=True)
native = platform.system() == 'Linux' and platform.machine() == 'x86_64'
stack_profile = configure_stack()
cases = json.loads(pathlib.Path('tests/control/cases.json').read_text())

def run(argv):
    result = subprocess.run([str(arg) for arg in argv], capture_output=True, timeout=20)
    assert result.returncode == 0, (argv, result)
    return result

observations = []
for index, case in enumerate(cases):
    source = pathlib.Path('tests/control') / case['source']
    for level in ('-O0', '-O2'):
        prefix = base / f'{index}{level}'
        first = prefix.with_suffix('.cir')
        second = prefix.with_suffix('.again.cir')
        direct = prefix.with_suffix('.direct.o')
        parsed = prefix.with_suffix('.parsed.o')
        run([compiler, '--serialize-ir', level, source, '-o', first])
        run([irtool, '--verify', first])
        run([irtool, first, '-o', second])
        assert first.read_bytes() == second.read_bytes(), (source, level)
        observed = run([irtool, '--interpret', first])
        assert observed.stdout == f"interpret main => {case['exit']}\n".encode(), (source, level, observed)
        run([compiler, '-c', level, source, '-o', direct])
        run([irtool, '-c', first, '-o', parsed])
        assert direct.read_bytes() == parsed.read_bytes(), (source, level)
        record = {'source': str(source), 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(), 'level': level,
                  'ir_sha256': hashlib.sha256(first.read_bytes()).hexdigest(), 'object_sha256': hashlib.sha256(parsed.read_bytes()).hexdigest(), 'native': native, 'stack_profile': stack_profile}
        if native:
            binary = prefix.with_suffix('.native')
            run(['cc', '-no-pie', parsed, '-o', binary])
            observed = subprocess.run([str(binary.resolve())], capture_output=True, timeout=5)
            assert observed.returncode == case['exit'], (source, level, observed)
            record['native_exit'] = observed.returncode
        observations.append(record)
        (base / 'observations.json').write_text(json.dumps(observations, indent=2) + '\n')

minimal_source = base / 'minimal.c'
minimal_source.write_text('int main(void) { return 17; }\n')
minimal_ir = base / 'minimal.cir'
run([compiler, '--serialize-ir', minimal_source, '-o', minimal_ir])
text = minimal_ir.read_text()
lines = text.splitlines(keepends=True)
constant = next(line for line in lines if line.startswith('inst const '))
parts = constant.split()
# Mutation cases are counted separately from authored source cases.
mutations = {
    'schema': text.replace('cinder-ir 4', 'cinder-ir 99', 1),
    'profile': text.replace('sysv-x86-64', 'other-target', 1),
    'missing-end': text.replace('end-module\n', ''),
    'trailing-data': text + 'unexpected\n',
    'truncated-name': text.replace('x6d61696e', 'x6d6', 1),
    'name-nul': text.replace('x6d61696e', 'x6d00696e', 1),
    'empty-name': text.replace('x6d61696e', 'x', 1),
    'unknown-type': text.replace('type 1 int', 'type 1 imaginary', 1),
    'wrong-size': text.replace('int 0 1 0 0 4 4', 'int 0 1 0 0 8 4', 1),
    'bad-alignment': text.replace('int 0 1 0 0 4 4', 'int 0 1 0 0 4 3', 1),
    'bad-boolean': text.replace('int 0 1 0 0', 'int 0 9 0 0', 1),
    'missing-return-type': text.replace('none 0 1 0 - params', 'none 0 none 0 - params', 1),
    'type-id-order': text.replace('type 1 int', 'type 2 int', 1),
    'unknown-op': text.replace('inst const', 'inst frobnicate', 1),
    'wrong-return': text.replace('term return 0', 'term return none', 1),
    'return-outside-table': text.replace('term return 0', 'term return 999', 1),
    'block-id': text.replace('block 0 ', 'block 3 ', 1),
    'bad-successor': text.replace('successors 0', 'successors 1 99', 1),
    'bad-predecessor': text.replace('predecessors 0', 'predecessors 1 99', 1),
    'instruction-budget': text.replace('instructions 1', 'instructions 1000001', 1),
    'truncated-term': text.replace('term return', 'term'),
    'embedded-nul': text.replace('globals 0', 'globals\x00 0', 1),
}
def extra_types(value):
    return text.replace('types 2\n', 'types 3\n', 1).replace('globals 0\n', value + 'globals 0\n', 1)

mutations.update({
    'pointer-cycle': extra_types('type 2 pointer 0 1 0 0 8 8 2 0 none 0 - params 0 fields 0 identity 100\n'),
    'array-cycle': extra_types('type 2 array 0 1 0 0 8 8 2 1 none 0 - params 0 fields 0 identity 100\n'),
    'aggregate-value-cycle': extra_types('type 2 struct 0 1 0 0 8 8 none 0 none 0 x6e6f6465 params 0 fields 1 identity 101\nfield x6e657874 2 0 0 0 align 0\n'),
    'aggregate-alignment': extra_types('type 2 struct 0 1 0 0 7 8 none 0 none 0 x6e6f6465 params 0 fields 0 identity 100\n'),
    'enum-layout': extra_types('type 2 enum 0 1 0 0 8 8 none 0 none 0 x636f6c6f72 params 0 fields 0 identity 100\n'),
    'float-unsigned': extra_types('type 2 float 0 1 1 0 4 4 none 0 none 0 - params 0 fields 0 identity 100\n'),
    'function-layout': text.replace('function 0 1 0 0 0 1', 'function 0 1 0 0 8 8', 1),
})
recursive = text.replace('types 2\n', 'types 4\n', 1).replace('globals 0\n',
    'type 2 struct 0 1 0 0 8 8 none 0 none 0 x6e6f6465 params 0 fields 1 identity 101\nfield x6e657874 3 0 0 0 align 0\n'
    'type 3 pointer 0 1 0 0 8 8 2 0 none 0 - params 0 fields 0 identity 100\nglobals 0\n', 1)
recursive_path = base / 'recursive-pointer-type.cir'
recursive_path.write_text(recursive)
run([irtool, '--verify', recursive_path])
for label, changed in {'unexpected-left': {6: '0'}, 'missing-result': {5: 'none'}, 'bad-result': {5: '999'}, 'missing-type': {2: 'none'}, 'bad-type': {2: '999'}, 'bad-hex': {8: 'not-a-bit-pattern'}, 'overflow-slot': {10: '2147483648'}}.items():
    fields = parts.copy()
    for position, value in changed.items(): fields[position] = value
    mutations[label] = text.replace(constant, ' '.join(fields) + '\n', 1)
prior = base / 'prior.o'
for label, content in mutations.items():
    fixture = base / (label + '.invalid.cir')
    fixture.write_text(content)
    prior.write_bytes(b'prior complete object')
    result = subprocess.run([str(irtool), '-c', str(fixture), '-o', str(prior)], capture_output=True, timeout=5)
    assert result.returncode != 0 and (b'error:' in result.stderr or b'fatal:' in result.stderr), (label, result)
    assert prior.read_bytes() == b'prior complete object', label
assert hashlib.sha256(compiler.read_bytes() + irtool.read_bytes()).hexdigest() == identity, 'compiler tools changed during IR verification'
print(f'IR text: {len(observations)} source round trips/backend comparisons and {len(mutations)} rejection mutations passed; native={native}')
