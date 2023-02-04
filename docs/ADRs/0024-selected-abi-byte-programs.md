# 0024: Selected ABI byte programs

Status: accepted

## Context

Incoming/outgoing ABI layouts were selected before allocation, but argument,
call, aggregate-return and variadic protocols still chose target operations
inside the encoder. This mixed typed target selection with physical emission
and left substantial raw IR dispatch in the backend.

## Decision

Construct owned ABI programs from verified IR operands and selected layouts.
The selector has no allocation dependency. Seven roles cover integer, SSE and
aggregate arguments, aggregate returns, variadic initialization/retrieval and
calls. Programs contain concrete byte fragments, explicit GPR/SSE value
transfers, symbolic frame patches and owned call relocations.

Argument staging, register exhaustion, mixed-class aggregate packing, hidden
return storage, binary32 boundary conversions, indirect/direct calls and
variadic cursor paths are selected before allocation. Internal variadic branch
displacements are fixed within byte fragments. Frame holes have fixed widths,
so resolving actual local or incoming homes does not change those branches.

The encoder replays programs and resolves verified frame slots or value
locations. It does not select these protocols by IR opcode or source type.
Source-type queries retained for debug metadata do not generate target code.

The machine verifier reconstructs complete programs from verified source and
recomputed layouts, then checks every step, byte, patch and relocation before
encoding can mutate output. This is structural validation. Independent ABI
execution and object checks provide separate semantic evidence.

## Validation

Forty-five source programs cover the seven roles, five step kinds and four
frame-patch kinds. The API probe mutates program headers, each step, byte
buffers, patch fields and owned relocation fields. Both verification and
encoding must reject every mutation before output changes. Restored programs
must produce the normal compiler's original object.

A separate Python reader reconstructs complete allocated program bytes from
retained MIR and physical locations. It independently encodes register/stack
transfers, resolves frame holes, checks actual call relocation sites and
requires contiguous matches at disassembler instruction boundaries. Near
branches must land inside the function on instruction boundaries. Deliberately
zeroed text, an incorrect protocol role and a wrong call target must fail.

Original sections and relocations agree with LLVM assembly. Optional baseline
checks require full O0/O2 object identity. GCC/Clang and IR executions check
expected source behavior; Linux additionally executes original Cinder objects
and builds the API probe through Cinder against the actual compiler core.
