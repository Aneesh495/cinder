#!/usr/bin/env python3
"""Reject altered pass proofs, including consistently rehashed observations."""
import json
import pathlib
import shutil
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path('tools').resolve()))
from evidence_integrity import EvidenceError, digest
from pass_evidence import verify_passes

compiler = pathlib.Path(sys.argv[1]).resolve()
reports = pathlib.Path('.agent-local/pass-pipeline').glob('*/observations.json')
original = max((p for p in reports if json.loads(p.read_text()).get('compiler_sha256') == digest(compiler)),
               key=lambda p: p.stat().st_mtime).parent
source = pathlib.Path('.').resolve()
native = json.loads((original / 'observations.json').read_text())['native']
verify_passes(original, source, compiler, require_native=native)


def copy_file(old, new):
    # Tool artifacts are immutable during these tests. Keep each modified
    # report/IR/object private so a tamper cannot change the original proof.
    if pathlib.Path(old).parent.name == 'tools':
        pathlib.Path(new).hardlink_to(old)
    else:
        shutil.copy2(old, new)


def rehash(base, data, name, contents):
    path = base / name
    path.write_bytes(contents if isinstance(contents, bytes) else contents.encode())
    data['artifacts'][name] = dict(sha256=digest(path), bytes=path.stat().st_size)


def stats(base, data, change):
    row = data['cases'][0]
    document = json.loads((base / row['stats']).read_text())
    change(document['passes'][0])
    text = json.dumps(document) + '\n'
    rehash(base, data, row['stats'], text)
    rehash(base, data, 'case-00/isolated/01-pipeline.json', text)
    row['stats_sha256'] = digest(base / row['stats'])


def commands(base, data, change):
    records = json.loads((base / 'commands.json').read_text())
    change(records)
    rehash(base, data, 'commands.json', json.dumps(records) + '\n')


def no_object(base, data):
    name = data['cases'][0]['objects'][0]['object']
    (base / name).unlink()
    data['artifacts'].pop(name)


def rehashed_object(base, data):
    item = data['cases'][0]['objects'][0]
    rehash(base, data, item['object'], b'not a native object\n')
    item['sha256'] = digest(base / item['object'])


def rehashed_outcome(base, data):
    row = data['cases'][0]
    row['interpreter']['integer'] += 1
    commands(base, data, lambda records: [record.update(stdout=json.dumps(row['interpreter']) + '\n')
        for record in records if '--classify' in record['argv'] and '/case-00/' in ' '.join(record['argv'])])


def broken_chain(base, data):
    name = 'complete-pipeline/09-dead-code.before.cir'
    path = base / name
    words = path.read_text().replace('loc 0 0 0 0 0', 'loc 0 0 1 0 0')
    if words == path.read_text():
        words = words.replace('inst const ', 'inst copy ', 1)
    rehash(base, data, name, words)


mutations = [
    ('missing-pair', lambda b, d: d['cases'].pop()),
    ('duplicated-positive', lambda b, d: d['cases'].__setitem__(1, d['cases'][0])),
    ('changed-input', lambda b, d: d['inputs'].pop('source/ssa.c')),
    ('missing-tool', lambda b, d: d.__setitem__('compiler_sha256', '0' * 64)),
    ('missing-object', no_object),
    ('rehashed-foreign-object', rehashed_object),
    ('changed-expected-result', lambda b, d: d['cases'][0].__setitem__('expected', 0)),
    ('rehashed-wrong-interpreter', rehashed_outcome),
    ('zero-transformations', lambda b, d: stats(b, d, lambda r: r.update(transformation_events=0))),
    ('fake-dimensions', lambda b, d: stats(b, d, lambda r: r.update(operations_before=r['operations_before'] + 1))),
    ('wrong-invalidation', lambda b, d: stats(b, d, lambda r: r.update(preserved_analyses=63, invalidated_analyses=0))),
    ('unverified-boundary', lambda b, d: stats(b, d, lambda r: r.update(verified_after=False))),
    ('missing-timing', lambda b, d: stats(b, d, lambda r: r.update(cpu_seconds=None))),
    ('nonfinite-timing', lambda b, d: stats(b, d, lambda r: r.update(cpu_seconds=float('nan')))),
    ('wrong-epoch', lambda b, d: stats(b, d, lambda r: r.update(epoch_after=999))),
    ('rehashed-broken-chain', broken_chain),
    ('hidden-fallback', lambda b, d: commands(b, d, lambda r: r[0]['argv'].__setitem__(0, 'gcc'))),
    ('missing-command', lambda b, d: commands(b, d, lambda r: r.pop())),
    ('failed-command', lambda b, d: commands(b, d, lambda r: r[0].update(exit=1))),
    ('wrong-command-output', lambda b, d: commands(b, d, lambda r: r[0].update(stdout='success\n'))),
]
with tempfile.TemporaryDirectory(prefix='cinder-pass-evidence-') as temporary:
    for label, mutate in mutations:
        base = pathlib.Path(temporary) / label
        shutil.copytree(original, base, copy_function=copy_file)
        data = json.loads((base / 'observations.json').read_text())
        mutate(base, data)
        (base / 'observations.json').write_text(json.dumps(data) + '\n')
        try:
            verify_passes(base, source, compiler, require_native=native)
        except (EvidenceError, ValueError, KeyError, OSError, IndexError):
            pass
        else:
            raise AssertionError('accepted altered proof: ' + label)
        shutil.rmtree(base)
    base = pathlib.Path(temporary) / 'missing-native-profile'
    shutil.copytree(original, base, copy_function=copy_file)
    data = json.loads((base / 'observations.json').read_text())
    data['native'] = False
    (base / 'observations.json').write_text(json.dumps(data) + '\n')
    try:
        verify_passes(base, source, compiler)
    except EvidenceError:
        pass
    else:
        raise AssertionError('accepted absent native execution profile')
print(f'Pass evidence: valid read-only proof and {len(mutations) + 1} independent tamper rejections passed')
