# Build status

Updated: 2026-10-07

## Repository and publication

- Workspace was empty at initialization and was initialized as Git branch `main`.
- Authenticated GitHub account: `Aneesh495`.
- Published repository: [Aneesh495/cinder](https://github.com/Aneesh495/cinder), public, branch `main`.
- The requested `cinder` owner namespace was not writable from the authenticated account, so no unrelated namespace was modified.
- Continuation checkpoints are pushed to `main`; current commit and native run are recorded below.

## Implemented behavior

- Checked arena/vector storage, source-file identities, source ranges, and structured diagnostics.
- Original preprocessing for object/function macros, `-D`, conditional branches, relative includes, `#error`, and bounded rescanning.
- Longest-match lexer for the implemented C token families with integer and decimal floating literal validation.
- Recursive-descent declarations/statements, precedence expressions, scalar target types, nested scopes, lvalue checks, call/return checks, and loop-context diagnostics.
- Typed CFG-like IR with local memory operations, explicit join phis with incoming edges, global loads/stores, integer and scalar floating operations, terminators, verifier, independent bounded interpreter, constant folding, effect-aware dead-code elimination, allocation traces/checker, direct x86-64/SSE2 scalar encoding, ELF64 relocatable output, CLI inspection paths, and a local HTML explorer summary.
- CFG analysis computes reachable reverse postorder, iterative immediate dominators, and conservative back-edge loop headers. `-O1/-O2` forwards same-block local loads, removes unused pure integer instructions, and reports transformation counters while retaining effectful operations.
- Inline struct/union layout now computes LP64 field offsets and tail padding. Constant integer globals, string-backed character arrays, `.data`, `.rodata`, `.bss`, data symbols, RIP-relative global loads, and `R_X86_64_PC32` relocations are emitted. Multiple input paths can produce separate objects or an owned temporary-object link attempt.
- Eight authored application fixtures and a deterministic defined-program generator are now part of the local workload suite.

The current native implementation covers scalar integer/float operations, aggregate value transfers, mixed INTEGER/SSE/MEMORY SysV argument lists and returns, and indirect calls. Complete C01-C48 coverage, the full ABI acceptance campaign, DWARF and self-hosting remain open. Hosted variadic state is implemented;
its full native interchange campaign is being validated. Passing incremental suites does not satisfy final acceptance.

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
| `make test-varargs` | pass, integer variadic smoke using authored `stdarg.h`; dynamic state has separate authored and interchange checks | `tests/run_varargs.sh` |
| `make test-apps` | pass, eight authored applications | `tests/run_apps.sh` |
| `make test-generated` | pass, 1,200 defined interpreter cases and 100 host-reference executions | `.agent-local/generated-summary.json` |
| `make test-abi` | pass for the declared scalar encoder/stack-frame smoke; cross-toolchain ABI gate unverified | `tests/run_abi.sh` |
| `make test-object` | pass, output recognized as ELF64 relocatable x86-64 | `tests/run_object.sh` |
| `make fuzz` | pass, ASan/UBSan build completed; coverage-guided campaign not run | `build-asan/cindercc` |
| `make selfhost` | incomplete placeholder, not a self-hosting check | `tests/selfhost.sh` |
| `make benchmark` | pass, two local compile timing samples recorded; performance campaign not claimed | `.agent-local/benchmarks/` |
| `make demo` | pass, object/IR/token artifacts generated | `.agent-local/demo/` |
| `make acceptance` | expected nonzero until all required campaigns have audited reports | `.agent-local/evidence/ACCEPTANCE.json` |
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

## Control-flow increment

The parser and lowering now preserve short-circuit evaluation, conditional
expressions, comma sequencing, prefix/postfix values, all compound assignments,
`for` initialization/step/continue, and `do` loop semantics. Local lowering
restores nested scope bindings. Unsupported address expressions produce an
error instead of a silent copy. Both execution paths skip optimizer tombstones.
Native remainder encoding now returns RDX after signed division, and unsigned
division/remainder use the unsigned instruction. Allocation uses preserved
R12-R15 rather than scratch or incoming argument registers; full liveness and
allocation are still pending.

`make test-control` checks 40 authored programs against an independently
compiled reference, both interpreter optimization levels, and emitted objects.
It additionally links and executes both levels on Linux x86-64. The macOS
results explicitly record `native=false`; the Linux workflow runs the same
cases before acceptance evidence generation.

## CFG allocation increment

Liveness now solves use/def/live-in/live-out bitsets to a fixed point, including
phi uses on incoming edges and loop backedges. Linear scan assigns R12-R15 and
XMM2-XMM7, spills SSE values across calls, and reuses disjoint spill slots. The
encoder consumes these assignments, preserves used callee-saved registers,
snapshots incoming argument registers, and stages outgoing mixed scalar
arguments before ABI moves. Integer and SSE overflow arguments retain source
order on the stack. NaN comparisons account for unordered flags; floating
negation flips the sign bit, including signed zero.

The checker independently reconstructs live sets by backward instruction
transfer. It rejects storage interference, reserved registers, wrong classes,
call clobbers, missing preservation, and invalid spill locations. The allocation
campaign contains 10,000 seeded pressure graphs with branches, backedges,
integer/SSE phis, and calls, plus 1,000 deliberately corrupted allocations.
These are allocation checks, not a claim that all IR semantics or the aggregate
ABI are complete. Source behavior checks now include 52 authored programs.
Native execution of the previous 40-case increment passed in Linux run
[37094039401](https://github.com/Aneesh495/cinder/actions/runs/37094039401).

## Object and verification increment

ELF headers, section headers, symbols, and relocations are serialized in
explicit target byte order. Local symbols precede globals and `.symtab` records
the first global index. Static functions/data retain local binding, function
sizes are recorded, and floating literal symbols remain local to their object.
Narrow global loads sign/zero extend the declared width; stores use that width.
Two-byte initializers occupy two bytes. Assembly output now preserves encoded
text, external relocations, all data sections, symbol binding, and sizes.

The object campaign generates 1,000 two-unit probes. Each is emitted at both
optimization levels, inspected by an independent ELF reader, and independently
assembled from the compiler's assembly output. On Linux, both object paths are
linked and executed. The harness uses an immutable compiler snapshot. A prior
local run overlapping a rebuild is retained only as exploratory evidence and
cannot satisfy a source-frozen acceptance gate.

IR verification now checks unique definitions, dominance, operand/result
classes, reciprocal CFG edges, local storage bounds, and complete phi incoming
edges. Normal compilation verifies both before and after optimization. Void
calls and optimizer tombstones have explicit effect-only handling. Signed
arithmetic in interpretation and constant folding avoids host overflow.

## Numeric token and storage increment

Semantic number parsing now validates digits, ranges, integer suffix order,
LP64 literal ranks, decimal/hex floating syntax, and float32 rounding. The
lexer preserves exponent signs inside preprocessing numbers. Float32 literals
use direct conversion rather than rounding a binary64 intermediate. Source
checks now include 65 positive programs and 17 malformed-number cases that
also require preservation of prior output. The complete C01-C48 registry is
present and records incomplete feature boundaries explicitly.

Arena block growth reserves alignment padding before returning large aligned
objects. A sanitizer probe writes complete allocations across alignments up to
4096 bytes. Linux run
[37096039587](https://github.com/Aneesh495/cinder/actions/runs/37096039587)
passed all 1,000 native object/assembly probes but failed the optimized GCC
sanitizer build on an implicit conditional-expression narrowing. The explicit
byte conversion fix is pushed as `ac9ef00`. Local GCC compiled that sanitizer
profile cleanly; its macOS installation has no ASan link runtime. Clang
provides the local sanitizer execution checks.

## Next action

Validate the numeric increment on Linux, then implement explicit scalar
conversions and addressable storage. Full SSA construction/edge copies, the
remaining language features, true bootstrap, debugging, acceptance integrity,
and required full campaigns remain open.

## Scalar conversion continuation

The numeric checkpoint `c8ecb2a` passed [Linux validation](https://github.com/Aneesh495/cinder/actions/runs/37096716174), including the 1,000-case native object/assembly campaign and sanitizer build.

Current changes add scalar specifier/qualifier handling, explicit casts, integer
promotions and usual arithmetic conversions, typed `IR_CONVERT`, narrow and bool
normalization, unsigned division/comparison/shift selection, float32 arithmetic
and argument/return boundaries, and unevaluated size/alignment queries. Targeted
conversion cases and invalid-constraint cases are being checked on strict GCC
release and Clang sanitizer builds. Native validation of this new checkpoint
is pending publication. See `CONVERSIONS.md`.

## SSA and native phi continuation

Implemented pruned iterated-dominance-frontier promotion, dominator-tree renaming,
explicit undef values, critical-edge splitting, physical edge-copy cycles, and
simultaneous interpreter phi evaluation. Copy propagation and unused phi/undef
cleanup are connected to optimization. The interpreter now shares mutable global
state across calls and returns explicit undefined/resource classifications.

Local strict GCC release and Clang ASan/UBSan validation passed 149 authored
reference/interpreter/object cases, 16 authored undefined-execution cases, and
1,000 generated simultaneous-phi/critical-edge/forced-physical-cycle cases. The
existing 10,000 allocation graphs and 1,000 rejection mutations also pass.
Native execution of this SSA checkpoint is pending publication.

Linux validation of `75cc3f8` found float32 overflow-argument preparation clobbering
a populated SSE argument register. The repaired caller writes overflow stack
arguments before filling ABI registers. Raw control objects and native binaries
are now retained on failure. Linux run
[37162479324](https://github.com/Aneesh495/cinder/actions/runs/37162479324)
passed `4f4a9b0`, including all authored control cases, all forced phi-cycle
native executions, and the complete native object/assembly campaign. This closes
the float32 caller defect.

## Typed IR serialization continuation

Added the original canonical IR writer/parser and standalone `cinderir` tool.
Verification now checks scalar widths and complete call/storage/return contracts.
The typed IR campaign executes 100,000 cases through original interpretation,
exact round trips, parsed interpretation, and O2 optimization. Local strict GCC
release and Clang ASan/UBSan runs each passed: 97,501 defined outcomes and 2,499
explicit undefined/resource outcomes. Coverage inventory caught a correlated
generator condition excluding uninitialized joins; the generator now exercises
those outcomes and the harness requires their presence.

The campaign exposed narrow signed minimum remainder folding into a defined
constant. Folding now checks the actual operand width for both division and
remainder; an authored undefined-execution regression retains the defect.
Source round trips and malformed-input mutations are being validated before
this checkpoint is published. Final acceptance, object memory, full frontend,
aggregate ABI, bootstrap, debugging, fuzzing, and full workload gates remain open.

Linux run [37164699489](https://github.com/Aneesh495/cinder/actions/runs/37164699489)
passed `4ea91ab`, including the complete typed IR campaign, native source round
trips, phi-cycle executions, and native object/assembly probes.

## Declaration continuation

Added scoped typedef/enumerator/tag bindings, stable aggregate identities,
forward completion, nested and abstract declarators, unnamed prototypes,
comma-separated declarations, and target-aware integer constant evaluation.
Later global declarations cannot retroactively resolve an undeclared use.
Floating globals now emit actual float/double data and SSE loads/stores; integer
constant conversion to floating globals respects signedness and float32 rounding.

Local strict GCC release and Clang ASan/UBSan checks passed 185 authored positive
source programs, 43 invalid-constraint cases, and 370 exact source IR round trips
with 36 malformed-IR rejection mutations. The complete typed IR campaign was
rerun after the type identity format change. Native validation of this increment
is pending publication. Addressable storage is the next implementation boundary;
the full acceptance gates remain incomplete.

Linux run [37166199680](https://github.com/Aneesh495/cinder/actions/runs/37166199680)
passed `79cc4de`, including all 185 native source cases, 370 direct/parsed IR
native checks, the full typed IR campaign, and native object probes.

## Acceptance integrity continuation

Removed verify's build/census/compiler-execution side effects and the legacy
empty-gate/incomplete-manifest success path. The exact full registry now guards
verification, with source bytes, generated configurations, compiler bytes, and
artifact hashes bound separately. Deleted/changed artifacts, altered compiler,
uncommitted/new source, configuration changes, corrupted bootstrap binding, and
truncated/vacuous manifests are rejected by integrity tests.

Ordinary CI records partial incremental validation separately. Native environment
flags and summary reuse no longer create acceptance passes. Full runners and
dedicated raw-report readers remain open and acceptance fails explicitly until
they exist. This is an integrity guard, not completed full acceptance. Pointer
and object memory implementation continues next.

## Pointer and object memory continuation

Added explicit array decay, subscripts, member selection, addresses, typed
scalar loads/stores, pointer offsets/differences, and object comparisons.
Addressed locals stay out of SSA promotion. Native stack objects use declared
byte widths and target extents. The interpreter owns byte objects with pointer
provenance, initialization, subobject bounds, alignment/access checks, and
block/loop lifetimes. `cinderir --classify` exposes the actual outcome as JSON.

The authored source corpus now includes arrays, nested members, scalar aliases,
pointer parameters/returns, and branch/loop scope exits. Undefined memory
cases are classified without native execution. Incremental tests and hosted
publication validate each checkpoint; aggregate values, complete
initializers, hosted runtime, and the full acceptance campaigns remain open.

## Indirect calls and callbacks

Function designators now lower to explicit function addresses. Indirect calls
use their typed signatures, preserve the target while staging arguments, and
emit `call r11`. The interpreter retains function identity through pointers,
arrays, fields, parameters, returns, and phis; null calls and incompatible
target signatures produce classified invalid access.

The source suite adds callbacks with integer/SSE overflow arguments, mixed
arguments, recursion, pointer results, and context objects. GCC and Clang
sanitizer builds check source execution and canonical IR round trips. The ABI
boundary test uses independent host assembly to poison unspecified high bits
of `_Bool` arguments and results. Linux runs compare both optimization levels
with GCC and Clang. Full aggregate and variadic callback ABI coverage remains
an open acceptance requirement.

## Literal storage and character arrays

Decoded ordinary/UTF-8 literals now preserve escapes and embedded NULs,
concatenate after macro expansion, retain array type for `sizeof`/addresses,
and emit private read-only storage. Character array initialization supports
inferred/exact bounds, zero padding, const elements, and loop lifetimes.
Explicit object transfer IR reaches the owned-memory interpreter and encoder.
The authored source suite includes 51 literal programs, malformed encoding and
profile checks, and independent object-contract mutations. See `LITERALS.md`.
Initializer lists, designators, compound literals, static addresses, and the
remaining language/ABI acceptance work are still incomplete.

## Declaration coalescing and source order

Extern/tentative/initialized file declarations now produce one owned symbol,
with compatible composite types, inherited static function linkage, and
end-of-unit completion for external tentative arrays. Incomplete array typedefs
instantiate separate object bounds. Semantic layout checks retain source order
instead of accepting an earlier use because a tag was defined later.
Authored source constraints and actual ELF binding/extent checks cover the
increment. Block-scope static/extern storage and static address initializers
still need their duration/linkage and relocation implementation.

## Static address relocations

Named object/function addresses, string pointers, array/member offsets, casts,
and null initialization now reach typed address records, owned ELF data
relocations, equivalent assembly, and independent global pointer initialization.
Canonical IR schema 2 retains domains and actual target signatures. Tests
inspect writable/read-only relocations and link two owned translation units.
Unsigned pointer indexes retain their signedness rather than being narrowed to
signed long. General initializer lists and block static/extern storage remain
open, along with the remaining ABI, runtime, debug, and acceptance requirements.

## Scalar constant initialization

Static arithmetic initialization now uses target conversions for integer,
boolean, float, and double values. It evaluates floating arithmetic/comparison
expressions, casts, conditional common types, and short-circuit operations.
Binary32 rounds at each typed operation; integer conversion bounds are checked.
Boolean object and function addresses fold without using a host address.
Integer constant expression rules remain separate for enum/array requirements.
The source suite adds 36 individually authored conversion and rounding cases.

## Subobject initialization

Nested brace lists, array/member designators, brace elision, inferred bounds,
and omitted-element zeroing now lower into explicit typed initialization
operations. Scalar, string, pointer, callback, and local aggregate-copy
initializers reach the independent object interpreter and original x86 encoder.
Repeated subarray/struct initializers clear omitted elements from earlier
initializers; overlapping global writes remove stale pointer relocations.
Const initialization has separate effects from subsequent ordinary stores.
Authored source cases cover these paths, invalid initializer constraints,
uninitialized self-reads, expired pointers, and writes to const subobjects.
Canonical IR mutations check initializer operand, effect, and extent contracts.
General aggregate expression results and argument/return ABI remain open.
The prior scalar constant checkpoint passed native Linux validation in run
`37407723011`. The subobject initializer checkpoint `96321b4` passed
[native Linux validation](https://github.com/Aneesh495/cinder/actions/runs/37550970670),
including owned object/assembly execution, scalar callback ABI checks, the
1,000-case multi-unit object campaign, sanitizers, and evidence integrity checks.

The GCC 15 macOS reference rejected a repeated implicit address-to-boolean
initializer in one aggregate although its single-scalar form and explicit
conversion compiled. Clang compiled all forms. The minimized sources and raw
reference outcomes are retained under `.agent-local/boolean-list-reference`.
The equality fixture uses an explicit `_Bool` conversion for the repeated
address; implicit conversion is still covered by separate scalar and member
cases. This reference difference is not counted as a Cinder divergence or a
successful cross-reference execution.

Assembly emission now orders a private copy of section fixups, so valid
out-of-order designated pointer initializers remain representable. The focused
initializer runner compares actual encoded text/data/readonly bytes and resolved
local section/value relocation targets against independently assembled output.
It retains named external/global symbol references instead of normalizing them
away. Reference compilation is bounded to four independent case workers.
Text assembly uses explicit ELF relocation directives and zero displacement
fields, preserving references that an assembler could otherwise resolve early
within the text section. This keeps the emitted assembly tied to the original
machine object's relocation contract, including local callbacks.

Two aggregate-copy/partial-designator cases retain GCC's different results.
[WG14 issue 0413](https://open-std.org/JTC1/SC22/WG14/issues/c11c17/issue0413.html),
fixed in C17, states that implicit zeroing preserves prior explicit
initialization. Cinder and Clang retain the copied sibling fields; GCC zeroes
them in the observed versions. `tests/reference_deviations.json` binds each
adjudication to the exact source hash, reference family, standard result, and
observed reference result. Changed inputs and unknown outcomes still fail.
Raw executions remain in the reports, and these cases cannot contribute to a
GCC/Clang agreement count. Cinder's interpreter and native outputs must always
match the standard result. This adjudication does not satisfy the full
differential acceptance campaign.

## Aggregate expression continuation

Struct/union assignment, chained assignment, selected conditional results,
comma results, aggregate copy initialization from these results, and member
reads now use explicit typed object transfers. Completed expression snapshots
are immutable in the interpreter and expire at the enclosing full expression.
Source assignment checks reject aggregates containing const subobjects while
allowing ordinary members that point to const storage.

Strict GCC and Clang sanitizer builds passed. Local validation passed 514
authored reference/interpreter/object cases, 1,028 canonical IR/backend
comparisons, 108 source rejections, 56 undefined-memory classifications, 31
malformed memory/lifetime instruction mutations, 100,000 typed IR cases, and
1,000 simultaneous-phi/critical-edge cases. The focused aggregate runner checks
28 authored inputs and independently assembled bytes/relocations. Three inputs
have storage-identity-sensitive GCC/Clang differences permitted by the C17
temporary-address correction; their raw results are retained, and they remain
ineligible for reference equality counts. The remaining 25 agree with both
references at both optimization levels. These local runs emit Linux target
objects but cannot execute them on macOS arm64.

The aggregate expression increment requires native validation after publication.
Aggregate argument/return classification, hosted variadics, the remaining
language families, debugging, self-hosting, full applications, and final
acceptance remain open. The next implementation step is complete System V
aggregate value transfer through calls and returns.

Linux run `37551883978` executed all 514 source observations and 1,028
IR/native comparisons successfully, then rejected a new reference observation:
Clang 18 returned 18 for `comma_snapshot.c`, while local newer Clang returned
14. The retained storage-identity probe remains ineligible for equality counts;
its policy now records the same exact aliasing outcome for both reference
families. This is a reference-policy correction, with the original failure
retained. The policy correction passed the full native workflow in run
`37552433250` for `41670e1`.

## Aggregate calling convention continuation

The original classifier and encoder now implement fixed struct/union arguments
and results, including mixed integer/SSE eightbytes, whole-argument rollback,
stack copies, and hidden result pointers. The interpreter copies parameters
into independent objects and transfers returns before ending callee lifetimes.
The verifier checks both call signatures, aggregate storage, and return effects.

Strict GCC and Clang sanitizer builds passed. Current checks passed 538 authored
source observations, 1,076 canonical IR/backend comparisons, 112 source
rejections, 60 memory classifications, and 42 malformed storage/call contracts.
The 24 new defined ABI inputs agree with GCC and Clang at both optimization
levels; owned objects match independently assembled bytes and relocations.
All 48 owned ABI objects also executed with the expected results in the scoped
x86-64 Linux VM. The initial 32 generated aggregate signatures passed local
GCC/Clang O0/O2 reference execution and owned object generation in both call
directions. A larger native campaign and the hosted Linux workflow remain
required before claiming this checkpoint fully validated.

The IR campaign rejected its recursive-call family because its builder put a
scalar type in the new actual-signature field. Both recursive calls now carry
their actual function type; the verifier contract remains strict. The rejected
run is retained. After the repair, all 100,000 typed IR round trips and
optimizer comparisons passed, with 97,501 defined executions and 2,499 separately
classified outcomes. The 1,000 SSA/critical-edge cases also passed.

Hosted variadic state, remaining language families, self-hosting, debugging,
the full applications, performance thresholds, and final acceptance remain open.

## Dynamic variadic state increment

The aggregate ABI checkpoint passed all native Linux checks in
[run 37554013720](https://github.com/Aneesh495/cinder/actions/runs/37554013720).
Variadic lowering now uses runtime cursor state, register-save storage, separate
GP/SSE offsets, whole-aggregate rollback, overflow stack arguments, `va_copy`,
and `va_end`. Authored `stdarg.h` is supplied by the compiler. The independent
VM tracks argument types and cursor lifetime in source order.

Local sanitizer checks passed 557 authored source observations and 1,114 IR
round trips. The focused variadic corpus and generated GCC/Clang interchange
checks retain their raw observations under `.agent-local`. Full native runs
and audited acceptance remain required; no complete-project claim is made.

Linux run [37555550311](https://github.com/Aneesh495/cinder/actions/runs/37555550311)
passed the complete frontend, native source, IR, and variadic UB checks, then
exposed an allocation probe with a missing function signature. The allocator
and independent checker now diagnose that malformed contract before touching
frame metadata. The 10,000-graph probe supplies real function signatures,
includes both variadic and fixed frames, and checks the missing-signature
rejection explicitly. The failed run's raw artifacts are retained locally.

## Compound literal storage increment

The repaired variadic checkpoint passed full Linux validation in
[run 37561808498](https://github.com/Aneesh495/cinder/actions/runs/37561808498),
including 512 variadic signatures, callbacks, and both-direction `va_list`
interchange with GCC and Clang. The new compound literal path uses typed
initializer plans, local ELF storage and relocations at file scope, and
explicit C block owners for automatic literals. Selection/iteration statements
and unbraced substatements preserve their declared scope and lifetime.

Focused GCC/Clang checks passed all 36 authored compound literal sources.
Both optimization levels passed native x86 Linux execution for all 72 objects.
Clang ASan/UBSan passed 593 source observations, 1,186 IR round trips, 127
source rejections, 79 classified UB cases, and 57 malformed memory contracts.
The 100,000-case IR campaign and 1,000 SSA/copy checks also passed.
Escaped block, body, condition, loop, and return addresses are separate VM
lifetime cases. The published checkpoint also passed full Linux validation in
[run 37600909822](https://github.com/Aneesh495/cinder/actions/runs/37600909822).
Goto scope entry and final family acceptance remain open.

## Initializer shape and constant-expression checks

Unknown array initializer shapes now complete at their definition point.
The shared subobject cursor preserves brace elision, nested designators,
aggregate copy values, and character string bounds. Compound literal shapes
are available to parser-time array bounds and enumerators without executing
initializer values. Retained constant expressions receive full semantic
checking with source-point bindings and their enclosing function context.
Their typed constant values must match the parser's recorded values.

Automatic literals used only by unevaluated operands reserve no runtime
storage. Large nested `sizeof` operands now compile and execute without
allocating their literal payloads on the stack. Value queries account for
array/function decay, comma and conditional expressions, subscripting,
member access, pointer arithmetic, and unary address/dereference operations.

GCC 15 Release and Clang ASan/UBSan builds passed all 64 authored compound
literal cases against GCC/Clang at O0/O2, with exact object/assembly bytes
and relocations. All 128 owned objects passed native x86 Linux execution
in the development VM. Both builds rejected all 140 constraint fixtures.
The sanitizer IR campaign passed 100,000 round trips and optimized execution
comparisons, with 97,501 defined cases and 2,499 independently classified
cases. Raw records are under `.agent-local/compound_literals`,
`.agent-local/constraints`, `.agent-local/ir-campaign`, and
`.agent-local/linux-vm/share/type-completion-final`. Clang ASan/UBSan also passed all 1,242 source IR round trips and 36 malformed
IR rejection mutations. Full acceptance remains incomplete.

Hosted run 37695187093 passed all 1,242 native IR round trips, the 100,000-case
IR campaign, and 1,000 SSA/copy cases, then failed a GCC 13 reference program
on the large unevaluated literal. Its stack page probes exhausted the default
8 MiB runtime stack. The retained original binary and an independent GCC 15
reproduction establish failure at 8 MiB and success at 64 MiB. Authored native
runners now declare and record the same 64 MiB stack budget for GCC, Clang,
and Cinder executables. See `docs/NATIVE_TEST_PROFILE.md`. Subsequent full
hosted validation is pending publication of this process-profile repair.

## Static assertion declarations

`_Static_assert` declarations now work at file, block, and aggregate member
scope, reuse the typed target constant evaluator, and retain their operands
for full semantic validation. Failed assertions include decoded, concatenated
message text; embedded NUL bytes are escaped so later text remains visible.
They create no member or runtime effect. Declarations used as unbraced
associated statements and aggregates containing no object member are rejected.

Both GCC Release and Clang ASan/UBSan builds passed 18 authored assertion
programs against GCC/Clang at O0/O2 with exact object/assembly comparisons.
All 36 owned objects passed actual x86 Linux execution. The sanitizer build
passed 1,278 full source IR round trips and 36 malformed IR rejections. All
153 invalid source cases were rejected, including required assertion messages
and preservation of prior artifacts. Raw assertion records are under
`.agent-local/static_assertions` and
`.agent-local/linux-vm/share/static-assertions`.

The repaired stack-profile run 37695954175 passed all 621 native source
observations and 1,242 native CIR comparisons, then stopped because GCC 13
accepted `va_start` in an unevaluated nonvariadic operand. This library
Description rule does not require a host diagnostic. The exact source is
now adjudicated in the diagnostic policy; Cinder rejection remains mandatory.
Every non-mandatory reference diagnostic entry now has a checked source hash.
A changed-source probe failed that check. The original failed artifacts
remain under `.agent-local/native-failure-11d2fd8`. Whole hosted validation
will rerun after publication. Full acceptance remains incomplete.

## Generic selection

`_Generic` now applies the C17 control conversions without integer
promotion, checks every association, and preserves the selected expression's
type and value category. Lowering, constant evaluation, initializer shape
queries, and static relocations evaluate only the selected expression.
Unevaluated control and unselected literals reserve no automatic storage.
The target's signed 32-bit enumeration types are compatible with `int`.

GCC Release and Clang ASan/UBSan both passed 48 authored programs against
GCC/Clang at O0/O2 with exact object/assembly comparisons and all 168 invalid
source rejections. All 96 owned objects passed native x86 Linux execution.
The sanitizer IR campaign passed 100,000 comparisons, with 97,501 defined
and 2,499 independently classified cases. Raw records are under
`.agent-local/generic`, `.agent-local/constraints`, `.agent-local/ir-campaign`,
and `.agent-local/linux-vm/share/generic`.
The sanitizer build also passed all 1,374 source CIR round trips/backend
comparisons and 36 malformed CIR mutations. Nested generic probes compiled
at depth 64 and rejected depths 129 and 1,024 with diagnostics, preserving
prior output and reporting no sanitizer failure.

The prior hosted run 37696602274 passed 639 native source observations,
1,278 native CIR comparisons, and the IR and SSA campaigns, then Clang 18
rejected a block enumeration using an unevaluated compound literal. The
declared hosted reference is now Clang 22, which passed the unchanged
regressions with GCC on the Linux development VM. Original failed evidence
is retained. See `docs/NATIVE_TEST_PROFILE.md`; full hosted validation will
run after publication. Full acceptance remains incomplete.


## Alignment and hosted validation

The generic-selection checkpoint `bf7c7f4` passed the complete hosted Linux
workflow [37698288413](https://github.com/Aneesh495/cinder/actions/runs/37698288413).
Its raw records include 1,374 native CIR comparisons, all 48 generic cases,
and 512 signatures in each scalar, aggregate, and variadic callback campaign.

Object and member alignment now reaches parser constraints, target aggregate
layout, local frame checks, global offsets, ELF section alignment, and CIR
schema 3. GCC Release and Clang ASan/UBSan passed 33 authored programs against
both reference compilers at O0/O2 and all 190 constraint rejections. All 66
owned alignment objects passed x86 Linux execution in the development VM.
The sanitizer build passed 1,440 source CIR comparisons, 36 malformed CIR
mutations, 100,000 generated IR comparisons, 1,000 SSA cases, and the
10,000-graph allocation campaign with 1,000 corrupt allocations.

Aligned aggregate interchange passed 32 generated signatures and 256 native
executions with GCC and Clang in both directions. Variadic aligned interchange also passed 32 signatures and 256 native
executions in both directions. The initial aligned aggregate attempt exposed
an invalid generated oracle: an out-of-range plain-char return was compared
against its unconverted integer. The generator now keeps byte-valued results
representable. The failed original input and observations are retained under
`.agent-local/abi-aggregates`; no equality checks or campaign sizes were reduced.
See `docs/ALIGNMENT.md`. Full acceptance remains incomplete.


Hosted alignment run [37701357316](https://github.com/Aneesh495/cinder/actions/runs/37701357316)
passed all 720 native source cases, 1,440 native CIR comparisons, the full IR
and SSA campaigns, and 10,000 allocation graphs. It then exposed a standalone
parallel-copy probe missing the new type/alignment helper from its link inputs.
The probe now links that authored helper explicitly. The compiler's normal
build had already linked it correctly. Full hosted validation will rerun.
