# ADR 0001: Explicit profile and representation boundaries

## Decision

Cinder targets a documented `c17-core` profile and keeps source, typed frontend, IR, machine IR, encoder, and object writer as separate contracts. Unsupported language or ABI behavior is diagnosed at the boundary that understands it.

## Rationale

A compact, explicit contract makes native output inspectable and prevents accidental host-compiler semantics from filling gaps. Target widths and layouts are target facts, not properties of the macOS or Linux build host.

## Consequences

The implementation can grow one end-to-end family at a time. Acceptance must distinguish implemented behavior, partial hooks, and unverified environments. A rejected option or construct is preferable to silently producing a misleading object.
