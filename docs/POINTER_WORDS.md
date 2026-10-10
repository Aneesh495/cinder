# Integer address representations in the interpreter

The LP64 target supplies `intptr_t` and `uintptr_t` in its authored `stdint.h`.
Object pointers can travel through a full-width signed or unsigned integer and
back through `void *`. The language basis is N1570 6.3.2.3 and 7.20.1.4; the
latter specifies the supported integer types' round-trip property. The native
encoder emits the target pointer bits. The independent interpreter uses object
tokens rather than process addresses, so tests compare dereferenced values and
pointer relationships rather than raw numeric addresses.

An interpreter value distinguishes an actual pointer from an integer carrying
address authority. The latter retains its original object identity, array or
subobject domain, declaration view and read-only origin. Full-width integer
conversion, arithmetic and bitwise operations can preserve that authority even
while the integer temporarily changes. Conversion back to a pointer decodes
an offset only within the original object token's address range. It cannot
select another object by guessing the numeric token. Subsequent access still
checks subobject bounds, alignment, effective type, initialization and lifetime.

The metadata survives scalar copies, phis, parameter/return transfers,
variadic arguments and full-width storage. Aggregate copies transfer matching
word records. A partial or overlapping byte/bit write removes the old record;
ordinary bytes are insufficient to invent pointer authority. Narrowing and
floating conversions also drop it. Null integers produce null pointers
without borrowing stale authority. Const declaration origin survives a trip
through integers and casts.

Integer computations mixing different object/subobject authority domains are
conservatively marked ambiguous. Their ordinary integer results remain usable,
but converting a nonzero ambiguous word back to a pointer is reported as
`unsupported`, rather than asserted to be language undefined behavior. The
current interpreter does not implement arbitrary address reconstruction or
cross-domain pointer encoding. Native compilation remains available for such
target-defined code. This model limit is separate from a verified lifetime,
read-only, bounds or initialization failure.

`make test-pointer-words` checks twenty-one authored programs at `-O0/-O2`
against GCC and Clang and compares canonical CIR, original objects and assembly
sections/relocations. The fixtures include reversible XOR/complement/unsigned
arithmetic, stack and global storage, loops/phis, integer/SSE aggregates,
variadic stack arguments, one-past pointers and qualified conditional members.
Linux additionally links and executes the original and parsed objects. Eight
model guards cover forged/truncated tokens, byte overwrites, different domains,
retirement, const writes and out-of-bounds/one-past reads. Invalid or unsupported
dereferences are never treated as native differential references.

Conditional-expression result slots are initialized by the selected branch.
They use `local.init`, including when their type inherited a top-level
qualifier from a const aggregate member. Ordinary source assignments keep
`local.store` and its verifier restriction. This distinction is required for
compiling the implementation itself with verification before promotion.

See [the editable authority diagram](diagrams/pointer-words.mmd) and
[the source-bound optimization pipeline](PASS_PIPELINE.md).
