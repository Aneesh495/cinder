# 0023: Selected storage addresses and bitfield roles

Status: accepted

## Context

Selected memory instructions owned typed load/store forms, but the encoder
still examined IR operation names to choose their base, value operand and
address mode. Bitfield load, update and normalization also shared an implicit
IR dispatch. This left addressing choices outside the selected contract.

## Decision

Select frame, RIP-relative symbol or indirect storage addressing with explicit
read/write direction, address/value IDs, frame slot, ModRM byte, extended-base
choice and relocation kind/addend. Record the actual staged register uses,
scratch changes and boolean-load flag effects. Resolve only the allocated
frame displacement during encoding.

Select distinct bitfield load, store and normalization roles. Keep their
already selected occupied-byte, mask and sign-extension plans. Dispatch memory,
bitfield and conversion encoders by selected role rather than raw IR opcode.
Argument/call, aggregate ABI and variadic protocol expansion remain separate
work; this change does not claim those protocols are fully selected.

## Validation

Fifteen defined sources cover all three address families, both directions,
twelve scalar storage profiles and three bitfield roles. An API probe alters
sixteen role/operand fields per selected memory/bitfield instruction. Both
verification and encoding must reject each alteration before output mutation;
restored plans must reproduce normal objects. Mutation counts are checked
against actual O0 MIR instruction counts.

Independent readers check addressing roles, IDs, frame/symbol constraints,
register contracts, actual load/store prefixes and symbol relocation sites.
Floating constant-pool loads are checked separately from named global storage.
Original and LLVM-assembled sections/relocations must agree. Optional baseline
checks require complete O0/O2 object identity. GCC/Clang and IR executions
check source results; Linux also executes original Cinder objects and builds
the API probe through Cinder against the actual compiler core.
