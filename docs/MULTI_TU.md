# Separate translation units

The driver accepts multiple input paths. Each translation unit receives independent source, preprocessing, scope, diagnostic, type, IR, and optimizer state. `-c a.c b.c` emits one object per input using the input basename, while normal link mode first emits owned temporary objects and then passes only those objects to the declared system linker boundary. User C source is never an argument to the linker process.

The current macOS host can verify separate ELF object creation but cannot link or execute Linux x86-64 objects. The multi-TU regression therefore checks two independently emitted objects and records the expected unavailable-host link diagnostic. Linux acceptance must additionally cross-link Cinder and reference objects in both directions, resolve duplicate/undefined symbols, and execute the linked result.

The linker receives original function/data symbols, direct-call and PC-relative
address relocations, and absolute addresses for typed static initializers.
Tentative definitions are resolved within each translation unit and use strong
storage. Common symbols, weak binding and visibility extensions remain outside
the current profile. Debug FDEs relocate their CIE section offsets when the
linker combines translation units; see [DEBUG.md](DEBUG.md).
