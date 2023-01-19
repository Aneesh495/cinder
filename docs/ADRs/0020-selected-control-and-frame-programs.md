# Selected control and physical frame programs

Status: accepted

The final encoder chose branch forms and return conversions from source
terminators, and recomputed frame preservation offsets. MIR must own these
target decisions before their bytes are emitted.

Each selected block now owns a control program: trap, return, jump or branch.
It records the value bank, value ID, fixed uses/clobbers, comparison or return
narrowing bytes, near branch opcodes and successor IDs. The selected verifier
reconstructs the contract from the verified function and rejects altered fields.
The encoder consumes the plan and only resolves actual instruction addresses.
Branches use fixed near encodings with signed rel32 range checks, without
relaxation. Unencodable targets fail with an internal diagnostic.

Physical frame programs are selected once allocation has assigned storage.
They own entry/exit bytes, each preserved register's exact save/restore bytes
and slot, all incoming register homes, and the variadic save-area offset.
The frame constructor bounds signed offsets, validates alignment and requires
the frame to contain all locals, spills, preservation and argument storage.
The independent allocator checker retains its separate storage-overlap and
liveness checks. Both it and the encoder also check the selected frame seal.

DWARF unwind events remain tied to actual emitted PC boundaries, including
every saved/restored register and each leave/return pair. Normal byte output
is preserved on the dedicated baseline fixtures. Original output must match
LLVM-assembled output independently.

Tests mutate every control field and eighteen frame fields, require rejection
before any function symbols or text are published, restore each plan and
compare the resulting object with normal compilation. Separate byte readers
check actual frame instructions, every near branch target, and return-site
preservation against independent disassembly. Defined execution covers scalar
and floating results, cyclic SSA, calls, recursion, variadic stack arguments,
large locals and the nonreturning path.

This increment does not move physical phi-copy scheduling, constants, bulk
memory, argument/call or variadic pseudo expansion out of the encoder. Those
remain separate machine-representation work.
