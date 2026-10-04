# Acceptance and artifact integrity

`make verify` only reads existing evidence. It does not build the compiler, run
it, recount source, generate reports, or update gates. Python bytecode writes
are disabled in the wrapper. Source inventory includes tracked and untracked
nonignored inputs, so an uncommitted edit or new configuration file invalidates
the binding. Generated build configurations and the actual compiler binary have
separate bindings. Referenced artifacts require exact byte sizes and hashes.

`tools/gate_registry.py` records every required gate, command, unit, and minimum
workload. The manifest must contain exactly that registry. Unknown/missing gates,
incomplete status, wrong target profile, skipped commands, missing reports, stale
source/configuration, substituted tools, and altered artifacts fail verification.
Full gate results require dedicated readers that compute outcomes from actual
raw campaign artifacts. Those readers and full runners are still being connected;
the guard intentionally cannot report complete acceptance in their absence.

`make acceptance` currently fails with an explicit incomplete manifest because
the full compiler and campaigns are unfinished. It does not bless the existing
smoke applications or reuse a generated workload summary as full acceptance.
The completed runner must execute the complete expensive registry and retain
raw progress before this command can succeed.

Ordinary Linux CI runs incremental frontend/native/object checks, the typed IR
campaign, sanitizer build, and integrity regression tests. It writes
`INCREMENTAL.json`, whose status is partial and whose purpose is explicit.
Incremental validation does not write a successful `ACCEPTANCE.json`.
Environment flags cannot assert native execution or skip required workloads.

`tests/test_evidence.py` checks deleted objects, changed reference output,
substituted compiler bytes, uncommitted/new source inputs, generated configuration
changes, corrupted bootstrap bindings, vacuous/incomplete manifests, and truncated
reports. Its positive fixtures test hash-binding primitives only and cannot pass
full acceptance. A subprocess verification also checks file hashes/timestamps
before and after to detect writes.

The source census is ignored private state. Counting is quote-aware and excludes
comments, blank/punctuation-only lines, directive continuations, and API prototype
boilerplate. It does not print production counts to CI logs. Final private
accounting still needs the reconstruction ledger and exclusion audit.
