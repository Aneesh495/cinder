# Frontend walkthrough

The frontend follows `source -> preprocessing -> tokens -> AST -> sema -> expression HIR -> CFG IR`. `CinderSourceManager` keeps immutable file bytes and source IDs. `CinderTokenStream` points into the retained preprocessing buffer, preserving spelling for dumps and diagnostics. `parser.c` uses recursive descent for declarations/statements and a precedence-climbing expression parser. Declarator parsing builds pointer and function types rather than flattening a spelling pattern.

The current semantic environment has a global scope and nested block scopes. Identifier lookup, duplicate declarations, function-call arity/type checks, return compatibility, lvalue requirements, loop-context checks, and scalar conditions are rejected before IR lowering. The type context uses explicit LP64 target widths, so host `sizeof` does not define target integer layout.

Structures, unions, enumerations, bitfields, aggregate initializers, floating
operations, variadics and nested declarators now have original frontend/native
paths with focused tests. [The feature matrix](FEATURES.json) records boundaries
and exclusions. A failed semantic check prevents native output. The expression
HIR is owned and consumed; control/storage plans still depend on semantic
metadata as detailed in [HIR.md](HIR.md).

## Example trace

For `int main(void) { int x = 40; return x + 2; }`:

1. preprocessing retains `int main...` and expands macros;
2. lexing emits keyword, identifier, punctuation, and number tokens;
3. parsing constructs a function declaration, local declaration, binary expression, and return statement;
4. semantic analysis assigns `int` types and marks `x` an lvalue;
5. HIR records typed constants, an explicit load of the `x` place, and addition;
6. CFG lowering emits `const`, `local.init`, `local.load`, `add`, and `return`;
7. optimization folds only operations whose operands are known integer constants under the IR contract;
8. x86 lowering uses a verified frame and direct instruction encoder;
9. ELF emission writes `.text`, symbols, relocation records for calls, and non-executable-stack metadata.
