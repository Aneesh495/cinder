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
Linux x86-64. The full aggregate, variadic, and callback interoperability
campaign is still incomplete.
