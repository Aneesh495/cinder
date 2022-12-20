# Encoder-derived DWARF frames

Status: implemented for debugger stack unwinding; source/value metadata remains open.

## Decision

Generate DWARF v4 `.debug_frame` records from actual encoder events. Selected
allocation supplies callee-saved frame slots. The byte emitter supplies the
precise post-instruction offset for each save, restore and epilogue. Use a
shared owned debug-section representation for direct ELF and assembly output.
Relocate both function addresses and section-relative CIE offsets.

## Reason

The frame base remains RBP through stack allocation and outgoing call storage.
The unwind description can therefore follow exact stores and restores without
predicting instruction sizes from IR. Multiple return blocks require separate
epilogue rules and restored body state for subsequent laid-out blocks.
Section-relative relocations make multi-unit linking explicit.

## Consequences

Non-debug output keeps its existing section layout. The ELF writer appends
bounded optional debug sections and their relocation sections. LLVM checks the
records and assembly identity; a separate reader checks rows against machine
instructions. GDB must select the DWARF unwinder on linked fixtures. This
increment provides debugger stack metadata only. It does not claim source
locations, local-value tracking or runtime exception unwinding.
