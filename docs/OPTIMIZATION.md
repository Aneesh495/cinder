# Conservative integer optimization

The current pipeline performs copy propagation and exact-width constant
folding, constant-edge CFG simplification, sparse conditional constants,
dominance-based integer value numbering, local memory forwarding, and dead
instruction elimination. The remaining required loop, strength, pass-record,
and machine representation work is tracked separately by full acceptance.

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

Dead-code removal retains potentially overflowing signed arithmetic, pointer
comparisons, and reads of possibly indeterminate SSA values. Phi definedness is
computed conservatively, while phi transfers themselves do not read their
incoming value. Calls, memory reads, and floating arithmetic remain observable.
Unused total unsigned/bit operations with defined operands may be removed.
The source suite includes unused overflow and uninitialized expressions that
must still be classified as invalid by the interpreter.

Analyses are rebuilt for each transformation; no CFG/dominance cache crosses a
shape change. The normal driver and canonical-IR tool verify transformed IR
before interpretation, allocation, or original native encoding. `make
test-optimizer` retains before/after CIR, actual changed-operation observations,
semantic results, and original objects. Positive fixtures require a change;
negative fixtures preserve dominance, volatile, alias, call, and floating
boundaries. On Linux the objects are linked and executed. This checkpoint does
not satisfy the complete ten-pass acceptance gate.
