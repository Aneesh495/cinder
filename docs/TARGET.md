# Native target and object contract

The native target is x86-64 System V on Linux, little-endian LP64. `source/x86_64.c` lowers scalar integer IR to a small directly encoded instruction set: stack frames, integer moves, arithmetic/bitwise operations, signed division, comparisons, branches, direct calls, and returns. The frame scheme uses `rbp` as a stable base and 16-byte frame sizing.

`source/elf64.c` writes ELF64 relocatable objects directly. The object includes `.text`, `.rela.text`, `.symtab`, `.strtab`, `.shstrtab`, and `.note.GNU-stack`. Direct calls create `R_X86_64_PLT32` relocations for symbols not resolved within the object. Symbol and section relationships are generated from the same machine object that supplies `-S` byte dumps.

The allocator now assigns short-lived values to caller-clobbered `r8`-`r11` when they do not cross a call, and gives other values verified frame spill slots. Native loads/stores consult those locations; call-live values are forced to memory conservatively. Phi plans are passed through an explicit parallel-copy resolver with cycle temporaries and are shown in `--dump-regalloc`. This is still not the final allocator: interval splitting, coalescing, fixed-register constraints, complete edge copies, and high-pressure SSE allocation remain open gates.

On macOS arm64, Cinder can build the frontend and emit an x86-64 ELF file for inspection. It cannot link or execute that file as a native macOS binary. Linux x86-64 execution and mixed-toolchain ABI checks require the declared Linux environment.
