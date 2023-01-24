#!/usr/bin/env python3
"""Exercise allocated copy schedules and conditional single-predecessor phi edges."""
import argparse
import hashlib
import json
import pathlib
import platform
import shutil
import subprocess
import tempfile

from test_unwind import digest, elf, tool


def reference(seed, single):
    if single:
        expected = 7 if seed & 2 or not seed & 1 else 9
        return f'int main(void) {{ int a=7,b=9; if ({int(not seed & 1)}) return a; return {"a" if seed & 2 else "b"}; }}\n', expected
    kind = ('int', 'float', 'double')[seed % 3]; a, b, c = [1 + (seed + n) % 3 for n in range(3)]
    iterations = 1 + seed % 31
    for _ in range(iterations): a, b, c = b, c, a
    expected = a * 100 + b * 10 + c
    initial = [1 + (seed + n) % 3 for n in range(3)]
    text = f'int main(void) {{ {kind} a={initial[0]},b={initial[1]},c={initial[2]}; for(int i=0;i<{iterations};++i) {{ {kind} t=a; a=b; b=c; c=t; }} return (int)(a*100+b*10+c); }}\n'
    return text, expected


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('compiler', type=pathlib.Path); parser.add_argument('--baseline-probe', type=pathlib.Path)
    parser.add_argument('--execution-profile', choices=('native', 'emulated', 'cross')); args = parser.parse_args()
    compiler = args.compiler.resolve(); identity = digest(compiler); linux = platform.system() == 'Linux' and platform.machine() == 'x86_64'
    profile = args.execution_profile or ('native' if linux else 'cross'); execute = profile in ('native','emulated'); assert not execute or linux
    files = sorted(pathlib.Path('source').glob('*.[ch]')) + sorted(pathlib.Path('runtime/include').rglob('*.h'))
    files += [pathlib.Path(p) for p in ('tests/ssa_probe.c','tests/test_mir_copies.py','tests/test_unwind.py','CMakeLists.txt')]
    inputs = {str(p):digest(p) for p in files}; source_hash = hashlib.sha256(json.dumps(inputs,sort_keys=True).encode()).hexdigest()
    base = pathlib.Path('.agent-local/mir-copies') / (source_hash+'-'+identity); base.mkdir(parents=True,exist_ok=True)
    root = pathlib.Path(tempfile.mkdtemp(prefix='run-',dir=base)).resolve(); artifacts=set(); commands=[]; observations=[]
    paths = {name:tool(name) for name in ('llvm-objdump','clang')}; paths['gcc']=tool('gcc-15' if shutil.which('gcc-15') else 'gcc')
    if execute: paths['cc']=tool('cc')
    tools={name:dict(path=path,sha256=digest(pathlib.Path(path))) for name,path in paths.items()}
    def run(argv,label,expected=0):
        argv=list(map(str,argv)); result=subprocess.run(argv,capture_output=True,timeout=120)
        out,err=root/(label+'.stdout'),root/(label+'.stderr');out.write_bytes(result.stdout);err.write_bytes(result.stderr);artifacts.update((out,err))
        commands.append(dict(argv=argv,exit=result.returncode,stdout=digest(out),stderr=digest(err)));(root/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        assert result.returncode==expected,(argv,result); return result.stdout.decode(errors='replace')
    for name,path in paths.items(): run([path,'--version'],name+'-version')
    core_inputs={}
    if execute:
        probe,obj=root/'ssa-probe',root/'ssa-probe.o'
        run([compiler,'-O0','-c','-Isource','-Iruntime/include','tests/ssa_probe.c','-o',obj],'probe-compile')
        core=[compiler.parent/'libcinder_core.a'] if (compiler.parent/'libcinder_core.a').is_file() else sorted(p for p in compiler.parent.glob('*.o') if p.name not in ('main.o','ir_main.o'))
        assert core;core_inputs={str(p):digest(p) for p in core};linker,flags=paths['cc'],[];cache=compiler.parent/'CMakeCache.txt'
        if cache.is_file() and 'CINDER_SANITIZE:BOOL=ON' in cache.read_text().splitlines():
            linker=next(line.split('=',1)[1] for line in cache.read_text().splitlines() if line.startswith('CMAKE_C_COMPILER:'));flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        run([linker,'-no-pie',*flags,obj,*core,'-o',probe],'probe-link');artifacts.update((probe,obj))
    else:
        probe=compiler.parent/'cinder_ssa_probe';assert probe.is_file()
        library=compiler.parent/'libcinder_core.a'
        if library.is_file():core_inputs[str(library)]=digest(library)
    cache=compiler.parent/'CMakeCache.txt'
    if cache.is_file():core_inputs[str(cache)]=digest(cache)
    probe_hash=digest(probe);baseline_hash=digest(args.baseline_probe.resolve()) if args.baseline_probe else None
    for single,count in ((False,36),(True,4)):
        for seed in range(count):
            label=('single-' if single else 'cycle-')+str(seed); obj=root/(label+'.o')
            row=json.loads(run([probe,str(seed),obj,*(['single'] if single else [])],label+'-probe'))
            text,expected=reference(seed,single); assert row['expected']==row['interpreter']==expected and row['rejected_copies']==9 and row['single_predecessor']==single
            assert row['physical_cycle']==(0 if single else 3) and row['split_edges']==(2 if single and not seed & 2 else 1)
            sections,symbols,_=elf(obj);assert sections['.text'] and any(s['kind']==2 and s['name']=='main' and s['size']>0 for s in symbols)
            run([paths['llvm-objdump'],'-dr',obj],label+'-disassemble');artifacts.add(obj)
            if args.baseline_probe and not single:
                baseline=root/(label+'.baseline.o');run([args.baseline_probe.resolve(),str(seed),baseline],label+'-baseline');artifacts.add(baseline);assert obj.read_bytes()==baseline.read_bytes()
            source=root/(label+'.c');source.write_text(text);artifacts.add(source)
            for name in ('gcc','clang',*(['cinder'] if execute else [])):
                binary=root/(label+'-'+name)
                if name=='cinder':run([paths['cc'],'-no-pie',obj,'-o',binary],label+'-link')
                else:run([paths[name],'-std=c17','-O0',source,'-o',binary],label+'-'+name+'-compile')
                run([binary],label+'-'+name+'-execute',expected%256);artifacts.add(binary)
            row.update(object_sha256=digest(obj),reference_sha256=digest(source));observations.append(row)
    assert len(observations)==40 and sum(r['rejected_copies'] for r in observations)==360
    assert digest(compiler)==identity and digest(probe)==probe_hash and inputs=={name:digest(pathlib.Path(name)) for name in inputs}
    assert all(digest(pathlib.Path(row['path']))==row['sha256'] for row in tools.values()) and all(digest(pathlib.Path(name))==value for name,value in core_inputs.items())
    if args.baseline_probe:assert digest(args.baseline_probe.resolve())==baseline_hash
    report=dict(schema=1,compiler_sha256=identity,source_sha256=source_hash,inputs=inputs,core_inputs=core_inputs,tools=tools,probe_sha256=probe_hash,baseline_sha256=baseline_hash,
                execution_profile=profile,native=profile=='native',target_execution=execute,observations=observations,commands=commands,artifacts={str(p.relative_to(root)):digest(p) for p in sorted(artifacts)})
    (root/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'Machine copies: 36 integer/SSE cyclic schedules, four conditional phi edges and 360 malformed schedules passed; native={report["native"]}; evidence={root}')


if __name__=='__main__':main()
