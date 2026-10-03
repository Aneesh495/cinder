# Register allocation

The allocator builds basic-block use/def sets and solves liveness to a fixed
point. Phi inputs belong to predecessor edges. A value's conservative interval
covers its definition, uses, and every live block boundary. Intervals are sorted
by their first position. Expired intervals release registers; under pressure,
the longest remaining interval spills when a shorter one can use its register.
Disjoint spilled intervals reuse frame slots.

R12-R15 hold integer values. XMM2-XMM7 hold scalar floating values. Fixed encoder
scratch registers and argument/return registers are reserved. SSE intervals
crossing calls spill because SysV does not preserve these registers. The
encoder saves and restores every allocated callee-saved GPR. Incoming argument
registers are captured before allocation moves can overwrite them. Outgoing
arguments are captured before assigning ABI registers, including mixed and
stack-passed scalar arguments.

The independent checker does not trust interval bounds. It rebuilds liveness
with backward instruction transfers and checks every definition and live
program point against physical locations. It checks reserved registers,
register classes, spill frame ownership, call clobbers, and preservation masks.
`make test-allocation` retains observations for 10,000 seeded pressure graphs
and rejects 1,000 corrupt allocations. The source/native tests exercise
branches, loops, calls, SSE pressure, and stack overflow arguments separately.

Aggregate ABI classification, float32 boundary conversions, indirect calls,
full variadic cursors, instruction scheduling, and physical edge-copy lowering
remain separate work. Current phis still use the original local slot in native
lowering, so this increment does not establish complete out-of-SSA support.
