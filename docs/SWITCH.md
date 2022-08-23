# Switch statements

`switch` evaluates its integer controlling expression once and applies integer
promotions. Case expressions must be integer constant expressions. They receive
full semantic checking, then conversion to the promoted controlling type.
Duplicate converted values and multiple default labels are errors. Each case
belongs to its nearest enclosing switch, including labels inside nested loops
or conditional substatements. Nested switches have independent case tables.

The typed CFG uses equality comparisons and explicit branch edges. Label blocks
are allocated before statement lowering. Source-order lowering preserves
fallthrough and stacked case labels, including statements after a return or
break. `break` targets the nearest loop or switch; `continue` targets the nearest
loop. Separate target and lifetime stacks preserve this distinction.

Dispatch enters the selected label's lexical scopes and begins their automatic
storage without executing skipped declarations or conditions. A declaration's
initializer runs only when that declaration is reached. Unmatched dispatch
without a default skips the complete body. Scope exits retire objects, including
selection-scoped compound literals. The independent interpreter classifies
reads of indeterminate or expired objects. Undefined programs run only in that
interpreter.

Case expressions belong to their statement AST. Parse-time constant evaluation
checks source-point eligibility; semantic traversal checks every operand,
including unselected generic associations and unevaluated call constraints.
The constant-expression ledger owns expressions from discarded declaration
syntax and does not share ownership of statement expressions.

`make test-switch` checks 46 individually authored programs against GCC and
Clang at both optimization levels, compares owned and independently assembled
ELF sections and relocations, executes canonical CIR, and requires direct and
CIR-generated objects to be byte-identical. Linux additionally executes all
three object paths. The suite includes Duff's device, wide integer dispatch,
fallthrough, nested switch/loop target resolution, labels sharing typedef
spellings, skipped initializer effects, aggregate returns, and scope entry.

Generated boundary probes are recorded separately: a 1,024-case dispatch runs
through canonical IR, a 4,096-case table passes semantic analysis, the next case
exceeds the documented table limit, and nested labels accept 128 levels while
rejecting greater depth. General statement nesting remains limited to 512.
An audited source-bound native report reader is still required for complete
feature-family acceptance.

These rules follow sections 6.2.4, 6.8.1, 6.8.4.2, 6.8.6.2, and 6.8.6.3 of the
[WG14 draft](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf).
