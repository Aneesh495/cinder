# Initialized scalar locations and the O0 debug profile

Status: accepted

An allocated local slot does not prove that its contents represent a current
source value. Promotion may remove every store, a branch may bypass
initialization, and loop reentry may leave an old iteration's bytes behind.

At `-g -O0`, scalar promotion preserves named local slots. Parameters and
unnamed temporaries retain their normal eligibility. Ordinary compilation and
`-g -O2` keep the existing optimization profile. The debug choice is recorded
in the function configuration before the normal pass pipeline runs.

The encoder records actual allocated CFA-relative homes for scalar locals
without a formed object address. Construction checks each home against the
target slot layout and rejects duplicates, wrong identities and escaping
objects. Aggregate locations and storage accessed through formed addresses
remain unavailable.

For each supported slot, a forward must-initialized analysis intersects all
reachable predecessor states. Entry starts uninitialized. Other blocks start
at the lattice's top, preserving initialization through loops when every
incoming path carries it. Begin/reset/end operations clear the state. Retained
direct scalar stores establish it only after their actual encoded end offset.
An undef/possibly undef phi source does not establish a value. Lexical scope
ranges further restrict every available interval.

The resulting `.debug_loc` lists use the current memory object, not a cached
value from initialization. A partial join remains unavailable even when one
particular run took its initializing edge. Promoted objects have empty lists;
an unused reserved slot never becomes a location. This deliberately trades
some availability for a small, explicit proof boundary.

Independent readers check initialization boundaries against actual store
bytes, both kinds of join, loop reentry and address escape. GDB checks integer,
double, character and pointer locals, nested shadows, unavailable intervals
and calls. Original/assembled debug bytes and relocations must still match.
O2 debug/plain text remains identical. O0 debug/plain storage layouts must
match, while the documented preservation choice can change text. Ordinary
objects are also compared byte-for-byte with the preceding compiler.
