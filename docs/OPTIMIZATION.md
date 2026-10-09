# Conservative integer optimization

The current pipeline performs copy propagation and exact-width constant
folding, constant-edge CFG simplification, sparse conditional constants,
dominance-based integer value numbering, unsigned strength reduction, local
memory forwarding, redundant copy/phi cleanup, natural-loop invariant motion,
and dead instruction elimination. Each pass has its own verified boundaries,
transformation record, analysis contract and process CPU timing. The machine
representation and remaining full acceptance work are tracked separately.

Constant facts are normalized to the declared width, signedness, and Boolean
representation before arithmetic or comparison. Invalid signed arithmetic is
left in the program. Pointer comparisons, floating operations, and float
conversions are not folded by this integer evaluator. Independent typed-IR
fixtures cover wrapped byte literals, signed word/byte literals, Boolean
normalization, invalid pointer comparisons, and indeterminate conditions.

CFG simplification selects an edge only from a proved integer constant. It
rebuilds predecessor/successor tables, filters phi inputs, removes unreachable
blocks, and remaps block IDs. Equal branch destinations alone do not justify
removing a read of an indeterminate condition. Source locations and retained
instruction order remain intact. Critical-edge splitting still runs afterward.

Sparse conditional propagation tracks executable edges separately from SSA
facts. Facts move from unobserved to a constant or varying; a constant can later
become varying. Indeterminate values are varying rather than an invented zero.
Phi nodes join only executable predecessor inputs. Pure integer conversions,
comparisons, and supported arithmetic can become constants; calls, memory,
pointers, and floating operations remain varying. If its 4,096-round fixed
point budget expires, the pass leaves the function unchanged.

Value numbering reuses equivalent integer computations only when the existing
definition dominates the new instruction. Instruction order is required within
a block. Keys include value/source types, canonical operands, opcode, literal,
and bitfield width/offset. Normalized integer literals can share a value.
Sibling definitions cannot stand in for each other. The candidate search is
bounded to the latest 4,096 opportunities; a missed opportunity retains the
original instruction. Floating operations, memory, pointers, and calls never
enter the table. Integer divisions can share a dominating computation: any
invalid first computation still executes before the reused result.

Copy cleanup redirects uses to an exactly matching, dominating SSA value.
Copies that can read an indeterminate value retain their instruction. A phi
with a single common non-self input can reuse that input only when its
definition dominates the phi block from outside. Loop self-inputs are ignored
when finding the common value; different incoming values and cycles are kept.
Phi elimination redirects uses and removes the transfer. It never introduces
an eager copy, because phi transfers can carry an indeterminate value without
reading it. The independent interpreter checks both used and unused
indeterminate phis. Entry-block phis are rejected by the verifier: first entry
has no predecessor from which to select an input.

`cinderir --pass=NAME input.cir -o output.cir` runs any one of the ten passes
listed in [PASS_PIPELINE.md](PASS_PIPELINE.md), with verification before and
after it. Combining an isolated pass with `-O1` or `-O2` is rejected.
`make test-optimizer` retains isolated before/after
observations and objects for integer, pointer, floating, effect, and cycle
fixtures, in addition to testing the complete normal pipeline.
`make test-passes` checks all ten positive/negative pairs, their actual stage
records, scalar promotion guards and the read-only evidence reader.

Local memory facts are confined to one block and an exact SSA address and
integer type. Roots must come from an actually declared nonvolatile local
object. Pointer casts cannot erase volatility of that declaration. A retained
first load guards reuse; a retained ordinary store guards removing a duplicate
store. An initializer or read does not establish permission to write const
storage. Any real write clears earlier alias facts. Calls, globals, aggregate
copies, bitfield operations, variadic effects and lifetime transitions clear
all facts. Unknown pointer roots, floating storage and volatile views also
clear the table. The pass retains the first access, including any invalid
read, write, bounds, lifetime or initialization check it would perform.

The focused suite covers 17 source programs and six independently constructed
CIR guards for mutable/const writes, reads after retirement/reset, and frozen
aggregate storage. It compares isolated before/after interpreter outcomes and
emits original objects for the source programs. Invalid cases compare their
classification without treating the placeholder integer as an execution value.

Dead-code removal retains potentially overflowing signed arithmetic, pointer
comparisons, and reads of possibly indeterminate SSA values. Phi definedness is
computed conservatively, while phi transfers themselves do not read their
incoming value. Calls, memory reads, and floating arithmetic remain observable.
Unused total unsigned/bit operations with defined operands may be removed.
The source suite includes unused overflow and uninitialized expressions that
must still be classified as invalid by the interpreter.

Unsigned multiplication by a power of two becomes a left shift, unsigned
division becomes a logical right shift, and unsigned remainder becomes a
low-bit mask. Constants are interpreted at the operation's actual width. The
shift must be smaller than that width; zero and other factors retain the
original operation. Multiplication or division by one becomes a copy, including
signed division by one. Other signed multiplication, division, and remainder
are retained. The pass creates a fresh shift/mask constant rather than changing
a constant shared by other operations. Each replacement still reads its input,
including remainder by one, so an indeterminate operand remains invalid.

`make test-rewrites` feeds real straight-line CIR fragments through the
production optimizer and evaluates both versions with a separate Python
integer model. The domain includes every byte value for every eligible shift,
a complete 16-bit division domain, wider boundaries and deterministic samples,
signed overflow, zero division, and indeterminate inputs. The read-only evidence
reader reconstructs every observation and rejects rehashed numeric and IR
corruptions. [Rewrite validation](REWRITE_VALIDATION.md) specifies its limits.

Natural-loop motion uses dominance-checked backedges and a verified existing
preheader. Only defined total integer operations can move onto a zero-trip
path. Trapping, memory, pointer, variable-shift, floating and effect operations
retain their placement. [Loop motion](LOOP_MOTION.md) gives the exact region,
operand and operation restrictions and the before/after test contract.

Analyses are rebuilt for each transformation; no CFG/dominance cache crosses a
shape change. The normal driver and canonical-IR tool verify transformed IR
before interpretation, allocation, or original native encoding. `make
test-optimizer` retains before/after CIR, actual changed-operation observations,
semantic results, and original objects. Positive fixtures require a change;
negative fixtures preserve dominance, volatile, alias, call, and floating
boundaries. On Linux the objects are linked and executed. This checkpoint does
not satisfy the complete ten-pass acceptance gate.
