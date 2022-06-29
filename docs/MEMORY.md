# Object memory

Typed expressions preserve array/function designators until value conversion.
Array decay yields an element pointer. Address-of and sizeof retain the object
type. Subscripts, dereference, and member selection form lvalues. Compound
assignment and increments evaluate their lvalue address once.

IR represents local/global addresses, scalar memory reads/writes, pointer
offsets, pointer differences, and member pointers explicitly. Addressed locals
are excluded from promotion. Pointer strides and member offsets come from the
LP64 type model, and the verifier checks them independently.

The interpreter allocates byte storage with a stable object identity, declared
type, initialization map, lifetime state, and stored pointer metadata. Pointers
carry object identity, offset, and subobject bounds. Loads inspect only owned
bytes and never dereference host addresses. Character access may inspect an
object representation; other accesses must match an aligned declared subobject.
Const objects/subobjects reject writes even after a cast.

Each block entry creates fresh local identities. Scope exits, break, continue,
and returns retire the objects being left. Reentering a loop cannot reuse
initialized bytes or revive a previously escaped pointer. Parameter objects
remain alive until their function returns. Calls pass full value metadata in
source argument order. Phi inputs are copied simultaneously.

Native code uses actual object extents and declared-width integer/SSE memory
operations. Arrays and aggregate objects occupy their complete target size.
RIP-relative addresses use original ELF relocations. Pointer offsets scale by
the exact target pointee size.

`make test-control` checks the authored defined memory programs against host
reference execution, IR interpretation, emitted objects, and native Linux
execution. `make test-memory` records separate UB classes through serialized
IR and never executes emitted undefined programs.

Aggregate expression results/ABI, bitfields, compound literals,
allocated storage/library models, restrict contracts, and goto scope entry
remain open. The abstract VM does not expose host addresses or padding values
as differential equality oracles.

Pointer conversions through 64-bit integer storage retain exact provenance
when the stored bits are unchanged. This applies to local/global words,
aliases, arrays, fields, parameters, returns, and phis, including callbacks.
Integer comparisons use integer semantics even when a word retains a tag.
Converting back to a pointer still observes the target object's lifetime.
Partial byte writes invalidate stored word tags. General character copying of
pointer representations and arithmetic reconstruction of addresses need
additional modeling; exact address bits are outside the differential oracle.

Brace initializers produce a source-ordered subobject plan. Array and member
selectors retain continuation through nested aggregates and brace elision.
Incomplete array bounds are inferred independently for each declared object.
Each explicit aggregate brace level zeros its omitted subobjects and padding;
a later complete subobject initializer replaces earlier contained actions.
Local copy initialization transfers bytes, initialization state, and stored
pointer identities. Const initialization uses explicit initialization effects;
subsequent ordinary writes still obey deep const checks. Global plans encode
scalar values and original pointer relocations at their actual byte offsets.
Overlapping later stores remove superseded relocations.

The interpreter keeps explicitly initialized scalar destinations uninitialized
until their expression has been evaluated. Self-reads are classified separately
from defined execution. Initializer ordering follows the chosen source order;
unspecified side-effect order and padding are not differential equality tests.
