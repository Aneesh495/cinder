#!/usr/bin/env python3
"""Verify authored target headers against system libc declarations and actual calls."""
import hashlib
import json
import pathlib
import platform
import shutil
import subprocess
import struct
import sys
from native_profile import configure_stack
from reference_policy import identify
from test_objects import inspect

compiler=pathlib.Path(sys.argv[1]).resolve()
irtool=compiler.parent/'cinderir'
group=sys.argv[2] if len(sys.argv)>2 else 'runtime'
assert group in ('runtime','flexible_allocated')
headers=sorted(pathlib.Path('runtime/include').rglob('*.h'))
inputs=headers+sorted(pathlib.Path('tests',group).glob('*.c'))+[pathlib.Path(__file__)]
hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
identity=hashlib.sha256(compiler.read_bytes()+irtool.read_bytes()+json.dumps(hashes,sort_keys=True).encode()).hexdigest()
root=pathlib.Path('.agent-local/runtime-headers' if group=='runtime' else '.agent-local/flexible-allocated')/identity;root.mkdir(parents=True,exist_ok=True)
native=platform.system()=='Linux' and platform.machine()=='x86_64'
stack=configure_stack()
references=[shutil.which('gcc-15') or shutil.which('gcc'),shutil.which('clang')];assert all(references)
reference_tools={reference:dict(identify(reference),binary_sha256=hashlib.sha256(pathlib.Path(reference).resolve().read_bytes()).hexdigest()) for reference in references}
for path in inputs:
    copy=root/'input-snapshot'/path;copy.parent.mkdir(parents=True,exist_ok=True);copy.write_bytes(path.read_bytes())
rows=[]
failures=[]
def run(argv,timeout=30):
    command=[str(a) for a in argv]
    try:
        result=subprocess.run(command,capture_output=True,text=True,timeout=timeout)
    except subprocess.TimeoutExpired as error:
        failures.append(dict(argv=command,timeout=error.timeout));(root/'failures.json').write_text(json.dumps(failures,indent=2)+'\n');raise
    if result.returncode!=0:
        failures.append(dict(argv=command,exit=result.returncode,stdout=result.stdout,stderr=result.stderr));(root/'failures.json').write_text(json.dumps(failures,indent=2)+'\n')
    assert result.returncode==0,(argv,result)
    return result

def execute(path):
    result=run([path.resolve()],10);assert not result.stdout and not result.stderr,result
    return dict(exit=result.returncode,stdout=result.stdout,stderr=result.stderr,executable_sha256=hashlib.sha256(path.read_bytes()).hexdigest())

def relocations(record):
    values = []
    sections = list(record['sections'])
    for r in record['relocations']:
        if r['symbol_binding'] == 0 and r['symbol_section'] != 0:
            target = ('section', sections[r['symbol_section']], r['symbol_value'] + r['addend'])
        else:
            target = ('symbol', r['symbol'], r['addend'])
        values.append((r['section'], r['offset'], r['type'], target))
    return sorted(values)
def contents(path):
    data = path.read_bytes()
    header = struct.unpack_from('<16sHHIQQQIHHHHHH', data)
    table = [struct.unpack_from('<IIQQQQIIQQ', data, header[6] + index * header[11]) for index in range(header[12])]
    names = table[header[13]]; strings = data[names[4]:names[4]+names[5]]
    result = {}
    for section in table:
        name = strings[section[0]:strings.index(0, section[0])].decode()
        if name in ('.text', '.data', '.rodata'):
            result[name] = data[section[4]:section[4]+section[5]]
    return result

for source in sorted(pathlib.Path('tests',group).glob('*.c')):
    row=dict(source=str(source),source_sha256=hashes[str(source)],references=[],objects=[],native=native)
    for reference in references:
        for level in ('-O0','-O2'):
            output=root/(source.stem+pathlib.Path(reference).name+level+'.reference')
            command=[reference,'-std=c17','-D_POSIX_C_SOURCE=200809L','-D_XOPEN_SOURCE=700','-D_DARWIN_C_SOURCE',level,source,'-o',output]
            run(command);row['references'].append(dict(argv=[str(a) for a in command],**execute(output)))
    for level in ('-O0','-O2'):
        prefix=root/(source.stem+level)
        obj=prefix.with_suffix('.o');assembly=prefix.with_suffix('.s');assembled=prefix.with_suffix('.assembled.o')
        cir=prefix.with_suffix('.cir');parsed=prefix.with_suffix('.parsed.o');canonical=prefix.with_suffix('.again.cir')
        for command in ([compiler,'-fverify-each',level,'-c',source,'-o',obj],[compiler,'-fverify-each',level,'-S',source,'-o',assembly],[compiler,'-fverify-each',level,'--serialize-ir',source,'-o',cir],[irtool,cir,'-o',canonical],[irtool,'-c',cir,'-o',parsed],[references[1],'-target','x86_64-linux-gnu','-c',assembly,'-o',assembled]):run(command)
        actual=inspect(obj);oracle=inspect(assembled,strict=False)
        assert contents(obj)==contents(assembled) and relocations(actual)==relocations(oracle),(source,level,'object/assembly disagreement')
        assert obj.read_bytes()==parsed.read_bytes() and cir.read_bytes()==canonical.read_bytes(),(source,level)
        observation=json.loads(run([irtool,'--classify',cir]).stdout)
        if source.name=='header_layout.c':assert observation['valid'] and observation['integer']==0,observation
        if group=='flexible_allocated':assert not observation['valid'] and observation['class']=='unsupported',observation
        record=dict(level=level,interpreter=observation,ir_sha256=hashlib.sha256(cir.read_bytes()).hexdigest(),objects={kind:hashlib.sha256(path.read_bytes()).hexdigest() for kind,path in [('owned',obj),('assembled',assembled),('parsed',parsed)]},executions=[])
        if native:
            for kind,path in [('owned',obj),('assembled',assembled),('parsed',parsed)]:
                output=prefix.with_suffix('.'+kind+'.native');run(['cc','-no-pie',path,'-o',output]);record['executions'].append(dict(kind=kind,**execute(output)))
        row['objects'].append(record)
    rows.append(row);(root/'observations.json').write_text(json.dumps(dict(compiler_sha256=hashlib.sha256(compiler.read_bytes()).hexdigest(),irtool_sha256=hashlib.sha256(irtool.read_bytes()).hexdigest(),inputs=hashes,reference_tools=reference_tools,native=native,stack_profile=stack,cases=rows),indent=2)+'\n')
    print(group+' probe:',source.name,'passed; native='+str(native),flush=True)
assert hashes=={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
assert identity==hashlib.sha256(compiler.read_bytes()+irtool.read_bytes()+json.dumps(hashes,sort_keys=True).encode()).hexdigest()
print(f'{group}: {len(rows)} authored probes passed; native={native}. External libc interpreter calls are reported separately.')
