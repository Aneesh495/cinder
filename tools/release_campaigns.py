#!/usr/bin/env python3
"""Complete native campaign producers. Raw observations remain independently readable."""
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import sys
import time

from evidence_integrity import digest, inventory_digest, revision, source_inputs

PROFILE = 'c17-core/linux-x86-64-lp64-sysv-elf64-nonpie'
BUILD_PROFILES = [('gcc-debug', 'gcc', 'Debug', False), ('gcc-release', 'gcc', 'Release', False),
                  ('clang-release', 'clang', 'Release', False), ('clang-sanitize', 'clang', 'RelWithDebInfo', True)]
BENCHMARKS = {
    'integer': 'int main(void){ unsigned x=1; for(unsigned i=0;i<3000000;i++) x=x*1664525U+1013904223U; return (int)(x&127U); }\n',
    'floating': 'int main(void){ double x=1.0; for(int i=0;i<1000000;i++) x=x*1.0000001+0.000001; return x>2.0 && x<3.0 ? 17 : 1; }\n',
    'memory': 'int main(void){ unsigned a[256]; for(unsigned i=0;i<256;i++) a[i]=i; for(unsigned k=0;k<20000;k++) for(unsigned i=0;i<256;i++) a[i]=(a[i]+k)^i; return (int)(a[37]&127U); }\n',
}
RECOVERY_KINDS = ('syntax', 'sema', 'include', 'utf8', 'missing-parent', 'directory', 'link', 'assembly', 'preprocess', 'explorer')


