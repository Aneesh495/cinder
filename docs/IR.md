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
Address operations are still open; their future lowering must mark addressed
slots ineligible before promotion.

Initial reads use `IR_UNDEF`. They never acquire an invented numeric zero in the
interpreter. Native code may choose an arbitrary representation for undefined
source behavior, but that representation is excluded from a defined-input oracle.

`source/ir_verify.c` checks reciprocal CFG edges, terminator consistency, unique
definitions, operand availability, dominance, local-slot bounds, scalar register
classes, conversion source types, and complete unique phi predecessor coverage.
The driver verifies before optimization and after optimization/edge splitting.
Exact call signatures, object-memory contracts, and round-trip parsing remain
open verifier work.

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
terminators; call depth is separately bounded. Object-based pointer memory remains
open and cannot be replaced by host pointers.

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
