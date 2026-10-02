# Integer variadic scalar slice

Cinder recognizes `...` in function types and provides an authored integer path for `va_start`, `va_arg(ap, int)`, and `va_end`. Each syntactic `va_arg` lowers to an explicit IR vararg read at its fixed ordinal after the named parameters. Native lowering reads the corresponding System V integer argument register or stack argument slot. Calls with more than six integer arguments push owned stack arguments with alignment padding and restore the outgoing area after the call.

The current slice is intentionally ordinal rather than a full hosted `va_list` implementation. A repeated `va_arg` inside a runtime loop does not advance a mutable save-area cursor, `va_copy` is not implemented, floating/default-promoted varargs are not implemented, and reference libc `va_list` interoperability is unverified on this host. Those constraints are diagnosed in documentation and excluded from the complete ABI gate rather than hidden behind a typedef.
