#!/usr/bin/env python3
"""Mutate complete retained native evidence and prove read-only rejection."""
import argparse
import hashlib
import json
import os
import pathlib
import shutil
import sys
import tempfile

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from evidence_integrity import EvidenceError, digest
from native_evidence import verify_bootstrap

parser=argparse.ArgumentParser()
parser.add_argument('artifacts',type=pathlib.Path)
parser.add_argument('--source',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[1])
args=parser.parse_args();original=args.artifacts.resolve();source=args.source.resolve()


def states(base):
    return {str(p.relative_to(base)):(p.stat().st_size,p.stat().st_mtime_ns) for p in base.rglob('*') if p.is_file()}


def replace(path,data):
    # Detach a temporary overlay from its read-only hard-linked original.
    temporary=path.with_name(path.name+'.replacement')
    temporary.write_bytes(data);temporary.replace(path)


before=states(original);source_before=states(source/'source')
result=verify_bootstrap(original,source)
assert result['programs']==1000 and result['native']==8000 and result['interpreted']==4000
mutations=[]
with tempfile.TemporaryDirectory(prefix='cinder-native-reader-') as temp:
    overlay=pathlib.Path(temp)/'artifacts';shutil.copytree(original,overlay,copy_function=os.link)
    original_manifest=(overlay/'manifest.json').read_bytes()
    original_summary=(overlay/'generated/summary.json').read_bytes()
    first=overlay/'generated/case-00000/observations.json';original_case=first.read_bytes()
    def check(name,changes):
        backups={str(path):path.read_bytes() for path in changes}
        for path,data in changes.items():replace(path,data)
        try:
            verify_bootstrap(overlay,source)
        except (EvidenceError,OSError,ValueError,KeyError,TypeError) as error:
            mutations.append(dict(name=name,error=str(error)))
        else:
            raise AssertionError('accepted corrupt native evidence: '+name)
        finally:
            for path,data in backups.items():replace(pathlib.Path(path),data)
    def manifest_change(name,edit):
        data=json.loads(original_manifest);edit(data);check(name,{overlay/'manifest.json':(json.dumps(data)+'\n').encode()})
    manifest_change('unfinished-bootstrap',lambda d:d.update(status='running'))
    manifest_change('emulated-profile',lambda d:d.update(execution_profile='emulated'))
    manifest_change('missing-module',lambda d:d['modules'].pop())
    manifest_change('unmatched-stage-object',lambda d:d['stages']['stage3']['objects'].update({'main.o':'0'*64}))
    manifest_change('missing-authored-command',lambda d:d['commands'].pop(next(i for i,c in enumerate(d['commands']) if c['role']=='stage3-authored')))
    manifest_change('failed-command',lambda d:d['commands'][0].update(exit=1))
    manifest_change('host-fallback-stage',lambda d:next(c for c in d['commands'] if c['role']=='stage2-compile')['argv'].__setitem__(0,d['linker']['path']))
    manifest_change('missing-dependency',lambda d:d['dependencies'].pop('crt1.o'))
    manifest=json.loads(original_manifest)
    dependency=next(iter(manifest['dependencies'].values()))['artifact']
    check('corrupt-link-dependency',{overlay/dependency:b'changed dependency'})
    check('corrupt-stage-binary',{overlay/'stage3/cindercc':b'changed original compiler'})
    check('changed-source-snapshot',{overlay/'input-snapshot/source/type.c':b'int substituted;\n'})
    check('truncated-generated-summary',{overlay/'generated/summary.json':b'{"schema":'})
    def case_change(name,edit):
        row=json.loads(original_case);edit(row);data=(json.dumps(row)+'\n').encode()
        summary=json.loads(original_summary);summary['reports']['case-00000/observations.json']=hashlib.sha256(data).hexdigest();summary_bytes=(json.dumps(summary)+'\n').encode()
        manifest=json.loads(original_manifest);manifest['generated_summary_sha256']=hashlib.sha256(summary_bytes).hexdigest()
        check(name,{first:data,overlay/'generated/summary.json':summary_bytes,overlay/'manifest.json':(json.dumps(manifest)+'\n').encode()})
    case_change('failed-reference-native',lambda d:d['native'][0].update(exit=(d['expected']+1)%256))
    case_change('missing-native-level',lambda d:d['native'].pop())
    case_change('inflated-eligible-index',lambda d:d.update(index=1))
    case_change('missing-interpreter-level',lambda d:d['interpreted'].pop())
    case_change('missing-original-unit',lambda d:d['objects'].pop(next(iter(d['objects']))))
    case_change('fallback-generated-command',lambda d:next(c for c in d['commands'] if c['role']=='owned-compile')['argv'].__setitem__(0,'/usr/bin/gcc'))
    case_change('native-output-changed',lambda d:d['native'][0].update(stdout='unexpected\n'))
    case_change('interpreter-command-changed',lambda d:next(c for c in d['commands'] if c['role']=='owned-interpreter').update(stdout='interpret main => 0\n'))
assert len(mutations)==20
assert states(original)==before and states(source/'source')==source_before,'reader changed input artifacts or source timestamps'
output=original/'reader-checks.json';output.write_text(json.dumps(dict(positive=result,mutations=mutations,read_only=True),indent=2)+'\n')
print('Native reader: complete real bootstrap accepted; 20 retained evidence corruptions rejected; original inputs unchanged.')
