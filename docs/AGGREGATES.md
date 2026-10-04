# Aggregate layout and global storage

Cinder now parses inline `struct` and `union` definitions, computes target LP64 field offsets and tail padding, and retains those offsets in the type model. Struct fields are placed in declaration order with each field's target alignment; unions begin every field at offset zero and take the maximum field size. Layout overflow and incomplete fields are diagnostics.

This increment deliberately separates layout from aggregate expression lowering. Aggregate declarations can be checked with `-fsyntax-only`; field selection, aggregate assignment/copy, bitfields, flexible members, and aggregate ABI classification remain incomplete until they have typed HIR and native tests.

Constant global objects cross the module and object boundary. Integer constant
expressions and scalar floating literals enter `.data`; uninitialized complete
objects enter `.bss`. String-initialized character arrays use writable `.data`
or `.rodata` according to element qualification, with target-size zero fill.
Symbols carry section-relative offsets, binding, and sizes. Scalar reads/writes
use declared widths and RIP-relative relocations. Float32 storage uses actual
32-bit representation and SSE conversion at the internal value boundary.
The interpreter shares mutable scalar globals across calls. Address aliases
and full object memory remain open.

The implementation does not infer layout from host `sizeof`. The object-section test parses the emitted ELF header and section-name table independently and checks `.text`, `.data`, `.rodata`, `.bss`, `.rela.text`, symbol/string tables, and non-executable-stack metadata.
