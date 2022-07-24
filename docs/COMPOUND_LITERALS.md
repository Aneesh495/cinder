# Compound literal objects

`(type){initializers}` produces an addressable lvalue with its own object.
It accepts scalar, array, struct, union, and qualified types. Unknown array
bounds are inferred from initializer continuation and designators. The
existing typed initializer plan supplies conversions, zero filling, nested
objects, copy initialization, and constant-address relocations.

File-scope literals have static storage with local ELF symbols. Nested literal
addresses use original data relocations. Their initializer expressions must
satisfy static initialization constraints, including when a literal appears
in an unevaluated `sizeof` expression. Every file-scope literal is checked.
The symbol registry emits each literal object once.

Automatic literals belong to their enclosing C block. Selection statements,
iteration statements, and each associated substatement have explicit scope
owners, including unbraced bodies. Scope entry begins each literal's object
lifetime; evaluating its expression runs the initializer without beginning a
second lifetime. Repeated loop-condition evaluation therefore uses one object
throughout the loop. Reentering its body starts a fresh lifetime. Scope exits,
break, continue, and returns use the existing explicit retirement effects.

Compound literals remain mutable when their type permits it. They differ from
immutable aggregate expression snapshots, which expire at a full-expression
boundary. Const writes and escaped automatic literal addresses are diagnosed
or classified by the independent VM. Native code uses actual local/global
storage and ABI aggregate transfers.

`make test-compound-literals` checks authored source observations with GCC and
Clang, emitted/assembled object bytes, and native execution on Linux. The
source suite additionally interprets both optimization levels. Undefined
lifetime and const-access fixtures run only through classified interpretation.

The contracts follow sections 6.5.2.5, 6.8.4, and 6.8.5 of the primary WG14
drafts [N1570](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf)
and [N1539](https://www.open-std.org/jtc1/SC22/wg14/www/docs/n1539.pdf).
Initializer shape queries complete unknown array bounds without evaluating
initializer values. Array-bound and enumerator expressions retain their
source-point bindings and enclosing function for full semantic checking,
including invalid expressions inside unevaluated initializers. The initializer
planner and shape queries share their subobject continuation cursor. Named
array definitions also complete at the end of their initializer, so later
constant bounds can use their size. Unevaluated automatic literals do not
reserve runtime storage, including large and nested literal operands.

Goto scope entry and the final feature-family acceptance reader remain open.

The editable storage diagram is `diagrams/compound-literal-storage.mmd`.
