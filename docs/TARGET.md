# Native target and object contract

The native target is x86-64 System V on Linux, little-endian LP64. `source/x86_64.c` lowers scalar integer IR to a small directly encoded instruction set: stack frames, integer moves, arithmetic/bitwise operations, signed division, comparisons, branches, direct calls, and returns. The frame scheme uses `rbp` as a stable base and 16-byte frame sizing.

`source/elf64.c` writes ELF64 relocatable objects directly. The object includes `.text`, `.rela.text`, `.symtab`, `.strtab`, `.shstrtab`, and `.note.GNU-stack`. Direct calls create `R_X86_64_PLT32` relocations for symbols not resolved within the object. Symbol and section relationships are generated from the same machine object that supplies `-S` byte dumps.

The current backend is a correctness-oriented stack-backed scalar slice. Constant global data now emits `.data`, `.rodata`, and `.bss`, and global integer reads use RIP-relative `R_X86_64_PC32` relocations. The allocator records virtual-value intervals and stack/register decisions for inspection, while the encoder uses stable frame slots for all values. This is an explicit limitation, not a claim that the final high-pressure register-allocation gate is complete. SSE, aggregate expression classification, variadics, PIC, debug locations, and indirect large returns remain unverified.

On macOS arm64, Cinder can build the frontend and emit an x86-64 ELF file for inspection. It cannot link or execute that file as a native macOS binary. Linux x86-64 execution and mixed-toolchain ABI checks require the declared Linux environment.
