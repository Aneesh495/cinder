# Cinder

Cinder is an original C17-core compiler project. It preprocesses and parses C source with its own implementation, lowers typed syntax through an inspectable intermediate representation, performs conservative scalar optimization, and emits Linux x86-64 ELF relocatable objects with a project-owned encoder.

The first target contract is Linux x86-64, little-endian LP64, System V AMD64, non-PIE relocatable output. The compiler itself is portable C17 and can be built on Linux x86-64 and macOS arm64. macOS hosts can build the frontend and cross-emit ELF objects; native Linux execution and self-hosting are separate evidence gates.

## Five-minute build

```sh
make bootstrap
make build
make test
make test-frontend
make test-globals
make test-multi
make test-parallel-copy
make test-float
make test-varargs
make test-generated
make test-apps
make test-native-linux
```

Compile a small translation unit to an ELF object without invoking an assembler:

```sh
build/cindercc -c examples/hello.c -o hello.o
readelf -h hello.o
```

Inspect each stage:

```sh
build/cindercc --dump-tokens examples/hello.c
build/cindercc --dump-ast examples/hello.c
build/cindercc --emit-ir examples/hello.c
build/cindercc --dump-mir examples/hello.c
build/cindercc --dump-regalloc examples/hello.c
build/cindercc -O2 --dump-passes examples/hello.c
build/cindercc -S examples/hello.c -o hello.s
```

The optimizer exposes ten named passes with verified before/after IR, analysis
invalidation records and CPU timing. See [`docs/PASS_PIPELINE.md`](docs/PASS_PIPELINE.md)
for trace output and isolated pass inspection. `make test-passes` checks real
positive/negative transformations and rejects altered proof artifacts.

Selected integer/SSE forms and register/clobber contracts now feed allocation
and the scalar encoder. [`docs/MACHINE_SELECTION.md`](docs/MACHINE_SELECTION.md)
describes the owned machine representation and the remaining target pseudos.

`-E`, `-S`, and `-c` use Cinder's own preprocessing and backend. Linking is deliberately an explicit boundary: on a declared Linux host the driver may pass Cinder-created objects and system libraries to the platform linker, but it never passes user C source to a host compiler.

## Language and target contract

The supported profile is documented in [`docs/LANGUAGE.md`](docs/LANGUAGE.md). It covers the scalar integer subset, pointers, arrays, functions, control flow, macros, constant expressions, and a growing aggregate model. Unsupported constructs are diagnosed rather than silently discarded. The initial contract excludes long double, complex, atomics, thread-local execution, VLAs, GNU inline assembly/vector extensions, C++, and unimplemented ABI classes.

Aggregate layout and constant global storage are now part of the implemented slice. The compiler parses inline struct/union definitions, emits target-sized `.data`, `.rodata`, and `.bss` sections, records data symbols, and accepts multiple input paths for separate object emission. See [`docs/AGGREGATES.md`](docs/AGGREGATES.md) and [`docs/MULTI_TU.md`](docs/MULTI_TU.md).

The implementation is organized by representation boundary: source/diagnostics, tokens/preprocessing, types/parser/sema, HIR/SSA IR, analysis/optimization, MIR/register allocation, x86-64 encoding, ELF64 objects, driver, and inspection. Design contracts and limitations live under [`docs/`](docs/), while acceptance output is generated under the ignored `.agent-local/` directory. Demonstration and benchmark outputs may also use the ignored `artifacts/` directory.

## Current status

See [`docs/BUILD_STATUS.md`](docs/BUILD_STATUS.md) for commands actually run, evidence paths, known limitations, and the next engineering increment. This is an engineering status document, not a claim of ISO C conformance or completed acceptance gates.

## References

The target and language contracts are informed by WG14 N1570 and the System V AMD64 ABI. External references are listed with the exact use and scope in [`docs/REFERENCES.md`](docs/REFERENCES.md).
