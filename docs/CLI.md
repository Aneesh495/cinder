# CLI reference

`cindercc` accepts C input paths and compiles each translation unit with fresh
source, preprocessing, semantic and optimizer state. See [MULTI_TU.md](MULTI_TU.md)
for output naming and linking.

| Option | Behavior |
| --- | --- |
| `-E` | Original preprocessing with source markers |
| `-S` | Assembly directives representing original encoded bytes and relocations |
| `-c` | Original ELF64 relocatable object |
| `-o PATH` | Primary output path |
| `-I DIR`, `-DNAME[=VALUE]` | Include search path and preprocessing definition |
| `-std=c17`, `-std=c17-core` | Declared C17 core profile |
| `-fsyntax-only` | Stop after semantic analysis |
| `-O0`, `-O1`, `-O2` | Optimization level |
| `-g` | DWARF v4 debugger stack frames; [current scope](DEBUG.md) |
| `-M`, `-MD`, `-MF PATH`, `-MQ TARGET` | Actual physical-file dependency rules |
| `--diagnostics=text`, `--diagnostics=json` | Text diagnostics or versioned JSON |
| `-ffile-prefix-map=OLD=NEW`, `-fmacro-prefix-map=OLD=NEW` | [Source-name mapping](REPRODUCIBILITY.md) |
| `--dump-tokens`, `--dump-ast`, `--dump-hir` | Preprocessing, semantic AST and owned HIR inspection |
| `--emit-ir`, `--serialize-ir` | Readable CFG IR or canonical CIR |
| `--dump-mir`, `--dump-regalloc` | Selected machine plans and actual allocation |
| `--dump-passes`, `--pass-stats PATH`, `--pass-trace DIR` | Verified pass records and before/after IR |
| `--interpret` | Execute `main` in the independent IR interpreter |
| `--explorer DIR` | Current offline stage summary |
| `-fverify-each` | IR verification at driver boundaries |
| `--`, `--help`, `--version` | End options, print usage or print version |

Unsupported options and incompatible output modes fail with nonzero status.
`-fPIC` is rejected. Object, assembly and dependency publication use owned
temporary files and atomic replacement per artifact. A normal compilation
error preserves complete previous output. Multiple artifacts are not a group
transaction. The full interruption/recovery campaign remains open.

Normal link mode requires Linux x86-64. The driver passes original objects to
the declared system linker driver for startup objects and libc. User C input
never reaches a host compiler. On macOS arm64, use `-c`, `-S` or stage inspection
for the Linux target. See [DRIVER_DIAGNOSTICS.md](DRIVER_DIAGNOSTICS.md) for
dependency escaping, collisions, diagnostics and exact error behavior.
