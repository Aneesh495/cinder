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

Local objects have a type-derived stack extent and alignment. Narrow scalar
loads and stores use their declared width; float objects occupy binary32
storage and convert to the interpreter/register binary64 representation on
load. Spill slots, preservation slots, and captured incoming arguments follow
the local object area. The checker reconstructs these extents independently
and rejects overlaps, undersized frames, and spill locations in reserved areas.

Native phi edges use simultaneous physical transfers, including cycles and
critical edges. Aggregate ABI classification, indirect calls, full variadic
cursors, explicit target instruction MIR, and scheduling remain separate work.
