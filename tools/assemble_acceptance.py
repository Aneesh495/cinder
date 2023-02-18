#!/usr/bin/env python3
"""Assemble independently verified native gates produced from one shared seed."""
import argparse
import json
import os
from pathlib import Path
import platform
import shutil

from acceptance import release_campaign
from evidence_integrity import digest, inventory_digest, revision, source_inputs, verify_bindings
from gate_registry import GATES, read_gate_report
from native_evidence import require
from verify_evidence import verify


def main():
    parser=argparse.ArgumentParser();parser.add_argument('compiler',type=Path);parser.add_argument('imports',type=Path)
    args=parser.parse_args();root=Path(__file__).resolve().parents[1];compiler=args.compiler.resolve()
    require(platform.system()=='Linux' and platform.machine()=='x86_64','assembly requires native Linux x86-64')
    base=root/'.agent-local/evidence';base.mkdir(parents=True,exist_ok=False)
    inputs=source_inputs(root);source_hash=inventory_digest(inputs)
    gates={key:dict(status='unverified',command=spec['command'],report=None) for key,spec in GATES.items()}
    observed=set()
    for manifest in sorted(args.imports.resolve().glob('*/.agent-local/evidence/ACCEPTANCE.json')):
        partial=json.loads(manifest.read_text());origin=manifest.parent
        verify_bindings(partial,root,origin,compiler)
        selected=partial.get('requested_gates',[])
        require(len(selected)==1,'a parallel package must contain exactly one selected gate')
        key=selected[0]
        require(key in GATES and key!='publication' and key not in observed,'unknown or duplicated parallel gate')
        gate=partial['gates'][key]
        require(gate.get('status')=='pass' and gate.get('command')==GATES[key]['command'],'parallel gate failed: '+key)
        report=json.loads((origin/gate['report']).read_text())
        require(report.get('source_sha256')==source_hash and report.get('compiler_sha256')==digest(compiler),'parallel gate seed or source mismatch')
        read_gate_report(key,report,root,origin,compiler)
        # Only bound artifacts are imported. Per-job generation/progress envelopes
        # stay in their original package rather than overwriting one another.
        for name in partial['artifacts']:
            if name in ('generation.json','progress.json'):continue
            source=origin/name;destination=base/name
            destination.parent.mkdir(parents=True,exist_ok=True)
            require(not destination.exists() or digest(destination)==digest(source),'parallel artifact collision: '+name)
            if not destination.exists():
                try:os.link(source,destination)
                except OSError:shutil.copy2(source,destination)
        gates[key]=gate;observed.add(key)
        print('Imported independently verified gate:',key,flush=True)
    require(observed==set(GATES)-{'publication'},'incomplete parallel campaign packages')
    report=release_campaign('publication',root,base,compiler,source_hash)
    gates['publication'].update(status='pass',report=report)
    payload=dict(schema=2,status='complete',profile='c17-core/linux-x86-64-lp64-sysv-elf64-nonpie',
                 source_revision=revision(root),source_inputs=inputs,source_sha256=source_hash,
                 compiler_sha256=digest(compiler),host=dict(system=platform.system(),machine=platform.machine(),release=platform.release()),
                 configuration_inputs={str(p.relative_to(root)):digest(p) for p in sorted(root.glob('build*/CMakeCache.txt'))},
                 requested_gates=sorted(GATES),open_campaign_readers=[],gates=gates,
                 artifacts={str(p.relative_to(base)):dict(bytes=p.stat().st_size,sha256=digest(p))
                            for p in sorted(base.rglob('*')) if p.is_file() and p.name not in ('ACCEPTANCE.json','INCREMENTAL.json')})
    (base/'ACCEPTANCE.json').write_text(json.dumps(payload,indent=2,sort_keys=True)+'\n')
    verify(root,base/'ACCEPTANCE.json',compiler)
    print('Full acceptance complete: all twenty native gates assembled and verified.',flush=True)


if __name__=='__main__':main()
