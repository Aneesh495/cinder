# Selected x86 machine representation

Allocation now owns a selected machine function, and the scalar encoder reads
its operation plans and operand copies. Integer arithmetic/bitwise/shift and
comparison forms have explicit x86 byte templates; SSE arithmetic records its
opcode and precision; SSE comparisons record condition/parity handling for
unordered values. This selection belongs to the normal object and assembly
paths, rather than an inspection-only copy of the CFG.

Selected blocks own instruction operands, call names, argument/class vectors,
phi incoming vectors and terminators. Type metadata remains borrowed from the
translation unit's type context. The machine value table records the register
bank and type for each definition. Allocation uses that bank table instead of
repeatedly scanning CFG instructions. The encoder also uses the table for
value normalization and ABI/phi operand queries.

Physical masks use hardware GPR codes in bits 0 through 15, XMM numbers in
bits 16 through 31 and flags in bit 32. Integer bundles describe their RAX/R10
operands and scratch changes, shifts additionally use RCX, and division also
clobbers RDX. SSE arithmetic uses XMM0/XMM1. Call descriptors declare the SysV
caller-clobbered GPR, XMM and flag set; values in allocatable XMM2 through XMM7
that live across such a point remain in stack storage. Source scalar widths
are normalized at value definitions; the integer ALU itself uses 64-bit forms.
Floating registers carry the existing internal binary64 representation while
float operations round through the selected binary32 forms.

The structural verifier binds operand IDs/types, literal bits, call attributes,
argument/class/phi vectors, target forms, masks, banks and CFG terminators to
the verified input. It runs at selection and before encoding. The independent
interpreter and native executions check the semantic result separately. The
machine verifier is not an independent proof of instruction semantics.

Critical edges are split before selection. Edge transfers use the selected
function's owned phi operands and the existing physical parallel-copy resolver.
`--dump-mir` prints selected forms, byte templates, banks and masks, together
with actual CFG analysis and optimizer counters.

Selection also owns incoming and outgoing ABI plans. These record INTEGER/SSE/
MEMORY classification, hidden return storage, register assignment, stack
offsets, argument staging and aligned outgoing frame size. Variadic retrieval
owns its selected layout. The encoder consumes these plans without calling the
ABI classifier or placing arguments again. The verifier recomputes and checks
each plan against the verified function and actual call signatures. Calls with
missing signatures are rejected before layout selection.

Memory and conversion operations still include typed target pseudo-operations
expanded during encoding. Their complete selection and a separate typed HIR
remain open. Full acceptance is incomplete.

`make test-mir` executes 43 independently specified typed integer/floating
cases through the interpreter and original object path. It rejects seven
altered machine plans per case and independently inspects ELF output. Linux
x86-64 additionally links and executes every object, including ordered and
unordered floating comparisons. `make test-allocation`, `make test-ssa` and
`make test-parallel-copy` exercise pressure, interference and physical cycles.
The same metadata helpers are shared by these standalone native probes; they
do not link the parser or AST lowerer.

`make test-mir-abi` checks 64 authored aggregate, variadic and integer-address
sources. It rejects 1,314 altered plans or missing signatures, restores each
plan and requires byte identity with the normal compiler's original object.
Linux x86-64 links and executes these same objects. The allocator pressure
fixture now supplies real formal and actual signatures; the null-signature
crash found during this work is retained in the private failure evidence.

See [the editable selection diagram](diagrams/machine-selection.mmd).
