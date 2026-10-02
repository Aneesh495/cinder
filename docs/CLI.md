# CLI reference

`cindercc` accepts one C translation unit per invocation in the current product slice.

- `-E` emits Cinder's preprocessed source.
- `-S` emits assembly syntax containing the bytes produced by the direct encoder.
- `-c` writes an ELF64 relocatable object.
- `-o PATH` selects the output artifact.
- `-I DIR` adds an include search directory, and `-DNAME[=VALUE]` defines a macro.
- `-fsyntax-only` stops after semantic analysis.
- `-O0`, `-O1`, and `-O2` select the conservative optimization level.
- `--dump-tokens`, `--dump-ast`, `--emit-ir`, `--dump-mir`, and `--dump-regalloc` expose stable inspection output.
- `--interpret` runs `main` in the independent IR interpreter.
- `--explorer DIR` writes a local HTML stage summary.
- `-fverify-each` enables IR verification at each driver boundary.

Unsupported options are rejected. Fatal diagnostics leave a requested object path untouched unless it was an already-existing unrelated file; atomic replacement and dependency-file publication are follow-up driver work. Normal link mode is available only on the declared Linux profile and passes a Cinder-created object to `cc`, never source C.
