# Native target and object contract

The native target is x86-64 System V on Linux, little-endian LP64. `source/x86_64.c` lowers scalar integer IR to a small directly encoded instruction set: stack frames, integer moves, arithmetic/bitwise operations, signed division, comparisons, branches, direct calls, and returns. The frame scheme uses `rbp` as a stable base and 16-byte frame sizing.

`source/elf64.c` writes ELF64 relocatable objects directly. The object includes `.text`, `.rela.text`, `.symtab`, `.strtab`, `.shstrtab`, and `.note.GNU-stack`. Direct calls create `R_X86_64_PLT32` relocations for symbols not resolved within the object. Symbol and section relationships are generated from the same machine object that supplies `-S` byte dumps.

The allocator assigns R12-R15 and XMM2-XMM7 from CFG liveness and reuses verified
spill slots. The encoder preserves assigned callee-saved registers and snapshots
incoming and outgoing arguments. Scalar integer/SSE overflow arguments retain
source order on the stack. Phi edge moves use the parallel-copy resolver with
cycle temporaries. Direct calls use `R_X86_64_PLT32`; function addresses use
`R_X86_64_PC32`, and typed indirect calls use `call r11`. Incoming `_Bool`
arguments and results use only the low byte specified by the ABI.

Full MIR constraints, interval splitting/coalescing, aggregate classification,
hosted variadics, PIC, debug locations, and large indirect returns remain open.

On macOS arm64, Cinder can build the frontend and emit an x86-64 ELF file for inspection. It cannot link or execute that file as a native macOS binary. Linux x86-64 execution and mixed-toolchain ABI checks require the declared Linux environment.
