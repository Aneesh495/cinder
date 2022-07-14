# Dynamic variadic state

Fixed syntactic ordinals cannot implement repeated reads, independent copies,
helper calls, or mixed register overflow. Variadic instructions therefore
take a typed pointer to mutable `va_list` storage. Explicit start, copy, and
end effects bracket reads; aggregate reads name their owned result slot.
The verifier rejects missing operands, incompatible pointers, incomplete
requested types, and incorrect scalar or aggregate result contracts.

Native lowering implements the System V register-save and overflow-stack
representation using the original classifier. Register-bank tests happen
before either offset changes. This preserves whole-argument rollback when
a mixed aggregate exhausts one bank. A caller and callee compiled by different
compilers can exchange the same representation.

The interpreter deliberately uses a different mechanism: a checked cursor
over source-order values and their promoted types. Each state records its
storage identity, initializing frame, argument frame, and active state.
Copies share immutable argument values and retain independent positions.
The VM classifies wrong promoted types, exhausted reads, repeated starts,
unmatched ends, and expired frames without dereferencing host pointers.

Authored checks and generated mixed-toolchain signatures validate the two
mechanisms independently. Raw native observations remain distinct from cross
object checks, and none substitute for the final acceptance readers.
