# Authored target headers and offset constants

The runtime include directory describes Cinder's Linux x86-64 LP64 target.
These declarations are independent of the build host's headers. This is a
narrow compiler runtime profile, not a complete standard library. The driver
searches user `-I` directories first, then the configured runtime directory.
The installed-path discovery contract remains open.

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
signal and wide-character API headers are outside this narrow runtime.

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


## Libc and POSIX boundary

Owned `stdio.h`, `stdlib.h`, `string.h`, `errno.h`, `ctype.h`, `time.h`,
`sys/types.h`, `sys/wait.h`, `sys/stat.h`, and `unistd.h` declare the APIs used
by the compiler. They require Linux x86-64 glibc or the tested musl ABI.
`FILE` is opaque. Only pointers cross the boundary. No private FILE layout,
locale machinery, stat structure, standard library implementation, or Darwin
ABI is supplied. POSIX declarations are available in this runtime profile;
feature-test macros on reference builds select the corresponding interfaces.

`pid_t` is int, `mode_t` unsigned int, and `ssize_t` and `time_t` long.
`struct tm` has the nine standard int fields followed by its Linux offset and
zone-pointer slots, for extent 56 and alignment 8. The private extension names
are not public libc spelling promises. Wait-status macros decode normal exit
status and signals. `errno` calls Linux's thread-local `__errno_location` accessor;
only the six named error values needed by the supported interface are provided.
The predefined target macros now include `__unix__` as well as `__linux__`, so
actual compiler conditional includes retain their POSIX driver declarations.

`inttypes.h` supplies the 64-bit, maximum and pointer printf spellings needed
by the implementation. `float.h` describes binary32/binary64. `math.h` supplies
`isfinite` using an authored inline byte inspection of the target binary64
representation. Float inputs convert exactly to double for classification;
there is no runtime host-compiler intrinsic. Complex and long-double inputs
remain excluded. Other mathematical functions are not declared.

`make test-runtime-headers` compiles 15 authored probes against both host
system-header references and Cinder at both optimization levels. A prototype
contract checks the declared standard/POSIX function types. Layout assertions
check target scalar and `struct tm` layouts and SysV va_list state. macOS
reference headers have different mode_t and va_list choices; exact target
assertions execute on Linux and Cinder rather than conflating host and target.
Every owned object agrees with an independent assembler in sections and
relocations, and with the parsed canonical IR object. Linux runs exercise
allocation, reallocation, character/string conversions, qsort callbacks,
formatted/SSE varargs, va_copy passed to libc, streams, calendar layout,
IEEE finite classification, temporary paths, fork/execvp, and waitpid.

Raw manifests bind all header/source/harness bytes, compiler/tool binaries,
reference versions, argv, outcomes and objects. External libc calls remain
classified as unavailable by the independent IR interpreter; these probes
are separate from the source-interpreter corpus. They are never counted as
interpreter successes. The Linux development VM passed 90 owned native
executions and 60 GCC/Clang system-header reference executions. Final hosted
runtime evidence and all required acceptance readers remain open.

The actual source syntax audit checks every CMake compiler module with these
headers. It passed 41 of 42 modules. `source/arena.c` currently requires flexible
array member support. This audit does not build stage 2 and is not self-hosting
acceptance. No compiler module is substituted with a precompiled host object.

Interface references: [Linux stdio](https://man7.org/linux/man-pages/man3/stdio.3.html)
and [POSIX definitions](https://pubs.opengroup.org/onlinepubs/9799919799/).
