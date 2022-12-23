# Encoder source locations and DWARF4 function entries

Status: implemented for source lines and function entries; types and values open.

## Decision

Collect source rows at actual encoder offsets, then serialize an owned DWARF4
line program and compilation-unit/subprogram entries after all functions have
been emitted. Keep function locations in IR independently of AST/HIR storage.
Use the existing debug section/fixup transport for both original ELF and
assembly output.

One line sequence covers each real function extent. Source-manager expansion
identities supply logical coordinates, include instances and file-prefix maps.
Each subprogram uses a relocated address and constant byte extent; CU section
references relocate as four-byte section offsets. The frame base is the
canonical frame address defined by the encoder's existing unwind records.

The resolved compilation directory follows file-prefix mapping. Macro-only
mapping remains limited to preprocessor string expansion. Physical dependency
names remain suitable for build tools after display-name mapping.

## Consequences

Instruction removal or byte-size changes cannot leave a separately predicted
PC table behind. Empty translation units remain valid compilation units.
Invalid function, row or source identities fail before section construction;
repeated finalization fails instead of appending a second partial CU.

Canonical CIR remains schema 6: source-only debug locations do not change
semantic round trips, and the CIR tool does not accept `-g`. Future serialized
debug metadata needs an explicit schema decision and ownership tests.

The prologue marker follows ABI register staging, before IR parameter stores.
It supports source stepping but cannot establish parameter availability.
Later value support must track actual initialized locations and lifetime
ranges. Allocated frame storage alone cannot justify displaying an optimized
variable.

## Verification

Independent Python readers decode ELF relocations, line-machine state and DIE
forms. LLVM checks DWARF structure, disassembles instruction boundaries and
assembles the same original debug directives for byte/relocation comparison.
Fixtures cover O0/O2, multiple units, includes, macro invocation, backward
`#line` coordinates, noreturn/empty functions and units without functions.
Relocated checkout trees produce identical mapped objects. A Cinder-built API
probe checks fifteen malformed maps and repeated finalization.

Linux GDB tests resolve source breakpoints, step and recover caller chains.
Execution reports distinguish native, emulated and cross-target profiles.
These tests leave type/value metadata and the complete debugging gate open.
