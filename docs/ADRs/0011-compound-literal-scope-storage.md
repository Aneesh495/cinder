# Compound literal scope storage

A compound literal is an lvalue object, so aggregate full-expression snapshots
cannot supply its lifetime or modification semantics. The AST records an
unnamed declaration and its actual C scope owner. The owner begins storage
at entry; evaluation applies the existing initializer actions to that same
object. Native storage and the VM's object identity therefore follow the
same scope boundaries through branches and loops.

Selection and iteration statements and their associated substatements are
blocks in C17. The parser represents unbraced substatements with explicit
block owners and preserves their tag namespace scope. Lowering retires owned
objects on normal exits and reuses existing break/continue/return cleanup.
Repeated condition evaluation retains its object until the statement ends;
loop-body entry starts a new lifetime.

File-scope literals are collected separately from named declarations and
receive local symbols. Their complete initializer plans are checked even
when an enclosing expression is unevaluated. Original symbolic relocations
connect nested literal objects. Emission caches each symbol in its module.

The source, IR, object/assembly, reference, native, and classified lifetime
checks cover these distinct contracts. Inferred literal arrays in parser-time
constant bounds and goto scope entry need further implementation and retain
explicit open feature-registry requirements.
