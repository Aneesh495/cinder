# Typed high-level IR

`source/hir.c` and `source/hir_control.c` construct an owned high-level unit
from semantic syntax. CFG lowering consumes its expressions, declarations,
structured statements, initialization actions and scope/label/case plans.
Target types belong to a separate type context. Type tags, member names and
parameter names are copied into that context before the AST is retired.
Names, literal bytes, call arguments, member paths and statement references
belong to HIR. Stable IDs follow registration order, including declarations
that insert initializer expressions while another expression is being built.

Scalar place reads are explicit `load` operations. Address-taking, decay,
conversions, updates, calls and aggregate places are distinct operations.
Read-modify-write operations hold one target place, preserving single
lvalue evaluation. Aggregate return members use readable temporary places
without becoming modifiable C lvalues. Layout queries become constants;
`_Generic` contributes only its selected expression to the runtime graph.
Variadic setup/copy/end are explicit operations. The final named parameter
of `va_start` is a semantic contract, not a runtime read.

Declarations own typed initializer actions. Canceled override entries are
removed during construction; remaining actions retain their order. Bitfield
bounds use the occupied byte extent. The semantic control algorithm supplies
scope transitions and resolved labels, which are transferred into owned
records. The lowering context uses those records for lifetime entry/exit,
loop exits, switch selection and goto transitions.

Checked constant/address helpers prepare static data and symbolic relocations
at HIR construction. HIR and IR share the typed static-object record contract;
CFG lowering deep-copies bytes, names and relocation paths into its own
module. It does not re-evaluate AST constants. IR parameter records also
belong to the IR module. Repeated lowering and destruction of HIR therefore
cannot invalidate an already constructed IR module.

The verifier checks owned IDs and operands, scalar place/load consistency,
addressability, conversion targets, initializers, scope parents, label/case
kinds and structured children. Iterative graph traversals reject expression,
statement and declaration-list cycles. Legitimate canonical-declaration and
goto references remain distinct from hierarchical child edges. CFG/SSA
verification checks the lowered operations again.

```sh
build/cindercc --dump-hir examples/hello.c
build/cindercc --dump-hir examples/hello.c examples/varargs.c
make test-hir
```

The version-two text dump includes real declarations, initializers, structured
statements, static object/relocation summaries and expressions with source
origins. Multiple input units produce independent graphs without linking or
publishing native output in inspection-only mode.

Seven authored fixtures cover scalar ownership, unevaluated selection,
aggregate return members, single evaluation of updates, variadic operations,
early compound-literal storage plans and combined global/control behavior.
The API probe mutates and destroys the AST before lowering. HIR expression
changes affect actual interpreted results. A second probe lowers globals,
loops, switch and goto, changes HIR static data, then destroys HIR; the
previous IR module must preserve its original result. Sixteen corrupted
expression/control/object-ownership graphs must fail verification. GCC/Clang
references run at O0/O2, with independent native object execution where the
target is available.

See [the lowering flow](diagrams/typed-hir.mmd) and
[the ownership decision](ADRs/0013-owned-expression-hir.md).
