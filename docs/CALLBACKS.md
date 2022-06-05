# Function pointers and callbacks

Function designators decay to pointers in value contexts. Explicit addresses,
dereferenced designators, callback parameters/returns, arrays, fields, and
conditional selection retain their function types. Call constraints check the
pointed-to prototype and apply the same scalar conversions as direct calls.

`function.address` produces an original symbol relocation. A `call` with no
symbol uses its left operand as the target; the verifier requires a compatible
pointer-to-function type. The encoder snapshots the target in R11 before
staging the arguments and uses the scalar SysV register/stack assignment.

The independent interpreter represents functions as immutable identities in
its object table. It resolves an indirect target only after checking identity,
lifetime, offset, and signature. It never calls a host address. External calls
remain unsupported by the interpreter unless explicitly modeled.

`make test-control` covers authored callback programs. `make test-abi` includes
the deliberately poisoned `_Bool` boundary regression with GCC and Clang on
Linux x86-64. `make test-abi-callbacks` generates 512 scalar signatures with
14 return families, integer/SSE overflow, and pointer arguments/results. Every
signature calls in both directions and sends callbacks in both directions.
Both references and Cinder O0/O2 results must match independently computed
integer or floating bits, or pointer identity. Raw commands, source/object/
executable hashes, outputs, toolchain identity, and real host architecture are
retained with the source and compiler binding. A macOS run records zero native
executions. These generated probes do not count toward the authored corpus.
Aggregate and variadic interoperability remain open requirements.

The poisoned-bit probe uses the published
[SysV INTEGER argument rule](https://gitlab.com/x86-psABIs/x86-64-ABI/-/blob/master/x86-64-ABI/low-level-sys-info.tex)
as its independent oracle: bits beyond the type's memory representation are
unspecified. The local Linux GCC 15/Clang 22 run observed a Clang O2 argument
deviation while Cinder O0/O2 passed with both host drivers. Reference deviations
remain in the raw report and are separate from normal C interoperability cases.
