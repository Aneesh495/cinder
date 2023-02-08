#!/usr/bin/env python3
"""Verify actual offline stages, pass snapshots, source links and byte mappings."""
import argparse
import base64
import hashlib
from html.parser import HTMLParser
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess
import tempfile
from test_unwind import digest, elf, tool


class Page(HTMLParser):
    def __init__(self, text):
        super().__init__(convert_charrefs=True)
        self.ids = []; self.links = []; self.downloads = []; self.scripts = 0; self.external = []
        self.panes = {}; self.pres = {}; self.current = None; self.in_pre = False; self.code = None; self.mappings = []
        self.feed(text)
    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if 'id' in a: self.ids.append(a['id'])
        if a.get('href', '').startswith('#'): self.links.append(a['href'][1:])
        if tag in ('script', 'img', 'iframe', 'link') and ('src' in a or 'href' in a): self.external.append(a)
        if tag == 'script': self.scripts += 1
        if a.get('href', '').startswith('data:text/plain;base64,'): self.downloads.append(base64.b64decode(a['href'].split(',', 1)[1], validate=True))
        if 'data-pane' in a:
            self.current = a['data-pane']; self.panes[self.current] = ''; self.pres[self.current] = ''
        if tag == 'pre': self.in_pre = True
        if tag == 'code' and 'data-pc-begin' in a: self.code = [int(a['data-pc-begin']), int(a['data-pc-end']), '']
    def handle_endtag(self, tag):
        if tag == 'pre': self.in_pre = False
        if tag == 'code' and self.code is not None: self.mappings.append(self.code); self.code = None
        if tag == 'section': self.current = None
    def handle_data(self, text):
        if self.code is not None: self.code[2] += text
        if self.current is not None:
            self.panes[self.current] += text
            if self.in_pre: self.pres[self.current] += text
    def validate(self):
        assert len(self.ids) == len(set(self.ids)), 'duplicate source or stage ID'
        assert not set(self.links) - set(self.ids), set(self.links) - set(self.ids)
        assert self.scripts == 1 and not self.external
        assert set(self.panes) == {'source', 'origins', 'passes', 'diagnostics'} | {'stage-' + str(i) for i in range(12)}


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('compiler', type=pathlib.Path)
    parser.add_argument('--execution-profile', choices=('native', 'emulated', 'cross')); args = parser.parse_args()
    compiler = args.compiler.resolve(); identity = digest(compiler)
    linux = platform.system() == 'Linux' and platform.machine() == 'x86_64'
    profile = args.execution_profile or ('native' if linux else 'cross'); execute = profile in ('native', 'emulated'); assert not execute or linux
    files = sorted(pathlib.Path('source').glob('*.[ch]')) + sorted(pathlib.Path('runtime/include').rglob('*.h')) + sorted(pathlib.Path('tests/explorer').glob('*'))
    files += [pathlib.Path(p) for p in ('tests/test_explorer.py', 'tests/test_unwind.py', 'CMakeLists.txt')]
    inputs = {str(p): digest(p) for p in files}; source_hash = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()
    base = pathlib.Path('.agent-local/explorer') / (source_hash + '-' + identity); base.mkdir(parents=True, exist_ok=True)
    root = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=base)).resolve(); temporary = root / 'temporary'; temporary.mkdir()
    environment = dict(os.environ, TMPDIR=str(temporary), LC_ALL='C', TZ='UTC', SOURCE_DATE_EPOCH='0')
    commands = []; artifacts = set(); observations = []
    def run(argv, label, expected=0):
        argv = list(map(str, argv)); result = subprocess.run(argv, capture_output=True, env=environment, timeout=120)
        out, err = root / (label + '.stdout'), root / (label + '.stderr'); out.write_bytes(result.stdout); err.write_bytes(result.stderr); artifacts.update((out, err))
        commands.append(dict(argv=argv, exit=result.returncode, stdout=digest(out), stderr=digest(err))); (root / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        assert result.returncode == expected, (argv, result)
        assert not list(temporary.iterdir()), 'owned pass trace directory leaked'
        return result.stdout.decode()
    tools = {name: tool(name) for name in ('llvm-objdump', 'clang')}
    tools['gcc'] = tool('gcc-15' if shutil.which('gcc-15') else 'gcc')
    if execute: tools['cc'] = tool('cc')
    identities = {name: dict(path=path, sha256=digest(pathlib.Path(path))) for name, path in tools.items()}
    for name, path in tools.items(): run([path, '--version'], name + '-version')
    objdump = tools['llvm-objdump']; native_runs = reference_runs = interpreted_runs = 0
    for name in ('pipeline', 'quoted', 'empty'):
        source = pathlib.Path('tests/explorer') / (name + '.c')
        modes = [('-O0', False), ('-O2', False)] + ([('-O0', True), ('-O2', True)] if name == 'pipeline' else [])
        for level, debug in modes:
            label = name + level + ('-debug' if debug else ''); report = root / label; obj, plain = root / (label + '.o'), root / (label + '.plain.o')
            trace = root / (label + '-trace'); trace.mkdir()
            flags = [level, '-fverify-each'] + (['-g'] if debug else [])
            run([compiler, *flags, '--explorer', report, '--pass-trace', trace, '-c', source, '-o', obj], label + '-report')
            run([compiler, *flags, '-c', source, '-o', plain], label + '-plain'); assert obj.read_bytes() == plain.read_bytes()
            page_path = report / 'index.html'; text = page_path.read_text(); page = Page(text); page.validate()
            assert 'Compilation completed.' in text and 'Compilation failed.' not in text
            for index, option in ((0, '--dump-tokens'), (1, '--dump-ast'), (2, '--dump-hir')):
                expected = run([compiler, *flags, option, source], label + '-stage-' + str(index)); assert page.pres['stage-' + str(index)] == expected
            ir = run([compiler, *flags, '--emit-ir', source], label + '-ir'); before, after = ir.split('; optimized IR\n')
            assert page.pres['stage-3'] == before.removeprefix('; before scalar promotion\n') and page.pres['stage-4'] == after
            assembly = root / (label + '.s'); run([compiler, *flags, '-S', source, '-o', assembly], label + '-assembly'); assert page.pres['stage-10'] == assembly.read_text()
            snapshots = sorted(trace.glob('*.before.cir')) + sorted(trace.glob('*.after.cir'))
            assert len(snapshots) == len(page.downloads) == (2 if level == '-O0' else 20)
            assert sorted(p.read_bytes() for p in snapshots) == sorted(page.downloads)
            sections, _, _ = elf(obj); code = sections['.text']; disassembly = run([objdump, '--no-show-raw-insn', '-dr', obj], label + '-disassembly')
            boundaries = {int(m[1], 16) for m in re.finditer(r'^\s*([0-9a-f]+):\s+[a-z]', disassembly, re.M)} | {len(code)}
            mappings = page.mappings
            assert name == 'empty' or mappings
            for start, end, encoded in mappings:
                assert start in boundaries and end in boundaries and start <= end
                assert bytes.fromhex(encoded) == code[start:end]
            if name == 'pipeline': assert 'explorer-logical.c' in text and 'macros.h' in text and 'Source to machine' in text
            if name == 'quoted': assert "<script>alert('source')</script>" not in text
            if name != 'empty':
                assert run([compiler, *flags, '--interpret', source], label + '-interpret') == 'interpret main => 0\n'; interpreted_runs += 1
                for reference in ('gcc', 'clang'):
                    binary = root / (label + '-' + reference)
                    run([tools[reference], level, '-std=c17', source, '-o', binary], label + '-' + reference + '-compile')
                    assert run([binary], label + '-' + reference + '-execute') == ''; reference_runs += 1; artifacts.add(binary)
                if execute:
                    binary = root / (label + '-cinder')
                    run([tools['cc'], '-no-pie', obj, '-o', binary], label + '-link')
                    assert run([binary], label + '-execute') == ''; native_runs += 1; artifacts.add(binary)
            artifacts.update((page_path, obj, plain, assembly, *snapshots)); observations.append(dict(source=str(source), level=level, mappings=len(mappings), pass_snapshots=len(snapshots), html_sha256=digest(page_path)))
    owned = root / 'owned-trace-report'; run([compiler, '-O2', '--explorer', owned, 'tests/explorer/pipeline.c'], 'owned-trace')
    owned_page = owned / 'index.html'; page = Page(owned_page.read_text()); page.validate(); assert len(page.downloads) == 20; artifacts.add(owned_page)
    bad = root / 'failed-report'; previous = root / 'previous.o'; previous.write_bytes(b'previous complete object\n')
    run([compiler, '--explorer', bad, '-c', 'tests/explorer/invalid.c', '-o', previous], 'invalid-source', 1); assert previous.read_bytes() == b'previous complete object\n'; artifacts.add(previous)
    page_path = bad / 'index.html'; page = Page(page_path.read_text()); page.validate(); assert 'Compilation failed.' in page_path.read_text() and 'Stage not reached' in page.panes['stage-1']; artifacts.add(page_path)
    collision = root / 'collision'; collision.mkdir(); source = collision / 'index.html'; source.write_text('int main(void) { return 0; }\n'); original = source.read_bytes()
    run([compiler, '--explorer', collision, source], 'input-collision', 1); assert source.read_bytes() == original; artifacts.add(source)
    destination = root / 'regular-file'; destination.write_bytes(b'complete previous output\n'); run([compiler, '--explorer', destination, 'tests/explorer/quoted.c'], 'regular-file', 1); assert destination.read_bytes() == b'complete previous output\n'; artifacts.add(destination)
    assert identity == digest(compiler) and inputs == {name: digest(pathlib.Path(name)) for name in inputs}
    assert all(digest(pathlib.Path(row['path'])) == row['sha256'] for row in identities.values())
    assert reference_runs == 12 and interpreted_runs == 6 and native_runs == (6 if execute else 0)
    report = dict(schema=1, compiler_sha256=identity, inputs=inputs, observations=observations, commands=commands, tools=identities, rejected_publications=2,
                  native=profile == 'native', execution_profile=profile, target_execution=execute, reference_executions=reference_runs, interpreted_executions=interpreted_runs, target_executions=native_runs,
                  artifacts={str(p.relative_to(root)): digest(p) for p in sorted(artifacts)})
    (root / 'report.json').write_text(json.dumps(report, indent=2) + '\n'); print(f'Explorer: eight real pipeline reports and an owned trace report, exact stage/pass/assembly captures, byte mappings and failed-input/output preservation passed; evidence={root}')


if __name__ == '__main__': main()
