# Target bitfields

The Linux x86-64 LP64 profile supports `_Bool`, `signed int`, `unsigned int`,
and plain `int` bitfields. Plain `int` is signed. Other base types receive an
explicit profile diagnostic. Widths are integer constant expressions, at most
one for `_Bool` and 32 for `int`. A zero-width field must be unnamed; alignment
specifiers and address/sizeof/offsetof queries on bitfields are rejected.

Packing follows the GCC/Clang default x86-64 little-endian layout. Adjacent
fields use increasing low-to-high bits without crossing their declared 8- or
32-bit unit. An unnamed zero-width field advances to the next declared unit
boundary. An ordinary member resumes at the next whole byte and then its own
alignment. Only named bitfields contribute their declared alignment; unnamed
padding fields contribute occupied bytes and boundaries. Unions place fields
at offset zero. Named union fields reserve their declared storage type's size.

Narrow bitfields promote to `int` in arithmetic, shifts, conditional operands,
switch controls, and default variadic arguments. A full-width unsigned field
promotes to `unsigned int`. Generic selection of a direct bitfield follows its
declared base type, matching Clang; GCC exposes a distinct width-specific type
for some such expressions. Generic selection of its promoted value follows
these integer promotions. Comma, assignment, and increment results retain
width for promotion, following GCC; Clang differs for postfix increment.
`sizeof` accepts these results and uses the declared storage type size,
following Clang. Actual member designators, including generic selections,
remain invalid operands. The three exact source-bound disagreement probes
are retained and excluded from differential eligibility. These underspecified
contexts are discussed in [WG14 N2958](https://open-std.org/JTC1/SC22/WG14/www/docs/n2958.htm).
The profile's signed narrowing keeps the low bits
and interprets them as two's complement, matching both references.

Member, arrow, generic-lvalue, assignment, and increment lowering retain the
field width. Loads extract and extend the value. Stores preserve neighboring
bits. Assignment and prefix-increment expressions return the stored value;
postfix increments return the previous value. Native accesses touch only the
occupied bytes, with one read per byte for a load and one read/write per byte
for a masked store. These are also the declared volatile access semantics.
No additional field load is used to determine an assignment result.

Initializers skip unnamed fields, retain physical anonymous-member paths, and
zero omitted bits without overwriting explicit neighbors. Static initializers
encode the same layout directly into original ELF data. The independent CIR
reader reconstructs packing before verification or code generation. Its schema
5 records distinguish ordinary members from unnamed zero-width fields.

`make test-bitfields` covers authored layout, signedness, initialization,
conversions, qualifiers, promotions, single evaluation, and ABI cases. Original
objects, assembly-oracle objects, and parsed-CIR objects are compared and
executed on the native target. ABI fixtures exchange values and callbacks with
both GCC and Clang, including register exhaustion and memory returns. Source
constraint and undefined-behavior probes run in the full suites. GCC's warnings
for const bitfield writes are preserved through exact source-bound policy;
Cinder still rejects those mandatory constraints.

These rules implement the choices permitted by C17 6.7.2.1. The feature remains
partial in the acceptance registry until its complete native evidence has a
dedicated audited reader.
