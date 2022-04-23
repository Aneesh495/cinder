# ELF objects and assembly

The writer emits ELF64 little-endian x86-64 relocatable objects with explicit
field serialization. Host structure padding and byte order do not define the
format. Section contents retain their target alignments. Symbols include
binding, kind, section, byte offset, and size; local symbols precede globals,
and the symbol table identifies the first global entry.

Static functions and objects are local. Floating literals are local read-only
objects, so two translation units may use the same generated literal name.
RIP-relative object references use PC32 relocations; direct calls use PLT32.
The encoder emits loads and stores at the declared integer global width.

`-S` represents the original encoder's bytes with assembly directives and
symbolic relocation expressions. It includes data, read-only data, BSS,
bindings, sizes, and a non-executable stack marker. Normal `-c` directly writes
an ELF object. The assembly oracle in `make test-object-campaign` is a separate
validation path using Clang's assembler.

The independent reader checks bounds, alignment, symbol ordering/sizes, local
binding, relocation indices/types/addends, and target identity. The campaign
runs 1,000 two-unit probes at O0 and O2. On Linux x86-64 it links and executes
both direct and assembled objects. Debug sections, data-address relocations,
complete aggregate initializers, and complete tentative-definition semantics
remain separate work.
