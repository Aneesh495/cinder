# Build status

Updated: 2026-10-02

## Repository and publication

- Workspace was empty at initialization and was initialized as Git branch `main`.
- Authenticated GitHub account: `Aneesh495`.
- Published repository: [Aneesh495/cinder](https://github.com/Aneesh495/cinder), public, branch `main`.
- The requested `cinder` owner namespace was not writable from the authenticated account, so no unrelated namespace was modified.
- Latest published implementation checkpoint before this final Linux validation update: `82a82f1`.

## Implemented behavior

- Checked arena/vector storage, source-file identities, source ranges, and structured diagnostics.
- Original preprocessing for object/function macros, `-D`, conditional branches, relative includes, `#error`, and bounded rescanning.
- Longest-match lexer for the implemented C token families with integer and decimal floating literal validation.
- Recursive-descent declarations/statements, precedence expressions, scalar target types, nested scopes, lvalue checks, call/return checks, and loop-context diagnostics.
- Typed CFG-like IR with local memory operations, explicit join phis with incoming edges, global loads/stores, integer and scalar floating operations, terminators, verifier, independent bounded interpreter, constant folding, effect-aware dead-code elimination, allocation traces/checker, direct x86-64/SSE2 scalar encoding, ELF64 relocatable output, CLI inspection paths, and a local HTML explorer summary.
- CFG analysis computes reachable reverse postorder, iterative immediate dominators, and conservative back-edge loop headers. `-O1/-O2` forwards same-block local loads, removes unused pure integer instructions, and reports transformation counters while retaining effectful operations.
- Inline struct/union layout now computes LP64 field offsets and tail padding. Constant integer globals, string-backed character arrays, `.data`, `.rodata`, `.bss`, data symbols, RIP-relative global loads, and `R_X86_64_PC32` relocations are emitted. Multiple input paths can produce separate objects or an owned temporary-object link attempt.
- Eight authored application fixtures and a deterministic defined-program generator are now part of the local workload suite.

The current native implementation is still a correctness-oriented scalar slice. It does not claim complete C17 conformance, complete C01-C48 coverage, full SSA renaming/out-of-SSA, final register allocation, aggregate expression/ABI classification, mixed integer/SSE or callback ABI support, full hosted variadic behavior, DWARF, PIC, or self-hosting.

## Commands and results

| Command | Result | Evidence |
|---|---|---|
| `make build` | pass on macOS arm64 with Apple Clang/CMake | `build/cindercc` |
| `make test` | pass, CTest smoke | CTest output |
| `make test-frontend` | pass, include/macro/`-D`/interpreter/negative diagnostics | `tests/run_frontend.sh` |
| `make test-globals` | pass, aggregate layout, global data, ELF sections, interpreter | `tests/run_globals.sh` |
| `make test-multi` | pass, separate multi-input ELF objects and unavailable-host link diagnostic on macOS | `tests/run_multi.sh` |
| `make test-native-linux` | pass on GitHub Ubuntu x86-64 workflow: native hello/globals execution, ELF inspection, multi-TU link, and result checks | [workflow run](https://github.com/Aneesh495/cinder/actions/runs/37086568060) |
| `make test-preprocessor` | pass | `tests/run_preprocessor.sh` |
| `make test-ir` | pass, dominators, loop headers, nonvacuous forwarding/dead-code/phi counters, verifier and allocation dump | `tests/run_ir.sh` |
| `make test-parallel-copy` | pass, standalone cycle and phi plan checks | `tests/run_parallel_copy.sh` |
| `make test-float` | pass, decimal literals, float IR/interpreter, SSE2 byte oracle, and ELF output | `tests/run_float.sh` |
| `make test-varargs` | pass, integer variadic fixed-ordinal reads and stack arguments; full `va_list` ABI remains incomplete | `tests/run_varargs.sh` |
| `make test-apps` | pass, eight authored applications | `tests/run_apps.sh` |
| `make test-generated` | pass, 1,200 defined interpreter cases and 100 host-reference executions | `.agent-local/generated-summary.json` |
| `make test-abi` | pass for the declared scalar encoder/stack-frame smoke; cross-toolchain ABI gate unverified | `tests/run_abi.sh` |
| `make test-object` | pass, output recognized as ELF64 relocatable x86-64 | `tests/run_object.sh` |
| `make fuzz` | pass, ASan/UBSan build completed; coverage-guided campaign not run | `build-asan/cindercc` |
| `make selfhost` | incomplete placeholder, not a self-hosting check | `tests/selfhost.sh` |
| `make benchmark` | pass, two local compile timing samples recorded; performance campaign not claimed | `.agent-local/benchmarks/` |
| `make demo` | pass, object/IR/token artifacts generated | `.agent-local/demo/` |
| `make acceptance` | pass, raw smoke evidence and incomplete gate registry generated | `.agent-local/evidence/ACCEPTANCE.json` |
| `make verify` | expected nonzero, incomplete required gates are rejected | terminal output |
| deterministic object check | pass, repeated `phi.c` objects had identical SHA-256 bytes | terminal output |
| altered evidence check | pass, verifier rejected appended evidence bytes | terminal output |

## Acceptance status

The Linux workflow has now independently passed native x86-64 hello/globals execution and multi-TU linking for `82a82f1`; the local macOS manifest still records Linux execution as unverified because its host cannot execute the target. The verifier checks those bindings before evaluating gate outcomes. The current manifest intentionally reports these required gates as unverified or unmet: Linux-native execution on this macOS arm64 host, complete stage 1/2/3 self-hosting, full 20,000-program differential/100,000-IR/50,000-rewrite/500-ABI/10,000-allocation/1,000-object/debug/fuzz/failure campaigns, complete SSA renaming/parallel-copy lowering, aggregate expression and ABI classification, mixed integer/SSE and callback ABI, full hosted `va_list`/`va_copy` support, and the private production census requirement. `make verify` rejects the manifest rather than converting missing workloads into passes.

## Continuation audit and current repairs

The continuation checkout starts from `499914d`. The existing checks do not
establish final compiler acceptance. `make test-apps` had no recipe and did no
work. `make selfhost` only printed a version. `test-debug` only accepted `-g`.
The machine boundary is a stub. Allocation locations reach the encoder, but
the allocator does not compute full CFG liveness or allocate SSE registers. The baseline preprocessor expanded identifiers in literals and lacked
stringification, token pasting, and full conditional expressions. These
preprocessor defects are repaired by the continuation increment below. Required language and evidence families remain incomplete.

Current repair: strict warning-free host builds, removal of dead IR code,
real application-target execution, atomic object/assembly/preprocessing output,
owned linking intermediates, and explicit non-PIE Linux linking. See
`ADRs/0003-atomic-artifact-publication.md` and `diagrams/publication.mmd`.
Validation: `make test-output` and the existing local behavior suite. Native
Linux validation runs in the GitHub workflow after publication.

## Token preprocessing increment

Implemented: preprocessing tokens, immutable token hide sets, argument
prescan, suffix rescanning, stringification, token pasting and placemarkers,
variadic arguments, translation-phase splicing/comments, full conditional
expressions with short-circuit evaluation, canonical includes, pragma-once,
`_Pragma`, `#line`, target macros, deterministic source-date settings, and
source spans linking semantic tokens to expansion/spelling/definition ranges.
`make test-preprocessor` now runs 71 individually authored token/negative
cases against the host C17 preprocessor and source-location checks. The
existing smoke, frontend, object, globals, multi-TU, IR, float, varargs,
application, and output-publication checks pass locally. Full nested macro
backtraces, dependency output, and prefix mapping remain open.

Strict Linux validation for `637406d` caught a GCC misleading-indentation
warning in AST inspection. It was corrected and pushed as `237fb19`.

## Next action

A Linux x86-64 environment is required for native execution, mixed-toolchain ABI checks, and self-hosting. The next implementation increment is full hosted `va_list` cursor/save-area behavior or broader aggregate expression lowering, with native or explicitly unavailable evidence.
