# Generic selection

`_Generic(control, type: expression, default: expression)` selects one
association using compatible types. The control undergoes lvalue, array,
and function conversions, including removal of top-level qualification.
It receives no integer promotion. These rules follow the C17 resolution of
[WG14 issue 481](https://open-std.org/JTC1/SC22/WG14/issues/c11c17/issue0481.html).

The control and every association receive semantic checking. Association
types must be complete object types at their source points; compatible
duplicates and repeated defaults are rejected. A control must match exactly
one association or use the single default. Selected expressions retain
their type and value category, including lvalues, arrays, functions, and
void results. This target profile uses signed 32-bit enumeration types,
compatible with `int`; distinct enumeration tags remain distinct types.

Only the selected expression is evaluated or lowered. Unevaluated controls
and unselected expressions create no calls, writes, loads, or automatic
compound literal storage. Constant expressions and static addresses use
the same selection rules while preserving full checks for every arm.
Parser nesting is bounded at 128 generic selections, with a diagnostic
before further recursive descent.

`make test-generic` compares all authored programs with GCC and Clang at
O0/O2 and checks owned object bytes and relocations against assembly.
Source/IR suites cover selected effects, array and function results,
aggregate copying, lvalue operations, constant bounds, and static
relocations. Constraint fixtures cover invalid controls, invalid
unselected expressions, compatible duplicates, incomplete and non-object
types, and missing matches. Full C43 family acceptance remains open.
