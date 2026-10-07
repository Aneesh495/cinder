# Aggregate calls and System V classification

Struct and union expressions already use typed storage addresses. Calls now
retain that representation through argument snapshots, callee parameter copies,
and caller-owned return objects. This preserves value semantics independently
of the physical calling convention. The interpreter transfers initialization
maps and pointer identities without consulting machine registers.

The original classifier in `source/abi.c` recursively merges field classes in
two eightbytes. Integer/pointer members dominate SSE members sharing an
eightbyte. Larger objects use MEMORY. Arguments use six integer and eight SSE
registers; an aggregate that cannot fit in either required bank goes entirely
on the stack without consuming registers. Memory results reserve the first
integer argument register for the caller's result pointer.

The encoder snapshots all incoming registers before lowering parameter copies.
Call sites stage every argument before assigning physical registers. This
protects argument values currently held in registers later used by the ABI.
Indirect callees are loaded after argument staging. Small results use the
independent integer and SSE return sequences; larger results are copied through
the hidden pointer. Exact byte packing avoids reading beyond short objects.

The rules follow the primary
[x86-64 psABI source](https://gitlab.com/x86-psABIs/x86-64-ABI/-/raw/master/x86-64-ABI/low-level-sys-info.tex).
The declared Cinder profile excludes x87, vector types, and unaligned packed
extensions. The classifier does not claim those classes. Hosted variadic
callee state requires its own implementation and remains a separate gate.

Authored observations cover integer/SSE/mixed/union/nested-array layouts,
whole-argument rollback, stack order, hidden-pointer register pressure,
recursion, callbacks, pointer provenance, and short objects. Generated probes
compare each named scalar member and full-width floating bits in both call
directions with GCC and Clang. Padding bytes are excluded from equality.
