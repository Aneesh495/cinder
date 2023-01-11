# Native target and object contract

The native target is x86-64 System V on Linux, little-endian LP64.
`source/x86_64.c` encodes selected integer/SSE operations, pointers, memory
accesses, branches, direct/indirect calls, variadic state and aggregate transfers.
The frame uses `rbp` as a stable base and 16-byte sizing.

`source/elf64.c` writes ELF64 relocatable objects directly. The object includes `.text`, `.rela.text`, `.symtab`, `.strtab`, `.shstrtab`, and `.note.GNU-stack`. Direct calls create `R_X86_64_PLT32` relocations for symbols not resolved within the object. Symbol and section relationships are generated from the same machine object that supplies `-S` byte dumps.

The allocator assigns R12-R15 and XMM2-XMM7 from CFG liveness and reuses verified
spill slots. The encoder preserves assigned callee-saved registers and snapshots
incoming and outgoing arguments. Scalar integer/SSE overflow arguments retain
source order on the stack. Phi edge moves use the parallel-copy resolver with
cycle temporaries. Direct calls use `R_X86_64_PLT32`; function addresses use
`R_X86_64_PC32`, and typed indirect calls use `call r11`. Incoming `_Bool`
arguments and results use only the low byte specified by the ABI.

Aggregate classification, stack overflow arguments, memory returns and hosted
variadics are implemented under the declared profile. Their contracts and
cross-toolchain tests accompany the language documentation.
Pointer/control/frame target pseudos still require full MIR selection. PIC and
aggregate/addressed local, global and parameter-write debug locations remain open.
`-g` emits original DWARF v4 frames, source lines, types, lexical scopes and
unchanged scalar parameter locations. O0 debugging also preserves and tracks
supported scalar locals; see [DEBUG.md](DEBUG.md).

On macOS arm64, Cinder can build the frontend and emit an x86-64 ELF file for inspection. It cannot link or execute that file as a native macOS binary. Linux x86-64 execution and mixed-toolchain ABI checks require the declared Linux environment.
