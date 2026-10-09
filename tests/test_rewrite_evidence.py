#!/usr/bin/env python3
"""Reject corrupt raw rewrite evidence and sanity-check the independent model."""
import hashlib
import json
import os
import pathlib
import shutil
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from bitvector import evaluate, fragment
from evidence_integrity import EvidenceError, digest
from rewrite_evidence import verify_rewrites
from run_rewrite_checks import module

source = pathlib.Path(__file__).resolve().parents[1]
base = pathlib.Path(sys.argv[1]).resolve()
compiler = pathlib.Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None

# These directed expectations do not use the production interpreter or pass.
for spec, argument, expected in [
    ((8, False, 'mul', 8), 35, (0, 24)),
    ((8, False, 'div.u', 8), 255, (0, 31)),
    ((8, False, 'mod.u', 32), 255, (0, 31)),
    ((8, True, 'div.s', 8), -35, (0, -4)),
    ((8, True, 'mod.s', 8), -35, (0, -3)),
    ((8, True, 'mul', 8), 35, (1, None)),
    ((8, True, 'div.s', -1), -128, (1, None)),
    ((8, False, 'div.u', 256), 9, (2, None)),
    ((64, False, 'mul', 1 << 63), 3, (0, 1 << 63)),
    ((64, False, 'mod.u', 1), None, (4, None)),
]:
    assert evaluate(fragment(module(*spec)), argument) == expected

original_state = {str(p.relative_to(base)): (p.stat().st_size, p.stat().st_mtime_ns)
                  for p in base.iterdir() if p.is_file()}
positive = verify_rewrites(base, source, compiler)
mutations = []
with tempfile.TemporaryDirectory(prefix='cinder-rewrite-reader-') as temp:
    overlay = pathlib.Path(temp)/'evidence'
    shutil.copytree(base, overlay, copy_function=os.link)
    original = (overlay/'summary.json').read_bytes()
    def replace(path, data):
        other = path.with_suffix(path.suffix+'.changed')
        other.write_bytes(data)
        other.replace(path)
    def check(name, files):
        backups = {p: p.read_bytes() for p in files}
        for p, data in files.items():
            replace(p, data)
        try:
            verify_rewrites(overlay, source, compiler)
        except (EvidenceError, OSError, ValueError, KeyError, TypeError, AssertionError, StopIteration):
            mutations.append(name)
        else:
            raise AssertionError('accepted corrupt evidence: '+name)
        finally:
            for p, data in backups.items():
                replace(p, data)
    def change(name, edit):
        data = json.loads(original)
        edit(data)
        check(name, {overlay/'summary.json': (json.dumps(data)+'\n').encode()})
    change('inflated-total', lambda d: d.update(defined=d['defined']+1))
    change('missing-fragment', lambda d: d['fragments'].pop())
    change('missing-source', lambda d: d['inputs'].pop('source/opt_strength.c'))
    change('changed-source-hash', lambda d: d['inputs'].update({'source/opt_strength.c': '0'*64}))
    change('failed-optimizer', lambda d: d['fragments'][0].update(exit=1))
    change('host-fallback-command', lambda d: d['fragments'][0]['command'].__setitem__(0, '/usr/bin/gcc'))
    change('missing-invalid-preconditions', lambda d: d.update(rejected_preconditions=0))
    change('wrong-transformation', lambda d: d['fragments'][0].update(changed=False))
    check('changed-transformed-ir', {overlay/'0000.after.cir': b'changed fragment\n'})
    transformed = overlay/'0003.after.cir'
    text = transformed.read_text()
    lines = text.splitlines()
    for at, line in enumerate(lines):
        fields = line.split()
        if fields[:2] == ['inst', 'const'] and fields[8] == '0000000000000001':
            fields[8] = '0000000000000002'
            lines[at] = ' '.join(fields)
            break
    else:
        raise AssertionError('missing real shift rewrite')
    wrong_ir = ('\n'.join(lines)+'\n').encode()
    data = json.loads(original)
    data['fragments'][3]['after'] = hashlib.sha256(wrong_ir).hexdigest()
    check('rehashed-semantically-wrong-shift', {transformed: wrong_ir, overlay/'summary.json': json.dumps(data).encode()})
    # Rehash wrong numeric observations to exercise the semantic reader.
    records = overlay/'observations.jsonl'
    raw = records.read_bytes()
    first, rest = raw.split(b'\n', 1)
    row = json.loads(first)
    row['after'] = [0, 123456]
    changed = json.dumps(row).encode()+b'\n'+rest
    data = json.loads(original)
    data['observations'] = hashlib.sha256(changed).hexdigest()
    check('rehashed-wrong-observation', {records: changed, overlay/'summary.json': json.dumps(data).encode()})
    truncated = raw[:raw.index(b'\n')+1]
    data = json.loads(original)
    data['observations'] = hashlib.sha256(truncated).hexdigest()
    check('rehashed-truncated-outcomes', {records: truncated, overlay/'summary.json': json.dumps(data).encode()})
assert len(mutations) == 12
assert original_state == {str(p.relative_to(base)): (p.stat().st_size, p.stat().st_mtime_ns)
                          for p in base.iterdir() if p.is_file()}
(base/'reader-checks.json').write_text(json.dumps(dict(positive=positive, rejected=mutations, read_only=True), indent=2)+'\n')
print('Rewrite reader: independent outcomes reconstructed; 12 corrupt reports rejected; input artifacts unchanged.')
