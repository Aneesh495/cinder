#!/usr/bin/env python3
"""Exercise in-memory includes without opening an external regular file or FIFO."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import tempfile
from test_unwind import digest,tool


def main():
    parser=argparse.ArgumentParser();parser.add_argument('compiler',type=Path);parser.add_argument('--execution-profile',choices=('native','emulated','cross'));args=parser.parse_args()
    compiler=args.compiler.resolve();linux=platform.system()=='Linux' and platform.machine()=='x86_64';profile=args.execution_profile or ('native' if linux else 'cross');execute=profile in ('native','emulated');assert not execute or linux
    files=sorted(Path('source').glob('*.[ch]'))+sorted(Path('runtime/include').rglob('*.h'))+[Path(p) for p in ('CMakeLists.txt','tests/source_buffer_probe.c','tests/test_source_buffers.py','tests/test_unwind.py')]
    inputs={str(p):digest(p) for p in files};identity=hashlib.sha256(json.dumps(inputs,sort_keys=True).encode()).hexdigest();base=Path('.agent-local/source-buffers')/(identity+'-'+digest(compiler));base.mkdir(parents=True,exist_ok=True);root=Path(tempfile.mkdtemp(prefix='run-',dir=base)).resolve()
    commands=[];artifacts=set();core_inputs={}
    def run(argv,label):
        result=subprocess.run(list(map(str,argv)),capture_output=True,timeout=120)
        out,err=root/(label+'.stdout'),root/(label+'.stderr');out.write_bytes(result.stdout);err.write_bytes(result.stderr);artifacts.update((out,err));commands.append(dict(argv=list(map(str,argv)),exit=result.returncode,stdout=digest(out),stderr=digest(err)));(root/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        assert result.returncode==0 and result.stderr==b'',(argv,result)
        return result.stdout
    if execute:
        probe=root/'source-buffer-probe';obj=root/'source-buffer-probe.o';run([compiler,'-O0','-c','-Isource','-Iruntime/include','tests/source_buffer_probe.c','-o',obj],'probe-compile')
        core=[compiler.parent/'libcinder_core.a'] if (compiler.parent/'libcinder_core.a').is_file() else sorted(p for p in compiler.parent.glob('*.o') if p.name not in ('main.o','ir_main.o'));assert core
        core_inputs={str(p):digest(p) for p in core};linker=tool('cc');flags=[];cache=compiler.parent/'CMakeCache.txt'
        if cache.is_file() and 'CINDER_SANITIZE:BOOL=ON' in cache.read_text().splitlines():
            linker=next(line.split('=',1)[1] for line in cache.read_text().splitlines() if line.startswith('CMAKE_C_COMPILER:'));flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        run([linker,'-no-pie',*flags,obj,*core,'-o',probe],'probe-link');artifacts.update((probe,obj))
    else:
        probe=compiler.parent/'cinder_source_buffer_probe';assert probe.is_file();core_inputs={str(probe):digest(probe)}
    probe_hash=digest(probe);compiler_hash=digest(compiler)
    fifo=root/'external.fifo';os.mkfifo(fifo);regular=root/'external.h';regular.write_text('#error external header must not be read in buffered mode\n');artifacts.add(regular)
    try:observed=json.loads(run([probe,fifo,regular],'buffered-pipeline'))
    finally:fifo.unlink()
    assert observed==dict(repeated_translation_units=1000,blocked_external_includes=2,rejected_buffers=5)
    assert inputs=={name:digest(Path(name)) for name in inputs} and digest(probe)==probe_hash and digest(compiler)==compiler_hash
    assert all(digest(Path(name))==value for name,value in core_inputs.items())
    (root/'report.json').write_text(json.dumps(dict(schema=1,execution_profile=profile,native=profile=='native',target_execution=execute,inputs=inputs,compiler_sha256=compiler_hash,probe_sha256=probe_hash,core_inputs=core_inputs,observed=observed,commands=commands,artifacts={str(p.relative_to(root)):digest(p) for p in sorted(artifacts)}),indent=2)+'\n')
    print('Buffered sources: 1,000 repeated typed pipelines, cached includes, blocked readable file/FIFO and five rejected buffers passed; evidence='+str(root))


if __name__=='__main__':main()
