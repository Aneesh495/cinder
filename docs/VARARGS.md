# Dynamic variadic state

Cinder supplies authored `stdarg.h` with `va_list`, `va_start`, `va_arg`,
`va_copy`, and `va_end`. Each read advances a runtime cursor. Loops and helper
functions use the same cursor; `va_copy` creates an independent cursor.
Variadic calls perform integer promotions and promote float to double.

Native `va_list` is the System V array of one 24-byte structure: unsigned
GP and FP offsets followed by overflow-stack and register-save pointers.
Variadic prologues preserve six integer and eight SSE registers in the
176-byte save area. Named arguments, including a hidden aggregate result
pointer, establish the initial offsets and stack position. Reads classify
the requested type, test both required register banks before advancing either,
and otherwise take the entire argument from the aligned overflow stack.
Small mixed aggregates are reconstructed into owned expression storage.

The independent interpreter uses typed argument values in source order.
It checks default-promoted types, the signed/unsigned representability and
void/character pointer exceptions, initialization, exhaustion, matching end,
and frame lifetime. It does not reproduce the encoder's register decisions.
Implementation-specific inspection of `va_list` fields is outside the
interpreter equality oracle; the native representation follows the ABI.

`make test-variadic` compares authored loops, copies, aggregate register
rollback, hidden result pointers, stack arguments, helper functions, and
promotion boundaries with GCC and Clang. `make test-abi-variadic` generates
512 signatures, compares named fields and floating bits, and exercises
variadic calls, callbacks, and `va_list` exchange in both directions on Linux.
All commands, compiler identities, source inventories, and raw observations
are retained. Local macOS object checks explicitly record `native=false`.
Undefined cases run only through the VM and cannot count as native agreement.

The target profile excludes x87, complex, and vector types. Installed header
discovery, complete language coverage, and final audited acceptance remain
separate requirements.

The representation and cursor rules follow the primary
[x86-64 psABI source](https://gitlab.com/x86-psABIs/x86-64-ABI/-/raw/master/x86-64-ABI/low-level-sys-info.tex)
and the C11/C17 variadic rules in section 7.16 of
[N1570](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf).

The editable state diagram is `diagrams/variadic-state.mmd`.

Cinder checks `va_start`'s declaration context even in unevaluated operands.
The source-rejection runner records host diagnostic differences for library
Description rules that do not require a diagnostic. Those entries are bound
to exact source hashes; Cinder still must reject the invalid builtin context
and preserve prior output. The unevaluated nonvariadic case is retained from
Linux run 37695954175, where GCC 13 accepted it without a diagnostic.
