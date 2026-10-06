# Declarations and constant expressions

The parser keeps ordinary names and tags in separate namespaces. Ordinary name
bindings distinguish objects, typedef names, and enumerators. Block/for scopes
restore their prior binding tables on exit. A typedef shadowed by an object
stops being a type name in that scope. Enumerator references become typed
integer expressions. Name visibility is retained so semantic analysis cannot
resolve an undeclared reference using a later global declaration.

Declarators are parsed into a binding tree before applying the declaration's
base type. Prefix pointers and suffix arrays/functions compose independently
of parentheses. Thus `int *a[3]` is an array of pointers and `int (*a)[3]` is a
pointer to an array. Abstract declarators use the same tree. Parameter arrays
and function types adjust to pointers. Unnamed prototype parameters are valid;
definitions require names. Typedef/function/array combinations preserve this
binding, including arrays of callbacks and pointers to multidimensional arrays.

Tag references retain their type identity. A later definition completes an
earlier forward declaration, including qualified copies. A tag definition in a
child scope has a separate identity even when its spelling matches an outer tag.
The IR type table preserves identities rather than comparing tag strings.

Integer constant evaluation uses target ranks, widths, promotions, unsigned
wrapping, signed overflow checks, casts, shifts, comparisons, conditional common
types, short-circuit selection, and size/alignment queries. Enum values must fit
`int`; fixed array bounds must be positive. Invalid division/remainder and shifts
are diagnosed when an evaluated constant is required. An unevaluated branch
does not trigger arithmetic evaluation. Constant bounds and enum expressions
are owned by the AST and cleaned up even after diagnostics.

Comma-separated declarations retain source order. Each local enters scope before
its initializer and before subsequent declarators. Function-body parameter names
share the redeclaration constraint with the outer body block. Storage-class
placement and richer initializer handling are still being extended. Block-scope
static/extern object storage is diagnosed until its duration/linkage lowering is
implemented. Empty
parameter lists are accepted as zero fixed parameters in this profile; untyped
identifier parameter lists and old-style definitions are diagnosed.

File-scope declarations build compatible composite types before initialization.
An earlier array bound therefore controls a later initializer with an omitted
bound. Extern declarations, tentative definitions, and one initialized
definition coalesce into one object. Incomplete external tentative arrays get
one element at translation-unit end. Duplicate definitions, incompatible
pointer/array types, conflicting qualifiers, and conflicting linkage are
diagnosed. Function declarations retain valid inherited internal linkage.

Each type records when its layout became complete. Size/alignment, member,
subscript, and pointer-step constraints use the expression's source order, so
a later tag definition cannot validate an earlier incomplete-type operation.
Incomplete array typedefs instantiate independent object bounds.

`make test-linkage` checks actual ELF symbol count, binding, kind, and size,
then rejects duplicate canonical IR globals. The authored source suite includes
redeclarations, prototype composites, source-order constraints, and inferred
array boundaries at both optimization levels. Field/index access, decay,
addresses, scalar callbacks, and character array initialization reach native
output. General aggregate values, flexible members, bitfields, static pointer
relocations, and the complete aggregate ABI remain open.
