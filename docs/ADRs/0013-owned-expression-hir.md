# Own high-level lowering plans

The CFG lowerer previously consumed semantic AST expressions and control
plans directly. HIR now owns a distinct expression graph, declarations,
structured statements, typed initializer actions and scope/label/case plans.
The retained lifetime probes destroy AST and semantic state before lowering,
then destroy HIR while continuing to execute an already built IR module.

Target types remain in the type-context arena. Names attached to those types
are sealed into that arena before AST retirement. Static object preparation
uses the existing checked constant/address rules during HIR construction;
CFG lowering clones the resulting typed bytes and relocation plan. IR module
parameter records and static data have independent ownership.

The existing semantic control algorithm produces the scope plan once for
transfer; its algorithm is not duplicated in the lowerer. Canonical declaration
references and goto targets can form legitimate reference cycles. Expression
and hierarchical statement dependencies, and declaration lists, must be
acyclic. The verifier distinguishes these relationships explicitly.

Stable IDs are assigned at registration, after a binding copy can insert more
nodes. Address-keyed memo storage is private; neither hash traversal nor
pointer addresses determine output order. Canceled semantic initializer
scratch entries are removed rather than weakening checks on active actions.
