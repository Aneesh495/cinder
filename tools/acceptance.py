#!/usr/bin/env python3
"""Keep incremental observations separate from complete campaign acceptance."""
import argparse
import json
import pathlib
import platform
import sys

from evidence_integrity import digest, inventory_digest, revision, source_inputs
from gate_registry import AUDITED_REPORT_READERS, GATES

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('mode', choices=('snapshot', 'run'))
    parser.add_argument('compiler', type=pathlib.Path)
    parser.add_argument('--root', type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    root = args.root.resolve()
    compiler = args.compiler.resolve()
    inputs = source_inputs(root)
    evidence = root / '.agent-local/evidence'
    evidence.mkdir(parents=True, exist_ok=True)
    profile = {'system': platform.system(), 'machine': platform.machine(), 'release': platform.release()}
    payload = {'schema': 2, 'profile': 'c17-core/linux-x86-64-lp64-sysv-elf64-nonpie',
               'source_revision': revision(root), 'source_inputs': inputs, 'source_sha256': inventory_digest(inputs),
               'compiler_sha256': digest(compiler), 'host': profile, 'artifacts': {},
               'gates': {name: {'status': 'unverified', 'command': spec['command'], 'report': None}
                         for name, spec in GATES.items()}}
    payload['configuration_inputs'] = {str(path.relative_to(root)): digest(path) for path in sorted(root.glob('build*/CMakeCache.txt'))}
    if args.mode == 'snapshot':
        payload['status'] = 'partial'
        payload['purpose'] = 'Incremental validation inventory; cannot satisfy full acceptance.'
        observations = []
        for directory in ('control', 'ir-text', 'ir-campaign', 'ssa', 'allocation', 'objects'):
            base = root / '.agent-local' / directory
            for path in sorted(base.rglob('*.json')) if base.exists() else []:
                if path.name not in {'observations.json', 'summary.json'}: continue
                observations.append({'path': str(path.relative_to(root)), 'bytes': path.stat().st_size, 'sha256': digest(path)})
        payload['incremental_observations'] = observations
        (evidence / 'INCREMENTAL.json').write_text(json.dumps(payload, indent=2, sort_keys=True) + '\n')
        print('Incremental evidence inventoried; full acceptance remains incomplete.')
        return 0
    payload['status'] = 'incomplete'
    missing = sorted(set(GATES) - AUDITED_REPORT_READERS)
    payload['open_campaign_readers'] = missing
    (evidence / 'ACCEPTANCE.json').write_text(json.dumps(payload, indent=2, sort_keys=True) + '\n')
    print('Full acceptance is incomplete: complete campaign runners/readers are not connected for ' + ', '.join(missing), file=sys.stderr)
    return 1

if __name__ == '__main__':
    raise SystemExit(main())
