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
repeatedly scanning CFG instructions. The encoder uses the table for value normalization. Selected ABI programs and
phi schedules carry their own transfer roles and register-bank choices.

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

Conditional edges targeting phis are split before selection, including
single-predecessor targets and branches with equal successors. After
allocation, the physical parallel-copy resolver creates owned source/value,
destination/value and bank schedules on the predecessor's selected block.
The encoder consumes those moves without inspecting phis or scheduling copies.
`--dump-mir` prints selected forms, byte templates, banks and masks, together
with actual CFG analysis and optimizer counters.

Selection also owns incoming and outgoing ABI plans. These record INTEGER/SSE/
MEMORY classification, hidden return storage, register assignment, stack
offsets, argument staging and aligned outgoing frame size. Variadic retrieval
owns its selected layout. The encoder consumes these plans without calling the
ABI classifier or placing arguments again. The verifier recomputes and checks
each plan against the verified function and actual call signatures. Calls with
missing signatures are rejected before layout selection.

Scalar object accesses own selected load/store prefixes and opcodes, explicit
frame/RIP-relative/indirect address roles, read/write direction, value IDs,
ModRM bytes, frame slots and symbol relocation contracts. Their precision plans preserve the
internal binary64 float representation and declared binary32 storage. Each
definition owns its sign/zero-extension or boolean-normalization template;
incoming ABI booleans retain the separate low-byte rule. Conversions select
bit transfer, signed/unsigned integer-to-float, float-to-signed/unsigned integer,
float-to-boolean or floating precision handling before encoding. Integer unary
operations carry byte templates. Bitfield plans record occupied bytes, masks,
shift and sign extension so masked updates preserve neighboring members.

The verifier seals those encodings and layout choices as well as the original
operand/type contracts. Pointer arithmetic and address formation now have
selected target programs. Branches, returns and physical frames also own
selected programs. Constants, value copies, floating sign changes, bulk memory
and zero-emission markers also have selected roles. Argument/call, aggregate-return and variadic protocols now own complete byte
programs and explicit value transfers. Their frame holes resolve through
verified allocation. The frontend's owned typed HIR is
documented in [HIR.md](HIR.md). Full acceptance is incomplete.

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

`make test-mir-scalar` checks 142 authored sources and rejects 20,025 altered
storage, conversion, bitfield, unary and value-normalization plans. It requires
normal original-object identity, independent ELF inspection, twelve storage
profiles, seven conversion modes and all three addressing families. Twelve
new volatile-storage fixtures additionally compare GCC/Clang and original/
assembled objects at O0/O2. Native Linux executes the retained original
objects. Generic ELF inspection keeps fixture-specific symbol assertions in
the object campaign that defines those symbols.

See [the editable selection diagram](diagrams/machine-selection.mmd).

Address plans select frame/RIP-relative `lea`, member immediates, scaled offset
programs and signed pointer differences. Their before/after byte segments
preserve operand staging while removing pointer-specific opcode selection from
the encoder. Frame slots resolve through actual allocation. Symbol plans carry
the exact PC-relative relocation and addend. Masks account for RBP reads,
RAX/R10 scratch, RDX in division and real flag effects. `--dump-mir` shows the
actual selected role, immediates, relocation and byte segments. See
[ADR 0019](ADRs/0019-selected-pointer-address-programs.md).

`make test-mir-address` checks four source units forming three programs. It
rejects 928 altered plans, restores them and requires normal object identity.
An independent reader checks all five roles and six typed origins against
hand-specified encodings and masks. The pointer-pressure fixture must actually
spill. GCC/Clang and IR references cover 24-byte strides, negative indexing,
signed differences, a 65536-byte member offset, callbacks and separate
compilation. Linux cross-links objects in both directions. Original sections
and relocations must match LLVM assembly; optional baseline checks require
complete byte identity with a preceding compiler. Reports retain unique run
directories, raw commands, tool/core hashes and original objects.

Control plans choose traps, integer/SSE/void returns, float return narrowing,
conditional comparisons, near jump opcodes and exact successor IDs. They
declare RAX/XMM0, RBP/RSP, scratch and flag effects before allocation. The
encoder resolves rel32 displacements after laying out blocks; a missing target
or signed displacement overflow fails compilation. It does not choose forms
from the source terminator or return type.

After allocation, frame selection owns the prologue, epilogue, preserved
register save/restore programs, incoming GPR/SSE homes and variadic area.
Offsets come from actual locals, spills, preservation and argument storage.
The allocation checker and encoder both reject stale or altered frame plans.
Unwind events follow the emitted selected bytes at their actual PCs.

