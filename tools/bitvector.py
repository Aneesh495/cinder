"""Independent, bounded integer fragment semantics for translation validation.

This module never imports the compiler interpreter or a production pass. Python
integers provide unbounded arithmetic before explicit width and definedness
checks. Only the straight-line fragment grammar below is accepted.
"""


def normalize(value, width, signed):
    bits = value & ((1 << width) - 1)
    return bits - (1 << width) if signed and bits >= (1 << (width - 1)) else bits


def fragment(text):
    lines = text.splitlines()
    assert lines[0] == 'cinder-ir 6 lp64-le sysv-x86-64'
    assert sum(line.startswith('function ') for line in lines) == 1
    assert sum(line.startswith('block ') for line in lines) == 1
    types = {}
    operations = []
    result = None
    allowed = {'arg', 'const', 'undef', 'nop', 'copy', 'mul', 'div.u',
               'div.s', 'mod.u', 'mod.s', 'shl', 'shr.u', 'and'}
    for line in lines:
        fields = line.split()
        if fields[0] == 'type' and fields[2] in ('char', 'short', 'int', 'long', 'llong'):
            types[int(fields[1])] = (8 * int(fields[7]), fields[5] == '0')
        elif fields[0] == 'inst':
            assert fields[1] in allowed, fields[1]
            assert fields[12] == '-' and fields[13] == '0'
            assert fields[16:18] == ['args', '0']
            operations.append((fields[1], int(fields[2]),
                               *(None if value == 'none' else int(value) for value in fields[5:8]),
                               int(fields[8], 16), int(fields[10])))
        elif fields[0] == 'term':
            assert fields[1] == 'return' and result is None
            result = int(fields[2])
    assert result is not None and types
    return types, operations, result


def evaluate(parsed, argument):
    types, operations, result = parsed
    values = {}
    for op, kind, dst, left, right, literal, slot in operations:
        width, signed = types[kind]
        if op == 'nop':
            continue
        if op == 'arg':
            assert slot == 0
            values[dst] = None if argument is None else normalize(argument, width, signed)
            continue
        if op == 'undef':
            values[dst] = None
            continue
        if op == 'const':
            values[dst] = normalize(literal, width, signed)
            continue
        a = values[left]
        b = values[right] if right is not None else None
        if a is None or (right is not None and b is None):
            return 4, None
        if op == 'copy':
            value = a
        elif op == 'mul':
            value = a * b
            if signed and not -(1 << (width - 1)) <= value < (1 << (width - 1)):
                return 1, None
        elif op in ('div.u', 'div.s', 'mod.u', 'mod.s'):
            if b == 0:
                return 2, None
            if signed and a == -(1 << (width - 1)) and b == -1:
                return 1, None
            quotient = abs(a) // abs(b)
            if (a < 0) != (b < 0):
                quotient = -quotient
            value = a - quotient * b if op.startswith('mod.') else quotient
        elif op in ('shl', 'shr.u'):
            if not 0 <= b < width:
                return 3, None
            if op == 'shl':
                value = a * (1 << b)
                if signed and (a < 0 or value >= (1 << (width - 1))):
                    return 3, None
            else:
                value = (a & ((1 << width) - 1)) // (1 << b)
        elif op == 'and':
            value = a & b
        else:
            raise AssertionError(op)
        values[dst] = normalize(value, width, signed)
    return (4, None) if values[result] is None else (0, values[result])
