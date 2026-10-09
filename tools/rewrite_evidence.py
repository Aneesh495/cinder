"""Read-only reconstruction of the complete integer rewrite campaign."""
import json
import pathlib

from bitvector import evaluate, fragment, normalize
from evidence_integrity import EvidenceError, digest
from run_rewrite_checks import domain, module, specimens


def require(condition, message):
    if not condition:
        raise EvidenceError(message)


def verify_rewrites(base, source, compiler=None):
    data = json.loads((base/'summary.json').read_text())
    require(data.get('schema') == 1, 'unsupported rewrite evidence')
    expected_inputs = {str(p.relative_to(source)) for p in (source/'source').glob('*.[ch]')}
    expected_inputs |= {'CMakeLists.txt', 'tools/run_rewrite_checks.py', 'tools/bitvector.py',
                        'tools/rewrite_evidence.py', 'tests/test_rewrite_evidence.py'}
    require(set(data.get('inputs', {})) == expected_inputs, 'missing rewrite source binding')
    for name, expected in data['inputs'].items():
        require(digest(source/name) == expected, 'changed rewrite source: '+name)
    if compiler is not None:
        require(digest(compiler) == data.get('compiler'), 'changed compiler binary')
        require(digest(compiler.parent/'cinderir') == data.get('irtool'), 'changed IR tool binary')
    require(digest(base/'observations.jsonl') == data.get('observations'), 'changed raw rewrite outcomes')
    expected = list(dict.fromkeys(specimens()))
    require(len(data.get('fragments', [])) == len(expected), 'missing rewrite fragment')
    checks = defined = changed = rejected = 0
    with (base/'observations.jsonl').open() as observations:
        for number, (row, spec) in enumerate(zip(data['fragments'], expected)):
            width, signed, op, factor = spec
            require(tuple(row.get(key) for key in ('width', 'signed', 'opcode', 'factor')) == spec, 'reordered fragment')
            before, after = base/f'{number:04d}.before.cir', base/f'{number:04d}.after.cir'
            require(before.read_text() == module(*spec), 'changed original fragment')
            require(digest(before) == row.get('before') and digest(after) == row.get('after'), 'changed fragment bytes')
            require(row.get('exit') == 0 and row.get('stdout') == row.get('stderr') == '', 'failed production optimizer command')
            command = row.get('command')
            require(isinstance(command, list) and len(command) == 5 and pathlib.Path(command[0]).name == 'cinderir'
                    and command[1] == '-O2' and command[3] == '-o'
                    and pathlib.Path(command[2]).name == before.name and pathlib.Path(command[4]).name == after.name,
                    'incorrect production fragment command')
            first, second = fragment(before.read_text()), fragment(after.read_text())
            normalized = normalize(factor, width, signed)
            eligible = ((op in ('mul', 'div.u', 'div.s') and normalized == 1) or
                        (not signed and normalized > 0 and normalized & (normalized-1) == 0))
            actual = not any(inst[0] == op for inst in second[1])
            require(actual == eligible and row.get('changed') is actual, 'invalid rewrite precondition')
            values = domain(*spec)+[None]
            require(row.get('start') == checks and row.get('checks') == len(values), 'inflated fragment checks')
            for value in values:
                record = json.loads(next(observations))
                a, b = evaluate(first, value), evaluate(second, value)
                require(a == b, 'semantically incorrect production rewrite')
                require(record == dict(fragment=number, input=value, before=list(a), after=list(b)), 'incorrect independent observation')
                checks += 1
                defined += a[0] == 0
            changed += actual
            rejected += not eligible
        require(observations.read() == '', 'extra raw rewrite outcomes')
    require(checks == data.get('checks') and defined == data.get('defined') and defined >= 50000,
            'incomplete defined-input rewrite campaign')
    require(changed == data.get('changed_fragments') and rejected == data.get('rejected_preconditions') and rejected > 0,
            'missing invalid-precondition evidence')
    return dict(checks=checks, defined=defined, invalid=checks-defined, changed=changed, rejected=rejected,
                summary_sha256=digest(base/'summary.json'))
