#!/usr/bin/env python3
"""Validate existing acceptance without building, executing, or writing outputs."""
import argparse
import json
import pathlib
import sys

from evidence_integrity import EvidenceError, artifact_path, verify_bindings
from gate_registry import AUDITED_REPORT_READERS, GATES

def verify(root, manifest, compiler):
    data = json.loads(manifest.read_text())
    if not isinstance(data, dict) or data.get('schema') != 2 or data.get('profile') != 'c17-core/linux-x86-64-lp64-sysv-elf64-nonpie':
        raise EvidenceError('unsupported acceptance schema or target profile')
    gates = data.get('gates')
    if not isinstance(gates, dict) or set(gates) != set(GATES):
        raise EvidenceError('required gate registry is missing, extra, or empty')
    if data.get('status') != 'complete':
        raise EvidenceError('acceptance is incomplete')
    artifacts = verify_bindings(data, root, manifest.parent, compiler)
    for key, spec in GATES.items():
        gate = gates[key]
        if not isinstance(gate, dict) or gate.get('status') != 'pass' or gate.get('command') != spec['command']:
            raise EvidenceError(f'required gate is skipped or mismatched: {key}')
        name = gate.get('report')
        if not isinstance(name, str) or name not in artifacts:
            raise EvidenceError(f'required raw report is missing: {key}')
        path = artifact_path(manifest.parent, name)
        report = json.loads(path.read_text())
        if not isinstance(report, dict) or report.get('schema') != 2 or report.get('gate') != key or report.get('source_sha256') != data['source_sha256'] or report.get('compiler_sha256') != data['compiler_sha256']:
            raise EvidenceError(f'required report bindings disagree: {key}')
        if key not in AUDITED_REPORT_READERS:
            raise EvidenceError(f'full raw-artifact reader is not implemented: {key}')
    return data

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('compiler', type=pathlib.Path)
    parser.add_argument('--root', type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
    parser.add_argument('--manifest', type=pathlib.Path)
    args = parser.parse_args()
    root = args.root.resolve()
    manifest = args.manifest or root / '.agent-local/evidence/ACCEPTANCE.json'
    try:
        verify(root, manifest.resolve(), args.compiler.resolve())
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f'verification failed: {error}', file=sys.stderr)
        return 1
    print('Existing full acceptance evidence verified.')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
