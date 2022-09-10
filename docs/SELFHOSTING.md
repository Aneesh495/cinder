# Compiler bootstrap

Run `make selfhost` on Linux x86-64 with GCC, Clang, CMake and Python 3.
The command requires the declared 64 MiB process stack and system libc/startup
objects. Other hosts return a nonzero unsupported-profile diagnostic. The
manual `Cinder deterministic bootstrap` workflow runs this campaign on the
Ubuntu 24.04 x86-64 profile and retains raw evidence on success or failure.

The runner first snapshots every production C/header file, owned runtime header,
authored test, generator and relevant build configuration. It inventories the
actual `.c` modules against CMake, including the separate IR tool entry point.
GCC builds a fresh stage 1 from this snapshot. Cinder stage 1 compiles every
module into stage 2 objects. The system driver links those objects, providing
only its declared startup/library dependencies. Cinder stage 2 repeats every
module compilation for stage 3. No module receives a host compiler fallback.

Both stages use identical source paths, options, runtime headers and
`SOURCE_DATE_EPOCH=0`, with `LC_ALL=C` and `TZ=UTC`. The current bootstrap profile
uses a fixed absolute source prefix recorded in its manifest; it does not claim
that arbitrary build-directory relocation produces identical output. Object
and executable hashes must match directly. Nothing is stripped or normalized
to make a mismatch pass. Both raw stages survive failures.

Stage 2 and stage 3 each execute the authored semantic, preprocessing,
constraint, defined/undefined memory, canonical IR, runtime, allocation and
cross-toolchain ABI tests in separate evidence workspaces. The generated subset
contains 1,000 eligible programs covering unsigned operations, array/pointer
loops, mixed INTEGER/SSE records, callbacks, bounded recursion, aggregate arrays,
switch dispatch, separate units and short-circuit effects. Both references and
both stages execute both optimization levels. Cinder compiles original objects
for all source units; the system compiler sees source only for independent
reference executables. A separate merged view permits interpreter comparisons
for multi-unit programs, and that view's source hash is recorded separately.

The generated campaign preserves input source, command argv, exit/output bytes,
native binaries, original objects and per-case reports. Stage 2/3 generated
objects must also match. A complete generated run records 8,000 native
executions and 4,000 interpreter executions; the acceptance unit remains 1,000
distinct source programs. Neither parameter expansion nor repeated stages
inflates that count. GCC/Clang disagreements, timeouts, mismatches and failed
compilations produce retained failures rather than eligible successes.

The development TCG VM is declared with
`CINDER_EXECUTION_PROFILE=emulated make selfhost`. It provides additional
functional evidence and cannot substitute for the native acceptance or
performance profiles. The final audited acceptance reader remains open until
the complete native campaign and its source-bound artifacts have been reviewed.

`make test-differential` uses the same bounded generator for the separate
20,000-program native campaign. The older `make test-generated` remains an
interpreter smoke campaign and cannot satisfy the native differential gate.
