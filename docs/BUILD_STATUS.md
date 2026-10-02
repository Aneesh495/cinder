# Build status

Updated: 2026-10-02

## Repository and publication

- Workspace was empty at initialization and was initialized as Git branch `main`.
- Authenticated GitHub account: `Aneesh495`.
- Published repository: [Aneesh495/cinder](https://github.com/Aneesh495/cinder), public, branch `main`.
- The requested `cinder` owner namespace was not writable from the authenticated account, so no unrelated namespace was modified.
- Last published checkpoint before this explicit-phi increment: `8b0004d`.

## Implemented behavior

- Checked arena/vector storage, source-file identities, source ranges, and structured diagnostics.
- Original preprocessing for object/function macros, `-D`, conditional branches, relative includes, `#error`, and bounded rescanning.
- Longest-match lexer for the implemented C token families with integer literal validation.
- Recursive-descent declarations/statements, precedence expressions, scalar target types, nested scopes, lvalue checks, call/return checks, and loop-context diagnostics.
- Typed CFG-like IR with local memory operations, explicit join phis with incoming edges, terminators, verifier, independent bounded interpreter, constant folding, dead-code elimination, allocation traces/checker, direct x86-64 scalar encoding, ELF64 relocatable output, CLI inspection paths, and a local HTML explorer summary.
- CFG analysis now computes reachable reverse postorder, iterative immediate dominators, and conservative back-edge loop headers. `-O1/-O2` forwards same-block local loads, removes unused pure integer instructions, and reports transformation counters while retaining effectful operations.

The current native implementation is a correctness-oriented scalar slice. It does not claim complete C17 conformance, complete C01-C48 coverage, full SSA phi insertion/out-of-SSA, final register allocation, aggregate/SSE/variadic ABI support, DWARF, PIC, full object data sections, or self-hosting.

## Commands and results

| Command | Result | Evidence |
|---|---|---|
| `make build` | pass on macOS arm64 with Apple Clang/CMake | `build/cindercc` |
| `make test` | pass, CTest smoke | CTest output |
| `make test-frontend` | pass, include/macro/`-D`/interpreter/negative diagnostics | `tests/run_frontend.sh` |
| `make test-preprocessor` | pass | `tests/run_preprocessor.sh` |
| `make test-ir` | pass, dominators, loop headers, nonvacuous forwarding/dead-code counters, verifier and allocation dump | `tests/run_ir.sh` |
| `make test-object` | pass, output recognized as ELF64 relocatable x86-64 | `tests/run_object.sh` |
| `make test-debug` | pass for accepted source/debug flag path; debugger gate unverified | `tests/run_debug.sh` |
| `make fuzz` | pass, ASan/UBSan build completed; campaign not run | `build-asan/cindercc` |
| `make acceptance` | pass, generated raw smoke evidence and incomplete gate registry | `.agent-local/evidence/ACCEPTANCE.json` |
| `make verify` | expected nonzero, incomplete required gates are rejected | terminal output |
| `python3 tools/source_census.py --root . --output .agent-local/source-census.json` | pass, 25 source files and 3,188 substantive production lines | ignored private ledger |
| `build/cindercc --interpret examples/hello.c` | pass, independent result `42` | terminal output |
| `build/cindercc --explorer /tmp/cinder-explorer examples/hello.c` | pass, local HTML report | `/tmp/cinder-explorer/index.html` |

## Acceptance status

`make acceptance` records pass, unverified, and unmet outcomes from actual raw artifacts. The current manifest intentionally reports the following unverified or unmet gates: Linux-native execution on this macOS arm64 host, complete stage 1/2/3 self-hosting, full differential/IR/rewrite/ABI/allocation/object/debug/fuzz/failure campaigns, complete SSA renaming/parallel-copy lowering, and the private 10,000-line production threshold. `make verify` rejects this manifest rather than converting missing workloads into passes.

## Next action

Complete phi-aware out-of-SSA parallel-copy lowering and strengthen the target/type boundary with aggregate layout tests. Refresh acceptance evidence after every target/profile increment.
