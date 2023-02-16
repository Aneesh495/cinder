#!/usr/bin/env python3
"""Reject missing and consistently rehashed application evidence corruption."""
import argparse
import json
from pathlib import Path
import shutil
import sys
import tempfile
sys.path.insert(0,str(Path('tools').resolve()))
from application_evidence import verify_applications
from evidence_integrity import EvidenceError,digest
from gate_registry import GATES,read_gate_report


def main():
    parser=argparse.ArgumentParser();parser.add_argument('compiler',type=Path);parser.add_argument('--report',type=Path);args=parser.parse_args();compiler=args.compiler.resolve()
    reports=[p for p in Path('.agent-local/applications').glob('*/run-*/report.json') if json.loads(p.read_text()).get('compiler_sha256')==digest(compiler)]
    original=args.report.resolve() if args.report else max(reports,key=lambda p:p.stat().st_mtime).parent.resolve()
    profile=json.loads((original/'report.json').read_text())['execution_profile'];native=profile=='native'
    proof=verify_applications(original,Path('.'),compiler,require_native=native)
    assert proof['applications']==8 and proof['cases']==40
    envelope=dict(status='pass',unit=GATES['applications']['unit'],observations=8,raw_directory=original.name)
    if native:
        assert read_gate_report('applications',envelope,Path('.'),original.parent,compiler)['applications']==8
    else:
        try:read_gate_report('applications',envelope,Path('.'),original.parent,compiler)
        except EvidenceError:pass
        else:raise AssertionError('non-native application result satisfied the gate')
    for field,value in (('observations',0),('observations',999999),('unit','attempted-objects'),('status','skipped'),('raw_directory','../escape')):
        altered=dict(envelope);altered[field]=value
        try:read_gate_report('applications',altered,Path('.'),original.parent,compiler)
        except (EvidenceError,OSError,ValueError):pass
        else:raise AssertionError('accepted substituted application gate '+field)
    def rehash(base,data,name,contents):
        path=base/name;path.write_bytes(contents);data['artifacts'][name]=digest(path)
    def wrong_reference(base,data):
        row=next(c for a in data['observations'] for c in a['executions'] if c['implementation']=='gcc')
        command=data['commands'][row['command']];rehash(base,data,command['stdout'],b'wrong reference result\n')
    def failed_target(base,data):
        row=next(c for a in data['observations'] for c in a['executions'] if c['implementation']=='clang')
        data['commands'][row['command']]['exit']=99
        rehash(base,data,'commands.json',(json.dumps(data['commands'])+'\n').encode())
    def foreign_object(base,data):
        obj=data['observations'][0]['objects'][0];rehash(base,data,obj['path'],b'not a target object\n');obj['sha256']=digest(base/obj['path'])
    def remove_object(base,data):
        name=data['observations'][0]['objects'][0]['path'];(base/name).unlink();data['artifacts'].pop(name)
    def host_fallback(base,data):
        row=next(c for c in data['commands'] if '-fverify-each' in c['argv']);row['argv'][0]=data['tools']['gcc']['path']
        rehash(base,data,'commands.json',(json.dumps(data['commands'])+'\n').encode())
    def no_commands(base,data):
        data['commands'].pop();rehash(base,data,'commands.json',(json.dumps(data['commands'])+'\n').encode())
    mutations=[('missing-app',lambda b,d:d['observations'].pop()),('missing-case',lambda b,d:d['observations'][0]['executions'].pop()),('zero-progress',lambda b,d:d.update(reference_executions=0)),('changed-input',lambda b,d:d['inputs'].pop('source/ir.c')),('foreign-compiler',lambda b,d:d.update(compiler_sha256='0'*64)),('missing-object',remove_object),('rehashed-object',foreign_object),('rehashed-reference',wrong_reference),('rehashed-command-exit',failed_target),('hidden-host-fallback',host_fallback),('truncated-commands',no_commands),('fake-count',lambda b,d:d.update(target_executions=999999))]
    def copy(old,new):
        if Path(old).parent.name=='tools':Path(new).hardlink_to(old)
        else:shutil.copyfile(old,new)
    with tempfile.TemporaryDirectory(prefix='application-reader-') as temporary:
        for label,mutate in mutations:
            base=Path(temporary)/label;shutil.copytree(original,base,copy_function=copy);data=json.loads((base/'report.json').read_text());mutate(base,data);(base/'report.json').write_text(json.dumps(data)+'\n')
            try:verify_applications(base,Path('.'),compiler,require_native=native)
            except (EvidenceError,OSError,ValueError,KeyError,IndexError):pass
            else:raise AssertionError('application reader accepted '+label)
    if not native:
        try:verify_applications(original,Path('.'),compiler)
        except EvidenceError:pass
        else:raise AssertionError('cross/emulated result became native application acceptance')
    print(f'Application evidence: {len(mutations)} missing, changed and consistently rehashed controls rejected; native={native}')


if __name__=='__main__':main()
