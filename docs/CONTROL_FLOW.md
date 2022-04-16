# Control flow

Logical operators lower to branches and normalize their results to zero or
one. The unselected operand is absent from the executed path. Conditional
expressions evaluate one arm and join through an explicit local slot. Nested
choices use block IDs so vector growth cannot invalidate a terminator pointer.

`for` has separate condition, body, step, and exit blocks. Continue targets the
step block; break targets the exit. `do` enters its body before testing the
condition. Every statement checks its active terminator before adding a fall
through edge. Prefix increment returns the updated value, postfix returns the
loaded value. Compound assignments load, compute, then store once for the
currently supported named scalar lvalues.

The independent interpreter executes the branches. `make test-control` retains
observations for each authored source and optimization level. On Linux x86-64
it also executes the resulting linked binary. This test family does not imply
support for pointer lvalues, casts, mixed arithmetic conversions, switch/goto,
or complete SSA construction. Those remain separate implementation work.
