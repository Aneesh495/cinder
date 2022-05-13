# Canonical typed IR

The schema header is `cinder-ir 1 lp64-le sysv-x86-64`. The format uses whitespace
separated tokens and optional `#` comments between tokens. The writer emits a
deterministic order. Type IDs follow first reachable use; recursive aggregate
identities are collected before their members. Integers and floating values use
sixteen hexadecimal digits representing exact 64-bit storage. Strings use `x`
followed by hexadecimal bytes; `-` is absent. `none` is an absent reference.

The type table records kind, qualifiers, completeness, signedness, plain-char
identity, target size/alignment, pointee/element type, array length, return type,
variadic state, tag, parameters, and aggregate fields including offsets and bit
positions. Scalar layouts must match the target. By-value aggregate cycles are
rejected; pointers to recursive aggregate identities are valid.

Globals record symbol binding, extern/initializer/read-only state, exact scalar
bits, initial bytes, and source location. Functions record signature, binding,
value/local counts, local storage types, parameters, and blocks. Blocks contain
reciprocal predecessor/successor tables, instructions, and one terminator.

Each instruction records opcode, result/source/callee types, destination and
operands, literal bits, local slot and operator metadata, optional symbol,
floating call result, call/phi arguments, argument register classes, phi incoming
blocks, and location. Terminators record return value, jump/branch targets,
condition, and location. Every block/function/module has an explicit end marker.

Parsing is limited to 64 MiB per module, one million values, 65,536 types and
blocks, eight million tokens, and 256 levels of structural type nesting. Vector
counts and decimal/hex accumulation are checked before indexing. Verification
precedes interpretation, allocation, and emission. Liveness rejects a graph
whose four principal bitset tables would exceed its 128 MiB budget. Malformed
input produces a diagnostic and cannot replace an existing output file.

Examples:

```sh
build/cindercc --serialize-ir -O2 tests/control/ssa_swap_two.c -o out/control.cir
build/cinderir --verify out/control.cir
build/cinderir --interpret out/control.cir
build/cinderir -c out/control.cir -o out/control.o
build/cinderir -S out/control.cir -o out/control.s
```

`tests/test_ir_text.py` checks exact canonical round trips across separate
processes and direct/parsed object equality. It also rejects corrupt signatures,
layouts, references, CFG tables, opcodes, truncation, numeric overflow, and type
cycles while preserving prior output. `tests/ir_campaign.c` uses independent
expected arithmetic values and undefined/resource classifications. It does not
reuse interpreter arithmetic to construct the oracle.
