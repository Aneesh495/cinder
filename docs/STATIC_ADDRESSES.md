# Static address initialization

File-scope pointer initializers resolve named objects/functions, explicit
addresses, array decay/indexing, member addresses, literal storage, pointer
casts, null constants, and constant pointer offsets. Resolution retains the
symbol, signed byte addend, actual target type, and array/subobject domain.
It does not evaluate a host address or run a host compiler on the input.

The canonical IR records an address list per global with the destination byte
offset and pointer type. Verification checks pointer storage, extents, target
declarations, domain shape, and overlapping initializers. Schema 2 explicitly
adds these records; schema 1 input is rejected instead of being misread.

The owned encoder creates `R_X86_64_64` data relocations. The ELF writer emits
`.rela.data` and `.rela.rodata` with the correct symbol-table and target-section
links. Const objects use read-only data. Assembly emits equivalent `.quad`
relocations. System tools are used only to assemble the test oracle or link
Cinder's owned objects.

The interpreter creates global objects before resolving their address records,
then initializes symbolic pointer bytes and provenance. Function identities are
shared with runtime function addresses. Initialization of a pointer beyond its
object domain is classified separately from later one-past access. External
object contents remain unavailable without a model or combined module.

The authored source suite includes 33 static-address programs. The dedicated
test compares original object and assembled relocation records, links two
Cinder translation units on Linux, and rejects corrupt canonical records
without replacing prior output. General initializer lists, designated values,
compound literals, and block static/extern storage remain open.
