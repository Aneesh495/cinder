# Verified pass execution and analysis contracts

Status: implemented

The optimizer previously called algorithms from a combined function and
provided aggregate counters. That obscured which algorithm changed the module
and made stage-specific verification and failure reproduction difficult.

The pipeline now executes ten named passes through one contract table. Each
pass records actual dimensions, transformation events, verifier outcomes and
CPU timing. Before/after CIR belongs to that execution boundary. The source
frontend requests baseline scalar promotion even at `-O0`; the textual CIR tool
keeps its `-O0` input unchanged so independently constructed storage guards have
an observable baseline. Isolated execution always runs one selected algorithm.

Current analyses own freshly constructed facts within an algorithm. There is
no shared cross-pass cache. Contracts nevertheless invalidate all value-use,
effect and liveness facts after a transformation, and all analysis shapes after
CFG changes. This conservative declaration supports a future cache without
allowing it to reuse facts the present implementation would rebuild.

Promotion must preserve the interpreter's write/lifetime/undefined-read
contracts on valid independently parsed IR. Eligibility therefore includes a
CFG may-state analysis for slot retirement and a conservative stored-value
definedness check. Renaming only updates the current run's newly inserted
phis. This retains independently authored valid phis and makes repeated
promotion idempotent.

Process CPU timing excludes verification and file publication. Fine-grained
zero durations are legitimate when the clock resolution exceeds the work.
Native benchmark measurements remain a separate campaign. A read-only reader
reconstructs actual stage chains and selected transformations rather than
accepting a self-reported event counter as proof of useful optimization.
