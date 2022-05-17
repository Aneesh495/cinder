"""Read-only source, tool, and artifact binding primitives."""
import hashlib
import json
import pathlib
import subprocess

class EvidenceError(ValueError):
    pass

def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(chunk)
    return value.hexdigest()

def source_inputs(root):
    result = subprocess.run(['git', '-C', str(root), 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], capture_output=True, check=True)
    paths = sorted(set(result.stdout.decode().split('\0')) - {''})
    files = {}
    for name in paths:
        path = root / name
        if not path.is_file() or path.is_symlink():
            raise EvidenceError(f'source input is missing or indirect: {name}')
        files[name] = digest(path)
    if not files or not any(name.startswith('source/') and name.endswith('.c') for name in files):
        raise EvidenceError('source inventory is empty')
    return files

def inventory_digest(files):
    return hashlib.sha256(json.dumps(files, sort_keys=True, separators=(',', ':')).encode()).hexdigest()

def revision(root):
    return subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()

def artifact_path(base, name):
    relative = pathlib.PurePosixPath(name)
    if not name or relative.is_absolute() or '..' in relative.parts or str(relative) != name:
        raise EvidenceError(f'unsafe artifact path: {name}')
    path = base / name
    if path.is_symlink() or base.resolve() not in path.resolve().parents:
        raise EvidenceError(f'indirect artifact path: {name}')
    return path

def verify_bindings(data, root, base, compiler):
    current = source_inputs(root)
    if data.get('source_revision') != revision(root) or data.get('source_inputs') != current or data.get('source_sha256') != inventory_digest(current):
        raise EvidenceError('evidence does not bind the current source/configuration bytes')
    if not compiler.is_file() or data.get('compiler_sha256') != digest(compiler):
        raise EvidenceError('compiler binary does not match the recorded compiler')
    configurations = data.get('configuration_inputs')
    if not isinstance(configurations, dict) or not configurations:
        raise EvidenceError('generated build configuration inventory is empty')
    for name, expected in configurations.items():
        path = artifact_path(root, name)
        if not path.is_file() or digest(path) != expected:
            raise EvidenceError(f'generated build configuration changed: {name}')
    artifacts = data.get('artifacts')
    if not isinstance(artifacts, dict) or not artifacts:
        raise EvidenceError('artifact inventory is empty')
    for name, record in artifacts.items():
        path = artifact_path(base, name)
        if not isinstance(record, dict) or type(record.get('bytes')) is not int or record['bytes'] < 0 or not path.is_file() or path.stat().st_size != record['bytes'] or digest(path) != record.get('sha256'):
            raise EvidenceError(f'artifact is missing or changed: {name}')
    return artifacts