class Campaign:
    def __init__(self, gate, source, compiler, output):
        self.gate, self.source, self.compiler, self.output = gate, Path(source).resolve(), Path(compiler).resolve(), Path(output).resolve()
        self.output.mkdir(parents=True, exist_ok=False)
        self.inputs = source_inputs(self.source)
        self.commands = []
        self.env = dict(os.environ, SOURCE_DATE_EPOCH='0', LC_ALL='C', TZ='UTC', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        shutil.copy2(self.compiler, self.output/'compiler')
        self.data = dict(schema=1, gate=gate, profile=PROFILE, source_prefix=str(self.source),
                         inputs=self.inputs, source_sha256=inventory_digest(self.inputs), source_revision=revision(self.source),
                         compiler_path=str(self.compiler), compiler_sha256=digest(self.compiler),
                         machine=platform.uname()._asdict(), native=platform.system() == 'Linux' and platform.machine() == 'x86_64',
                         environment={k:self.env[k] for k in ('SOURCE_DATE_EPOCH','LC_ALL','TZ')}, commands=self.commands)

    def run(self, argv, role, expected=0, timeout=1800, cwd=None):
        command = list(map(str, argv)); started = time.monotonic()
        record = dict(role=role, argv=command, cwd=str(cwd or self.source), expected=expected)
        self.commands.append(record)
        self.save('running')
        try:
            result = subprocess.run(command, cwd=cwd or self.source, env=self.env, capture_output=True, timeout=timeout)
            record.update(exit=result.returncode, stdout=result.stdout.decode(errors='replace'), stderr=result.stderr.decode(errors='replace'), elapsed=time.monotonic()-started)
        except subprocess.TimeoutExpired:
            record.update(timeout=timeout, elapsed=time.monotonic()-started)
            self.save('failed'); raise
        self.save('running')
        assert result.returncode == expected, (self.gate, role, command, result.returncode, result.stderr[-4000:])
        return record

    def save(self, status):
        self.data['status'] = status
        (self.output/'campaign.json').write_text(json.dumps(self.data, indent=2)+'\n')

    def retain(self, path, name):
        destination = self.output/name
        if Path(path).is_dir(): shutil.copytree(path, destination)
        else: shutil.copy2(path, destination)
        return destination

    def latest(self, pattern, name):
        paths = list(self.source.glob(pattern))
        assert paths, pattern
        return self.retain(max(paths, key=lambda p:p.stat().st_mtime).parent, name)

    def finish(self):
        assert source_inputs(self.source) == self.inputs and digest(self.compiler) == self.data['compiler_sha256'], 'campaign inputs changed'
        self.data['artifacts'] = {str(p.relative_to(self.output)):dict(bytes=p.stat().st_size, sha256=digest(p))
                                  for p in sorted(self.output.rglob('*')) if p.is_file() and p != self.output/'campaign.json'}
        self.save('pass')
        from release_evidence import verify_campaign
        count = verify_campaign(self.gate, self.output, self.source, self.compiler)
        print(self.gate+': '+str(count)+' verified observations; evidence='+str(self.output), flush=True)
        return count


def feature_sources(source):
    ledger = json.loads((source/'tests/control/cases.json').read_text())
    authored = {str((source/'tests/control'/r['source']).resolve()):r['exit'] for r in ledger}
    families = json.loads((source/'docs/FEATURES.json').read_text())['families']
    rows = []
    for family in families:
        candidates = [p for p in family['valid_cases']+family['boundary_cases'] if str((source/p).resolve()) in authored]
        fallback = {'C02':'examples/macro_args.c', 'C03':'examples/macro_args.c', 'C22':'tests/release/operators.c',
                    'C46':'examples/hello.c', 'C48':'examples/hello.c'}
        name = candidates[0] if candidates else fallback[family['id']]
        rows.append(dict(id=family['id'], source=name, negative=family['negative_cases'][:1]))
    return rows


def features(c):
    rows = []
    for spec in feature_sources(c.source):
        directory = c.output/spec['id']; directory.mkdir()
        path = c.source/spec['source']; expected = None
        for host in ('gcc','clang'):
            binary = directory/host
            c.run([host,'-std=c17','-O0',path,'-o',binary], spec['id']+'-reference-compile')
            result = subprocess.run([str(binary)], capture_output=True, env=c.env, timeout=10)
            assert not result.stdout and not result.stderr
            if expected is None: expected = result.returncode
            c.run([binary],spec['id']+'-reference',expected)
        for level in ('-O0','-O2'):
            obj, binary = directory/(level+'.o'), directory/level
            c.run([c.compiler,level,'-g','-fverify-each','-c',path,'-o',obj],spec['id']+'-compile')
            c.run(['gcc','-no-pie',obj,'-o',binary],spec['id']+'-link')
            c.run([binary],spec['id']+'-native',expected)
            result = c.run([c.compiler,level,'--interpret',path],spec['id']+'-interpret')
            assert result['stdout'] == f'interpret main => {expected}\n'
        for negative in spec['negative']:
            c.run([c.compiler,'-fsyntax-only',c.source/negative],spec['id']+'-negative',1)
        rows.append(dict(spec, expected=expected))
    c.data['families'] = rows


def fresh(c):
    for name, host, kind, sanitize in BUILD_PROFILES:
        directory = c.output/name
        cc = str(Path(shutil.which(host)).resolve())
        c.run([cc,'--version'],name+'-version')
        c.run(['cmake','-S',c.source,'-B',directory,'-DCMAKE_C_COMPILER='+cc,'-DCMAKE_BUILD_TYPE='+kind,
               '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DCINDER_SANITIZE='+('ON' if sanitize else 'OFF')],name+'-configure')
        c.run(['cmake','--build',directory,'--target','cindercc','cinderir','cinder_source_buffer_probe','--parallel','4'],name+'-build')
        c.run(['tests/run_smoke.sh',directory/'cindercc'],name+'-smoke')
        c.run([sys.executable,'-B','tests/test_fuzz_regressions.py',directory/'cindercc'],name+'-regressions')
        c.run([sys.executable,'-B','tests/test_source_buffers.py',directory/'cindercc'],name+'-buffers')


def original(c):
    for name, inputs, expected in [('hello',['examples/hello.c'],42), ('globals',['examples/globals.c'],42),
                                   ('multi',['examples/tu_a.c','examples/tu_b.c'],9)]:
        directory=c.output/name;directory.mkdir()
        objects=[]
        for index, source in enumerate(inputs):
            obj=directory/f'{index}.o'
            c.run([c.compiler,'-O2','-c',c.source/source,'-o',obj],name+'-compile');objects.append(obj)
        c.run(['gcc','-no-pie',*objects,'-o',directory/'explicit'],name+'-link')
        c.run([directory/'explicit'],name+'-native',expected)
        c.run([c.compiler,'-O2',*[c.source/p for p in inputs],'-o',directory/'driver'],name+'-driver')
        c.run([directory/'driver'],name+'-driver-native',expected)


def recovery(c, count):
    cases=[]
    for index in range(count):
        kind=RECOVERY_KINDS[index%len(RECOVERY_KINDS)];directory=c.output/f'case-{index:04d}';directory.mkdir()
        source=directory/'input.c';destination=directory/'result'
        text={'syntax':'struct Broken { int f(void) { return 0; } };', 'sema':'int main(void){return absent;}',
              'include':'#include "absent-header.h"\n', 'utf8':'\xff', 'link':'int absent(void); int main(void){return absent();}',
              'assembly':'int main(void){return absent;}', 'preprocess':'#error forced failure\n',
              'explorer':'int main(void){return absent;}'}
        source.write_bytes((text.get(kind,'int main(void){return 7;}')+'\n').encode('latin1'))
        sentinel=f'previous complete artifact {index}\n'.encode();destination.write_bytes(sentinel)
        mode={'link':[], 'assembly':['-S'], 'preprocess':['-E'], 'explorer':['--explorer',str(directory/'report'),'-c']}.get(kind,['-c'])
        target=destination
        if kind=='missing-parent':target=directory/'absent/result'
        if kind=='directory':target=directory/'destination';target.mkdir()
        c.run([c.compiler,*mode,source,'-o',target],f'{index}-failure',1,timeout=15)
        assert destination.read_bytes()==sentinel and not list(directory.rglob('*.cinder-*'))
        source.write_text('int main(void){return 7;}\n')
        c.run([c.compiler,'-c',source,'-o',destination],f'{index}-recovered',timeout=15)
        assert destination.read_bytes().startswith(b'\x7fELF') and not list(directory.rglob('*.cinder-*'))
        cases.append(dict(index=index,kind=kind,sentinel=sentinel.decode(),object_sha256=digest(destination)))
    c.data['cases']=cases


def performance(c):
    rows=[]
    for name,text in BENCHMARKS.items():
        source=c.output/(name+'.c');source.write_text(text)
        expected=None
        for tool,cc in [('gcc','gcc'),('clang','clang'),('cinder',str(c.compiler))]:
            for level in ('-O0','-O2'):
                binary=c.output/(name+'-'+tool+level)
                obj=binary.with_suffix('.o')
                build=c.run([cc,level,'-c',source,'-o',obj],name+'-'+tool+level+'-compile')
                c.run(['gcc','-no-pie',obj,'-o',binary],name+'-'+tool+level+'-link')
                observed=subprocess.run([str(binary)],capture_output=True,env=c.env,timeout=30)
                assert not observed.stdout and not observed.stderr
                if expected is None:expected=observed.returncode
                assert observed.returncode==expected
                samples=[c.run([binary],name+'-'+tool+level+'-sample',expected,timeout=30)['elapsed'] for _ in range(5)]
                rows.append(dict(workload=name,tool=tool,level=level,expected=expected,compile_seconds=build['elapsed'],
                                 samples=samples,median_seconds=statistics.median(samples),object_bytes=obj.stat().st_size))
    c.data['measurements']=rows


def publication(c):
    sha=revision(c.source)
    c.run(['git','ls-remote','origin','refs/heads/main'],'remote')
    c.run(['gh','api','repos/Aneesh495/cinder/actions/runs?head_sha='+sha+'&per_page=100'],'hosted')
    c.data['revision']=sha


def produce(gate, source, compiler, output, count=None):
    c=Campaign(gate,source,compiler,output)
    assert c.data['native'], 'native Linux x86-64 is required'
    if gate=='fresh-build':fresh(c)
    elif gate=='original-pipeline':original(c)
    elif gate=='feature-families':features(c)
    elif gate=='authored-suite':
        c.run([sys.executable,'-B','tests/test_control.py',c.compiler],'authored')
        c.retain(c.source/'.agent-local/control'/digest(c.compiler),'authored')
    elif gate=='differential-programs':
        c.run([sys.executable,'-B','tools/run_native_programs.py','--compiler','cinder='+str(c.compiler),'--count',str(count or 20000),
               '--profile','native','--output',c.output/'generated'],'differential',timeout=28800)
    elif gate=='ir-semantics':
        c.run(['cmake','--build',c.compiler.parent,'--target','cinder_ir_campaign','--parallel','4'],'build-probe')
        c.run([sys.executable,'-B','tests/test_ir_campaign.py',c.compiler.parent/'cinder_ir_campaign',str(count or 100000)],'ir',timeout=3600)
        c.latest('.agent-local/ir-campaign/*/summary.json','ir')
    elif gate=='rewrite-validation':
        c.run([sys.executable,'-B','tools/run_rewrite_checks.py',c.compiler],'rewrites',timeout=3600)
        c.latest('.agent-local/rewrite-checks/*/summary.json','rewrites')
    elif gate=='abi':
        for script, flags, folder in [('tests/test_abi_callbacks.py',[],'abi-callbacks'),
                                      ('tests/test_abi_aggregates.py',[],'abi-aggregates'),
                                      ('tests/test_abi_aggregates.py',['--variadic'],'abi-variadic')]:
            c.run([sys.executable,'-B',script,c.compiler,*flags,'--count',str(count or 512)],folder,timeout=7200)
            c.latest('.agent-local/'+folder+'/*/*/summary.json',folder)
    elif gate=='allocation':
        c.run(['tests/run_allocation.sh'],'allocation')
        c.retain(c.source/'.agent-local/allocation/observations.jsonl','observations.jsonl')
    elif gate=='object-output':
        c.run([sys.executable,'-B','tests/test_objects.py',c.compiler,'--count',str(count or 1000)],'objects',timeout=7200)
        c.retain(c.source/'.agent-local/objects'/digest(c.compiler),'objects')
    elif gate=='debugging':
        c.run(['cmake','--build',c.compiler.parent,'--target','cinder_unwind_probe','cinder_debug_line_probe','cinder_debug_type_probe','--parallel','4'],'debug-probes')
        c.run(['tests/run_debug.sh',c.compiler],'debug',timeout=3600)
        for folder in ('unwind','debug-lines','debug-types','debug-parameters','debug-scopes','debug-values'):
            c.latest('.agent-local/'+folder+'/*/run-*/report.json',folder)
    elif gate=='failure-recovery':recovery(c,count or 1000)
    elif gate=='performance':performance(c)
    elif gate=='documentation-product':
        c.run([c.compiler,'--help'],'help');c.run([c.compiler,'--version'],'version')
        c.run([sys.executable,'-B','tests/test_explorer.py',c.compiler],'explorer')
        c.latest('.agent-local/explorer/*/run-*/report.json','explorer')
        c.run([sys.executable,'-B','tests/test_driver.py',c.compiler],'driver')
    elif gate=='publication':publication(c)
    else:raise ValueError('unknown release campaign: '+gate)
    return c.finish()


def main():
    parser=argparse.ArgumentParser();parser.add_argument('gate');parser.add_argument('compiler',type=Path)
    parser.add_argument('--output',type=Path,required=True);parser.add_argument('--count',type=int)
    args=parser.parse_args();produce(args.gate,Path(__file__).resolve().parents[1],args.compiler,args.output,args.count)


if __name__=='__main__':main()
