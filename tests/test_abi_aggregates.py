#!/usr/bin/env python3
"""Execute generated aggregate signatures without comparing padding bytes."""
import argparse
import concurrent.futures
import hashlib
import json
import pathlib
import platform
import random
import shutil
import struct
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from evidence_integrity import inventory_digest, revision, source_inputs

# Each list describes the independently observed scalar members, not object
# representation bytes. Arrays and unions exercise recursive classification.
LAYOUTS = {
    'B1': ('struct B1 { unsigned char x; };', [('x', 'unsigned char')]),
    'B3': ('struct B3 { unsigned char x[3]; };', [(f'x[{i}]', 'unsigned char') for i in range(3)]),
    'B9': ('struct B9 { unsigned char x[9]; };', [(f'x[{i}]', 'unsigned char') for i in range(9)]),
    'I1': ('struct I1 { int x; };', [('x', 'int')]),
    'I2': ('struct I2 { long x[2]; };', [('x[0]', 'long'), ('x[1]', 'long')]),
    'F2': ('struct F2 { float x[2]; };', [('x[0]', 'float'), ('x[1]', 'float')]),
    'D2': ('struct D2 { double x[2]; };', [('x[0]', 'double'), ('x[1]', 'double')]),
    'IF': ('struct IF { long x; double y; };', [('x', 'long'), ('y', 'double')]),
    'FI': ('struct FI { double x; long y; };', [('x', 'double'), ('y', 'long')]),
    'M8': ('struct M8 { int x; float y; };', [('x', 'int'), ('y', 'float')]),
    'L3': ('struct L3 { long x[3]; };', [(f'x[{i}]', 'long') for i in range(3)]),
    'NF': ('struct Cell { float x[2]; }; struct NF { struct Cell cells[2]; };', [(f'cells[{i}].x[{j}]', 'float') for i in range(2) for j in range(2)]),
    'U8': ('union U8 { double fraction; long whole; };', [('whole', 'long')]),
}
SCALARS = ('long', 'int', 'float', 'double', '_Bool')
COMMON = '\n'.join(value[0] + '\n' + f'typedef {"union" if name == "U8" else "struct"} {name} {name};' for name, value in LAYOUTS.items()) + '\n'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def leaves(kind):
    return LAYOUTS[kind][1] if kind in LAYOUTS else [('', kind)]


def specification(seed):
    rng = random.Random(0xC17AB200 + seed)
    names = list(LAYOUTS)
    result = names[seed % len(names)]
    scenarios = (
        ['long'] * 5 + ['I2', 'long', 'D2'],
        ['double'] * 7 + ['D2', 'double', 'IF'],
        ['double'] * 8 + ['IF', 'long'],
        ['long'] * 6 + ['B9', 'I2', 'L3', 'long'],
        [names[(seed + i) % len(names)] for i in range(6)] + ['float', 'double'],
        [rng.choice(names + list(SCALARS)) for _ in range(20)],
        [],
        [rng.choice(names + list(SCALARS)) for _ in range(seed % 13 + 1)],
    )
    arguments = scenarios[seed % len(scenarios)]
    declarations, forwarded, body = [], [], [f'long sum = {seed % 97}L;']
    total = seed % 97
    values = []
    for i, kind in enumerate(arguments):
        declarations.append(f'{kind} a{i};')
        forwarded.append(f'a{i}')
        member_values = []
        for j, (member, scalar) in enumerate(leaves(kind)):
            value = rng.randrange(1, 32) if scalar != '_Bool' else rng.randrange(2)
            expression = f'a{i}' + ('.' + member if member else '')
            declarations.append(f'{expression} = ({scalar}){value};')
            weight = (i + j) % 7 + 1
            body.append(f'sum += (long){expression} * {weight}L;')
            total += value * weight
            member_values.append(value)
        values.append(member_values)
    body.append(f'{result} result;')
    expected = []
    for j, (member, scalar) in enumerate(leaves(result)):
        base = total % 61 + j * 3 if scalar == 'unsigned char' else total + j * 3
        expression = f'(sum % 61L) + {j * 3}L' if scalar == 'unsigned char' else f'sum + {j * 3}L'
        if scalar in ('float', 'double'):
            expression = f'({scalar})({expression}) + ({scalar})0.25'
            bits = struct.unpack('<I' if scalar == 'float' else '<Q', struct.pack('<f' if scalar == 'float' else '<d', base + 0.25))[0]
        else:
            bits = base
        body.append(f'result.{member} = ({scalar})({expression});')
        expected.append(f'{bits:016x}')
    body.append('return result;')
    parameters = ', '.join(f'{kind} a{i}' for i, kind in enumerate(arguments)) or 'void'
    return dict(seed=seed, result=result, arguments=arguments, values=values,
                parameters=parameters, forwarded=', '.join(forwarded),
                declarations='\n'.join(declarations), body='\n'.join(body),
                expected=':'.join(expected))


