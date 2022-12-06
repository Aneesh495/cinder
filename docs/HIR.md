# Typed expression HIR

`source/hir.c` constructs an owned expression graph after semantic analysis.
Runtime expression lowering in `source/ir.c` consumes `CinderHIRExpr` nodes,
with stable per-unit IDs, target types, source origins, place/value categories
and owned operands. Names, literal bytes, call argument arrays and member paths
belong to the HIR arena. Target types and resolved declaration metadata remain
borrowed from the semantic unit.

Scalar place reads become explicit `load` nodes. Address-taking, array/function
decay, conversions, updates, calls, conditional selection and aggregate places
remain distinct operations. Read-modify-write operations own a single target
place, so `values[index++] += value` evaluates the target once. Aggregate
return members use readable temporary places without becoming modifiable C
lvalues.

`sizeof`, alignment queries and `offsetof` become typed constants. `_Generic`
selects its semantically resolved expression; its controlling and unselected
expressions do not enter the runtime graph. Variadic setup/copy/end are explicit
operations, with no untyped pseudo-callee. The final-parameter contract of
`va_start` is checked by semantic analysis and is not a runtime value read.

The verifier checks owned IDs/operands, scalar load/place consistency,
addressability, conversion targets and argument bounds. A dependency traversal
rejects cycles without recursively walking arbitrarily deep graphs. Conversion
and operation semantics are checked again at the typed CFG/SSA boundary.

```sh
build/cindercc --dump-hir examples/hello.c
make test-hir
```

The ownership probe changes an AST initializer after HIR construction and
confirms its original value survives lowering. Changing the corresponding HIR
constant changes the interpreted program result. Ten corrupted graph variants
must fail verification. Five authored fixtures cover selected/unevaluated
expressions, temporary aggregate members, single evaluation of updates,
variadic operations and scalar address/load/conversion/call behavior. Their
O0/O2 native objects are independently executed where the target is available,
and GCC/Clang references run at both levels.

This is the expression boundary. Structured statements, declaration/storage
plans and static constant-address construction still use semantic AST metadata.
They must move into owned high-level control/object records before the full HIR
boundary is complete. The current lifetime therefore keeps that metadata alive
through CFG lowering. HIR inspection reports this implemented expression graph.

See [the lowering flow](diagrams/typed-hir.mmd) and
[the ownership decision](ADRs/0013-owned-expression-hir.md).
