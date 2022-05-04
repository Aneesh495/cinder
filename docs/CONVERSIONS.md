# Scalar types and conversions

The frontend assigns LP64 ranks and widths to expressions before IR lowering.
`char`, `signed char`, and `unsigned char` are distinct types. Plain `char` is
signed on the target. Integer promotions map `_Bool`, character types, and
short types to `int`. The usual arithmetic conversions select a common rank,
signedness, or floating format. Shift operands are promoted independently.

Assignments, initializers, prototype arguments, conditional arms, and returns
contain explicit typed cast nodes. Compound assignments compute in the
operation type and convert back to the object type. Their result is the
converted stored value. Postfix updates preserve the old value. Const objects
cannot be assigned or updated; nested blocks may shadow names, but a function's
outer block cannot redeclare a parameter.

`IR_CONVERT` records both source and destination types. The verifier checks the
source against its value definition. Integer values have a canonical 64-bit
representation: unsigned narrow values are zero extended, signed narrow values
are sign extended, and `_Bool` is zero or one. Operations wrap only for unsigned
types. The independent interpreter rejects signed overflow, invalid shifts,
and floating conversions outside the destination integer range.

Floating IR values use a canonical binary64 container. Float32 arithmetic still
executes as binary32: the interpreter uses float operations, and the encoder
uses scalar SSE single-precision instructions before extending the result.
Integer-to-float32 conversions round directly to binary32. A binary64 intermediate
would incorrectly round values just beyond certain binary32 halfway points.
The unsigned 64-bit conversion handles the high bit using a shift with a sticky
low bit, then doubles the floating result.

The scalar SysV boundary carries float32 arguments and returns as binary32,
including overflow arguments on the stack. Internal spill/local containers
remain eight bytes. Mixed argument lists consume independent GPR and SSE
register sequences. Aggregate classification and complete hosted variadics
remain separate open work.

`sizeof` accepts expressions or type names and does not evaluate its operand.
`_Alignof` accepts object type names. Complex abstract declarators and variable
length arrays remain open.

`make test-control` compares the authored cases with a host reference, the
independent interpreter at both optimization levels, and native emitted programs
when run on Linux x86-64. `make test-constraints` requires rejected source to
preserve an existing complete output artifact. Cross-toolchain ABI campaigns
must supplement same-compiler calls before the full ABI gate can pass.
