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
Call signatures, scalar widths, fixed argument classes/ordinals, prototype
arity, variadic promotions, storage types, and return types are checked explicitly.
Object-memory contracts remain open verifier work.

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
This campaign does not cover the pending object-memory instructions or serve as
final frozen-source acceptance.
