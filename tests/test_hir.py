#!/usr/bin/env python3
"""Exercise owned expression HIR, semantic selection and its actual consumer."""
import hashlib
import json
import pathlib
import platform
import shutil
import subprocess
import sys

from test_objects import inspect

compiler = pathlib.Path(sys.argv[1]).resolve()
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
sources = sorted(pathlib.Path('source').glob('*.[ch]')) + sorted(pathlib.Path('runtime/include').rglob('*.h'))
sources += sorted(pathlib.Path('tests/hir').glob('*.c')) + [pathlib.Path(__file__), pathlib.Path('tests/hir_probe.c'), pathlib.Path('tests/test_objects.py'), pathlib.Path('CMakeLists.txt')]
hashes = {str(p): digest(p) for p in sources}
identity = hashlib.sha256(compiler.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
base = (pathlib.Path('.agent-local/hir') / identity).resolve(); base.mkdir(parents=True, exist_ok=True)
native = platform.system() == 'Linux' and platform.machine() == 'x86_64'
commands, rows = [], []

def run(argv, success=True):
    result = subprocess.run(list(map(str, argv)), capture_output=True, text=True, timeout=120)
    commands.append(dict(argv=list(map(str, argv)), exit=result.returncode, stdout=result.stdout, stderr=result.stderr))
    save()
    assert (result.returncode == 0) == success, commands[-1]
    return result

def save():
    (base / 'observations.json').write_text(json.dumps(dict(inputs=hashes, compiler_sha256=digest(compiler), native=native, commands=commands, cases=rows), indent=2) + '\n')

for filename, expected in (('owned-expression.c', 17), ('unevaluated.c', 16), ('returned-member.c', 10), ('update-once.c', 26), ('variadic-operations.c', 7)):
    source = pathlib.Path('tests/hir') / filename
    first = run([compiler, '--dump-hir', source]); second = run([compiler, '--dump-hir', source])
    assert first.stdout == second.stdout and first.stdout.startswith('typed expression HIR 1\n')
    assert '0x' not in first.stdout and ' load ' in first.stdout
    assert 'generic' not in first.stdout and 'sizeof' not in first.stdout
    if filename == 'unevaluated.c': assert 'name=unused' not in first.stdout and 'constant=4' in first.stdout and 'constant=9' in first.stdout
    if filename == 'owned-expression.c': assert ' address ' in first.stdout and ' convert ' in first.stdout and ' call ' in first.stdout
    if filename == 'returned-member.c': assert ' member-place ' in first.stdout and ' call ' in first.stdout
    if filename == 'update-once.c': assert ' update ' in first.stdout and ' index-place ' in first.stdout
    if filename == 'variadic-operations.c': assert ' va-start ' in first.stdout and ' va-copy ' in first.stdout and ' va-end ' in first.stdout
    dump = base / (filename + '.hir'); dump.write_text(first.stdout)
    row = dict(source=str(source), source_sha256=digest(source), expected=expected, hir_sha256=digest(dump), objects={})
    row['references'] = []
    for reference in (shutil.which('gcc-15') or shutil.which('gcc'), shutil.which('clang')):
        assert reference, 'GCC and Clang references are required'
        for level in ('-O0', '-O2'):
            executable = base / (filename + pathlib.Path(reference).name + level)
            run([reference, '-std=c17', level, source, '-o', executable])
            result = subprocess.run([str(executable)], capture_output=True, timeout=10)
            assert result.returncode == expected and not result.stdout and not result.stderr
            row['references'].append(dict(compiler=reference, level=level, exit=result.returncode, executable_sha256=digest(executable)))
    for level in ('-O0', '-O2'):
        result = run([compiler, level, '--interpret', source]); assert result.stdout == f'interpret main => {expected}\n'
        output = base / (filename + level + '.o'); run([compiler, level, '-c', source, '-o', output])
        assert inspect(output)['sections']['.text']['size'] > 0; row['objects'][level] = digest(output)
        if native:
            executable = output.with_suffix('.native'); run(['cc', '-no-pie', output, '-o', executable])
            result = subprocess.run([str(executable)], capture_output=True, timeout=10)
            assert result.returncode == expected and not result.stdout and not result.stderr
            row[level + '_native'] = dict(exit=result.returncode, executable_sha256=digest(executable))
    rows.append(row); save()

bad = base / 'bad.c'; bad.write_text('int main(void) { return unknown; }\n')
result = run([compiler, '--dump-hir', bad], False); assert not result.stdout
result = run([compiler, '-M', '--dump-hir', pathlib.Path('tests/hir/owned-expression.c')], False); assert not result.stdout
result = run([compiler, '-fsyntax-only', '--dump-hir', pathlib.Path('tests/hir/owned-expression.c')]); assert result.stdout.startswith('typed expression HIR 1\n')

if native:
    obj = base / 'hir-probe.o'; probe = base / 'hir-probe'
    run([compiler, '-c', '-I', pathlib.Path('source').resolve(), pathlib.Path('tests/hir_probe.c').resolve(), '-o', obj])
    library = compiler.parent / 'libcinder_core.a'
    core = [library] if library.is_file() else sorted(p for p in compiler.parent.glob('*.o') if p.name not in ('main.o', 'ir_main.o'))
    assert core, 'missing owned core for HIR probe linking'
    cache = compiler.parent / 'CMakeCache.txt'
    link_profile = []
    linker = 'cc'
    if cache.is_file() and 'CINDER_SANITIZE:BOOL=ON' in cache.read_text().splitlines():
        linker = next(line.split('=', 1)[1] for line in cache.read_text().splitlines() if line.startswith('CMAKE_C_COMPILER:FILEPATH='))
        link_profile = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    run([linker, '-no-pie', *link_profile, obj, *core, '-o', probe])
else:
    probe = compiler.parent / 'cinder_hir_probe'; assert probe.is_file(), 'build the cinder_hir_probe target first'
result = run([probe, pathlib.Path('tests/hir/owned-expression.c').resolve()])
assert result.stdout == 'HIR ownership: AST mutation isolated, HIR mutation consumed, ten malformed graphs rejected\n'
assert hashes == {str(p): digest(p) for p in sources}, 'HIR source changed during verification'
assert identity == hashlib.sha256(compiler.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
print('Typed HIR: five semantic/native-object fixtures, deterministic dumps, owned consumer and ten verifier rejections passed')
