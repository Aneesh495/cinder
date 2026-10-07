# Canonical typed IR

The schema header is `cinder-ir 2 lp64-le sysv-x86-64`. The format uses whitespace
separated tokens and optional `#` comments between tokens. The writer emits a
deterministic order. Type IDs follow first reachable use; recursive aggregate
identities are collected before their members. Integers and floating values use
sixteen hexadecimal digits representing exact 64-bit storage. Strings use `x`
followed by hexadecimal bytes; `-` is absent. `none` is an absent reference.

The type table records kind, qualifiers, completeness, signedness, plain-char
identity, target size/alignment, pointee/element type, array length, return type,
variadic state, tag, stable type identity, parameters, and aggregate fields including offsets and bit
positions. Scalar layouts must match the target. By-value aggregate cycles are
rejected; pointers to recursive aggregate identities are valid.

Globals record symbol binding, extern/initializer/read-only state, exact scalar
bits, initial bytes, typed symbolic address records, and source location.
An address records its destination offset, symbol, exact signed addend,
pointer/target types, domain begin/end, and function identity flag.
Functions record signature, binding,
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

The schema is under development with the compiler. Artifacts from earlier
checkpoints are source-bound evidence and must be regenerated after format or
semantic changes.

`cinderir --classify input.cir` executes `main` and writes a JSON result with
`valid`, numeric `classification`, stable `class`, integer result, floating
result flag, and exact floating bits. Classified undefined/resource outcomes
are successful measurements for this mode; `valid` reports whether execution
was defined. Ordinary `--interpret` still fails on those outcomes.

Address, typed memory, pointer arithmetic/member, and local lifetime opcodes
retain their types and operands in canonical text. The parser verifies object
slots, pointer strides, field offsets, and scalar access widths before use.

`object.copy` and `object.init` transfer the complete extent of a typed array
or aggregate through two compatible object pointers. They are effect-only
instructions. Initialization is explicit because const objects can be
initialized while ordinary writes remain constrained. The interpreter copies
initialization flags and pointer metadata with the object representation.

`va.start`, `va.copy`, and `va.end` are effect-only instructions with a typed
list pointer on the left; copy has a second list pointer on the right.
`va_arg` records its requested type in `source_type`. Scalar reads return
that type and use no storage slot. Aggregate reads return a pointer to the
requested type and name a compatible local result slot. Runtime state remains
explicit across loops and calls; no syntactic argument ordinal is retained.
