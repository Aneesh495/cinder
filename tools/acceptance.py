#!/usr/bin/env python3
"""Keep incremental observations separate from complete campaign acceptance."""
import argparse
import datetime
import json
import pathlib
import platform
import shutil
import subprocess
import sys
import time
import uuid

from evidence_integrity import EvidenceError, digest, inventory_digest, revision, source_inputs
from gate_registry import AUDITED_REPORT_READERS, GATES, read_gate_report


def campaign_command(command, root, path, timeout):
    record = dict(argv=command, cwd=str(root), status='running')
    path.write_text(json.dumps(record, indent=2) + '\n')
    started = time.monotonic()
    try:
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired as error:
        def output(value):
            return value.decode(errors='replace') if isinstance(value, bytes) else value
        record.update(status='timeout', timeout=error.timeout, stdout=output(error.stdout), stderr=output(error.stderr))
        raise
    except BaseException as error:
        record.update(status='failed', error=str(error))
        raise
    else:
        record.update(status='complete', exit=result.returncode, stdout=result.stdout, stderr=result.stderr)
        return result
    finally:
        record['elapsed'] = time.monotonic() - started
        path.write_text(json.dumps(record, indent=2) + '\n')


def pass_campaign(root, evidence, compiler, source_sha256):
    """Execute the implemented pass campaign and retain its native raw proof."""
    command = [sys.executable, '-B', str(root / 'tests/test_pass_pipeline.py'), str(compiler)]
    result = campaign_command(command, root, evidence / 'nonvacuous-passes.command.json', 1800)
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


def selfhost_campaign(root, evidence, compiler, source_sha256, retained=None):
    """Generate a complete native bootstrap or import an already retained one."""
    raw = retained
    if raw is None:
        identity = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '-' + uuid.uuid4().hex
        raw = evidence / 'raw/self-hosting' / identity
        command = [sys.executable, '-B', str(root / 'tools/selfhost.py'), str(compiler), '--output', str(raw), '--profile', 'native']
        result = campaign_command(command, root, evidence / 'self-hosting.command.json', 14400)
        if result.returncode != 0:
            raise EvidenceError('native bootstrap failed; original command and artifacts retained')
    else:
        raw = raw.resolve()
        from native_evidence import verify_bootstrap
        # Check exact source and seed bytes before copying any retained package.
        verify_bootstrap(raw, root, compiler)
        destination = evidence / 'raw/self-hosting' / ('import-' + uuid.uuid4().hex)
        shutil.copytree(raw, destination)
        raw = destination
    from native_evidence import verify_bootstrap
    proof = verify_bootstrap(raw, root, compiler)
    report = dict(schema=2, gate='self-hosting', source_sha256=source_sha256,
                  compiler_sha256=digest(compiler), status='pass', unit=GATES['self-hosting']['unit'],
                  observations=proof['programs'], raw_directory=str(raw.relative_to(evidence)))
    read_gate_report('self-hosting', report, root, evidence, compiler)
    name = 'self-hosting.json'
    (evidence / name).write_text(json.dumps(report, indent=2) + '\n')
    return name


def application_campaign(root, evidence, compiler, source_sha256):
    command = [sys.executable, '-B', str(root / 'tests/test_applications.py'), str(compiler)]
    result = campaign_command(command, root, evidence / 'applications.command.json', 1800)
    if result.returncode != 0:
        raise EvidenceError('application campaign failed; original command and artifacts retained')
    candidates = [path for path in (root / '.agent-local/applications').glob('*/run-*/report.json')
                  if json.loads(path.read_text()).get('compiler_sha256') == digest(compiler)]
    if not candidates:
        raise EvidenceError('application campaign did not publish its proof')
    raw = evidence / 'raw/applications' / uuid.uuid4().hex
    shutil.copytree(max(candidates, key=lambda p: p.stat().st_mtime).parent, raw)
    from application_evidence import verify_applications
    proof = verify_applications(raw, root, compiler)
    report = dict(schema=2, gate='applications', source_sha256=source_sha256,
                  compiler_sha256=digest(compiler), status='pass', unit=GATES['applications']['unit'],
                  observations=proof['applications'], raw_directory=str(raw.relative_to(evidence)))
    read_gate_report('applications', report, root, evidence, compiler)
    name = 'applications.json'
    (evidence / name).write_text(json.dumps(report, indent=2) + '\n')
    return name


