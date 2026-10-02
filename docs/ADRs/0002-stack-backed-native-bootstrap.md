# ADR 0002: Stack-backed scalar native bootstrap

## Decision

The first native backend uses stable `rbp`-relative slots for every IR value and local, while keeping a separate interval/allocation trace. Direct calls, integer operations, CFG branches, relocations, and returns are encoded from the same machine object used for assembly dumps.

## Rationale

A stack-backed representation makes frame offsets and object bytes straightforward to inspect while the frontend, IR interpreter, and ELF writer stabilize. It avoids hiding missing value-location invariants behind a host assembler or an unverified register cache.

## Consequences

The current implementation is not the final optimized register allocator. High-pressure register use, SSE classes, call-preserved values, spill-slot reuse, and copy-cycle lowering remain explicit acceptance gaps. The allocation trace is useful evidence of the boundary but cannot be counted as completion of the final allocator gate.
