#!/usr/bin/env python3
"""Distinguish repeated physical headers and retain their real include paths."""
import hashlib
import json
import pathlib
import subprocess
import sys

compiler = pathlib.Path(sys.argv[1]).resolve()
h = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
inputs = sorted(pathlib.Path('source').glob('*.[ch]')) + [pathlib.Path(__file__), pathlib.Path('CMakeLists.txt')]
hashes = {str(p): h(p) for p in inputs}
identity = hashlib.sha256(compiler.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
base = pathlib.Path('.agent-local/source-instances') / identity; base.mkdir(parents=True, exist_ok=True)
rows = []
for bad in (0, 1):
    work = base / ('repeat-' + str(bad)); work.mkdir(exist_ok=True)
    header = work / 'repeat.h'
    header.write_text('#line LINE FILE\nint NAME(void) { return BODY; }\n')
    text = ''
    for i in range(2):
        if i: text += '#undef LINE\n#undef FILE\n#undef NAME\n#undef BODY\n'
        text += f'#define LINE {10 + 10*i}\n#define FILE "instance-{i}.h"\n#define NAME function_{i}\n#define BODY ' + ('unknown' if bad == i else '0') + '\n#include "repeat.h"\n'
    source = work / 'main.c'; source.write_text(text)
    command = [str(compiler), '--diagnostics=json', '-fsyntax-only', str(source)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    assert result.returncode != 0 and not result.stdout, result
    data = json.loads(result.stderr)
    entry = next(e for e in data['diagnostics'] if 'undeclared' in e['message'])
    location = entry['range']; definition = entry['macro_definition']; chain = entry['include_backtrace']
    assert location['file'] == f'instance-{bad}.h' and location['line'] == 10 + bad*10, entry
    assert definition['file'] == str(source.resolve()) and definition['line'] == (4 if bad == 0 else 13), entry
    assert len(chain) == 1 and chain[0]['line'] == (5 if bad == 0 else 14), entry
    assert location['frame'] == 2 + bad and location['origin'] > 0, entry
    assert entry['spelling_range']['file'] == str(source.resolve()), entry
    rows.append(dict(argv=command, exit=result.returncode, stderr=data))
    text_result = subprocess.run([str(compiler), '-fsyntax-only', str(source)], capture_output=True, text=True, timeout=30)
    assert text_result.returncode != 0 and f'instance-{bad}.h:{10 + bad*10}:' in text_result.stderr
    assert 'included from here' in text_result.stderr

for depth in (1, 2, 4, 8):
    for kind in ('semantic', 'preprocessor', 'utf8'):
        work = base / f'{kind}-{depth}'; work.mkdir(exist_ok=True)
        source = work / 'main.c'; source.write_text('#include "level0.h"\n')
        for i in range(depth):
            header = work / f'level{i}.h'
            if i + 1 < depth: header.write_text(f'#include "level{i+1}.h"\n')
            elif kind == 'semantic': header.write_text('int broken(void) { return unknown; }\n')
            elif kind == 'preprocessor': header.write_text('/* unterminated\n')
            else: header.write_bytes(b'int broken(void) { return 0; }\n\xff')
        command = [str(compiler), '--diagnostics=json', '-fsyntax-only', str(source)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        assert result.returncode != 0 and not result.stdout
        data = json.loads(result.stderr); entry = data['diagnostics'][0]; chain = entry['include_backtrace']
        assert len(chain) == depth, (kind, depth, entry)
        expected = [str((work / f'level{i}.h').resolve()) for i in range(depth-2, -1, -1)] + [str(source.resolve())]
        assert [loc['file'] for loc in chain] == expected, entry
        assert entry['range']['file'] == str(header.resolve()), entry
        rows.append(dict(argv=command, exit=result.returncode, stderr=data))

work = base / 'builtins'; work.mkdir(exist_ok=True)
header = work / 'again.h'; header.write_text('#line LINE FILE\nint NAME(void) { return __LINE__; }\n')
source = work / 'main.c'
source.write_text('#define LINE 11\n#define FILE "one.h"\n#define NAME first\n#include "again.h"\n#undef LINE\n#undef FILE\n#undef NAME\n#define LINE 22\n#define FILE "two.h"\n#define NAME second\n#include "again.h"\nint main(void) { return first() != 11 || second() != 22; }\n')
command = [str(compiler), '--interpret', str(source)]
result = subprocess.run(command, capture_output=True, text=True, timeout=30)
assert result.returncode == 0 and result.stdout == 'interpret main => 0\n' and not result.stderr, result
rows.append(dict(argv=command, exit=result.returncode, stdout=result.stdout, stderr=result.stderr))
(base / 'observations.json').write_text(json.dumps(dict(inputs=hashes, compiler_sha256=h(compiler), cases=rows), indent=2) + '\n')
assert hashes == {str(p): h(p) for p in inputs}
assert identity == hashlib.sha256(compiler.read_bytes() + json.dumps(hashes, sort_keys=True).encode()).hexdigest()
print('Source instances: repeated-header locations/macros, twelve include chains and logical-line execution passed')
