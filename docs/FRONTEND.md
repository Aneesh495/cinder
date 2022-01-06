# Frontend walkthrough

The frontend follows `source -> preprocessing -> tokens -> AST -> sema -> IR`. `CinderSourceManager` keeps immutable file bytes and source IDs. `CinderTokenStream` points into the retained preprocessing buffer, preserving spelling for dumps and diagnostics. `parser.c` uses recursive descent for declarations/statements and a precedence-climbing expression parser. Declarator parsing builds pointer and function types rather than flattening a spelling pattern.

The current semantic environment has a global scope and nested block scopes. Identifier lookup, duplicate declarations, function-call arity/type checks, return compatibility, lvalue requirements, loop-context checks, and scalar conditions are rejected before IR lowering. The type context uses explicit LP64 target widths, so host `sizeof` does not define target integer layout.

The initial end-to-end slice is intentionally scalar. `struct`, `union`, `enum`, aggregate initializers, floating operations, variadics, and advanced declarators have reserved token/type categories but are not claimed complete until they cross semantic, IR, ABI, encoder, and object tests. A failed semantic check prevents native output.

## Example trace

For `int main(void) { int x = 40; return x + 2; }`:

1. preprocessing retains `int main...` and expands macros;
2. lexing emits keyword, identifier, punctuation, and number tokens;
3. parsing constructs a function declaration, local declaration, binary expression, and return statement;
4. semantic analysis assigns `int` types and marks `x` an lvalue;
5. lowering emits `const`, `local.store`, `local.load`, `add`, and `return`;
6. optimization folds only operations whose operands are known integer constants under the IR contract;
7. x86 lowering uses a verified frame and direct instruction encoder;
8. ELF emission writes `.text`, symbols, relocation records for calls, and non-executable-stack metadata.
