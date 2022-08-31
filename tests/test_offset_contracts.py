#!/usr/bin/env python3
"""Check target offsetof and fundamental headers through canonical IR and native objects."""
import hashlib
import json
import pathlib
import platform
import subprocess
import sys

compiler = pathlib.Path(sys.argv[1]).resolve()
irtool = compiler.parent / 'cinderir'
identity = hashlib.sha256(compiler.read_bytes()+irtool.read_bytes()).hexdigest()
root = pathlib.Path('.agent-local/offset-contracts') / identity
root.mkdir(parents=True, exist_ok=True)
records = []
native = platform.system() == "Linux" and platform.machine() == "x86_64"

def run(argv):
    return subprocess.run([str(arg) for arg in argv], capture_output=True, text=True, timeout=20)

def save(row):
    records.append(row); (root/'observations.json').write_text(json.dumps(records, indent=2)+'\n')

for source in sorted(pathlib.Path('tests/offset').glob('*.c')):
    for level in ('-O0', '-O2'):
        cir = root/(source.stem+level+'.cir'); canonical = cir.with_suffix('.again.cir')
        obj, parsed = cir.with_suffix('.o'), cir.with_suffix('.parsed.o')
        for argv in ([compiler, '--serialize-ir', level, '-fverify-each', source, '-o', cir], [irtool, cir, '-o', canonical], [compiler, '-c', level, source, '-o', obj], [irtool, '-c', cir, '-o', parsed]):
            result = run(argv); assert result.returncode == 0, (argv, result)
        value = json.loads(run([irtool, '--classify', cir]).stdout)
        assert value['valid'] and value['integer'] == 0, (source, level, value)
        assert cir.read_bytes() == canonical.read_bytes() and obj.read_bytes() == parsed.read_bytes(), (source, level)
        if native:
            executable=cir.with_suffix('.native');result=run(['cc','-no-pie',parsed,'-o',executable]);assert result.returncode==0,result
            result=run([executable.resolve()]);assert result.returncode==0 and not result.stdout and not result.stderr,(source,level,result)
        save(dict(kind='authored', native=native, native_exit=result.returncode if native else None, source=str(source), source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), level=level, interpreter=value, ir_sha256=hashlib.sha256(cir.read_bytes()).hexdigest(), object_sha256=hashlib.sha256(obj.read_bytes()).hexdigest()))

assert hashlib.sha256(compiler.read_bytes()+irtool.read_bytes()).hexdigest()==identity
print(f"Offset/header contracts: {len(records)} authored CIR executions and object identities passed; native={native}.")

# These generated boundary probes are separate from the authored observation ledger.
for depth in (8, 64, 129, 1024):
    source=root/f'nesting-{depth}.c'
    index='0'
    for _ in range(depth): index=f'__cinder_offsetof(struct A, a[{index}])'
    source.write_text('#include <stddef.h>\nstruct A { int a[2]; };\nint main(void) { return '+index+'; }\n')
    output=source.with_suffix('.o');output.write_bytes(b'previous complete object')
    result=run([compiler, '-c', source, '-o', output])
    if depth <= 64: assert result.returncode==0 and output.read_bytes().startswith(b'\x7fELF'),result
    else: assert result.returncode>0 and 'offsetof nesting exceeds' in result.stderr and 'Sanitizer' not in result.stderr and output.read_bytes()==b'previous complete object',result
    save(dict(kind='generated-nesting', depth=depth, source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), exit=result.returncode, stderr=result.stderr))

for length in (256, 257):
    source=root/f'path-{length}.c'
    declarations=['struct T0 { int x; };']
    declarations += [f'struct T{i} {{ struct T{i-1} child; }};' for i in range(1,length)]
    designator='child.'*(length-1)+'x'
    source.write_text('#include <stddef.h>\n'+'\n'.join(declarations)+f'\nint main(void) {{ return offsetof(struct T{length-1}, {designator}); }}\n')
    result=run([compiler,'-fsyntax-only',source])
    if length==256: assert result.returncode==0,result
    else: assert result.returncode>0 and 'offsetof designator exceeds' in result.stderr and 'Sanitizer' not in result.stderr,result
    save(dict(kind='generated-path', length=length, source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), exit=result.returncode, stderr=result.stderr))

invalid={
    'dynamic-index':'struct A { int a[5]; }; int main(void) { int n=1; return offsetof(struct A, a[n]); }',
    'negative-index':'struct A { int a[5]; }; int main(void) { return offsetof(struct A, a[-1]); }',
    'past-array':'struct A { int a[5]; }; int main(void) { return offsetof(struct A, a[6]); }',
    'past-nested':'struct B { int x; }; struct A { struct B a[5]; }; int main(void) { return offsetof(struct A, a[5].x); }',
    'empty-designator':'struct A { int a; }; int main(void) { return offsetof(struct A, ); }',
    'missing-bracket':'struct A { int a[5]; }; int main(void) { return offsetof(struct A, a[1); }',
    'pointer-type':'struct A { int a; }; int main(void) { return offsetof(struct A *, a); }',
}
for name,text in invalid.items():
    source=root/(name+'.c');source.write_text('#include <stddef.h>\n'+text+'\n')
    output=source.with_suffix('.o');output.write_bytes(b'previous complete object')
    result=run([compiler,'-c',source,'-o',output])
    assert result.returncode>0 and 'error:' in result.stderr and 'Sanitizer' not in result.stderr and output.read_bytes()==b'previous complete object',result
    save(dict(kind='generated-invalid', name=name, source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), exit=result.returncode, stderr=result.stderr))

assert hashlib.sha256(compiler.read_bytes()+irtool.read_bytes()).hexdigest()==identity
print('Offset/header boundaries: 6 bounded parser probes and 7 invalid/profile designators passed.')
