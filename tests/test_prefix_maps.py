#!/usr/bin/env python3
"""Check relocated objects, source names, map order and physical dependencies."""
import hashlib
import json
import os
import pathlib
import subprocess
import sys

from test_objects import inspect

compiler = pathlib.Path(sys.argv[1]).resolve()
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
inputs = sorted(pathlib.Path('source').glob('*.[ch]')) + sorted(pathlib.Path('runtime/include').rglob('*.h'))
inputs += [pathlib.Path('CMakeLists.txt'), pathlib.Path(__file__), pathlib.Path('tests/test_objects.py')]
hashes = {str(p): digest(p) for p in inputs}
identity = hashlib.sha256(compiler.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
base = (pathlib.Path('.agent-local/prefix-maps') / identity).resolve()
base.mkdir(parents=True, exist_ok=True)
environment = dict(os.environ, SOURCE_DATE_EPOCH='0')
rows = []

def run(arguments, success=True, cwd=base):
    command = [str(compiler), *map(str, arguments)]
    result = subprocess.run(command, cwd=cwd, env=environment, capture_output=True, text=True, timeout=30)
    row = dict(argv=command, cwd=str(cwd), exit=result.returncode, stdout=result.stdout, stderr=result.stderr)
    rows.append(row)
    (base / 'observations.json').write_text(json.dumps(dict(inputs=hashes, compiler_sha256=digest(compiler), source_date_epoch=0, cases=rows), indent=2) + '\n')
    assert (result.returncode == 0) == success, row
    return result

def mapped(kind, old, new):
    return f'-f{kind}-prefix-map={old}={new}'

def file_literal(path, flags, expected):
    path.write_text('const char *file = __FILE__;\n')
    result = run(['-E', *flags, path])
    assert result.stdout.strip() == 'const char * file = ' + json.dumps(expected, ensure_ascii=False) + ' ;', result.stdout

trees = []
for name in ('checkout one', 'checkout two'):
    tree = base / name; tree.mkdir(exist_ok=True); trees.append(tree)
    (tree / 'names.h').write_text('static const char *header_name = __FILE__;\n')
    (tree / 'main.c').write_text('#include "names.h"\nconst char *root_name = __FILE__;\n'
        'static int equal(const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }\n'
        'int main(void) { return !equal(root_name, "/src/main.c") || !equal(header_name, "/src/names.h"); }\n')
    for level in ('-O0', '-O2'):
        output = tree / (level + '.o')
        run([level, '-c', mapped('file', tree, '/src'), tree / 'main.c', '-o', output])
        assert inspect(output)['sections']['.text']['size'] > 0
        assert b'/src/main.c\0' in output.read_bytes() and b'/src/names.h\0' in output.read_bytes()
        assert str(tree).encode() not in output.read_bytes()
        result = run([level, '--interpret', mapped('file', tree, '/src'), tree / 'main.c'])
        assert result.stdout == 'interpret main => 0\n'
    dep = run(['-M', mapped('file', tree, '/src'), tree / 'main.c'])
    assert '/src/' not in dep.stdout
    assert str(tree / 'main.c').replace(' ', '\\ ') in dep.stdout
    assert str(tree / 'names.h').replace(' ', '\\ ') in dep.stdout
for level in ('-O0', '-O2'):
    assert (trees[0] / (level + '.o')).read_bytes() == (trees[1] / (level + '.o')).read_bytes()

path = base / 'order.c'
file_literal(path, [], str(path))
file_literal(path, [mapped('file', base, '/first'), mapped('file', base, '/last')], '/last/order.c')
file_literal(path, [mapped('file', base, '/narrow'), mapped('file', base.parent, '/broad')], '/broad/' + base.name + '/order.c')
file_literal(path, [mapped('file', base.parent, '/broad'), mapped('file', base, '/narrow')], '/narrow/order.c')
file_literal(path, [mapped('macro', base, '/macro'), mapped('file', base, '/file')], '/file/order.c')
file_literal(path, [mapped('file', base, '/file'), mapped('macro', base, '/macro')], '/macro/order.c')
file_literal(path, [mapped('file', base, '/once'), mapped('file', '/once', '/twice')], '/once/order.c')
file_literal(path, [mapped('file', base, '')], '/order.c')
file_literal(path, [mapped('macro', '', 'prefix:')], 'prefix:' + str(path))
file_literal(path, [mapped('macro', base, '/a=b ü')], '/a=b ü/order.c')

logical = base / 'logical.c'
logical.write_text('#line 41 "/old/logical.c"\nconst char *one = __FILE__;\n#line 81\nconst char *two = __FILE__;\n')
result = run(['-E', mapped('file', '/old', '/mapped'), mapped('file', '/mapped', '/twice'), logical])
assert result.stdout.count('"/mapped/logical.c"') == 2 and '/twice' not in result.stdout

bad = base / 'bad.c'; bad.write_text('int main(void) { return unknown; }\n')
for flags, expected in (([mapped('file', base, '/display')], '/display/bad.c'),
                        ([mapped('macro', base, '/macro')], str(bad)),
                        ([mapped('file', base, '/display'), mapped('macro', base, '/macro')], '/display/bad.c')):
    result = run(['--diagnostics=json', '-fsyntax-only', *flags, bad], False)
    entry = next(e for e in json.loads(result.stderr)['diagnostics'] if 'undeclared' in e['message'])
    assert entry['range']['file'] == expected and entry['range']['line'] == 1
    assert bad.read_bytes()[entry['range']['offset']:entry['range']['offset'] + entry['range']['length']] == b'unknown'
    result = run(['-fsyntax-only', *flags, bad], False); assert expected + ':1:' in result.stderr

header = base / 'bad.h'; header.write_text('#define BAD unknown\nint broken(void) { return BAD; }\n')
bad.write_text('#include "bad.h"\n')
result = run(['--diagnostics=json', '-fsyntax-only', mapped('file', base, '/display'), bad], False)
entry = next(e for e in json.loads(result.stderr)['diagnostics'] if 'undeclared' in e['message'])
assert entry['range']['file'] == '/display/bad.h' and entry['macro_definition']['file'] == '/display/bad.h'
assert entry['include_backtrace'][0]['file'] == '/display/bad.c'
result = run(['--dump-tokens', mapped('file', base, '/tokens'), trees[0] / 'main.c'])
assert '/tokens/checkout one/main.c' in result.stdout and '/tokens/checkout one/names.h' in result.stdout

output = base / 'previous.o'; dependency = base / 'previous.d'
for flag in ('-ffile-prefix-map=missing', '-fmacro-prefix-map=missing', '-fdebug-prefix-map=a=b'):
    output.write_bytes(b'previous object\n'); dependency.write_bytes(b'previous dependencies\n')
    run(['--diagnostics=json', '-c', '-MD', '-MF', dependency, flag, trees[0] / 'main.c', '-o', output], False)
    assert output.read_bytes() == b'previous object\n' and dependency.read_bytes() == b'previous dependencies\n'
assert not list(base.rglob('*.cinder-*'))

first, second = base / 'first.c', base / 'second.c'
first.write_text('#define LOCAL 1\n#line 100 "/logical/first.c"\nconst char *first = __FILE__;\n')
second.write_text('#ifdef LOCAL\n#error leaked macro\n#endif\nconst char *second = __FILE__;\n')
run(['-c', mapped('file', base, '/multi'), mapped('file', '/logical', '/logical-mapped'), first, second])
assert b'/logical-mapped/first.c\0' in (base / 'first.o').read_bytes()
assert b'/multi/second.c\0' in (base / 'second.o').read_bytes()
assert b'/logical-mapped/first.c' not in (base / 'second.o').read_bytes()
assert hashes == {str(p): digest(p) for p in inputs}, 'source changed during prefix verification'
assert identity == hashlib.sha256(compiler.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
print(f'Prefix maps: {len(rows)} commands, relocated object identity, physical dependencies and independent translation units passed')