def census_campaign(root, evidence, compiler, source_sha256):
    raw = evidence / 'raw/private-size/census.json'
    command = [sys.executable, '-B', str(root / 'tools/source_census.py'), '--root', str(root), '--output', str(raw)]
    result = campaign_command(command, root, evidence / 'private-size.command.json', 60)
    if result.returncode != 0:
        raise EvidenceError('private census generation failed; command retained')
    count = json.loads(raw.read_text())['total_substantive_lines']
    report = dict(schema=2, gate='private-size', source_sha256=source_sha256,
                  compiler_sha256=digest(compiler), status='pass', unit=GATES['private-size']['unit'],
                  observations=count, raw_report=str(raw.relative_to(evidence)))
    read_gate_report('private-size', report, root, evidence, compiler)
    name = 'private-size.json'
    (evidence / name).write_text(json.dumps(report, indent=2) + '\n')
    return name


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('mode', choices=('snapshot', 'run'))
    parser.add_argument('compiler', type=pathlib.Path)
    parser.add_argument('--root', type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
    parser.add_argument('--gate', action='append', choices=sorted(AUDITED_REPORT_READERS),
                        help='Run only selected implemented gates; defaults to all implemented gates')
    parser.add_argument('--bootstrap-evidence', type=pathlib.Path,
                        help='Import complete native bootstrap evidence after independent source/seed verification')
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
        if args.gate or args.bootstrap_evidence:
            parser.error('gate selection and bootstrap import apply only to run')
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
    requested = sorted(set(args.gate) if args.gate is not None else AUDITED_REPORT_READERS)
    if args.bootstrap_evidence is not None and 'self-hosting' not in requested:
        parser.error('bootstrap import requires the self-hosting gate')
    payload['requested_gates'] = requested
    (evidence / 'generation.json').write_text(json.dumps(dict(schema=1, command=sys.argv,
        host=profile, source_revision=payload['source_revision'], source_sha256=payload['source_sha256'],
        compiler_sha256=payload['compiler_sha256'], configuration_inputs=payload['configuration_inputs'], requested_gates=requested), indent=2) + '\n')
    missing = sorted(set(GATES) - AUDITED_REPORT_READERS)
    payload['open_campaign_readers'] = missing
    native = profile['system'] == 'Linux' and profile['machine'] == 'x86_64'
    for key in sorted(AUDITED_REPORT_READERS):
        if key not in requested:
            payload['gates'][key]['reason'] = 'campaign was not selected for this generation'
            continue
        if key != 'private-size' and not native and not (key == 'self-hosting' and args.bootstrap_evidence is not None):
            payload['gates'][key]['reason'] = 'requires native Linux x86-64 execution'
            continue
        try:
            if key == 'nonvacuous-passes':
                report = pass_campaign(root, evidence, compiler, payload['source_sha256'])
            elif key == 'self-hosting':
                report = selfhost_campaign(root, evidence, compiler, payload['source_sha256'], args.bootstrap_evidence)
            elif key == 'applications':
                report = application_campaign(root, evidence, compiler, payload['source_sha256'])
            elif key == 'private-size':
                report = census_campaign(root, evidence, compiler, payload['source_sha256'])
            else:
                raise EvidenceError('implemented reader has no campaign producer: ' + key)
            payload['gates'][key].update(status='pass', report=report)
        except (EvidenceError, OSError, ValueError, KeyError, subprocess.TimeoutExpired) as error:
            payload['gates'][key].update(status='failed', reason=str(error))
    payload['artifacts'] = {str(path.relative_to(evidence)): dict(bytes=path.stat().st_size, sha256=digest(path))
                            for path in sorted(evidence.rglob('*')) if path.is_file()
                            and path.name not in ('ACCEPTANCE.json', 'INCREMENTAL.json')}
    if source_inputs(root) != inputs:
        raise EvidenceError('source inputs changed during acceptance execution')
    complete = not missing and all(gate['status'] == 'pass' for gate in payload['gates'].values())
    payload['status'] = 'pass' if complete else 'incomplete'
    (evidence / 'ACCEPTANCE.json').write_text(json.dumps(payload, indent=2, sort_keys=True) + '\n')
    if complete:
        print('Full acceptance passed with independently verified campaign artifacts.')
        return 0
    print('Full acceptance is incomplete: complete campaign runners/readers are not connected for ' + ', '.join(missing), file=sys.stderr)
    return 1

if __name__ == '__main__':
    raise SystemExit(main())
