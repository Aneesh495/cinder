# Alignment declarations and storage

The LP64 target supports fundamental alignments 1, 2, 4, 8, and 16.
`_Alignof(type)` uses target type layout. `_Alignas(type)` and `_Alignas(ICE)`
set object or member storage alignment. Zero has no effect; multiple
specifiers select their maximum, which cannot weaken natural alignment.
Alignment remains separate from scalar type size and alignment.
Extended requests above 16 receive a source diagnostic.

Alignment specifiers cannot apply to typedef names, functions, parameters,
register objects, or bitfields. Type operands must denote complete object
types. Constant operands receive semantic checking even in unselected
expressions. Aligned object redeclarations must agree, and definitions
must state the alignment when any declaration does. Standalone tag
specifiers warn that alignment has no object declarator and leave type
layout unchanged. These rules follow C11/C17 section 6.7.5 in the
[WG14 draft](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf).

Requested member alignment controls field offsets, aggregate alignment,
and tail padding. It therefore reaches array stride, aggregate copying,
and SysV argument/result classification. Requested automatic alignment
reaches checked frame layout; requested global alignment reaches original
ELF offsets. Data, readonly, and BSS sections have 16-byte alignment.
Assembly output preserves section alignment, bytes, and relocations.

Canonical CIR retains requested field, global, and local alignment.
The decoder rejects malformed requests and inconsistent non-bitfield
aggregate layout. The allocator and its independent checker validate local
requests and aligned frame extents.

`make test-alignment` checks authored source against GCC and Clang at O0/O2,
compares directly emitted objects with independently assembled output,
and rejects corrupt CIR, unsupported extended alignment, and excessive
nesting while preserving existing output. Aggregate and variadic callback
campaigns include aligned INTEGER, SSE, and MEMORY families in both call
directions. Full C44 acceptance still requires audited native evidence.
