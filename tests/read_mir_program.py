"""Decode selected ABI steps independently using retained physical allocations."""
import re
import struct


def transfer(step, function):
    kind, value, reg = step['kind'], step['value'], step['reg']
    bank, normalization = function['values'][value]; location = function['locations'][value]
    assert kind in (1, 2, 3, 4) and bank == (1 if kind in (1, 3) else 2)
    assert reg in ((0, 10) if kind == 1 else range(8) if kind == 2 else (0,))
    integer = isinstance(location, int)
    if integer:
        offset = struct.pack('<i', location)
        program = {1: (b'\x48\x8b\x85' if reg == 0 else b'\x4c\x8b\x95'),
                   2: b'\xf2\x0f\x10' + bytes((0x85 | reg << 3,)),
                   3: b'\x48\x89\x85', 4: b'\xf2\x0f\x11\x85'}[kind] + offset
    elif bank == 1:
        assert location in ('r12', 'r13', 'r14', 'r15'); physical = int(location[1:])
        source, destination = (physical, reg) if kind == 1 else (0, physical)
        program = bytes((0x48 | (4 if source >= 8 else 0) | (1 if destination >= 8 else 0),
                         0x89, 0xc0 | ((source & 7) << 3) | (destination & 7)))
    else:
        assert re.fullmatch(r'xmm[2-7]', location); physical = int(location[3:])
        source, destination = (physical, reg) if kind == 2 else (0, physical)
        program = b'\xf2\x0f\x10' + bytes((0xc0 | destination << 3 | source,))
    if kind == 3:
        assert normalization in (b'', b'\x89\xc0', b'\x48\x63\xc0', b'\x0f\xb6\xc0', b'\x48\x0f\xbe\xc0',
                                 b'\x0f\xb7\xc0', b'\x48\x0f\xbf\xc0', b'\x48\x85\xc0\x0f\x95\xc0\x0f\xb6\xc0')
        program = normalization + program
    return program


