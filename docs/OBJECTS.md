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
both direct and assembled objects. Static data addresses use absolute 64-bit
relocations to functions or objects. Aggregate initializer actions and
tentative-definition storage follow the typed static-object plan.

With `-g`, the writer appends optional owned debug sections and corresponding
RELA sections. Debug section symbols are local and precede global symbols.
Eight-byte address fields use `R_X86_64_64`; four-byte section offsets use
`R_X86_64_32`. The original twelve section indices stay fixed, including the
symbol and string tables. At most sixteen debug sections are accepted. Names,
alignments, field bounds, relocation widths/types and overlap are checked
before publication. The source profile uses `.debug_line`, `.debug_abbrev`
and `.debug_info` alongside `.debug_frame`, `.debug_loc` and `.debug_ranges`; original assembly preserves their
bytes and relocation meaning. See [DEBUG.md](DEBUG.md) for current scope.
