#!/usr/bin/env python3
"""Check actual production strength rewrites with independent integer semantics."""
import hashlib
import json
import pathlib
import random
import subprocess
import sys

from bitvector import evaluate, fragment, normalize
from evidence_integrity import digest


def instruction(op, dst, left='none', right='none', literal=0, slot=-1):
    return (f'inst {op} 1 none none {dst} {left} {right} {literal & ((1 << 64)-1):016x} '
            f'0000000000000000 {slot} 0 - 0 noreturn 0 args 0 classes 0 incoming 0 loc 0 0 0 0 0')


def module(width, signed, op, factor):
    kind = {8: 'char', 16: 'short', 32: 'int', 64: 'llong'}[width]
    instructions = [instruction('arg', 0, slot=0), instruction('const', 1, literal=factor),
                    instruction(op, 2, 0, 1)]
    return ('cinder-ir 6 lp64-le sysv-x86-64\ntypes 2\n'
            'type 0 function 0 1 0 0 0 1 none 0 1 0 - params 1 fields 0 identity 17\n'
            'type-param x78 1\n'
            f'type 1 {kind} 0 1 {int(not signed)} 0 {width//8} {width//8} none 0 none 0 - params 0 fields 0 identity 10\n'
            'globals 0\nfunctions 1\n'
            'function x7472616e73666f726d 0 1 noreturn 0 3 1 0 params 1 blocks 1\n'
            'local 1 align 0\nparam x78 1\n'
            'block 0 x656e747279 instructions 3 predecessors 0 successors 0\n'
            + '\n'.join(instructions) + '\nterm return 2 none none none none loc 0 0 0 0 0\n'
            'end-block\nend-function\nend-module\n')


def specimens():
    for width in (8, 16, 32, 64):
        for shift in range(width):
            for op in ('mul', 'div.u', 'mod.u'):
                yield width, False, op, 1 << shift
        for signed in (False, True):
            for factor in (0, 1, 3, 7, 1 << width, 1 << (width - 1), -1):
                for op in (('mul', 'div.s', 'mod.s') if signed else ('mul', 'div.u', 'mod.u')):
                    yield width, signed, op, factor


def domain(width, signed, op, factor):
    # Every byte input, plus a complete 16-bit domain for an actual divide
    # rewrite. Wider fragments use boundaries and a reproducible random domain.
    if width == 8 or (width == 16 and not signed and op == 'div.u' and factor == 256):
        return [normalize(x, width, signed) for x in range(1 << width)]
    mask = (1 << width) - 1
    values = {0, 1, 2, 3, mask, mask - 1, mask // 2, mask // 2 + 1}
    normalized = factor & mask
    for value in (normalized - 1, normalized, normalized + 1):
        values.add(value & mask)
    rng = random.Random((width << 80) ^ (int(signed) << 79) ^ (factor & ((1 << 64)-1)) ^ int.from_bytes(op.encode(), 'big'))
    while len(values) < 256:
        values.add(rng.getrandbits(width))
    return sorted({normalize(value, width, signed) for value in values})


def main():
    compiler = pathlib.Path(sys.argv[1]).resolve()
    irtool = compiler.parent / 'cinderir'
    inputs = sorted(pathlib.Path('source').glob('*.[ch]')) + [pathlib.Path('CMakeLists.txt'), pathlib.Path('tools/run_rewrite_checks.py'), pathlib.Path('tools/bitvector.py'), pathlib.Path('tools/rewrite_evidence.py'), pathlib.Path('tests/test_rewrite_evidence.py')]
    hashes = {str(p): digest(p) for p in inputs}
    identity = hashlib.sha256((digest(irtool) + json.dumps(hashes, sort_keys=True)).encode()).hexdigest()
    root = pathlib.Path('.agent-local/rewrite-checks') / identity
    root.mkdir(parents=True, exist_ok=True)
    (root / 'summary.json').unlink(missing_ok=True)
    checks = defined = rejected = changed = 0
    rows = []
    seen = set()
    with (root / 'observations.jsonl').open('w') as observations:
        for width, signed, op, factor in specimens():
            key = width, signed, op, factor
            if key in seen:
                continue
            seen.add(key)
            number = len(rows)
            before = root / f'{number:04d}.before.cir'
            after = root / f'{number:04d}.after.cir'
            before.write_text(module(*key))
            command = [str(irtool), '-O2', str(before), '-o', str(after)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=30)
            assert result.returncode == 0, (key, result)
            first, second = fragment(before.read_text()), fragment(after.read_text())
            normalized = normalize(factor, width, signed)
            expected = ((op in ('mul', 'div.u', 'div.s') and normalized == 1) or
                        (not signed and normalized > 0 and normalized & (normalized-1) == 0))
            actual = not any(inst[0] == op for inst in second[1])
            assert actual == expected, (key, expected, second)
            changed += actual
            rejected += not expected
            values = domain(*key) + [None]
            start = checks
            for value in values:
                a, b = evaluate(first, value), evaluate(second, value)
                assert a == b, (key, value, a, b)
                observations.write(json.dumps(dict(fragment=number, input=value, before=a, after=b), separators=(',', ':'))+'\n')
                checks += 1
                defined += a[0] == 0
            rows.append(dict(width=width, signed=signed, opcode=op, factor=factor, changed=actual,
                             command=command, exit=result.returncode, stdout=result.stdout, stderr=result.stderr,
                             start=start, checks=len(values), before=digest(before), after=digest(after)))
    assert defined >= 50000 and changed > 0 and rejected > 0
    assert hashes == {str(p): digest(p) for p in inputs}
    assert identity == hashlib.sha256((digest(irtool)+json.dumps(hashes,sort_keys=True)).encode()).hexdigest()
    summary = dict(schema=1, inputs=hashes, compiler=digest(compiler), irtool=digest(irtool), fragments=rows,
                   checks=checks, defined=defined, rejected_preconditions=rejected, changed_fragments=changed,
                   observations=digest(root/'observations.jsonl'))
    (root/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(f'Rewrite checks: {defined} independently evaluated defined inputs; {checks-defined} invalid inputs; {changed} changed fragments; {rejected} rejected preconditions.')
    subprocess.run([sys.executable, '-B', 'tests/test_rewrite_evidence.py', str(root), str(compiler)], check=True)


if __name__ == '__main__':
    main()
