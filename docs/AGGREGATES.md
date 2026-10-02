# Aggregate layout and global storage

Cinder now parses inline `struct` and `union` definitions, computes target LP64 field offsets and tail padding, and retains those offsets in the type model. Struct fields are placed in declaration order with each field's target alignment; unions begin every field at offset zero and take the maximum field size. Layout overflow and incomplete fields are diagnostics.

This increment deliberately separates layout from aggregate expression lowering. Aggregate declarations can be checked with `-fsyntax-only`; field selection, aggregate assignment/copy, bitfields, flexible members, and aggregate ABI classification remain incomplete until they have typed HIR and native tests.

Constant global objects cross the module and object boundary. Integer initializers enter `.data`; uninitialized complete objects enter `.bss`; character arrays initialized from string literals enter `.rodata` with target-size zero fill. The ELF writer publishes section-aligned data symbols with sizes and section-relative values. A global integer read emits an x86-64 RIP-relative load and an `R_X86_64_PC32` relocation. Global mutation is represented in IR and native output, but the independent interpreter currently classifies mutation as unsupported.

The implementation does not infer layout from host `sizeof`. The object-section test parses the emitted ELF header and section-name table independently and checks `.text`, `.data`, `.rodata`, `.bss`, `.rela.text`, symbol/string tables, and non-executable-stack metadata.
