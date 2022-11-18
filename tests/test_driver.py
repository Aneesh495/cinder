#!/usr/bin/env python3
"""Verify real dependency rules, diagnostic ranges and preserved output failures."""
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys

from test_objects import inspect

compiler = pathlib.Path(sys.argv[1]).resolve()
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
inputs = sorted(pathlib.Path('source').glob('*.[ch]')) + sorted(pathlib.Path('runtime/include').rglob('*.h'))
inputs += [pathlib.Path('CMakeLists.txt'), pathlib.Path(__file__), pathlib.Path('tests/test_objects.py')]
hashes = {str(p): digest(p) for p in inputs}
identity = hashlib.sha256(compiler.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
base = (pathlib.Path('.agent-local/driver') / identity).resolve()
base.mkdir(parents=True, exist_ok=True)
work = base / 'inputs #$'; work.mkdir(exist_ok=True)
leaf, header, source = work / 'leaf: space$.h', work / 'top #$.h', work / 'unit #$.c'
leaf.write_text('#define LEAF 7\n')
header.write_text('#pragma once\n#include "leaf: space$.h"\n#define VALUE LEAF\n')
source.write_text('#include "top #$.h"\n#include "top #$.h"\n#if 0\n#include "inactive_missing.h"\n#endif\nint main(void) { return VALUE; }\n')
frozen = {str(p): digest(p) for p in (leaf, header, source)}
rows = []

def run(arguments, success=True, cwd=base):
    command = [str(compiler), *map(str, arguments)]
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, timeout=30)
    row = dict(argv=command, cwd=str(cwd), exit=result.returncode, stdout=result.stdout, stderr=result.stderr)
    rows.append(row)
    (base / 'observations.json').write_text(json.dumps(dict(inputs=hashes, compiler_sha256=digest(compiler), cases=rows), indent=2) + '\n')
    assert (result.returncode == 0) == success, row
    return result

def diagnostics(result):
    record = json.loads(result.stderr)
    assert record['schema'] == 1 and record['tool'] == 'cindercc' and record['diagnostics']
    assert result.stdout == ''
    return record['diagnostics']

target = 'out #$: name.o'
dep = base / 'rule.d'
first = run(['-M', '-MQ', target, source])
assert first.stderr == '' and first.stdout.startswith('out\\ \\#$$\\:\\ name.o: ')
assert 'inactive_missing' not in first.stdout and first.stdout.count('top\\ \\#$$.h') == 1
dep.write_text(first.stdout)
make = shutil.which('gmake') or shutil.which('make'); assert make
reference = subprocess.run([make, '-f', str(dep), '--no-print-directory', '-n', target], cwd=base, capture_output=True, text=True, timeout=30)
assert reference.returncode == 0, reference
second = run(['-M', '-MQ', target, source]); assert second.stdout == first.stdout
run(['-M', '-MF', dep, source]); assert source.name.replace(' ', '\\ ').replace('#', '\\#').replace('$', '$$') in dep.read_text()
obj = base / 'normal.o'; custom = base / 'custom.d'
run(['-std=c17-core', '-c', '-MD', '-MF', custom, source, '-o', obj])
assert inspect(obj)['sections']['.text']['size'] > 0 and custom.read_text().startswith(str(obj) + ':')
normal_bytes = obj.read_bytes()
for profile in ('-std=c17', '--std=c17-core'):
    run([profile, '-c', source, '-o', obj]); assert obj.read_bytes() == normal_bytes
run(['-c', '-MD', source, '-o', obj]); assert obj.with_suffix('.d').is_file()
assembly = base / 'normal.s'; run(['-S', '-MD', source, '-o', assembly]); assert assembly.with_suffix('.d').is_file()
run(['-fsyntax-only', '-MD', '-MF', custom, source])
run(['-E', '-MD', '-MF', custom, source]); assert custom.is_file()

bad = work / 'bad ü#.c'; bad.write_text('int main(void) { return unknown; }\n')
macro = work / 'macro.c'; macro.write_text('#define BAD unknown\nint main(void) { return BAD; }\n')
warning = work / 'warning.c'; warning.write_text('#pragma unrecognized_feature\nint main(void) { return 0; }\n')
malformed = work / 'bad-bytes.c'; malformed.write_bytes(b'int main(void) { return 0; }\n\xff')
for path in (bad, macro, malformed):
    obj.write_bytes(b'previous object\n'); custom.write_bytes(b'previous dependencies\n')
    entries = diagnostics(run(['--diagnostics=json', '-c', '-MD', '-MF', custom, path, '-o', obj], False))
    assert any(entry['severity'] in ('error', 'fatal') for entry in entries)
    assert obj.read_bytes() == b'previous object\n' and custom.read_bytes() == b'previous dependencies\n'
entries = diagnostics(run(['--diagnostics=json', '-fsyntax-only', bad], False))
primary = next(e for e in entries if 'unknown' in e['message'])
assert primary['range']['file'] == str(bad) and primary['range']['line'] == 1
assert bad.read_bytes()[primary['range']['offset']:primary['range']['offset'] + primary['range']['length']] == b'unknown'
entries = diagnostics(run(['--diagnostics=json', '-fsyntax-only', macro], False))
primary = next(e for e in entries if 'unknown' in e['message'])
assert primary['range']['line'] == 2 and primary['macro_definition']['line'] == 1
entries = diagnostics(run(['--diagnostics=json', '-fsyntax-only', warning]))
assert entries[0]['severity'] == 'warning'
diagnostics(run(['--diagnostics=json', '-fsyntax-only', work / 'missing.c'], False))
for arguments in (['-std=gnu17', source], ['-fPIC', source], ['-MF', custom, source],
                  ['-M', '--dump-mir', source], ['-o'], ['-I'], ['-D'], ['--pass-stats'],
                  ['--explorer'], ['-MF'], ['-MQ'], []):
    diagnostics(run(['--diagnostics=json', *arguments], False))

for destination in (source, header, base / './normal.o', base):
    obj.write_bytes(b'previous object\n')
    diagnostics(run(['--diagnostics=json', '-c', '-MD', '-MF', destination, source, '-o', obj], False))
    assert obj.read_bytes() == b'previous object\n'
diagnostics(run(['--diagnostics=json', '-c', source, '-o', source], False))
diagnostics(run(['--diagnostics=json', '-c', '-MD', '-MF', base / 'absent/deps.d', source, '-o', obj], False))
assert obj.read_bytes() == b'previous object\n'
assert frozen == {str(p): digest(pathlib.Path(p)) for p in frozen}
assert not list(base.rglob('*.cinder-*')), 'owned temporary output leaked'
diagnostics(run(['--diagnostics=json', '--dump-ast', bad], False))
assert 'main' in run(['--dump-ast', source]).stdout
other = work / 'other.c'; other.write_text('int other(void) { return 1; }\n')
diagnostics(run(['--diagnostics=json', '-M', '-MF', custom, source, other], False))
multi = run(['-M', source, other]); assert multi.stdout.count(': ') == 2
run(['-c', '-MD', source, other])
assert (base / 'unit #$.o').is_file() and (base / 'unit #$.d').is_file() and (base / 'other.d').is_file()
dash = base / '-leading.c'; dash.write_text('int main(void) { return 2; }\n')
run(['-fsyntax-only', '--', dash.name])
assert hashes == {str(p): digest(p) for p in inputs}, 'driver inputs changed during verification'
assert identity == hashlib.sha256(compiler.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
print(f'Driver: {len(rows)} dependency/profile/diagnostic/publication commands and GNU Make parsing passed')
