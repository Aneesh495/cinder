# Natural loops and invariant motion

`cinderir --pass=loop-motion input.cir -o output.cir` isolates the same
transformation used by `-O2`. The verifier runs before and after an isolated
invocation. The pass moves existing instructions and preserves their SSA IDs,
types, locations and operand order. It does not replace them with a separately
computed example or invoke the host compiler.

A backedge must lead to a block that dominates its source. The pass unions the
reverse predecessor closures of all backedges into that header. Every member
must be reachable and dominated by the header; a side entry rejects the region.
Irreducible control flow is retained. The loop must have one outside predecessor
whose sole successor is an unconditional jump to the header. This existing
preheader receives hoisted operations. Multiple outside predecessors or a
conditional predecessor retain the loop's original placement.

An operand must have a defined SSA value outside the loop and its definition
must dominate the preheader. A value already moved into that preheader can
support another move. Phi and indeterminate-value facts prevent an eager read
of a possibly uninitialized value. The pass scans to a fixed point with a
bounded iteration budget. Exhaustion retains further opportunities; it never
invents an invariant. Processing later headers first can expose inner-loop
invariants to an enclosing loop.

Only total integer operations can run on a zero-trip path: integer constants,
proved-defined copies, integer conversions, bitwise operations, integer
comparisons, unsigned arithmetic, and unsigned shifts with a known in-range
count. Signed arithmetic, division/remainder, variable shifts, floating
operations, pointer operations, loads/stores, calls, lifetime operations and
variadic effects retain their placement. An invariant address or divisor does
not establish permission to speculate an access or division.

The focused suite contains 18 independently authored source programs. It
requires an actual change of block for the selected opcode's original SSA ID
in each positive example, and unchanged placement in the corresponding
negative cases. Examples cover chains, conversions, comparisons, fixed shifts,
nested loops, do loops, multiple latches, volatile accesses, calls and
irreducible control flow. Each source emits normal `-O0`, normal `-O2`, and
isolated-pass objects, with interpreter comparisons and native execution on
the Linux profile.

Ten constructed CIR examples add zero-trip and first-iteration guards for
unsigned multiplication, division by zero, signed overflow, indeterminate
copies, out-of-range shifts, and multiple outside predecessors. Defined guard
programs also produce before/after objects. Invalid executions compare their
classification rather than treating the integer placeholder as an oracle.