def sources(spec):
    result, params, args = spec['result'], spec['parameters'], spec['forwarded']
    extra_params = '' if params == 'void' else ', ' + params
    extra_args = '' if not args else ', ' + args
    common = COMMON + f'typedef {result} (*Callback)({params});\n'
    callee = f'{result} cinder_callee({params}) {{\n{spec["body"]}\n}}\n'
    provider = common + callee + f'{result} cinder_apply(Callback cb{extra_params}) {{ return cb({args}); }}\n'
    caller = common + callee + f'''{result} host_callee({params});
{result} host_apply(Callback cb{extra_params});
int report({result} value);
int main(void) {{
    {spec['declarations']}
    int direct = report(host_callee({args}));
    int callback = report(host_apply(cinder_callee{extra_args}));
    return direct + callback;
}}
'''
    report = [f'int report({result} value) {{', 'int failed = 0;']
    expected = spec['expected'].split(':')
    for j, (member, scalar) in enumerate(leaves(result)):
        if scalar in ('float', 'double'):
            integer = 'unsigned int' if scalar == 'float' else 'unsigned long long'
            report.append(f'{integer} bits{j}; memcpy(&bits{j}, &value.{member}, sizeof(value.{member})); unsigned long long actual{j} = bits{j};')
        else:
            report.append(f'unsigned long long actual{j} = (unsigned long long)value.{member};')
        report.append(f'printf("%016llx{":" if j + 1 < len(expected) else ""}", actual{j});')
        report.append(f'failed |= actual{j} != 0x{expected[j]}ULL;')
    report += ['printf("\\n");', 'return failed;', '}']
    host = '#include <stdio.h>\n#include <string.h>\n' + common
    host += f'{result} host_callee({params}) {{\n{spec["body"]}\n}}\n' + '\n'.join(report) + '\n'
    library = host + f'{result} host_apply(Callback cb{extra_params}) {{ return cb({args}); }}\n'
    driver = host + f'''{result} cinder_callee({params});
{result} cinder_apply(Callback cb{extra_params});
int main(void) {{
    {spec['declarations']}
    int direct = report(cinder_callee({args}));
    int callback = report(cinder_apply(host_callee{extra_args}));
    return direct + callback;
}}
'''
    return {'provider.c': provider, 'caller.c': caller,
            'host_driver.c': driver, 'host_library.c': library}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('compiler')
    parser.add_argument('--count', type=int, default=512)
    parser.add_argument('--jobs', type=int, default=4)
    options = parser.parse_args()
    assert 1 <= options.count <= 100000 and 1 <= options.jobs <= 32
    compiler = pathlib.Path(options.compiler).resolve()
    native = platform.system() == 'Linux' and platform.machine() == 'x86_64'
    inputs = source_inputs(ROOT)
    source_hash = inventory_digest(inputs)
    identity = digest(compiler)
    base = ROOT / '.agent-local/abi-aggregates' / source_hash / identity
    base.mkdir(parents=True, exist_ok=True)
    frozen = base / 'cindercc'
    shutil.copy2(compiler, frozen)
    assert digest(frozen) == identity
    hosts = [shutil.which(name) for name in (('gcc', 'clang') if native else ('gcc-15', 'clang'))]
    assert all(hosts), 'both reference toolchains are required'
    toolchains = [dict(path=host, version=subprocess.check_output([host, '--version'], text=True),
                       sha256=digest(pathlib.Path(host).resolve())) for host in hosts]
    configuration = {str(p.relative_to(ROOT)): digest(p) for p in
                     (compiler.parent / 'CMakeCache.txt', compiler.parent / 'CMakeFiles/cinder_core.dir/flags.make')}

    def probe(seed):
        spec = specification(seed)
        work = base / f'case-{seed:04d}'
        work.mkdir(exist_ok=True)
        record = dict(specification=spec, native=native, commands=[], artifacts={})
        def persist():
            (work / 'observations.json').write_text(json.dumps(record, indent=2) + '\n')
        def run(argv, expected_output=None, role='compile'):
            result = subprocess.run([str(a) for a in argv], capture_output=True, timeout=30)
            record['commands'].append(dict(role=role, argv=[str(a) for a in argv], exit=result.returncode,
                stdout=result.stdout.decode(errors='replace'), stderr=result.stderr.decode(errors='replace')))
            persist()
            assert result.returncode == 0, (seed, argv, result)
            if expected_output is not None:
                assert result.stdout.decode() == expected_output and result.stderr == b'', (seed, argv, result)
        for name, text in sources(spec).items():
            path = work / name
            path.write_text(text)
            record['artifacts'][name] = dict(sha256=digest(path), bytes=path.stat().st_size)
        expected = (spec['expected'] + '\n') * 2
        for mode, counterpart in (('provider', 'host_driver'), ('caller', 'host_library')):
            source, host_source = work / (mode + '.c'), work / (counterpart + '.c')
            for index, host in enumerate(hosts):
                for level in ('-O0', '-O2'):
                    reference = work / f'reference-{mode}-{index}{level}'
                    argv = [host, '-std=c17', level, source, host_source, '-o', reference]
                    if native: argv.insert(1, '-no-pie')
                    run(argv)
                    run([reference], expected, 'reference')
                    record['artifacts'][reference.name] = dict(sha256=digest(reference), bytes=reference.stat().st_size)
            for level in ('-O0', '-O2'):
                obj = work / (mode + level + '.o')
                run([frozen, level, '-fverify-each', '-c', source, '-o', obj])
                header = obj.read_bytes()[:64]
                assert header[:7] == b'\x7fELF\x02\x01\x01' and struct.unpack_from('<H', header, 18)[0] == 62
                record['artifacts'][obj.name] = dict(sha256=digest(obj), bytes=obj.stat().st_size)
                if native:
                    for index, host in enumerate(hosts):
                        for host_level in ('-O0', '-O2'):
                            executable = work / f'{mode}{level}-{index}{host_level}'
                            run([host, '-std=c17', host_level, '-no-pie', obj, host_source, '-o', executable])
                            run([executable], expected, 'native')
                            record['artifacts'][executable.name] = dict(sha256=digest(executable), bytes=executable.stat().st_size)
        persist()
        return dict(seed=seed, expected=spec['expected'], result=spec['result'], arguments=spec['arguments'],
                    reference_executions=sum(command['role'] == 'reference' for command in record['commands']),
                    native_executions=sum(command['role'] == 'native' for command in record['commands']),
                    report=str((work / 'observations.json').relative_to(base)), report_sha256=digest(work / 'observations.json'))

    observations = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=options.jobs) as pool:
        for observation in pool.map(probe, range(options.count)):
            observations.append(observation)
            if len(observations) % 64 == 0:
                print(f'Aggregate ABI: {len(observations)}/{options.count} signatures observed', flush=True)
    assert source_inputs(ROOT) == inputs, 'source changed during the ABI campaign'
    assert digest(compiler) == identity and digest(frozen) == identity, 'compiler changed during the ABI campaign'
    assert all(digest(ROOT / name) == expected for name, expected in configuration.items()), 'configuration changed'
    summary = dict(schema=1, scope='aggregate fixed signatures and callbacks; full hosted variadic state remains open',
        source_revision=revision(ROOT), source_inputs=inputs, source_sha256=source_hash,
        compiler_sha256=identity, configuration_inputs=configuration, toolchains=toolchains,
        host=dict(system=platform.system(), machine=platform.machine(), uname=platform.uname()._asdict()), native=native,
        cases=options.count, reference_executions=sum(record['reference_executions'] for record in observations),
        native_executions=sum(record['native_executions'] for record in observations),
        aggregate_return_families=sorted({record['result'] for record in observations}), observations=observations)
    (base / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(f'Aggregate ABI: {options.count} signatures, both directions, two toolchains; native={native}')


if __name__ == '__main__':
    main()
