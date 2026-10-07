# Aggregate expression storage

Aggregate values retain complete target layout. Their current IR representation
is a typed object address with explicit copy effects, rather than an integer
value masquerading as a struct. Lvalue reads can name their existing storage;
assignment, conditional, and comma results materialize bounded automatic
snapshots. Conditional lowering evaluates only its selected arm and transfers
that arm into the shared result storage.

The interpreter freezes a constructed snapshot and retires it at the enclosing
full expression. Native lowering reserves target-aligned frame storage and
emits the actual copy. Freeze and lifetime instructions have no native code
because undefined lifetime/write behavior is not a checked native runtime.
Deep const members prevent aggregate assignment in semantic analysis.

C17 permits temporary addresses to be nonunique. Cinder chooses separate
snapshots. Source-mutating temporary-identity probes have retained differing
reference observations and are excluded from reference agreement counts. The
policy binds the input hash, expected profile result, reference family and
observed discrepancy; it rejects changed or unknown mismatches.

Aggregate calls and returns need their own System V classification and value
transfer implementation. This representation increment does not satisfy that
ABI boundary.
