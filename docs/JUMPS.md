# Labels and jumps

Labels use a separate function-wide namespace. Forward jumps, backward jumps,
labels after returns, and labels nested in ordinary blocks or iteration bodies
resolve before output publication. Undefined labels and duplicate labels in
the same function are errors. A label may share its spelling with an object,
parameter, typedef, member, or tag. C17 requires a statement after the colon;
a declaration or trailing colon is rejected.

`source/control.c` builds a lexical scope tree and sorted label table. Lowering
reserves every automatic object's slot and every label's CFG block before
walking statements. Name expressions retain their resolved declaration and
slot even when a jump bypasses the declaration's initializer. A jump emits
retirement for scopes it leaves and storage entry for scopes it enters. Objects
in the common ancestor scope keep their identity and value. Entering a block
through a label does not execute skipped initializers.

C17 6.2.4 paragraph 6 requires initialization each time a declaration is reached,
and an indeterminate value if the reached declaration has no initializer.
`local.reset` clears the interpreter's initialized-byte and stored-pointer
metadata while preserving object identity. SSA promotion represents the same
reset with an undefined value. Native execution leaves indeterminate storage
unspecified and emits no initialization instruction for the reset itself.
The typed IR verifier checks the slot, storage type, effect-only result, and
absence of value operands.

The authored suite covers 40 source programs, including irreducible backedges,
entry into a `for` body followed by `continue`, reinitialization of scalars and
aggregates, persistent block statics, callbacks, and aggregate returns.
`make test-goto` compares GCC and Clang results, owned object sections and
relocations, canonical CIR, interpreted results, and re-encoded objects.
Linux additionally executes the generated objects. Memory UB probes cover
skipped initializers, expired scopes, repeated uninitialized declarations,
read-only objects, and selection-scoped compound literals. Undefined native
programs are not executed.

Nested labels are limited to 128, statements to 512, and the function label
table to 65,536. Generated boundary probes are recorded separately from the
individually authored sources. Feature-family acceptance still requires an
audited source-bound native report reader.

Language rules follow C17 6.2.1, 6.2.3, 6.2.4, 6.8.1, and 6.8.6.1 in the
[WG14 draft](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf).
