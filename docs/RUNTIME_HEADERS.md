# Authored target headers and offset constants

The runtime include directory describes Cinder's Linux x86-64 LP64 target.
These declarations are independent of the build host's headers. This is a
narrow compiler runtime profile, not a complete standard library. The driver
searches user `-I` directories first, then the configured runtime directory.
The installed-path and remaining libc/POSIX interfaces are still open.

`stdbool.h` defines the four C17 macros. `stddef.h` declares unsigned-long
`size_t`, signed-long `ptrdiff_t`, signed-int `wchar_t`, and a record with
16-byte alignment for `max_align_t`. The latter has an implementation-specific
size of 16 bytes. It is an alignment token, not a libc wire structure. Reference
libcs may give it a different size. `NULL` expands to `(void *)0`.

`limits.h` describes signed plain char, 8-bit bytes, 16-bit short, 32-bit int,
and 64-bit long and long long, with exact typed extrema. `MB_LEN_MAX` is 16,
matching the declared glibc locale profile. No locale or multibyte conversion
implementation is claimed. `stdint.h` supplies exact and least widths 8/16/32/64,
fast-8 as char, fast-16/32/64 as long, pointer and maximum integers as long,
their matching limits, and token-pasted constant macros. Small constant macros
have the promoted int type. SIG_ATOMIC, WCHAR and WINT limits describe the
Linux libc int, signed-int and unsigned-int representations; the corresponding
API headers remain to be implemented and checked.

## Real `offsetof`

`offsetof(type, member)` expands to the reserved `__cinder_offsetof` intrinsic.
The parser creates an expression owning a typename and a member/index path.
Semantic analysis checks every index, including discarded generic associations
and unevaluated `sizeof` operands. The constant evaluator walks the target
record fields and array strides, checks source-point completeness, and rejects
missing members, pointer traversal, scalar indexing, nonconstant indices,
overflow, and nonaddressable bitfields. No null pointer is evaluated and no
runtime object or load is created.

Nested members, arrays of records, matrices, and unions are supported. An array
index may select its one-past address only as the final designator. Negative or
further out-of-bounds indices are rejected. This is a deliberate profile bound;
some host compiler builtins accept wider designators as extensions. Pointer
members may be selected, but the designator cannot follow their pointees.
The intrinsic result is an integer constant expression of target `size_t`.
It participates in array bounds, enums, assertions, case labels, static
initializers, and generic type selection. Lowering emits an ordinary typed
IR constant, consumed by both the interpreter and original native encoder.

The parser bounds intrinsic nesting to 128 and member/index paths to 256.
The shared constant evaluator also bounds combined expression depth to 256.
These are independent bounds. The preprocessor has its own macro expansion
bound. Malformed or over-budget input produces diagnostics and preserves a
previous complete output file.

## Validation

`make test-offset` checks 34 individually authored observations with GCC and
Clang at `-O0/-O2`, executes both Cinder interpreter levels, compares original
encoded sections/relocations with independently assembled output, round-trips
68 canonical IR modules, and requires direct and parsed objects to be identical.
On Linux it executes owned, assembled, and parsed objects. Generated parser
bounds and invalid designators are recorded separately. The shared constraint
suite includes 13 offset violations and checks nonzero status and atomic output.

Reference assertions use each header's declared integer types. Exact target
maximum alignment is asserted on Linux x86-64 and Cinder; the macOS arm64 host
has a different standard-header alignment. Raw reference disagreements and
startup timeouts are retained in the private audit rather than reported as
successful executions. Native workflow artifacts bind observations to the
compiled input and compiler bytes.

See [the offset pipeline](diagrams/offsetof.mmd),
[WG14 N1570 sections 7.18 through 7.20](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf),
and [the variadic profile](VARARGS.md).
