# Build status

Cinder implements the declared C17-core profile with an original preprocessor,
parser, semantic checker, owned HIR, verified SSA IR, ten optimizer passes,
selected machine programs, register allocation, x86-64 encoder, ELF writer,
DWARF output, driver and offline compilation explorer.

The supported execution target is Linux x86-64, LP64, System V AMD64,
non-PIE. macOS arm64 supports building the compiler, frontend checks and ELF
cross-emission. The exact language exclusions remain in [LANGUAGE.md](LANGUAGE.md).
This release does not claim complete ISO C17 conformance.

## Completion contract

Every producer and read-only reader in the twenty-gate acceptance registry is
connected. `make acceptance` runs the complete native campaigns and retains raw
commands, inputs, objects, executables, observations and source/tool bindings.
`make verify` accepts only a complete manifest for the exact current source and
compiler. Partial runs, cross-host reports and stale artifacts fail verification.
See [ACCEPTANCE.md](ACCEPTANCE.md) for the workload and reproduction commands.

The release workflow is **Cinder complete acceptance**. Its uploaded archive is
the source-bound completion record. Ordinary Linux validation and deterministic
bootstrap also check the published commit. Git commit dates preserve the
repository's historical chronology; workflow run timestamps record actual
validation times.

## Final engineering work

Buffered source sets let the four coverage-guided fuzz targets run the actual
preprocessor, parser/sema, canonical IR round trips and object backend without
reading arbitrary host includes. A malformed aggregate-member recovery loop
found by fuzzing now advances after a rejected token. The failing input is a
permanent bounded regression.

Fresh GCC/Clang profiles, all 48 feature families, the complete authored suite,
20,000 generated differential programs, 100,000 typed IR cases, independent
rewrite checks, scalar/aggregate/variadic ABI campaigns, 10,000 allocation
pressure graphs, 1,000 object probes, debugger sessions, four million fuzz
executions, 1,000 failure/recovery cases, eight applications, deterministic
self-hosting, frozen native measurements and CLI/explorer checks all have
explicit raw-artifact readers. No summary counter alone satisfies a gate.

## Deliberate limits

Long double, complex, atomics, thread-local execution, VLAs, GNU inline assembly
and vector extensions, C++, and ABI classes outside the declared profile are
unsupported. Debug information covers frames, source lines, types, scopes,
unchanged scalar parameters and supported initialized scalar locals at O0.
Addressed/aggregate local values and assigned parameter locations are not part
of the supported debugger-value contract. Optimization is conservative rather
than a promise to match GCC or Clang performance.

[BUILD_HISTORY.md](BUILD_HISTORY.md) preserves earlier implementation observations.
Its pending-work statements describe those historical snapshots.
