#!/usr/bin/env python3
"""Verify authored cross-unit static storage and real unresolved declarations."""
import hashlib
import json
import pathlib
import platform
import shutil
import subprocess
import sys

from native_profile import configure_stack
from test_objects import inspect

compiler = pathlib.Path(sys.argv[1]).resolve()
irtool = compiler.parent / 'cinderir'
identity = hashlib.sha256(compiler.read_bytes() + irtool.read_bytes()).hexdigest()
base = pathlib.Path('.agent-local/block-storage-contracts') / identity
base.mkdir(parents=True, exist_ok=True)
root = pathlib.Path('tests/block_storage/native')
native = platform.system() == 'Linux' and platform.machine() == 'x86_64'
profile = configure_stack()
references = [shutil.which('gcc-15') or shutil.which('gcc'), shutil.which('clang')]
assert all(references)
observations = []

def invoke(argv):
    result = subprocess.run([str(arg) for arg in argv], capture_output=True, timeout=30)
    assert result.returncode == 0, (argv, result)
    return result

def save(row):
    observations.append(row)
    (base / 'observations.json').write_text(json.dumps(dict(identity=identity, native=native, stack_profile=profile, records=observations), indent=2)+'\n')

for case in json.loads((root / 'cases.json').read_text()):
    sources = [root / name for name in case['owned']]
    provider = None if case['provider'] is None else root / case['provider']
    for level in ('-O0', '-O2'):
        objects = {'owned': [], 'assembled': []}
        for source in sources:
            stem = base / (case['name']+'-'+source.stem+level)
            obj, asm, oracle = stem.with_suffix('.o'), stem.with_suffix('.s'), stem.with_suffix('.assembled.o')
            cir, again, parsed = stem.with_suffix('.cir'), stem.with_suffix('.again.cir'), stem.with_suffix('.parsed.o')
            for mode, output in (('-c', obj), ('-S', asm), ('--serialize-ir', cir)):
                invoke([compiler, level, '-fverify-each', mode, source, '-o', output])
            invoke([references[1], '-target', 'x86_64-linux-gnu', '-c', asm, '-o', oracle])
            invoke([irtool, cir, '-o', again]); invoke([irtool, '-c', cir, '-o', parsed])
            assert cir.read_bytes() == again.read_bytes() and obj.read_bytes() == parsed.read_bytes(), (source, level)
            actual = inspect(obj); assembled = inspect(oracle, strict=False)
            for section in ('.text', '.data', '.rodata', '.bss'):
                assert actual['sections'][section] == assembled['sections'][section], (source, level, section)
            for symbol in actual['symbols']:
                if symbol['name'].startswith('.LST.'):
                    assert symbol['binding'] == 0, (source, level, symbol)
            row = dict(kind='unit', case=case['name'], source=str(source), source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), level=level, object_sha256=actual['sha256'], assembled_sha256=assembled['sha256'], ir_sha256=hashlib.sha256(cir.read_bytes()).hexdigest())
            if case['interpreter'] is not None:
                value = json.loads(invoke([irtool, '--classify', cir]).stdout)
                assert value['class'] == case['interpreter'], (case, level, value)
                assert value['valid'] == (case['interpreter'] == 'defined'), (case, value)
                if value['valid']: assert value['integer'] == 0, value
                row['interpreter'] = value
            save(row)
            objects['owned'].append(obj); objects['assembled'].append(oracle)
        for reference in references:
            # References execute on both hosts; Cinder objects execute on x86 Linux.
            ref = base / (case['name']+'-'+pathlib.Path(reference).name+level+'.reference')
            args = [reference, '-std=c17', level, *sources]
            if provider is not None: args.append(provider)
            invoke([*args, '-o', ref])
            result = invoke([ref.resolve()]); assert result.stdout == result.stderr == b'', result
            save(dict(kind='reference', case=case['name'], reference=reference, level=level, exit=result.returncode, executable_sha256=hashlib.sha256(ref.read_bytes()).hexdigest()))
            library = None
            if provider is not None:
                library = base / (case['name']+'-'+pathlib.Path(reference).name+level+'.provider.o')
                # Host provider object is needed only for native x86 linking.
                if native: invoke([reference, '-std=c17', level, '-c', provider, '-o', library])
            for kind, units in objects.items():
                row = dict(kind='native-link', case=case['name'], reference=reference, level=level, path=kind, expected=0, native=native, objects=[str(path) for path in units], provider=str(library) if library is not None else None)
                if native:
                    output = base / (case['name']+'-'+pathlib.Path(reference).name+level+'-'+kind+'.native')
                    invoke([reference, '-no-pie', *units, *([] if library is None else [library]), '-o', output])
                    result = invoke([output.resolve()]); assert result.stdout == result.stderr == b'', (case, result)
                    row.update(exit=result.returncode, executable_sha256=hashlib.sha256(output.read_bytes()).hexdigest())
                save(row)

assert hashlib.sha256(compiler.read_bytes() + irtool.read_bytes()).hexdigest() == identity
print('Block storage contracts: 7 authored linkage profiles, CIR identity, local symbols, unresolved extern classification, and native cross-unit paths passed; native='+str(native))
