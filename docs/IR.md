# Cinder IR and verification

`source/ir.c` lowers typed scalar expressions into explicit operations and CFG
terminators. Values record target types. Assignments, calls, and returns carry
frontend conversions rather than asking the encoder to infer source semantics.
Global loads/stores remain explicit effects keyed by symbol name. Volatile
locals remain ordered memory operations.

`source/analysis.c` computes reachable reverse postorder without recursive CFG
traversal, immediate dominators, dominator-tree children, dominance frontiers,
and backedges. Irreducible regions still have dominators; only edges whose
headers dominate their tails are reported as natural backedges.

`source/ssa.c` promotes eligible scalar slots using live-slot analysis and
iterated dominance-frontier insertion. An explicit undo stack renames definitions
while walking the dominator tree. Phi inputs are assigned on predecessor edges.
Unused joins are pruned by live-in facts. Volatile objects stay in memory.
Slots referenced by `local.address` stay in memory. A block-entry lifetime
operation resets promoted locals to explicit undef values, including loop
reentry; lexical visibility does not preserve a previous iteration value.

Initial reads use `IR_UNDEF`. They never acquire an invented numeric zero in the
interpreter. Native code may choose an arbitrary representation for undefined
source behavior, but that representation is excluded from a defined-input oracle.

`source/ir_verify.c` checks reciprocal CFG edges, terminator consistency, unique
definitions, operand availability, dominance, local-slot bounds, scalar register
classes, conversion source types, and complete unique phi predecessor coverage.
The driver verifies before optimization and after optimization/edge splitting.
Call signatures, scalar widths, fixed argument classes/ordinals, prototype
arity, variadic promotions, storage types, and return types are checked explicitly.
Pointer offsets have a checked pointee stride and direction; member pointers
carry verified field offsets. Typed memory operations check pointer, access,
and stored-value types. Local lifetime operations bind to the storage table.
Explicit memory dependency graphs and aggregate values remain open.

`source/mir.c` splits critical edges carrying phi transfers before allocation.
The encoder emits edge transfers through the parallel-copy resolver using
physical storage identities. A cycle saves its old source in reserved `r11` or
`xmm1`; ordinary scratch operations use `rax` or `xmm0`. The target block emits
no slot-backed replacement for its phi. `--dump-regalloc` groups phi plans by
incoming edge.

`source/ir_interp.c` independently executes values, local state, shared mutable
globals, and bounded recursive calls. It snapshots every phi input before writing
any destination. Execution classifications distinguish signed overflow, division
by zero, invalid shifts, uninitialized state, conversion range errors, unsupported
operations, malformed IR, and resource limits. Steps count instructions and
terminators; call depth is separately bounded. Object-based pointer memory is
implemented in `source/interp_memory.c` using owned byte objects and object IDs.
Pointer values preserve provenance, offsets, and subobject bounds through
parameters, returns, phis, and pointer storage. Byte initialization, const
storage, alignment, typed access, one-past bounds, and lifetime expiry have
separate checks. Block entry creates a fresh identity; break, continue, return,
and normal scope exit retire the objects being left.

`make test-ssa` constructs integer, float32, and float64 cyclic phi graphs with
critical edges and forced physical register cycles. An independent rotation
oracle checks interpretation before and after splitting. Linux x86-64 also links
and executes each emitted object. The authored control suite exercises promotion
through nested joins, loops, break/continue paths, swaps, global mutations, and
volatile accesses. `make test-undefined` classifies invalid executions without
running their native code.

Analyses are rebuilt on demand. Edge splitting invalidates CFG/dominance/liveness;
copy propagation invalidates uses/liveness; dead instruction removal invalidates
definition and use tables. No cached analysis crosses these boundaries.

`--serialize-ir -o program.cir` writes deterministic typed IR. `cinderir` reads
the format, verifies it, and can print canonical IR, interpret it, or emit an
object or assembly. The parser carries no frontend AST dependency. Normal and
parsed IR produce identical objects for the scalar source corpus. See
`IR_FORMAT.md` for fields and parser bounds.

`make test-ir-campaign` executes 100,000 typed modules through original and
parsed interpretation and O2 optimization. It exhausts unsigned 8-bit addition
and samples signed overflow, unsigned multiplication, division/remainder,
shifts, floating operations/conversions, memory joins, and recursive calls.
Undefined/resource outcomes are inventoried separately from defined execution.
The harness retains every canonical input and observation, source/tool hashes,
per-family counts, and artifact hashes under ignored `.agent-local/ir-campaign`.
The authored memory suite separately covers pointer/object instructions and
classifies invalid memory executions without running native undefined programs.
The generated typed campaign has not yet been expanded to object-memory graphs.
These incremental suites do not establish final frozen-source acceptance.

`local.init`, `memory.init`, `object.init`, and `zero.init` represent construction
of automatic objects separately from later ordinary stores. Scalar initializers
retain RHS evaluation before their destination store. Aggregate brace plans
emit omitted-byte zeroing at each explicit brace level, followed by surviving
source-ordered subobject initializers. Whole object initialization copies byte
initialization and pointer metadata in the interpreter. The x86 encoder emits
original typed stores or bounded byte transfers. A zero effect carries an
explicit byte pointer and extent; the verifier rejects scalar result IDs,
extra operands, mismatched pointer types, and invalid extents.
