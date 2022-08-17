# Block storage

Block-scope `static` objects use owned ELF data, read-only data, or BSS with
deterministic local symbols. They initialize once before execution, retain
their values across calls and block re-entry, and preserve requested target
alignment. Automatic declarations continue to use checked local lifetimes.
Name expressions carry their resolved declaration through lowering, so a
static or external object cannot accidentally use an automatic object's slot.

Block-scope `extern` declarations share the compatible translation-unit
symbol. The source-point visible declaration determines internal or external
linkage. A hidden automatic or block-static declaration has no linkage;
declaring an extern in an inner block therefore introduces external linkage.
Declarations that mix internal and external linkage are diagnosed. Compatible
array bounds compose at the declaration's source point. Definitions still
require consistent explicit alignment when an aligned extern is declared.

Unresolved extern objects remain undefined ELF symbols. The independent
interpreter classifies a reached access to missing external storage as
unsupported. It does not allocate a zero-filled stand-in. Block statics share
the static initializer evaluator, including aggregate plans and object/member
relocations. Nonconstant static initialization and extern initializers are
rejected before publication.

`make test-block-storage` checks 47 authored standalone programs against GCC
and Clang at both optimization levels and compares owned object sections and
relocations against independent assembly. Seven additional profiles cover
cross-unit static identity, host-provided arrays/records, incomplete extern
types, lexical shadowing, and unused external declarations. These profiles
also require canonical CIR identity and exact defined native exits on Linux.
Read-only static storage has separate interpreter UB probes.

Rules are based on C17 6.2.2, 6.2.4, 6.7.5, 6.7.9, and 6.8.5, as reproduced
in the [WG14 draft](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf).
The feature registry remains partial until audited native report readers and
the complete language-family evidence are available.
