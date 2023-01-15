# Selected pointer and address programs

Status: accepted

Pointer scaling, member offsets and address formation were expanded from typed
IR inside the final encoder. These decisions belong before allocation so the
machine representation owns its target forms and physical constraints.

`MIR_ADDRESS` has five selected roles: frame addresses, RIP-relative symbol
addresses, member displacements, scaled offsets and signed element differences.
Six typed IR origins use them, including both object and function symbols.
Each plan records its before/after byte program, scale or displacement, frame
slot and relocation contract. The source operands and symbol names remain
owned machine operands. No AST or token access occurs in the encoder.

Frame plans select `lea` from RBP; allocation supplies the final slot offset.
Symbol plans select RIP-relative `lea` with `R_X86_64_PC32` and addend -4.
Offsets select the stride multiply, optional negation and final address add.
Differences select subtraction, sign extension and signed division by the
element stride. Member displacements must fit signed 32-bit encoding.

Masks use actual hardware codes. Frame addressing reads RBP without changing
flags. Symbol formation changes only RAX. Pointer addition and member offsets
change flags; signed differences also change RDX. The allocator consumes these
contracts, and the verifier checks every selected field before encoding.

The encoder consumes the selected byte programs and typed target operands.
It resolves only allocated locations and object relocation offsets; it no
longer chooses pointer scale, direction, member immediates or symbol address
forms. Ordinary objects remain byte-identical to the preceding compiler on
the dedicated fixtures.

Independent checks decode the dumped plans against hand-specified bytes and
masks, compare original output with LLVM assembly, and execute GCC/Clang and
IR references. The corpus includes a 24-byte element stride, negative indexing
from within an array, positive/negative differences, a 65536-byte member
offset, actual pointer spills across calls, and mixed-toolchain separate
compilation. Sixteen corruptions per selected address must fail verification.
Control/frame expansion and other target pseudos remain separate work.
