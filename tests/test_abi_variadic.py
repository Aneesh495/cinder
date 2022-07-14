"""Generate variadic calls, callbacks, and cross-toolchain va_list transfers."""
from test_abi_aggregates import COMMON, leaves


def sources(spec):
    result = spec['result']
    args = spec['forwarded']
    extra_args = ', ' + args if args else ''
    # Variadic float and small integer arguments are retrieved using their
    # promoted types. The later source conversion is independently observed.
    reads = []
    for i, kind in enumerate(spec['arguments']):
        promoted = 'double' if kind == 'float' else 'int' if kind == '_Bool' else kind
        reads.append(f'{kind} a{i} = va_arg(list,{promoted});')
    body = spec['body'].removesuffix('return result;')
    common = '#include <stdarg.h>\n' + COMMON + f'typedef {result} (*Callback)(int,...);\n'
    read_body = '\n'.join(reads) + '\n' + body
    cinder = f'''{result} host_read(va_list list);
{result} cinder_read(va_list list) {{
    {read_body}
    return result;
}}
{result} cinder_callee(int tag,...) {{
    va_list list; va_start(list,tag);
    {read_body}
    va_end(list); return result;
}}
{result} cinder_apply(Callback callback,int tag,...) {{
    va_list list; va_start(list,tag);
    {' '.join(reads)}
    va_end(list);
    return callback(tag{extra_args});
}}
{result} cinder_forward(int tag,...) {{
    va_list list; va_start(list,tag);
    {result} result = host_read(list);
    va_end(list); return result;
}}
'''
    host = f'''{result} cinder_read(va_list list);
{result} host_read(va_list list) {{
    {read_body}
    return result;
}}
{result} host_callee(int tag,...) {{
    va_list list; va_start(list,tag);
    {read_body}
    va_end(list); return result;
}}
{result} host_apply(Callback callback,int tag,...) {{
    va_list list; va_start(list,tag);
    {' '.join(reads)}
    va_end(list);
    return callback(tag{extra_args});
}}
{result} host_forward(int tag,...) {{
    va_list list; va_start(list,tag);
    {result} result = cinder_read(list);
    va_end(list); return result;
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
    host_common = '#include <stdio.h>\n#include <string.h>\n' + common + host + '\n'.join(report) + '\n'
    provider = common + cinder
    caller = common + cinder + f'''{result} host_callee(int tag,...);
{result} host_apply(Callback callback,int tag,...);
{result} host_forward(int tag,...);
int report({result} value);
int main(void) {{
    {spec['declarations']}
    int first = report(host_callee({spec['seed']}{extra_args}));
    int second = report(host_apply(cinder_callee,{spec['seed']}{extra_args}));
    int third = report(host_forward({spec['seed']}{extra_args}));
    int fourth = report(cinder_forward({spec['seed']}{extra_args}));
    return first+second+third+fourth;
}}
'''
    driver = host_common + f'''{result} cinder_callee(int tag,...);
{result} cinder_apply(Callback callback,int tag,...);
{result} cinder_forward(int tag,...);
int main(void) {{
    {spec['declarations']}
    int first = report(cinder_callee({spec['seed']}{extra_args}));
    int second = report(cinder_apply(host_callee,{spec['seed']}{extra_args}));
    int third = report(host_forward({spec['seed']}{extra_args}));
    int fourth = report(cinder_forward({spec['seed']}{extra_args}));
    return first+second+third+fourth;
}}
'''
    return {'provider.c': provider, 'caller.c': caller,
            'host_driver.c': driver, 'host_library.c': host_common}
