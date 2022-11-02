#!/usr/bin/env python3
"""Keep incremental observations separate from complete campaign acceptance."""
import argparse
import json
import pathlib
import platform
import shutil
import subprocess
import sys

from evidence_integrity import EvidenceError, digest, inventory_digest, revision, source_inputs
from gate_registry import AUDITED_REPORT_READERS, GATES, read_gate_report


def pass_campaign(root, evidence, compiler, source_sha256):
    """Execute the implemented pass campaign and retain its native raw proof."""
    command = [sys.executable, '-B', str(root / 'tests/test_pass_pipeline.py'), str(compiler)]
    result = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=1800)
    log = dict(argv=command, exit=result.returncode, stdout=result.stdout, stderr=result.stderr)
    (evidence / 'nonvacuous-passes.command.json').write_text(json.dumps(log, indent=2) + '\n')
    if result.returncode != 0:
        raise EvidenceError('pass campaign failed; command output retained')
    candidates = []
    for path in (root / '.agent-local/pass-pipeline').glob('*/observations.json'):
        data = json.loads(path.read_text())
        if data.get('compiler_sha256') == digest(compiler) and data.get('irtool_sha256') == digest(compiler.parent / 'cinderir'):
            candidates.append(path)
    if not candidates:
        raise EvidenceError('pass campaign did not publish its proof')
    raw = evidence / 'raw/nonvacuous-passes'
    if raw.exists():
        shutil.rmtree(raw)
    shutil.copytree(max(candidates, key=lambda p: p.stat().st_mtime).parent, raw)
    from pass_evidence import verify_passes
    proof = verify_passes(raw, root, compiler)
    report = dict(schema=2, gate='nonvacuous-passes', source_sha256=source_sha256,
                  compiler_sha256=digest(compiler), status='pass', unit=GATES['nonvacuous-passes']['unit'],
                  observations=proof['passes'], raw_directory='raw/nonvacuous-passes')
    # The count below is accepted only after independent native reconstruction.
    read_gate_report('nonvacuous-passes', report, root, evidence, compiler)
    name = 'nonvacuous-passes.json'
    (evidence / name).write_text(json.dumps(report, indent=2) + '\n')
    return name

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
        for directory in ('control', 'ir-text', 'ir-campaign', 'ssa', 'allocation', 'objects', 'pass-pipeline'):
            base = root / '.agent-local' / directory
            for path in sorted(base.rglob('*.json')) if base.exists() else []:
                if path.name not in {'observations.json', 'summary.json'}: continue
                observations.append({'path': str(path.relative_to(root)), 'bytes': path.stat().st_size, 'sha256': digest(path)})
        payload['incremental_observations'] = observations
        (evidence / 'INCREMENTAL.json').write_text(json.dumps(payload, indent=2, sort_keys=True) + '\n')
        print('Incremental evidence inventoried; full acceptance remains incomplete.')
        return 0
    payload['status'] = 'incomplete'
    (evidence / 'generation.json').write_text(json.dumps(dict(schema=1, command=sys.argv,
        host=profile, source_revision=payload['source_revision'], source_sha256=payload['source_sha256'],
        compiler_sha256=payload['compiler_sha256'], configuration_inputs=payload['configuration_inputs']), indent=2) + '\n')
    missing = sorted(set(GATES) - AUDITED_REPORT_READERS)
    payload['open_campaign_readers'] = missing
    if profile['system'] == 'Linux' and profile['machine'] == 'x86_64':
        try:
            report = pass_campaign(root, evidence, compiler, payload['source_sha256'])
            payload['gates']['nonvacuous-passes'].update(status='pass', report=report)
        except (EvidenceError, OSError, ValueError, KeyError, subprocess.TimeoutExpired) as error:
            payload['gates']['nonvacuous-passes'].update(status='failed', reason=str(error))
    else:
        payload['gates']['nonvacuous-passes']['reason'] = 'requires native Linux x86-64 execution'
    payload['artifacts'] = {str(path.relative_to(evidence)): dict(bytes=path.stat().st_size, sha256=digest(path))
                            for path in sorted(evidence.rglob('*')) if path.is_file()
                            and path.name not in ('ACCEPTANCE.json', 'INCREMENTAL.json')}
    if source_inputs(root) != inputs:
        raise EvidenceError('source inputs changed during acceptance execution')
    (evidence / 'ACCEPTANCE.json').write_text(json.dumps(payload, indent=2, sort_keys=True) + '\n')
    print('Full acceptance is incomplete: complete campaign runners/readers are not connected for ' + ', '.join(missing), file=sys.stderr)
    return 1

if __name__ == '__main__':
    raise SystemExit(main())
