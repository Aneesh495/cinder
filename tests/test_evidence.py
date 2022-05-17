#!/usr/bin/env python3
"""Exercise source/artifact tampering and read-only verification boundaries."""
import copy
import json
import pathlib
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path('tools').resolve()))
from evidence_integrity import EvidenceError, digest, inventory_digest, revision, source_inputs, verify_bindings
from gate_registry import GATES
from verify_evidence import verify

def reject(action, reason):
    try:
        action()
    except (EvidenceError, OSError, ValueError):
        return
    raise AssertionError(reason)

def states(root):
    return {str(path.relative_to(root)): (digest(path), path.stat().st_mtime_ns)
            for path in root.rglob('*') if path.is_file() and '.git' not in path.parts}

with tempfile.TemporaryDirectory(prefix='cinder-evidence-') as directory:
    root = pathlib.Path(directory)
    (root / 'source').mkdir()
    source = root / 'source/example.c'
    source.write_text('int example(void) { return 7; }\n')
    subprocess.run(['git', 'init', '-q', str(root)], check=True)
    subprocess.run(['git', '-C', str(root), 'add', 'source/example.c'], check=True)
    subprocess.run(['git', '-C', str(root), '-c', 'user.name=Cinder Test', '-c', 'user.email=test@example.invalid', 'commit', '-qm', 'owned integrity fixture'], check=True)
    base = root / '.agent-local/evidence'
    base.mkdir(parents=True)
    (root / '.git/info/exclude').write_text('.agent-local/\nbuild/\n')
    (root / 'build').mkdir()
    configuration = root / 'build/CMakeCache.txt'
    configuration.write_text('owned configuration fixture\n')
    compiler = base / 'compiler'
    compiler.write_bytes(b'owned hash fixture, never executed')
    inputs = source_inputs(root)
    object_file = base / 'program.o'
    reference = base / 'reference.stdout'
    bootstrap = base / 'bootstrap.json'
    object_file.write_bytes(b'owned object hash fixture')
    reference.write_bytes(b'expected observation\n')
    bootstrap.write_text('{"stage_two_sha256":"owned fixture"}\n')
    data = {'schema': 2, 'status': 'complete', 'profile': 'c17-core/linux-x86-64-lp64-sysv-elf64-nonpie',
            'source_revision': revision(root), 'source_inputs': inputs, 'source_sha256': inventory_digest(inputs),
            'compiler_sha256': digest(compiler),
            'configuration_inputs': {'build/CMakeCache.txt': digest(configuration)},
            'artifacts': {path.name: {'bytes': path.stat().st_size, 'sha256': digest(path)} for path in (object_file, reference, bootstrap)},
            'gates': {name: {'status': 'pass', 'command': spec['command'], 'report': None} for name, spec in GATES.items()}}
    # Positive binding check tests integrity primitives only. This fixture must
    # never pass full acceptance, because it has no executed campaign reports.
    assert verify_bindings(data, root, base, compiler) == data['artifacts']
    manifest = base / 'ACCEPTANCE.json'
    manifest.write_text(json.dumps(data))
    reject(lambda: verify(root, manifest, compiler), 'hash fixtures became full acceptance')
    original = object_file.read_bytes()
    object_file.unlink()
    reject(lambda: verify_bindings(data, root, base, compiler), 'missing object accepted')
    object_file.write_bytes(original)
    reference.write_bytes(b'changed reference\n')
    reject(lambda: verify_bindings(data, root, base, compiler), 'changed reference accepted')
    reference.write_bytes(b'expected observation\n')
    compiler.write_bytes(b'substituted binary')
    reject(lambda: verify_bindings(data, root, base, compiler), 'substituted compiler accepted')
    compiler.write_bytes(b'owned hash fixture, never executed')
    source.write_text('int example(void) { return 9; }\n')
    reject(lambda: verify_bindings(data, root, base, compiler), 'uncommitted source edit accepted')
    source.write_text('int example(void) { return 7; }\n')
    untracked = root / 'source/untracked.h'
    untracked.write_text('#define OTHER 3\n')
    reject(lambda: verify_bindings(data, root, base, compiler), 'new configuration input accepted')
    untracked.unlink()
    configuration.write_text('changed generated configuration\n')
    reject(lambda: verify_bindings(data, root, base, compiler), 'changed build configuration accepted')
    configuration.write_text('owned configuration fixture\n')
    bootstrap.write_text('{"stage_two_sha256":"changed"}\n')
    reject(lambda: verify_bindings(data, root, base, compiler), 'corrupt bootstrap binding accepted')
    bootstrap.write_text('{"stage_two_sha256":"owned fixture"}\n')
    for change in ({'gates': {}}, {'status': 'incomplete'}, {'profile': 'other-target'}, {'artifacts': {}}):
        mutated = copy.deepcopy(data)
        mutated.update(change)
        manifest.write_text(json.dumps(mutated))
        reject(lambda: verify(root, manifest, compiler), 'incomplete/vacuous manifest accepted')
    manifest.write_text('{"schema":2,')
    reject(lambda: verify(root, manifest, compiler), 'truncated report accepted')
    before = states(root)
    result = subprocess.run([sys.executable, '-B', str(pathlib.Path('tools/verify_evidence.py').resolve()), str(compiler), '--root', str(root), '--manifest', str(manifest)], capture_output=True)
    assert result.returncode != 0 and b'verification failed:' in result.stderr
    assert states(root) == before, 'verification modified source, evidence, or timestamps'

dry_run = subprocess.run(['make', '-n', 'verify'], capture_output=True, text=True, check=True)
assert 'cmake' not in dry_run.stdout and 'source_census' not in dry_run.stdout
print('evidence: source/tool/artifact tampering rejected; verification is read-only; full campaign readers remain open')
