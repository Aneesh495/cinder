#!/usr/bin/env python3
"""Execute original scalar ABI probes in both directions with two host compilers.

Generated probes do not count as individually authored source cases. Aggregate
and variadic ABI campaigns must be added before the full ABI gate can pass.
"""
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

TYPES = ('signed char', 'unsigned char', 'short', 'unsigned short', 'int',
         'unsigned int', 'long', 'unsigned long', 'long long',
         'unsigned long long', '_Bool', 'float', 'double', 'int *')
FP = {'float', 'double'}
UNSIGNED = {'unsigned char', 'unsigned short', 'unsigned int',
            'unsigned long', 'unsigned long long'}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def c_remainder(a, b):
    return a - (abs(a) // b * (-1 if a < 0 else 1)) * b


def specification(seed):
    rng = random.Random(0xC17AB100 + seed)
    result = TYPES[seed % len(TYPES)]
    count = seed % 21
    # Deliberately exercise both register banks and both overflow paths.
    if seed % 8 == 0:
        arguments = ['float' if seed % 16 else 'double'] * 12
    elif seed % 8 == 1:
        arguments = ['int'] * 12
    elif seed % 8 == 2:
        arguments = [TYPES[(seed + i * 3) % len(TYPES)] for i in range(20)]
    else:
        arguments = [rng.choice(TYPES) for _ in range(count)]
    if result == 'int *' and 'int *' not in arguments:
        arguments.insert(0, 'int *')
    values, literals = [], []
    anchor = 17 + seed % 41
    for kind in arguments:
        if kind == 'int *':
            value, literal = anchor, '&anchor'
        elif kind in FP:
            value = rng.randrange(-256, 257) / 4
            literal = f'({kind})({value:.2f})'
        elif kind == '_Bool':
            value = rng.randrange(2)
            literal = f'(_Bool){value}'
        else:
            bound = 61 if 'char' in kind else 3001 if 'short' in kind else 50001
            value = rng.randrange(bound) if kind in UNSIGNED else rng.randrange(-bound, bound)
            literal = f'({kind})({value}L)'
        values.append(value)
        literals.append(literal)
    total = seed % 97 - 48 + sum(int(value) * (i % 7 + 1) for i, value in enumerate(values))
    expression = {
        'signed char': '(signed char)(sum % 101L)',
        'unsigned char': '(unsigned char)((unsigned long)sum % 251UL)',
        'short': '(short)(sum % 30001L)',
        'unsigned short': '(unsigned short)((unsigned long)sum % 65001UL)',
        'int': '(int)sum',
        'unsigned int': '(unsigned int)sum',
        'long': 'sum + 4886691840L',
        'unsigned long': '(unsigned long)sum + 4886691840UL',
        'long long': '(long long)sum - 4886691840LL',
        'unsigned long long': '(unsigned long long)sum + 4886691840ULL',
        '_Bool': '(_Bool)(sum % 2L)',
        'float': '(float)sum + 0.25f',
        'double': '(double)sum - 0.25',
        'int *': 'a' + str(arguments.index('int *')) if 'int *' in arguments else '',
    }[result]
    if result == 'signed char': actual = c_remainder(total, 101)
    elif result == 'unsigned char': actual = (total & ((1 << 64) - 1)) % 251
    elif result == 'short': actual = c_remainder(total, 30001)
    elif result == 'unsigned short': actual = (total & ((1 << 64) - 1)) % 65001
    elif result == 'unsigned int': actual = total & 0xffffffff
    elif result in ('long', 'unsigned long', 'unsigned long long'): actual = total + 4886691840
    elif result == 'long long': actual = total - 4886691840
    elif result == '_Bool': actual = int(c_remainder(total, 2) != 0)
    elif result == 'float': actual = struct.unpack('<I', struct.pack('<f', total + 0.25))[0]
    elif result == 'double': actual = struct.unpack('<Q', struct.pack('<d', total - 0.25))[0]
    elif result == 'int *': actual = 1
    else: actual = total
    expected = actual & ((1 << 64) - 1)
    parameters = ', '.join(f'{kind} a{i}' for i, kind in enumerate(arguments)) or 'void'
    forwarded = ', '.join(f'a{i}' for i in range(len(arguments)))
    body = f'long sum = {seed % 97 - 48}L;\n'
    for i, kind in enumerate(arguments):
        body += f'sum += (long)({"*" if kind == "int *" else ""}a{i}) * {i % 7 + 1}L;\n'
    body += f'return {expression};\n'
    return dict(seed=seed, result=result, arguments=arguments, values=values,
                parameters=parameters, forwarded=forwarded, literals=', '.join(literals),
                body=body, anchor=anchor, expected=f'{expected:016x}')


def sources(spec):
    result, params, args = spec['result'], spec['parameters'], spec['forwarded']
    extra_params = '' if params == 'void' else ', ' + params
    extra_args = '' if not args else ', ' + args
    common = f'extern int anchor;\ntypedef {result} (*Callback)({params});\n'
    callee = f'{result} cinder_callee({params}) {{\n{spec["body"]}}}\n'
    provider = common + callee + f'{result} cinder_apply(Callback cb{extra_params}) {{ return cb({args}); }}\n'
    caller = common + callee + f'''{result} host_callee({params});
{result} host_apply(Callback cb{extra_params});
int report({result} value);
int main(void) {{
    int direct = report(host_callee({spec['literals']}));
    int callback = report(host_apply(cinder_callee{', ' if spec['literals'] else ''}{spec['literals']}));
    return direct + callback;
}}
'''
    if result in FP:
        bits = f'unsigned {"int" if result == "float" else "long long"} bits; memcpy(&bits, &value, sizeof(value)); unsigned long long actual = bits;'
    elif result == 'int *':
        bits = 'unsigned long long actual = value == &anchor;'
    else:
        bits = 'unsigned long long actual = (unsigned long long)value;'
    host = '#include <stdio.h>\n#include <string.h>\n' + common
    host += f'int anchor = {spec["anchor"]};\n{result} host_callee({params}) {{\n{spec["body"]}}}\n'
    host += f'''int report({result} value) {{
    {bits}
    printf("%016llx\\n", actual);
    return actual != 0x{spec['expected']}ULL;
}}
'''
    library = host + f'{result} host_apply(Callback cb{extra_params}) {{ return cb({args}); }}\n'
    driver = host + f'''{result} cinder_callee({params});
{result} cinder_apply(Callback cb{extra_params});
int main(void) {{
    int direct = report(cinder_callee({spec['literals']}));
    int callback = report(cinder_apply(host_callee{', ' if spec['literals'] else ''}{spec['literals']}));
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
    base = ROOT / '.agent-local/abi-callbacks' / source_hash / identity
    base.mkdir(parents=True, exist_ok=True)
    frozen = base / 'cindercc'
    shutil.copy2(compiler, frozen)
    assert digest(frozen) == identity
    host_names = ('gcc', 'clang') if native else ('gcc-15', 'clang')
    hosts = [shutil.which(name) for name in host_names]
    assert all(hosts), ('both reference toolchains are required', host_names)
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
                assert result.stdout.decode() == expected_output, (seed, argv, result)
        for name, text in sources(spec).items():
            path = work / name
            path.write_text(text)
            record['artifacts'][name] = dict(sha256=digest(path), bytes=path.stat().st_size)
        expected = (spec['expected'] + '\n') * 2
        for mode, counterpart in (('provider', 'host_driver'), ('caller', 'host_library')):
            source, host_source = work / (mode + '.c'), work / (counterpart + '.c')
            for index, host in enumerate(hosts):
                reference = work / f'reference-{mode}-{index}'
                argv = [host, '-std=c17', '-O2', source, host_source, '-o', reference]
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
                        executable = work / f'{mode}{level}-{index}'
                        run([host, '-std=c17', '-O2', '-no-pie', obj, host_source, '-o', executable])
                        run([executable], expected, 'native')
                        record['artifacts'][executable.name] = dict(sha256=digest(executable), bytes=executable.stat().st_size)
        persist()
        return dict(seed=seed, expected=spec['expected'], result=spec['result'], arguments=spec['arguments'],
                    reference_executions=sum(command['role'] == 'reference' for command in record['commands']),
                    native_executions=sum(command['role'] == 'native' for command in record['commands']),
                    report=str((work / 'observations.json').relative_to(base)), report_sha256=digest(work / 'observations.json'))

    with concurrent.futures.ThreadPoolExecutor(max_workers=options.jobs) as pool:
        observations = []
        for observation in pool.map(probe, range(options.count)):
            observations.append(observation)
            if len(observations) % 64 == 0:
                print(f'ABI callbacks: {len(observations)}/{options.count} cases observed', flush=True)
    assert source_inputs(ROOT) == inputs, 'source changed during the ABI campaign'
    assert digest(compiler) == identity and digest(frozen) == identity, 'compiler changed during the ABI campaign'
    assert all(digest(ROOT / name) == expected for name, expected in configuration.items()), 'configuration changed'
    summary = dict(schema=1, scope='scalar callbacks; aggregate and variadic ABI remain open',
        source_revision=revision(ROOT), source_inputs=inputs, source_sha256=source_hash,
        compiler_sha256=identity, configuration_inputs=configuration, toolchains=toolchains,
        host=dict(system=platform.system(), machine=platform.machine(), uname=platform.uname()._asdict(),
            virtual_machine=pathlib.Path('/sys/class/dmi/id/product_name').read_text().strip()
                if pathlib.Path('/sys/class/dmi/id/product_name').is_file() else None), native=native,
        cases=options.count, reference_executions=sum(record['reference_executions'] for record in observations),
        native_executions=sum(record['native_executions'] for record in observations),
        scalar_return_families=sorted({record['result'] for record in observations}), observations=observations)
    (base / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(f'ABI callbacks: {options.count} scalar signatures, both directions, two toolchains; native={native}')


if __name__ == '__main__':
    main()
