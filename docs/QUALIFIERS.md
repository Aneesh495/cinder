# Declared storage and qualified views

A const-qualified declaration defines immutable storage. A const-qualified
pointer view of an originally mutable object does not change that declaration.
The interpreter preserves the actual declared storage type and selected member
origin independently of subsequent pointer casts and copies.

A mutable union member can be written even when another alternative has a
const-qualified type. Scanning every overlapping alternative would incorrectly
make the union's mutable member immutable. Member operations instead select
physical indices from the actual declared object. Qualifiers inherited from a
const-qualified object and qualifiers declared on the selected member remain
immutable origins. Array selections retain their element declaration. Pointer
copies, arguments, callbacks, variadic storage, and aggregate copies preserve
this origin. Whole aggregate assignments still reject aggregate types containing
const members; initialization can populate their storage.

Static address initializers retain the physical member path in CIR schema 6,
in addition to the root target type, byte displacement, and bounded pointer
domain. The independent reader validates this path before interpretation or
emission. Mutable and const union alternatives can share the same address and
extent while having different declared origins. Pointer equality continues to
compare actual object and byte offset, independent of qualifiers.

`make test-qualifiers` compares authored automatic/static, nested/anonymous,
array, scalar/bitfield, callback/variadic, and cast cases against GCC and Clang.
Original and parsed objects round-trip exactly, and the assembly oracle matches
encoded sections and relocations. Native target execution is required on Linux.
The memory suite includes invalid writes through converted pointers to selected
const union members, arrays, and aggregate members. Invalid CIR origins fail
before replacing prior output.
