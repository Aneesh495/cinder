# Separate translation units

The driver accepts multiple input paths. Each translation unit receives independent source, preprocessing, scope, diagnostic, type, IR, and optimizer state. `-c a.c b.c` emits one object per input using the input basename, while normal link mode first emits owned temporary objects and then passes only those objects to the declared system linker boundary. User C source is never an argument to the linker process.

The current macOS host can verify separate ELF object creation but cannot link or execute Linux x86-64 objects. The multi-TU regression therefore checks two independently emitted objects and records the expected unavailable-host link diagnostic. Linux acceptance must additionally cross-link Cinder and reference objects in both directions, resolve duplicate/undefined symbols, and execute the linked result.

The current linker boundary is intentionally narrow: constant data symbols and direct function-call relocations are emitted. Full tentative-definition merging, common symbols, visibility, weak symbols, global pointer initializers, and complete data relocation coverage remain open ABI work.
