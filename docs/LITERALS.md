# Literal encoding and storage

Source bytes must be well-formed UTF-8 without embedded NULs. The source
manager rejects overlong sequences, bad continuation bytes, surrogates,
truncation, and code points beyond U+10FFFF before preprocessing. Identifier
spelling is currently ASCII. Physical byte offsets remain unchanged.

Ordinary and `u8` string literals use UTF-8 execution bytes. Simple escapes,
up to three octal digits, greedy hexadecimal escapes, and valid universal
character names are decoded explicitly. Numeric escapes must fit a target
byte. Adjacent literals concatenate after preprocessing and preserve embedded
NULs. Ordinary character constants require one target byte and use the signed
target character mapping. Multi-character constants and wide/Unicode code-unit
literals receive explicit profile diagnostics.

String expressions have character array type, including a final NUL. `sizeof`
and address operands preserve the array; value contexts decay it. Each emitted
literal gets private read-only storage with a symbol relocation. The interpreter
owns the same bytes and rejects attempts to modify them.

Character array initializers infer incomplete bounds, allow exact bounds that
omit the final NUL, and zero-fill remaining bytes. Automatic arrays use an
explicit typed `object.init` transfer from immutable initializer storage.
Typedef array qualification reaches the element type. Native transfers copy
the verified extent; independent interpretation checks bounds, alignment,
declared access type, read-only state, initialization bytes, and pointer tags.
The transfer preserves pointer metadata without dereferencing a host address.

`make test-control` includes 51 authored literal programs; `make test-memory`
classifies literal writes and out-of-bounds reads. `make test-literals` rejects
unsupported literal profiles, malformed source encoding, and corrupted object
transfer contracts while preserving prior output. Aggregate initializer lists,
designators, compound literals, and static pointer relocations remain open.
