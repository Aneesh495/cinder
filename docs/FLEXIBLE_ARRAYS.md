# Flexible array members

Cinder accepts a final named incomplete array in a struct after another named
member. Its element type must be complete. The member has zero fixed size but
its element alignment contributes to record layout and tail padding. Arrays
of such records are rejected. A union can contain the record, including through
other unions; that union cannot become a struct member or an array element.
Pointers to any of these types remain ordinary pointer members.

For example, `struct S { int count; double values[]; };` has size 8, alignment
8, and `values` at offset 8 on the declared target. Allocating
`sizeof(struct S) + n * sizeof(double)` provides the header and tail storage.
Allocation, overflow checking and deallocation remain the program's duties.
`offsetof(struct S, values)` is a target integer constant. The array remains an
incomplete type, so applying `sizeof` to the member is rejected.

Aggregate initialization and assignment operate on the fixed header. Flexible
members cannot be initialized by positional or designated initializers. Passing
and returning the struct uses its fixed size and System V classes; the flexible
member contributes no INTEGER or SSE class. This matches GCC's fixed-header
ABI. Owned callers exchange these values and callbacks with GCC, and memory
returns with both references. Pointer-based header interfaces exercise both
GCC and Clang, including callback calls and register exhaustion.

Clang's x86-64 classifier puts flexible-member records in MEMORY, including
small headers that GCC passes in registers. Its incompatible small by-value
profiles are recorded separately with exact source hashes and independently
checked `sret`/`byval` LLVM signatures. They cannot count as successful Cinder
interoperability observations. The original failed cross-call, compiler/object
hashes and both providers remain in the private failure artifacts. A change in
Clang's signatures fails the discrepancy check and requires a new audit.
See the [Clang classifier](https://raw.githubusercontent.com/llvm/llvm-project/main/clang/lib/CodeGen/Targets/X86.cpp)
and [GCC's ABI change](https://gcc.gnu.org/gcc-4.4/changes.html).

The interpreter determines a tail's array bound from the containing object's
actual extent. A fixed record can provide usable tail padding. For example,
`struct P { long count; char tag; char values[]; };` has seven accessible tail
bytes within its 16-byte extent. Each byte must be initialized before reading.
Recursive union membership preserves the containing storage extent. A tail
with no whole element supports its base address; accessing an element or
forming a one-past pointer is classified as undefined. Initialization,
qualification and lifetime checks also cover tail storage.

Native allocation tests use real `malloc`, `calloc`, `realloc` and `free` with
integer, floating, pointer, record and multidimensional tails. These external
calls remain unavailable in the interpreter and are reported as such. They do
not count as interpreted source successes. Header copies are tested without
assuming padding contents or copying payload bytes beyond the fixed size.

Canonical CIR records preserve the incomplete array type and fixed aggregate
layout. Import independently rejects illegal placement, missing names, bad
extents/alignment, recursive value types, forbidden embeddings and arrays of
flexible records before output publication.

Run `make test-flexible test-constraints test-memory`. The focused tests include
authored source observations, reference execution, encoded section/relocation
comparisons, CIR/object identities, native allocation and cross-toolchain calls,
plus malformed type graphs. Anonymous member promotion remains a separate open
language requirement; this increment covers named headers. Full acceptance and
the required bootstrap campaign remain incomplete.

The semantics follow C17 6.7.2.1 paragraphs 3 and 18 through 26 in the
[public C11 draft](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf),
whose flexible-member rules also apply to this C17 profile.
