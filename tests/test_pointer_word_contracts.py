#!/usr/bin/env python3
"""Check integer address authority through real CIR and retained memory guards."""
import hashlib
import json
import pathlib
import platform
import subprocess
import sys

compiler = pathlib.Path(sys.argv[1]).resolve()
irtool = compiler.parent / 'cinderir'
project = pathlib.Path('.').resolve()
inputs = [pathlib.Path('tests/test_pointer_word_contracts.py'), pathlib.Path('CMakeLists.txt')]
inputs += sorted(pathlib.Path('source').glob('*.[ch]'))
inputs += [p for p in sorted(pathlib.Path('runtime').rglob('*')) if p.is_file()]
inputs += sorted(pathlib.Path('tests/pointer_words').glob('*.c')) + [pathlib.Path('tests/control/cases.json')]
hash_file = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
hashes = {str(p): hash_file(p) for p in inputs}
identity = hashlib.sha256(compiler.read_bytes() + irtool.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
root = pathlib.Path('.agent-local/pointer-word-contracts') / identity
root.mkdir(parents=True, exist_ok=True)
native = platform.system() == 'Linux' and platform.machine() == 'x86_64'
rows, commands = [], []


def run(argv, expected=0):
    result = subprocess.run(list(map(str, argv)), capture_output=True, text=True, timeout=30)
    commands.append(dict(argv=list(map(str, argv)), exit=result.returncode, stdout=result.stdout, stderr=result.stderr))
    (root / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    assert result.returncode == expected, (argv, result)
    return result


def save(row):
    rows.append(row)
    (root / 'observations.json').write_text(json.dumps(dict(inputs=hashes, compiler_sha256=hash_file(compiler),
        irtool_sha256=hash_file(irtool), native=native, cases=rows), indent=2) + '\n')


ledger = {pathlib.Path('tests/control', row['source']).resolve(): row['exit']
          for row in json.loads(pathlib.Path('tests/control/cases.json').read_text())}
for source in sorted(pathlib.Path('tests/pointer_words').glob('*.c')):
    expected = ledger[source.resolve()]
    objects = []
    for level in ('-O0', '-O2'):
        cir = root / (source.stem + level + '.cir')
        canonical = cir.with_suffix('.again.cir')
        obj = cir.with_suffix('.o')
        original = cir.with_suffix('.source.o')
        run([compiler, level, '--serialize-ir', source, '-o', cir])
        outcome = json.loads(run([irtool, '--classify', cir]).stdout)
        assert outcome['valid'] and outcome['classification'] == 0 and outcome['integer'] == expected, (source, level, outcome)
        run([irtool, cir, '-o', canonical])
        run([irtool, '-c', cir, '-o', obj])
        run([compiler, level, '-c', source, '-o', original])
        assert cir.read_bytes() == canonical.read_bytes() and obj.read_bytes() == original.read_bytes()
        record = dict(level=level, object=str(obj), sha256=hash_file(obj), ir_sha256=hash_file(cir), interpreter=outcome)
        if native:
            executable = obj.with_suffix('.native')
            run(['cc', '-no-pie', obj, '-o', executable])
            result = run([executable.resolve()], expected)
            assert result.stdout == result.stderr == ''
            record.update(native_exit=result.returncode, executable_sha256=hash_file(executable))
        objects.append(record)
    save(dict(source=source.name, source_sha256=hash_file(source), expected=expected, objects=objects))

# These are interpreter-model guards. A guessed process address, truncated
# address or combination of independent address domains is not a native
# differential oracle. Never execute the invalid dereferences as references.
guards = [
    ('readonly', 'const int x=3; uintptr_t w=(uintptr_t)(const void *)&x; w^=7; w^=7; *(int *)(void *)w=4; return 0;', 12),
    ('retired', 'uintptr_t w; { int x=3; w=(uintptr_t)(void *)&x; w^=7; } w^=7; return *(int *)(void *)w;', 10),
    ('outside-domain', 'int x[2]={3,5}; uintptr_t w=(uintptr_t)(void *)x; w+=3*sizeof(int); return *(int *)(void *)w;', 9),
    ('one-past-read', 'int x[2]={3,5}; uintptr_t w=(uintptr_t)(void *)x; w+=2*sizeof(int); return *(int *)(void *)w;', 9),
    ('forged-token', 'int x=3; (void)x; uintptr_t w=0x100000000UL; return *(int *)(void *)w;', 11),
    ('truncated-token', 'int x=3; unsigned narrow=(unsigned)(uintptr_t)(void *)&x; return *(int *)(void *)(uintptr_t)narrow;', 11),
    ('byte-overwrite', 'int x=3; uintptr_t w=(uintptr_t)(void *)&x; ((unsigned char *)&w)[0]^=1; return *(int *)(void *)w;', 11),
    ('mixed-domains', 'int x=3,y=5; uintptr_t a=(uintptr_t)(void *)&x,b=(uintptr_t)(void *)&y; uintptr_t w=(a^b)^b; return *(int *)(void *)w;', 6),
]
for name, body, expected in guards:
    source = root / (name + '.guard.c')
    source.write_text('#include <stdint.h>\nint main(void) { ' + body + ' }\n')
    for level in ('-O0', '-O2'):
        cir = root / (name + level + '.guard.cir')
        run([compiler, level, '--serialize-ir', source, '-o', cir])
        result = run([irtool, '--classify', cir])
        outcome = json.loads(result.stdout)
        assert not outcome['valid'] and outcome['classification'] == expected, (name, level, outcome)
        save(dict(kind='interpreter-model-guard', name=name, level=level, classification=expected,
                  source_sha256=hash_file(source), ir_sha256=hash_file(cir), outcome=outcome))

assert hashes == {str(p): hash_file(p) for p in inputs}, 'word contract inputs changed during verification'
assert identity == hashlib.sha256(compiler.read_bytes() + irtool.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
print(f'Pointer words: {len(list(pathlib.Path("tests/pointer_words").glob("*.c")))} authored round trips, exact CIR/object identity and {len(guards)} model guards passed; native={native}')