`make test-mir-control` checks all four control roles, integer/SSE/void returns,
multiple returns, recursion, variadics, physical spills and a 16 KiB local.
An independent reader decodes frame bytes and every branch target against
LLVM instruction boundaries. Twelve corruptions per control and eighteen per
frame must fail both verification and encoding before output mutation. Restored
plans reproduce ordinary objects. GCC/Clang and IR references check three
programs, including the actual nonreturning exit path on Linux. Optional
baseline checks compare complete objects with the preceding compiler. See
[ADR 0020](ADRs/0020-selected-control-and-frame-programs.md).

The copy checker separately simulates symbolic values in physical registers,
spill cells and the GPR/SSE temporaries. It verifies simultaneous phi results,
allowed endpoints, banks, bounds and temporary accounting without invoking the
resolver. Both allocation verification and encoding require it to pass. Dumped
copy schedules now describe the actual allocated schedule, rather than a
separately resolved virtual copy set.

`make test-mir-copies` checks 36 integer/binary32/binary64 cyclic schedules and
four single-predecessor/equal-successor cases, with GCC/Clang references and
original target execution on Linux. Nine corruptions per case include a wrong
live value, missing final transfer and unsafe ordering around a cycle temporary.
Each must fail verification and encoding before output mutation. Optional
baseline probes require complete object identity for the preceding cyclic
profile. Linux compiles the API probe with Cinder and links the actual core.
See [ADR 0021](ADRs/0021-allocated-phi-transfer-schedules.md).

Constant materialization, bulk memory, argument/call and variadic expansion
retain target pseudos. These remaining boundaries are explicit work; selected
control/frame/copy programs alone do not close the complete machine-IR gate.

Materialization plans own full integer immediates, canonical binary64 pool bits,
PC-relative floating fixups, undefined-value placeholders, integer/SSE copies
and floating sign masks. The encoder consumes these roles and byte programs.
Undefined placeholders do not establish a defined source value. Bulk plans
own zero/copy extents and `rep stosb`/`rep movsb` programs, including the
24-byte `va_copy` object. Lifetime, phi, no-op and `va_end` markers explicitly
emit no bytes. See [ADR 0022](ADRs/0022-selected-materialization-and-bulk.md).

`make test-mir-material` checks seven value roles, two bulk roles and zero-byte
markers. The API probe alters fifteen fields on every such instruction and
requires both verification and encoding rejection before output mutation, then
restores each plan and requires normal original-object identity. Independent
readers check hand-specified encodings, masks, constant-pool bits and actual
floating relocation sites. O0/O2 programs cover integer/SSE copies, wide
immediates, negative zero, initialized conditional values, structure copies,
zeroing and independent variadic cursors across register exhaustion. GCC and
Clang provide reference executions; Linux also executes Cinder objects and
compiles the API probe through Cinder against the actual compiler core.

`make test-mir-storage` checks fifteen source units across twelve scalar
storage profiles and load/store/normalization bitfield roles. Sixteen altered
fields per selected instruction must fail both verification and encoding
before output mutation. Actual O0 MIR counts bind the rejection workload.
Independent readers compare address choices, register effects and actual
load/store forms, then count named-symbol relocation sites without treating
floating constant pools as globals. Thirty O0/O2 objects match the preceding
compiler and LLVM-assembled code/relocations. GCC/Clang and IR runs check every
source result; Linux additionally executes original Cinder objects and uses a
Cinder-compiled API probe. See
[ADR 0023](ADRs/0023-selected-storage-address-and-bitfield-roles.md).

ABI protocols now use owned byte programs from `source/mir_program.c`. Seven
roles cover integer/SSE/aggregate entry, aggregate return, variadic start/get
and calls. Steps contain target bytes, GPR/SSE virtual-value transfers, frame
patches and direct-call relocations. Selection decides staging, packing,
register assignment, binary32 conversions and internal variadic branches.
Encoding replays those steps and resolves physical locations. The raw IR
instruction switch has been removed from the encoder.

`--dump-mir` prints program roles, actual step bytes, frame patches, relocations
and definition-normalization templates. `--dump-regalloc` additionally prints
allocated object slots and alignments. `make test-mir-program` checks forty-five
programs, all seven roles, five step kinds and four frame-patch kinds. A
separate reader independently encodes allocated transfers, resolves selected
frame holes and requires complete program sequences in actual function bytes
at disassembler instruction boundaries. It checks call relocation targets and
near-branch destinations. Zeroed code, wrong roles and changed call targets
are negative controls.

The API probe rejects altered headers, steps, bytes, frame patches and owned
fixups before either verifier or encoder output can change. Restored plans
match normal original objects. O0/O2 code and relocations match LLVM assembly;
optional baseline checks compare complete objects. GCC/Clang and IR executions
check source observations. Linux also executes original objects and compiles
the API probe through Cinder with the actual compiler core. See
[ADR 0024](ADRs/0024-selected-abi-byte-programs.md) and the
[editable protocol diagram](diagrams/abi-programs.mmd).
