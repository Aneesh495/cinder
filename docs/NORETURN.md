# Nonreturning functions

`_Noreturn` applies to declarations of function identifiers, including
repeated specifiers, prototypes, definitions, and block prototypes. It
cannot apply to object or typedef declarations, function pointers,
parameters, members, type names, or hosted `main`. It is a declaration
contract, so function pointer compatibility and ABI classification remain
unchanged. The authored `stdnoreturn.h` exposes the `noreturn` macro for the C17 profile.

Compatible redeclarations combine the contract. Semantic function bindings
retain declaration identity, including block declarations and later file
redeclarations. Lowering resolves the canonical contract for definitions
and direct calls. An ordinary local pointer with the same name retains
its own binding. Indirect calls reach the marked definition's contract.

The C11/C17 rule in section 6.7.4 requires a marked function to avoid
returning and recommends a diagnostic when it appears capable of doing so.
See the [WG14 draft](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf).
The implementation emits one warning for a possible return path. Its
bounded CFG walk follows loops, constant conditions, and marked calls;
unknown conditions remain conservative. The diagnostic does not reject a
translation unit merely because a marked function is never called.

Canonical CIR schema 4 retains function and call contracts. The independent
interpreter classifies a reached return as `noreturn_return`; evaluation
before the return remains observable. Native emission uses original `UD2`
bytes at marked function returns and after marked calls. No undefined
returning-function program is executed as a native correctness oracle.

`make test-noreturn` runs authored reference/object comparisons, checks
warnings and dead return paths, rejects malformed CIR contracts while
preserving output, and checks defined process exits through libc. Native
exit probes run only on Linux x86-64. Their IR classification explicitly
records that external process termination is not yet an interpreter
service. Full C45 acceptance still requires audited native evidence.
