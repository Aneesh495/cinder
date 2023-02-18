# Acceptance and artifact integrity

`make acceptance` executes the complete native Linux x86-64 release contract.
`make verify` only reads existing evidence and bound source. Verification never
builds or executes the compiler, launches producers, changes reports or updates
gate values. Python bytecode writes are disabled in the wrapper.

The native environment needs CMake, GCC, Clang, LLVM inspection tools, binutils,
GDB, Python 3 and authenticated GitHub CLI access. The publication gate checks
`origin/main` and successful Linux validation and deterministic-bootstrap runs
on the exact commit. The **Cinder complete acceptance** workflow provisions the
declared environment and uploads the raw evidence as a tar.gz archive. Its
parallel jobs use one shared compiler binary. The assembly job verifies every
partial package, requires all nineteen computation gates, then checks publication
and writes the complete twenty-gate manifest.

```sh
make build
make acceptance
make verify
```

## Required campaigns

| Gate | Minimum workload |
| --- | --- |
| Fresh builds | Four strict GCC/Clang profiles, including ASan/UBSan |
| Original native pipeline | Original objects and normal driver linking, including multiple translation units |
| Feature families | 48 source-linked language families with reference, interpreter, native and diagnostic observations |
| Authored suite | All authored source cases, at least 1,200 |
| Differential programs | 20,000 distinct bounded eligible programs, GCC/Clang/Cinder at O0/O2 and IR comparisons |
| IR semantics | 100,000 archived typed IR cases, round trips and optimized comparisons |
| Rewrite validation | At least 50,000 independently modeled defined inputs and invalid-precondition controls |
| Nonvacuous passes | Ten positive/negative transformation pairs |
| ABI | 512 scalar, 512 aggregate and 512 variadic signatures, both directions and both reference toolchains |
| Allocation | 10,000 pressure graphs plus 1,000 rejected mutations |
| Object output | 1,000 two-unit probes, O0/O2, original and independently assembled native objects |
| Debugging | At least 25 actual GDB sessions plus independent DWARF and instruction checks |
| Native fuzzing | One million executions per instrumented preprocessor, parser, IR and object target |
| Failure recovery | 1,000 failed operations followed by successful compilation |
| Applications | Eight real applications, including three multi-TU builds and forty frozen cases |
| Self-hosting | Every compiler module compiled through Cinder; identical stage-two/three objects and executables; 1,000 native generated programs |
| Performance | Frozen integer, floating and memory workloads; three compilers, O0/O2 and five execution samples per cell |
| Documentation product | Actual CLI, driver and eight offline explorer profiles |
| Publication | Exact published and hosted-tested tip |
| Private census | Recomputed substantive production source inventory |

`tools/gate_registry.py` owns the machine-readable registry. Its workload
thresholds are unchanged. Every report dispatches to a dedicated reader that
reconstructs the relevant raw artifacts. Missing, unknown, skipped, nonnative,
truncated, altered or stale evidence fails. Status becomes `complete` only
after every required reader passes.

Source inventory includes tracked and untracked nonignored inputs. Generated
build configurations and compiler bytes have separate bindings. Referenced
artifacts require exact byte sizes and SHA-256 hashes. Readers check command
roles, compiler boundaries, actual target ELF artifacts, source oracles,
reference/native/interpreter outcomes and observed workload totals.

## Individual campaigns

Repeated `--gate` options generate selected evidence. A partial run remains
incomplete and returns failure even when every selected campaign passes.

```sh
python3 -B tools/acceptance.py run build/cindercc --gate applications --gate private-size
make test-source-buffers test-fuzz-regressions
make fuzz
make test-recovery
make benchmark
make demo
```

For macOS fuzz development, use `make fuzz FUZZ_CC=/opt/homebrew/opt/llvm/bin/clang`
with a Clang installation containing libFuzzer. Native acceptance still requires
Linux x86-64. Fuzz reports retain initial source/CIR seeds, mutated corpora,
instrumented targets, actual coverage logs and pipeline counters. Sanitizer
failures, harness I/O errors, empty downstream coverage and unresolved crash or
timeout artifacts fail the reader.

`--bootstrap-evidence PATH` imports a complete retained bootstrap only after
source and seed verification. Fresh campaigns use unique directories and retain
failure artifacts. Performance reports record measurements without imposing an
unsupported speed claim.

## Evidence transport and integrity tests

Hosted artifacts use tar.gz transport to preserve original POSIX filenames,
including colon-containing dependency-test names. Extract into a separate
directory. On Python 3.12 or newer, use the data extraction filter:

```sh
python3 -c 'import tarfile; tarfile.open("cinder-complete-acceptance.tar.gz").extractall("extracted", filter="data")'
```

Run the readers against the matching source checkout and recorded compiler.
The archive does not rename, regenerate or omit raw evidence. Source and tool
bindings continue to apply after extraction.

`tests/test_evidence.py` checks source, configuration, compiler and artifact
substitution and verifies that failed verification is read-only.
`tests/test_release_evidence.py` checks every successful full gate and rejects
sixty altered gate envelopes without modifying artifacts. Existing application,
pass, census, rewrite and bootstrap readers also retain their corruption controls.
Ordinary Linux CI writes a separate partial `INCREMENTAL.json`; it cannot
substitute for complete acceptance.
