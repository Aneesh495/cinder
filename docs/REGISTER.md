# Register objects and variadic parameters

`register` uses automatic storage and the ordinary allocation pipeline. It is a
source addressability restriction, not a promise to place an object in a machine
register. Explicit addresses of a register object or any nested aggregate member
are rejected, including inside unevaluated operands and generic selections.
Register arrays cannot decay to pointers. Their complete array type remains
available to `sizeof`, including array-valued generic selections. A register
pointer can still access and take addresses of the objects it points to.
Register aggregate copies, arguments, and returns use the ordinary aggregate ABI.

Function parameters retain their declaration's register and original
array/function adjustment properties. These source properties do not participate
in function type compatibility and are consumed before executable IR. A previous
prototype cannot transfer its register restriction or original array syntax to
a later function definition. Names in retained constant expressions preserve
source-point declaration identity, so array bounds and prototype scopes cannot
lose addressability restrictions.

`va_start` must name the actual final named parameter declaration. A local object
with the same spelling is rejected. Evaluated uses are also rejected when that
parameter is declared register, was originally declared with array/function type,
or has a scalar type changed by default promotions. The target's int-compatible
enumerations, qualified int, double, pointers, and aggregate parameters remain
eligible. A promotion-changing parameter in an unevaluated generic association
or `sizeof` operand does not invoke `va_start` and is accepted when its declaration
identity is correct.

The invalid variadic cases follow a C library Description rule whose violation
has undefined behavior; host compilers may warn or accept them. Register-array
conversion is also a semantic UB rule, including inherited member storage. Each
non-mandatory reference diagnostic has an exact source hash and a stated rule
in `tests/constraints/reference_policy.json`. Raw GCC and Clang diagnostics are
retained. Cinder still requires rejection before object publication.

`make test-register` checks 30 individually authored defined programs against
GCC and Clang at O0/O2 and compares direct, independently assembled, and
canonical-IR object paths. It covers scalar updates, pointer cancellation,
aggregate copies and ABI operations, register parameters, prototype/definition
differences, original array syntax, declaration shadowing, and valid variadic
parameter families. Undefined variadic inputs are not run natively.

Rules follow sections 6.3.2.1, 6.5.3.2, 6.7.1, 6.7.6.3, and 7.16.1.4 of the
[WG14 draft](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf).
