# Owned DWARF types and unchanged ABI parameter locations

Status: implemented for signature types and unchanged scalar parameters;
automatic/global variables and parameter write tracking open.

## Decision

Serialize types referenced by emitted function signatures into the same DWARF4
compilation unit. Use source semantic layouts for scalar encodings, record
sizes, member positions, array bounds and bitfield data-bit offsets. Retain
enumerator names/values in the type context and parameter declaration locations
in the declarator/type records copied into HIR and IR.

Register each referenced type before emitting it. Queue newly discovered types,
write CU-relative reference placeholders and patch them after every type offset
is known. Use type-record identity plus remaining qualification for debug
deduplication. Semantic aggregate identity alone is insufficient for qualified
copies. Recursive types need forward references, not unbounded recursion.

Before optimization, reject parameter value tracking if the parameter slot is
assigned or its address is formed. Describe unchanged scalar arguments using
actual ABI registers followed by their private incoming staging homes; describe
stack arguments at their incoming CFA-relative address. Each location list has
an explicit relocated function base and relative address ranges. The encoder
supplies its real staging boundary and save-slot offsets.

Assigned or escaped parameter entries omit their location attribute. An
arbitrary allocated frame slot does not prove initialization, persistence after
promotion, or the current value at a source breakpoint. The private staging
area is valid for an unchanged argument because lowered code does not reuse it
for local objects, spills or outgoing calls.

## Ownership and serialization

The type context owns enum debug names and copies qualified enum tables. HIR
retirement does not invalidate types or IR-owned parameter records. Debug
construction borrows these semantic records only until it has serialized owned
bytes and fixups. ELF publication can occur after IR, type and source teardown.

Canonical CIR stays at schema 6 because these additions are source debugging
metadata. A CIR round trip preserves semantic types and execution; it does not
preserve source debug declarations. `cinderir` continues to reject `-g`.

## Verification

Independent DIE and line readers check all reference targets, exact declarator
coordinates, recursive records, incomplete declarations, flexible/fixed arrays,
qualifier chains, enum values and variadic callback types. Separately specified
LP64 sizes and offsets make layout errors observable; six altered metadata
records must fail those assertions. LLVM checks structure and assembly identity.
GCC/Clang execute the eligible fixture at O0/O2.

The lifetime probe retires AST/sema, HIR, then IR/types/sources at their actual
ownership boundaries and publishes the resulting original ELF object. Linux
GDB checks type rendering, actual scalar/pointer/record observations and an
unavailable assigned parameter. Independent location-list decoding checks the
actual encoded staging stores. These checks do not close
the complete native debugging/value-location gate.