def validate_dump(text, sections, symbols, relas, disassembly):
    functions = {}; function = program = step = None; allocating = None; origin = None
    for line in text.splitlines():
        match = re.match(r'selected-mir (\S+) target=', line)
        if match:
            name = match[1]; assert name not in functions
            function = functions[name] = dict(programs=[], values={}, locations={}, slots={}); allocating = None
        elif line.startswith('machine-value '):
            match = re.fullmatch(r'machine-value (\d+) bank=(\d+) normalization=([0-9a-f]*) incoming-bool=(yes|no)', line); assert match
            function['values'][int(match[1])] = int(match[2]), bytes.fromhex(match[3])
        elif line.startswith('  machine-kind=12 '): origin = re.search(r' origin=(\S+)', line)[1]
        elif line.startswith('    protocol '):
            match = re.fullmatch(r'    protocol role=(\d+) steps=(\d+)', line); assert match
            role, count = map(int, match.groups()); assert role == {'arg': 1, 'farg': 2, 'aggregate.arg': 3, 'aggregate.return': 4, 'va.start': 5, 'va_arg': 6, 'call': 7}[origin]
            program = dict(role=role, count=count, steps=[]); function['programs'].append(program)
        elif line.startswith('      step='):
            match = re.fullmatch(r'      step=(\d+) kind=(\d+) value=(\d+) reg=(\d+) bytes=([0-9a-f]*)', line); assert match
            index, kind, value, reg = map(int, match.groups()[:4]); assert index == len(program['steps'])
            step = dict(kind=kind, value=value, reg=reg, bytes=bytes.fromhex(match[5]), patches=[], fixups=[]); program['steps'].append(step)
        elif line.startswith('        frame-patch '):
            match = re.fullmatch(r'        frame-patch kind=(\d+) offset=(\d+) slot=(-?\d+) addend=(-?\d+)', line); assert match
            step['patches'].append(tuple(map(int, match.groups())))
        elif line.startswith('        symbol-fixup '):
            match = re.fullmatch(r'        symbol-fixup offset=(\d+) type=(\d+) symbol=(\S+) addend=(-?\d+)', line); assert match
            step['fixups'].append((int(match[1]), int(match[2]), match[3], int(match[4])))
        elif line.startswith('allocation function='):
            name = re.search(r'function=(\S+)', line)[1]; allocating = functions[name]
        elif allocating is not None and line.startswith('  frame-program '):
            match = re.search(r'size=(\d+) incoming-base=(\d+) variadic=(-?\d+)', line); assert match
            allocating['frame'], allocating['incoming'], allocating['variadic'] = map(int, match.groups())
        elif allocating is not None and line.startswith('  object-slot='):
            match = re.fullmatch(r'  object-slot=(\d+) offset=(-?\d+) bytes=(\d+) align=(\d+)', line); assert match
            slot, offset, size, alignment = map(int, match.groups()); assert -allocating['frame'] <= offset < 0 and size > 0 and alignment > 0 and offset % alignment == 0
            allocating['slots'][slot] = offset
        elif allocating is not None and line.startswith('  %v'):
            match = re.fullmatch(r'  %v(\d+) \[\d+,\d+\] -> (?:(r1[2-5]|xmm[2-7])|stack (-?\d+)\(%rbp\))', line); assert match, line
            allocating['locations'][int(match[1])] = match[2] if match[2] else int(match[3])
    code = sections['.text']; targets = {s['name']: s for s in symbols if s['kind'] == 2 and s['section'] == '.text'}
    boundaries = {int(m[1], 16) for m in re.finditer(r'^\s*([0-9a-f]+):\s+[a-z][^\n]*$', disassembly, re.M)}
    relocations = {r['offset']: r for r in relas.get('.text', [])}; roles = set(); step_roles = set(); patch_roles = set(); observations = []
    for name, function in functions.items():
        symbol = targets[name]; begin, end = symbol['value'], symbol['value'] + symbol['size']; cursor = begin
        for program in function['programs']:
            assert len(program['steps']) == program['count'] > 0
            expected = bytearray(); fixups = []; roles.add(program['role'])
            for step in program['steps']:
                kind = step['kind']; assert kind in range(5); step_roles.add(kind)
                if kind != 0:
                    assert not step['bytes'] and not step['patches'] and not step['fixups']; expected.extend(transfer(step, function)); continue
                assert step['value'] == 0xffffffff and step['reg'] == 0 and step['bytes']
                data = bytearray(step['bytes'])
                for patch_kind, offset, slot, addend in step['patches']:
                    assert patch_kind in range(4) and 0 <= offset <= len(data) - 4; patch_roles.add(patch_kind)
                    assert data[offset:offset + 4] == bytes(4)
                    if patch_kind == 0: value = function['slots'][slot]
                    elif patch_kind == 1:
                        assert slot in range(6); value = -(function['incoming'] + slot + 1) * 8
                    elif patch_kind == 2:
                        assert slot in range(8); value = -(function['incoming'] + slot + 7) * 8
                    else:
                        assert slot == 0 and function['variadic'] == -function['frame']; value = function['variadic']
                    struct.pack_into('<i', data, offset, value + addend)
                for offset, relocation, target, addend in step['fixups']:
                    assert relocation == 4 and addend == -4 and 0 <= offset <= len(data) - 4
                    assert data[offset - 1] == 0xe8 and data[offset:offset + 4] == bytes(4)
                    fixups.append((len(expected) + offset, target))
                expected.extend(data)
            found = code.find(expected, cursor, end)
            assert found >= 0 and found in boundaries and (found + len(expected) in boundaries or found + len(expected) == end), (name, program['role'], cursor, expected.hex())
            for offset, target in fixups:
                actual = relocations[found + offset]; assert (actual['kind'], actual['symbol']['name'], actual['addend']) == (4, target, -4)
            observations.append(dict(function=name, role=program['role'], offset=found, bytes=len(expected), steps=len(program['steps']), patches=sum(len(s['patches']) for s in program['steps']), fixups=len(fixups)))
            cursor = found + len(expected)
        for pc in boundaries:
            if not begin <= pc < end: continue
            width = 1 if code[pc] == 0xe9 else 2 if code[pc] == 0x0f and pc + 1 < end and 0x80 <= code[pc + 1] <= 0x8f else 0
            if width:
                target = pc + width + 4 + struct.unpack_from('<i', code, pc + width)[0]; assert begin <= target < end and target in boundaries
    return roles, step_roles, patch_roles, observations
