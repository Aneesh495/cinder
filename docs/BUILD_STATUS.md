# Build status

Updated: 2026-10-16

## Real application corpus and evidence reader

Eight applications now implement JSON parsing, dynamic word frequency, graph
algorithms, a bytecode VM, stable sort/search, binary checksums, blocked matrix
multiplication and a CSV CLI. Three use multiple translation units. Forty
frozen inputs separate twenty-five valid cases from fifteen expected input
rejections; output files and preservation of prior output are checked as well
as stdout, stderr and status.

Both strict GCC and ASan/UBSan development compilers emit all twenty-two
original unit/mode objects. Each host run passes 160 GCC/Clang O0/O2 reference
executions and forty Clang ASan/UBSan reference executions. Independent Python
oracles reproduce every frozen fixture. Twelve raw-proof corruption controls
and five gate-envelope controls pass. Cross/emulated reports cannot satisfy
the native application gate.

The native workload compiles complete applications through the normal driver,
retains object-only system linker argv and compares its objects with standalone
original emission. The new gate reader reconstructs every command, input,
object, executable, reference result and output file rather than trusting
summary counters. This reader is connected to acceptance and both bootstrap
stages. Native publication validation for this increment is pending. The full
differential, fuzz, failure-recovery, performance and remaining acceptance
campaigns remain incomplete.

## Offline compilation explorer

`--explorer DIR` now publishes actual source/provenance, tokens, typed AST/HIR,
verified IR, selected machine programs, physical allocation, CFG liveness,
generated CFG diagrams, pass differences, assembly, relocations and diagnostics
in one offline HTML file. Source and CFG links activate their destination
panes. Pass snapshots are downloadable in full. The encoder tracks source PCs
without requiring debug sections or changing ordinary object bytes.

Strict GCC and ASan/UBSan builds pass eight report profiles plus owned-trace,
failed-input and publication tests. Captured stages, complete pass snapshots
and assembly match standalone artifacts. Independent ELF/disassembler checks
validate every reported byte range. Both references execute twelve cases and
the IR interpreter executes six. Browser checks verify SVG/source links,
search, keyboard tabs and a clean console; actual screenshots and provenance
are included in [EXPLORER.md](EXPLORER.md).

Both frozen builds also pass driver/output/pass-pipeline checks, all six debug
suites and selected control/ABI-program regressions. Every one of sixty-one
production source files compiles through Cinder. The report implementation uses
the existing atomic output boundary to reject non-directory destinations; it
does not require an undeclared host-specific `struct stat` layout. The
Cinder-built emulated compiler passes all six debug suites, forty actual GDB
sessions, all Explorer profiles and driver/output/pass-pipeline checks. Its
independent audit binds all sixty-one original source objects and the raw
results. Native ordinary validation and deterministic bootstrap both passed
for Explorer and for the subsequent integer-format header fix.

## Complete selected ABI programs

Argument entry, calls, aggregate returns and variadic protocols now own
concrete byte programs before allocation. Seven roles contain explicit GPR/SSE
value transfers, fixed-width frame holes and owned call relocations. Selection
handles argument staging, mixed-class packing, register exhaustion, hidden
return storage, binary32 conversions and variadic cursor paths. The encoder
replays programs using verified physical locations. Its raw IR instruction
switch has been removed; source-type queries retained for debug metadata do
not choose target instructions.

Strict GCC and ASan/UBSan builds pass forty-five source programs covering all
seven roles, five step kinds and four frame-patch kinds. Their 272 programs,
1,023 steps, 280 frame patches and 63 call fixups reject 9,995 altered records
before output mutation. Restored plans match normal objects. An independent
reader reconstructs complete allocated program sequences, checks contiguous
function bytes, actual call targets and disassembler boundaries, and rejects
zeroed code, wrong roles and wrong call targets. Ninety O0/O2 objects match the
preceding compiler and LLVM-assembled code/relocations; GCC/Clang and IR
observations agree for every program.

Both builds pass the eight preceding machine suites, all six debug suites and
1,000 forced physical phi-cycle cases. The GCC build also passes all 1,239
source cases and 2,478 CIR/backend comparisons plus 36 rejection mutations.
All sixty-one actual production modules compile through Cinder. The Cinder-built emulated Linux compiler passes all twelve machine/debug
suites, forty actual GDB sessions and eighteen runtime probes. Independent
audits bind all sixty-one original objects and fifty-nine core objects to the
raw results. The ABI-program suite passes ninety original target executions,
180 reference executions and 9,995 altered-plan rejections. Its initial outer
harness deadline expired; the unchanged complete workload passed with a
longer deadline, and both attempts remain recorded. Native ordinary validation
and the complete deterministic bootstrap also passed. Explorer and the realistic application corpus were completed in the
subsequent increments above. The remaining acceptance campaigns are open.

## Selected storage addresses and bitfield roles

Memory instructions now select frame, RIP-relative symbol or indirect
addressing, explicit load/store direction, address/value IDs, frame slots,
ModRM bytes and symbol relocation contracts. Fixed-register and scratch masks
reflect staged operands, addressing and boolean-load flag effects. Bitfield
load/store/normalization and conversions dispatch by selected role. Their raw
IR cases have been removed from the encoder.

Strict GCC and ASan/UBSan builds pass fifteen source units, twelve scalar
storage profiles and all three bitfield roles. The API probe rejects 1,760
alterations across 87 memory and 23 bitfield instructions before output
mutation. Counts match actual O0 MIR instructions. Thirty O0/O2 objects match
the preceding compiler byte for byte and agree with LLVM-assembled sections
and relocations. Sixty GCC/Clang executions and thirty IR runs match expected
source results. The address reader now selects its machine kind explicitly,
so memory base values are not mistaken for address-producing operations.

Both builds pass the seven preceding machine suites, all six debug suites and
1,000 forced phi-cycle cases. All sixty actual production modules compile
through Cinder. The Cinder-built emulated Linux compiler passes all eleven machine/debug
suites, forty actual GDB sessions and eighteen runtime probes. Independent
audits bind its sixty original source objects, fifty-eight-object probe core,
selected plans, raw debugger values and original/assembled/CIR executions.
These are functional emulation results. Native ordinary validation and the
complete deterministic bootstrap both passed. ABI program selection was
completed in the subsequent increment above; the remaining acceptance
campaigns are still open.

## Selected value materialization and bulk memory

Integer/floating constants, undefined-value placeholders, GPR/SSE copies and
floating sign changes now own selected result banks, literal bits, byte
programs and floating relocation contracts. Zeroing, object initialization,
object assignment and `va_copy` own explicit extents and zero/copy programs.
Lifetime, phi, no-op and variadic-end markers explicitly emit no bytes. The
encoder consumes these plans instead of expanding their raw IR operations.

Strict GCC and ASan/UBSan builds pass seven value roles, both bulk roles and
4,905 altered-plan rejections across 193 materializations, five bulk operations
and 129 markers. Each rejection is checked by the verifier and by encoding
before output mutation. Restored plans match normal objects. Four O0/O2
objects match the preceding compiler byte for byte; independent readers check
text, floating constant-pool bits and PC-relative fixups against selected plans
and LLVM assembly. GCC/Clang and IR runs cover both defined source programs,
including negative zero, copies and independent overflowing variadic cursors.

Both builds pass the six preceding machine suites, all six debug suites and
1,000 forced phi-cycle cases. The compiler emits all sixty actual production
source objects. The frozen compiler also passes all 1,239 source cases and 2,478 CIR/backend
comparisons plus 36 malformed-input rejections. The Cinder-built emulated Linux
compiler passes ten machine/debug suites, forty actual GDB sessions and all
eighteen runtime probes. Independent audits bind all original source/core
objects, raw debugger values, 72 reference executions and 108 original,
assembled or CIR-derived runtime executions. These are functional emulation
results. Native ordinary validation and deterministic bootstrap passed. Their raw
artifacts were independently rehashed and replayed: sixty actual compiler
modules, matching stage-2/stage-3 objects and executables, 1,000 eligible
generated programs, 8,000 native executions, 4,000 IR executions and eighty
actual GDB sessions across both stages. These are historical source-bound
results. Storage roles were completed
in the subsequent increment above; argument/call/aggregate/variadic target
protocols remain open. The remaining acceptance campaigns are still open.

## Allocated phi transfer schedules

Physical phi copies are now scheduled after allocation and owned by selected
predecessor blocks. The encoder consumes value/bank moves without inspecting
phis or invoking the resolver. A separate checker simulates physical storage
tokens and cycle temporaries, then requires the original simultaneous phi
results. It accepts valid alternative schedules and rejects lost live values,
bad endpoints/banks, stale plans and inconsistent accounting before encoding.

Conditional edges with single-predecessor phi targets are now split as well as
critical edges. Branches whose two arms share a phi target also receive the
required transfer. The direct IR regressions force distinct physical storage
so omitted transfers cannot pass through accidental register coalescing.

Strict GCC and ASan/UBSan builds pass 36 integer/binary32/binary64 cyclic
programs, four conditional-edge regressions and 360 altered schedule checks.
All 36 cyclic objects match the preceding API probe byte for byte. Eight
ordinary O0/O2 control objects also match the preceding compiler. Existing
selected-form, ABI, scalar, address and control/frame suites pass both builds.
The deterministic native bootstrap passed. Ordinary native validation exposed
an allocator stress fixture that skipped the required conditional phi-edge
split. The fixture now runs the MIR boundary and frees its generated edge
names. Both strict GCC and ASan/UBSan checks pass all 10,000 graphs and 1,000
rejected mutations against the published compiler sources. The full ordinary native rerun and deterministic bootstrap both passed after
this fixture fix. Constant and bulk-memory selection were completed in the
subsequent increment above. Argument/call and variadic target programs remain
open.

Compiling the API probe with Cinder exposed a missing `strtoul` declaration in
the authored runtime header. The declaration and a native libc boundary
fixture now cover the exact function-pointer type, bases, end pointers and
unsigned result limits. Both host builds pass all eighteen runtime probes;
external libc execution remains explicitly unsupported by the IR interpreter.
The Cinder-built emulated Linux compiler also passes all nine machine/debug
suites, forty GDB sessions and eighteen runtime probes. An independent audit
binds its sixty original source objects, fifty-eight-object API probe core,
selected schedules and raw debugger results. These are functional emulation
results.

## Selected control and physical frame programs

Branches, traps and scalar/void/SSE returns now own verified machine programs.
The encoder consumes selected comparison, narrowing and near-branch bytes,
then checks final signed rel32 displacements. After allocation, a physical
frame plan owns entry/exit bytes, R12-R15 preservation programs, incoming
register homes and the variadic area. Unwind events follow actual emitted PCs.
Physical phi-copy scheduling was completed in the subsequent increment above.
Other remaining target pseudos are still open.

Strict GCC and ASan/UBSan builds pass all four control roles, fourteen physical
frames and 1,212 malformed control/frame rejections before output mutation.
Independent readers check actual save/restore bytes and every near-branch
target against instruction boundaries. Three reference programs cover multiple
returns, integer/float/void results, cyclic SSA, recursion, variadic register
exhaustion, real spills and a 16 KiB local. Restored plans match normal objects;
eight O0/O2 ordinary objects match the preceding compiler byte for byte.
Native ordinary validation and deterministic bootstrap passed after the
forced-layout fixture was updated to regenerate its physical frame plan.

The final development compiler also passes all 1,239 source cases and 2,478 CIR
round trips with 36 malformed-input rejections. Both builds pass 1,000 forced
physical phi-cycle cases and 10,000 independent allocation checks. The
Cinder-built emulated Linux compiler passes all eight machine/debug suites,
including 40 actual GDB sessions, the address suite and 1,212 control/frame
mutations. Independent audits bind its 60 original source objects, 58-object
probe core and all retained artifacts. This is functional emulation evidence.

The preceding initialized-local revision passed native ordinary validation and
the complete deterministic bootstrap. Downloaded raw evidence was independently
rehashed and replayed: sixty compiler modules, matching stage-2/stage-3 objects
and executables, 1,000 eligible generated programs, and forty actual GDB
sessions on each stage. These are historical source-bound results. Final
acceptance still requires the remaining implementation and complete campaigns.

## Selected pointer and address plans

Pointer arithmetic and local/global/function/member address formation now use
selected MIR byte programs and actual fixed-register/clobber contracts. The
encoder resolves allocated frame offsets and emits selected programs and
relocations. Raw pointer expansion cases have been removed from its IR switch.
Control/frame selection was completed in the subsequent increment above.

Strict GCC and ASan/UBSan builds pass five address roles and six typed origins,
928 plan rejections, original/assembled identity and three independent reference
programs. Ordinary objects match the preceding compiler at O0/O2. Both builds
also pass the existing 43-form, 64-source ABI and 142-source scalar suites,
including all existing plan mutations and restored object identity. All 60
current compiler modules compile through Cinder into original ordinary objects.
The frozen compiler passes all 1,239 source cases and 2,478 CIR round trips
plus 36 malformed-input rejections. The Cinder-built emulated Linux compiler
passes the address suite, both mixed-toolchain call directions and all six
debug suites with 40 actual GDB sessions. An independent audit binds its source
objects, probe core, artifacts and reported values. Native ordinary validation
and the complete deterministic bootstrap for this address revision both passed.

## Initialized scalar debug locations

The O0 debug profile preserves named scalar local storage. Locations derive
from actual allocated CFA homes, reachable predecessor intersection, retained
store-end PCs and lifetime resets. Uninitialized intervals, partial joins,
promoted O2 values and addressed/aggregate locals remain unavailable. The
ordinary optimization profile is unchanged.

Strict GCC and ASan/UBSan builds pass all six debug suites. The new value suite
checks eight initialization/join/loop/pointer boundaries, actual store bytes,
reference/interpreter behavior and five deliberately false availability
records. The source API probe now rejects 48 malformed maps plus repeated
finalization. Frame and source readers retain O2 debug/plain text equality
and O0 data/rodata/BSS layout equality. Independent baseline checks also
compare complete ordinary objects with the preceding compiler.

All 60 current compiler modules compile through Cinder as original ordinary
objects. The owned emulated Linux compiler passes all six debug suites and 40 actual
GDB sessions. An independent audit binds all ordinary source objects, the
58-object probe core, debug artifacts and recorded current/unavailable values.
Native value checks and deterministic bootstrap subsequently passed, including
forty actual debugger sessions on each of stages 2 and 3. Final native
acceptance and performance still require the full source-bound campaigns.

## Owned lexical scope increment

Automatic-object declarations and nested scope identities now survive AST/HIR
retirement in IR-owned metadata. Every encoded operation has an actual byte
range. Original DWARF4 lexical blocks and `.debug_ranges` describe declaration
suffixes, nested shadows and loop scopes. Local values are explicitly
unavailable pending initialized-storage tracking; no reserved slot is exposed
as a current value.

Strict GCC and ASan/UBSan builds pass the independent O0/O2 scope, type, line,
frame and parameter readers. The scope reader checks six shadow/loop boundaries
and rejects four semantic corruptions. The source API probe now rejects 43
malformed maps plus repeated finalization before mutating output. All 60
production modules compile into original debug objects through Cinder and pass
LLVM DWARF verification without warnings. The emulated Linux compiler passes
all five debug suites, including 26 actual GDB sessions; a second scope run
also requires an actual step into the callee and its returned value. Full
1,239-case source and 2,478-round-trip CIR campaigns pass on the frozen host
compiler. The native ordinary and deterministic bootstrap runs for the scope increment
both passed.

## Stack parameter verification increment

The debug parameter suite checks a seventeen-argument, two-translation-unit
call at O0/O2. Independent readers check GPR/SSE register exhaustion, actual
incoming save bytes, stack arguments at CFA offsets 0/8/16, all debug-section
bytes and relocations against LLVM assembly, and six deliberately corrupted
location records. Linux runs also cross-link GCC/Clang objects in both
directions and inspect every parameter before and after a nested call in GDB.
This extends verification of unchanged scalar parameters. Automatic local
variables, parameter writes and the full debugging acceptance gate remain open.

The preceding type/parameter source passed native Linux validation and the
complete deterministic bootstrap, including authored suites for stages 2 and 3.
The current macOS frozen build completed 1,239 source cases and 2,478 CIR
round trips. An independent audit of the emulated Linux build verified all
59 original source objects, the 57-object core used by API probes, and twelve
GDB sessions across frame, line and type suites. Emulation remains separate
from native acceptance and performance evidence.


## Owned types and unchanged scalar parameter values

Debug construction now emits the actual function signature type graph:
target scalars, qualifiers, pointers, fixed/flexible arrays, complete/incomplete
records, unions, bitfields, enums and variadic function pointers. Forward
references support recursive records. Enumerator names/values belong to the
type context; parameters retain their declarator token coordinates through
HIR/IR. Unchanged scalar parameters have register and CFA-home location lists
derived from actual incoming ABI staging. Assigned or escaped parameters omit
their locations. They cannot display a stale original ABI value.

Strict GCC and ASan/UBSan builds passed O0/O2 independent layout/type checks,
original/assembled debug identity and all six deliberate metadata corruptions.
Both passed nineteen source-table checks, twenty-two malformed source/ABI map
rejections, repeated-finalization rejection and the ownership probe, which
destroys AST/HIR before construction and IR/types/sources before ELF output.
GCC/Clang references returned the expected result at both levels. The strict
frame regression also passed with unchanged non-debug object bytes. The
independent location reader checks base selections, address ranges, register
numbers and actual staging store bytes. An exploratory GDB check read the enum,
SSE and pointer values and a record field while leaving the assigned parameter
unavailable.
The sanitizer IR campaign passed all 100,000 execution/round-trip/optimizer
comparisons: 97,501 defined outcomes and 2,499 classified outcomes.

Cinder compiled every actual implementation module with `-g`; LLVM verified
all 59 original objects with no warnings. The final owned VM frame, source and
type debugger checks are pending. Native ordinary validation and deterministic
bootstrap passed for the preceding source-line portability fix. Source/CIR
broad regressions are running against separate frozen compilers for this
increment. The complete debugging gate still requires automatic variable
locations, parameter write tracking and its native campaign.

Failed attempts remain retained: a missing location-list base rejected by
LLVM, an unavailable flexible-record pointer that GDB tried to read, and a
source regression interrupted by host disk exhaustion. Another source attempt
was invalidated when its compiler was rebuilt during execution; the replacement
uses a separate frozen binary. Historical IR evidence was compressed and
verified byte for byte to recover working space. None of these attempts counts
as passed acceptance.

Evidence: `.agent-local/debug-types/`, `.agent-local/debug-lines/`,
`.agent-local/ir-campaign/` and
`.agent-local/linux-vm/share/debug-types-20261014/`. The final VM snapshot is
`.agent-local/linux-vm/share/debug-parameters-20261014/`; frozen host binaries
are under `.agent-local/frozen-debug-parameters/`. Historical raw IR archives
and per-file hashes are under `.agent-local/archives/ir-campaign/`.
See [DEBUG.md](DEBUG.md),
[ADR 0016](ADRs/0016-owned-debug-type-graph.md) and
[the debug mapping](diagrams/debug-frame.mmd). Next finish current validation,
then implement owned local declarations, lexical ranges and value availability.

## Encoder source lines and function entries

`-g` now emits original DWARF4 line programs, compilation units and subprogram
entries alongside the existing frame tables. Actual encoder offsets determine
row addresses and function extents. Logical include and macro expansion
coordinates, backward `#line` values, file-prefix maps and the mapped
compilation directory feed these records. This source-line increment preceded
the signature type graph above; the full debugging gate is incomplete.

Strict GCC and ASan/UBSan builds each passed nineteen independently decoded
source-table cases, including relocated checkout identity and empty/noreturn
units. Original ELF and LLVM-assembled output matched for every debug section
and relocation. The API probe rejected fifteen malformed source maps and
repeated finalization before adding sections or fixups. Both builds also
passed the six-table frame regression and twelve malformed output records;
debug-disabled objects matched the previous compiler byte for byte.

CIR validation passed 2,478 source round trips/backend comparisons and 36
rejection mutations. Sanitizer checks passed 41 driver commands, 33 prefix-map
commands and the repeated-include/source-instance regression. Cinder compiled
all 58 actual implementation modules with `-g`; LLVM verified every original
object. The owned Linux VM linked the complete original-object compiler and
passed its frame suite. Its first source-stepping attempt exposed an incorrect
test expectation after a return with no following statement. The fixture now
contains a distinct following statement; the failed evidence remains retained.
The replacement emulated run passed all six source-stepping sessions and four
frame sessions. An independent audit checked the source, object, compiler and
artifact hashes, all 58 original implementation objects, and both probes'
links with all 56 original core objects. The reports explicitly identify
emulation; these sessions do not satisfy the native debugging gate.

Native CI then failed the strict glibc build because the new module requested
POSIX declarations without enabling the XSI `realpath` declaration. The module
now uses `_XOPEN_SOURCE=700`, matching preprocessing and dependency handling.
The failed native run is retained; native replacement validation is pending.

Evidence: `.agent-local/debug-lines/`, `.agent-local/unwind/` and
`.agent-local/linux-vm/share/debug-lines-20261014/`. The replacement VM tree is
`.agent-local/linux-vm/share/debug-lines-20261014b/`. See [DEBUG.md](DEBUG.md),
[ADR 0015](ADRs/0015-encoder-source-lines.md) and
[the source-to-encoder mapping](diagrams/debug-frame.mmd). Next complete the
native replacement runs, then implement owned type/value metadata and accurate
value availability.

## Encoder-derived DWARF frames

`-g` now emits original DWARF v4 `.debug_frame` records. Encoder events record
the exact CFA transition after the frame push/base move, each R12-R15 store
and restore, and every leave/return path. FDEs cover actual function sizes and
relocate their function addresses and CIE section offsets. ELF and assembly
consume the same owned debug bytes and fixups. This frame increment preceded
the source-line work above; the complete debugging gate is not satisfied.

Strict GCC and ASan/UBSan builds passed O0/O2 checks on three source units,
six original/assembled frame tables and twelve malformed section/relocation
rejections. Independent LLVM disassembly and a separate row reader checked
actual save/load bytes, all returns and epilogue state. LLVM assembly preserved
text/frame bytes and relocation meaning. Debug-disabled objects matched the
previous compiler byte for byte. Both reference compilers executed the single
and two-unit fixtures at both levels. The object regression passed 1,000
two-unit O0/O2 probes with the assembly oracle on the cross-target host.

Cinder compiled every implementation module into original objects. The owned
Linux VM linked those objects into a compiler and ran the full unwind test,
including its Cinder-built output-rejection probe and four actual GDB sessions.
GDB selected the DWARF unwinder and recovered the caller chains in both single
and multi-unit binaries. The report explicitly records `emulated`, so this
functional run does not substitute for native debugger acceptance.

Evidence: `.agent-local/unwind/`, `.agent-local/selfhost-object-audit/`,
`.agent-local/linux-vm/share/debug-selfhost/` and
`.agent-local/linux-vm/share/debug-unwind/`. Final tool/input/object bindings
passed independent audit in `.agent-local/linux-vm/share/debug-unwind-tool-bound/`.
See [DEBUG.md](DEBUG.md),
[the encoder mapping](diagrams/debug-frame.mmd) and
[ADR 0014](ADRs/0014-encoder-derived-debug-frames.md). Next implement source
line/type/value metadata and the full native debugger campaign.

## Interpreted call suspension and bounded recursion

Native validation of the owned-HIR increment reached the generated IR campaign
and failed at case 65,537. The recursion generator still supplied an AST
declaration where the IR API required an owned parameter record. The generator
now uses that record without changing the cases or oracle.

The repaired campaign exposed a sanitizer stack overflow before the declared
256-call interpreter limit on the macOS host. Instruction dispatch now returns
before a nested interpreted call. A small bounded wrapper owns suspended frame
state and resumes its instruction index after receiving the result. This
preserves call, object and variadic semantics while keeping the large dispatch
stack frame out of host recursion.

Strict GCC and ASan/UBSan builds each passed all 100,000 generated execution,
round-trip and optimizer comparisons: 97,501 defined outcomes and 2,499
classified outcomes. New source checks verify the exact call-depth boundary
and call resumption through a loop at O0/O2. Cinder compiled the revised
interpreter implementation into an original ELF object. Raw failed CI and
sanitizer attempts remain retained separately.
The full source regression also passed 1,239 reference/interpreter/object
cases; sanitizer checks passed 136 memory classifications, 57 malformed memory
contracts and nineteen variadic object/assembly pairs.

Evidence: `.agent-local/ci-03aead7-failure/` and
`.agent-local/ir-campaign/`. See [IR.md](IR.md) and
[the suspension flow](diagrams/interpreter-calls.mmd). Useful DWARF output and
the debugging campaign remain open.

## Owned control and object HIR

HIR now owns declarations, structured statements, active initializer actions,
scopes, labels/cases, bindings and static object/relocation plans. CFG lowering
has no AST argument. IR parameter records and cloned static data belong to the
IR module. Type-context names are sealed before AST retirement. Inspection
reports these actual records with stable IDs and source-linked expressions.

Strict GCC and ASan/UBSan builds passed seven authored fixtures with O0/O2
reference/interpreter/object checks and sixteen malformed-graph rejections.
The lifetime probe destroys AST/sema before lowering; another changes HIR
static data then destroys HIR while the previous IR module preserves its result.
This covers global addresses, local static data, loops, switch and goto.

Self-compilation first exposed expression IDs assigned before binding copies
inserted initializer nodes. Registration now assigns the final ID, retaining
strict verification; every implementation module compiled afterward. A later
bitfield/source regression exposed canceled semantic initializer scratch
entries copied as active actions. Construction now filters canceled entries,
preserving active order. The previous broad attempts failed on overwritten
initializers and remain excluded. Fresh separate binaries passed all 52
bitfield cases at both levels and 40 malformed bitfield contracts. Replacement full source/CIR regressions passed: 1,238 source cases and
2,476 round trips/backend comparisons with 36 malformed-CIR rejections.
The final Linux VM ran 42 original/assembled/parsed objects and 28 references,
plus the Cinder-built lifetime probe linked with original core objects. Exact
source/object/ELF/executable bindings passed independent audit. This is
emulated functional evidence, not native performance proof.

Evidence: `.agent-local/hir/`, `.agent-local/selfhost-object-audit/`,
`.agent-local/hir-override-regression/` and
`.agent-local/linux-vm/share/hir-complete-final/`. The prior
`hir-owned-final` tree remains a retained pre-filter functional snapshot. See [HIR.md](HIR.md). Remaining required work includes machine
pointer/control/frame selection, useful debug information, explorer,
applications and full campaign readers.

## Independent HIR inspection units

The multi-unit driver now treats `--dump-hir` as stage inspection, matching
its other dump options. Each unit produces its own graph without entering the
link path. The regression keeps a previous executable sentinel and requires
that no object, assembly or temporary output is created. Separate strict and
sanitizer builds verify this dispatch change while the semantic regression
binaries from the expression checkpoint remain frozen.

## Owned typed expression HIR

The normal source pipeline now builds owned HIR expressions and verifies them
before CFG lowering. Scalar reads, addresses, conversions, member/index places,
updates and calls are explicit. Layout queries become constants and generic
selection removes unevaluated syntax. Variadic setup/copy/end have typed HIR
operations, fixing an untyped pseudo-callee exposed by compiling `diag.c`.
The first failing self-compilation snapshot remains retained. The corrected
compiler emits original objects for every current production module.

Strict GCC and sanitizer builds passed five new semantic fixtures at O0/O2,
deterministic source-linked dumps, GCC/Clang reference results, AST/HIR mutation
isolation through the actual consumer and ten malformed-graph rejections.
The full 1,236-source reference/interpreter/object suite and 2,472 CIR
round trips/backend comparisons passed, including 36 malformed-CIR rejections.
Sanitizers passed 353 source constraints, 25 arithmetic undefined cases,
136 memory undefined cases and 57 malformed memory contracts. Existing driver,
prefix-map and source-instance checks passed. The owned Linux VM executed 30
original/assembled/parsed objects, 20 independent reference outcomes and a
Cinder-compiled API probe linked with original core objects; source/object/
executable bindings passed independent audit. This is emulated functional
proof, separate from the real Linux bootstrap and performance gates.
Structured control/declaration/static-address plans still depend on semantic
AST metadata; the full HIR boundary is not complete.

Evidence: `.agent-local/hir/`, `.agent-local/selfhost-object-audit/` and
`.agent-local/linux-vm/share/hir-final/`. See [HIR.md](HIR.md) and
[the actual lowering flow](diagrams/typed-hir.mmd). Next transfer control/storage
plans, then complete debug output, remaining machine selection and full readers.

## Hosted evidence transport

The driver checkpoint's native bootstrap completed compilation and checks,
but run `38101192726` failed during artifact upload. GitHub rejected the
colon in the deliberately tested `leaf: space$.h` filename. The complete job
log is retained privately; no downloadable artifact was published by that run.
Both workflows now transport raw trees as tar.gz archives, preserving the
original POSIX names and bytes. A local round trip checked colon, quote,
newline and Unicode names. Reader contracts remain unchanged after safe
extraction. Hosted validation `38102437887` and bootstrap `38102478417` both passed
for this transport fix. The downloaded archive is retained for independent
source-bound verification.

Evidence: `.agent-local/driver-bootstrap-upload-failure.log` and
`.agent-local/artifact-filename-archive-check/`. See
[ACCEPTANCE.md](ACCEPTANCE.md) for extraction and read-only verification.

## Reproducible source prefixes

File and macro prefix options now map `__FILE__`, while file-prefix options
also map text/JSON diagnostic and token names. Last matching option wins;
substitution is performed once. Logical `#line` names are stored unmapped so
later line controls cannot apply a rule twice. Physical include reads,
canonical identities, source offsets and dependency rules remain physical.

Strict GCC and ASan/UBSan builds passed 33 focused commands each, including
identical O0/O2 original objects from two relocated trees, physical dependencies,
logical names, overlap/order handling, invalid-option output preservation and
independent units within one driver process. The existing 41 driver, 74
preprocessor and source-instance checks passed under sanitizers. All actual
compiler modules compiled through Cinder to original objects. Eight original/
assembled objects and eight GCC/Clang outcomes ran in the owned Linux VM;
source, object and executable bindings passed independent audit. This VM proof
is emulated functional evidence, not a native performance measurement.

Evidence: `.agent-local/prefix-maps/`, `.agent-local/selfhost-object-audit/`
and `.agent-local/linux-vm/share/prefix-final/`. See
[REPRODUCIBILITY.md](REPRODUCIBILITY.md). Typed HIR, useful debug records and
remaining full acceptance readers are still open.

## Include instances and qualified pointer subtraction

Each preprocessing inclusion now has a stable frame, parent and actual include
directive location. Tokens preserve that frame and their exact preprocessed
origin. Logical `#line` state belongs to an inclusion rather than a physical
file shared by every inclusion. Text/JSON diagnostics use the precise macro
definition/spelling and traverse the real include chain.

A retained reproducer included one header with `first.h:10` and later with
`second.h:20`. The first function's error incorrectly reported the later alias.
Both first/second error variants now report their own logical file/line and
root directive. Strict GCC/ASan builds passed those cases, twelve nested
semantic/preprocessor/UTF-8 failure chains and logical-line interpreter
execution. The existing 41 driver and 74 preprocessor cases passed.

Self-compilation exposed a separate const/mutable pointer-subtraction lowering
failure in the origin lookup. Semantic analysis now inserts a pointer type
with the union of compatible pointee qualifiers before lowering subtraction.
The IR operand-type verifier remains strict. Eight scalar/record cases cover
both directions and const, volatile, combined and mixed qualifiers; all passed
GCC/Clang and original/assembled O0/O2 checks. All 54 actual implementation
modules now compile to original objects. The Linux VM executed 34 original/
assembled pointer and logical-line objects with 36 reference outcomes; frozen
source, object and executable hashes were independently audited.

The first broad regression was invalidated when its compiler was rebuilt.
Those partial artifacts are retained and are excluded from acceptance. Fresh
separate GCC/ASan binaries are frozen for the replacement 1,231-source and
2,462-CIR regression. Both complete replacement runs passed, including all
36 malformed-CIR rejections. Constraints, CTest, census and evidence checks
passed.

Evidence: `.agent-local/source-instances/`,
`.agent-local/provenance-instances-repro/`, `.agent-local/pointer_difference/`,
`.agent-local/origin-regression-invalidated/`,
`.agent-local/linux-vm/share/origin-final/` and
`.agent-local/selfhost-object-audit/`. See
[the editable source-instance diagram](diagrams/source-instances.mmd).
Reproducible-prefix mapping, typed HIR, debug output and full acceptance remain
open.

## Dependencies, profiles and JSON diagnostics

The driver implements original-preprocessor `-M`/`-MD` output, `-MF`, quoted
`-MQ` targets, deterministic active include inventories and Make escaping.
The C17/core profile flags reject other language modes. Schema-one JSON
diagnostics preserve token ranges and available macro definitions on stderr,
including source-read and malformed-UTF-8 failures. Typed AST inspection now
requires successful semantic analysis. Source/output aliases, dependency
collisions and directory destinations are rejected before compilation output.
See [DRIVER_DIAGNOSTICS.md](DRIVER_DIAGNOSTICS.md).

Strict GCC/ASan builds passed all 41 driver commands, GNU Make rule parsing,
multi-unit dependencies, structured errors/warnings, token-range assertions and
preservation of existing source/object/dependency files. All 54 implementation
modules compiled to original objects. The seventeen target-header probes,
74 preprocessor cases, 353 constraints, existing driver/frontend/IR/float/
variadic smoke, CTest and artifact-integrity checks passed. The new opaque
directory-handle interface passed six original/assembled/parsed Linux VM
executions and four GCC/Clang references, with independent frozen-source,
object and executable auditing. The complete 1,223-source and 2,446-CIR
regressions passed against these frozen binaries, including 36 malformed CIR
rejections.

Evidence: `.agent-local/driver/`, `.agent-local/runtime-headers/`,
`.agent-local/linux-vm/share/directory-driver-final/` and
`.agent-local/selfhost-object-audit/`. Complete include-instance traces,
reproducible-prefix mapping, typed HIR, debug output and the remaining product
campaigns are still open. The bootstrap producer/reader now requires the new
driver suite when it belongs to the exact verified source snapshot.

The `9df7a64` selected-storage checkpoint passed hosted
[native validation](https://github.com/Aneesh495/cinder/actions/runs/38089645870)
and [bootstrap](https://github.com/Aneesh495/cinder/actions/runs/38089705999).
Its downloaded bootstrap was independently reread against the exact archive
and all twenty corruption checks passed. `b5931a8` was subsequently pushed
after the account's usage allowance returned; the hosted tip was verified.

## Bootstrap and private census gate dispatch

Acceptance now dispatches the independently checked bootstrap reader and the
private census reader alongside the nonvacuous-pass reader. Fresh self-hosting
generates a complete native package; retained packages require exact source
and seed verification before import. Partial generation records its selected
gates and keeps the rest unverified. Complete status requires every required
gate to pass a dedicated reader. Seventeen full-campaign readers remain open.

The self-host gate accepted the complete frozen `24d9562` native bootstrap and
rejected five altered gate envelopes while preserving all artifact bytes and
timestamps. The source inventory determines whether the newer machine-storage
fixture group belongs to that exact snapshot; current snapshots require it.
The private census was recomputed from current bound source and eleven altered
counts, inventories, rules or envelopes were rejected. Source bytes/timestamps
remained unchanged. The generator integration passed on macOS with the actual
private census result and explicit unverified native pass/self-host gates.
Incomplete full acceptance remained nonzero and verification wrote nothing.

The scalar checkpoint's 142 original objects and 568 reference outcomes also
passed independent frozen-source/ELF/executable auditing, including exact-source
adjudication of retained reference exclusions. The earlier ABI checkpoint
`cb72093` passed hosted
[native validation](https://github.com/Aneesh495/cinder/actions/runs/38088619554)
and [bootstrap](https://github.com/Aneesh495/cinder/actions/runs/38088659628).
Native runs for `9df7a64` are checked separately as they finish. The next work
remains pointer/control selection, typed HIR, debugging, driver/explorer behavior
and the remaining campaigns.

## Selected storage and conversions

Scalar memory accesses now carry selected load/store encodings for frame,
global and pointer addressing. Value definitions carry selected normalization
templates. Conversion modes, integer unary encodings and occupied-byte
bitfield masks are chosen before allocation and checked before encoding.
The encoder consumes these plans for the normal object and assembly paths.
See [MACHINE_SELECTION.md](MACHINE_SELECTION.md).

Strict GCC/ASan builds passed 142 authored scalar-plan cases, 20,025 corrupted
plan rejections and normal original-object identity. All twelve storage
profiles, seven conversion modes and three addressing families were exercised.
Twelve new volatile-storage sources passed both reference compilers and the
original/assembled O0/O2 object checks. The complete regression passed 1,223
source cases and 2,446 CIR round trips with 36 malformed mutations. The scalar
43-form/301-mutation and ABI 64-source/1,314-mutation suites, 1,000 physical phi
cycles, 10,000 allocation graphs with 1,000 mutations, parallel copies,
353 constraints, ten-pass records, twelve promotion guards, CTest and evidence
checks passed. All 53 implementation modules compiled to original objects.
The Linux VM executed all 142 original scalar-plan objects; full GCC/Clang
reference observations are being retained separately.

A generic ELF helper incorrectly treated every `narrow` symbol as the object
campaign's static short. The valid conversion-return function exposed this.
Fixture symbol checks now remain in that campaign, including all original
and assembler-oracle objects. Generic section/symbol/relocation checks remain
strict. The initial failure and eight complete two-unit fixture probes are
retained privately. The VM also reproduced the existing source-bound generic
bitfield reference disagreement. Its raw result is retained and the reference
runner uses the existing adjudication registry.

Evidence: `.agent-local/mir-scalar/`, `.agent-local/machine_memory/`,
`.agent-local/mir-scalar-inspector-failure/`,
`.agent-local/linux-vm/share/mir-scalar-final/` and
`.agent-local/selfhost-object-audit/`. Pointer/control expansion, typed HIR,
debugging and the remaining acceptance campaigns still require work.

The selected ABI checkpoint `cb72093` additionally completed all 1,211
source cases and 2,422 CIR round trips, and its 64 original VM objects plus
256 GCC/Clang reference executions passed the independent frozen-source audit.
The `24d9562` native bootstrap package was independently reread against its
exact source archive: stage identity, complete authored suites, 1,000 generated
programs, 8,000 native/4,000 interpreter outcomes and twenty tamper rejections
passed.

## Selected ABI plans

Incoming function parameters, outgoing calls and variadic retrieval now own
selected ABI layouts before allocation. The encoder consumes classified
returns/arguments, register placement, stack offsets and aligned argument
staging without repeating ABI classification. The machine verifier checks
every field against the verified source signature. Missing call signatures
produce a diagnostic before layout selection.

Strict GCC/ASan builds passed all 64 authored aggregate, variadic and
integer-address cases with original object identity and 1,314 rejected plans
or signatures. Both reference compilers agreed on all focused source cases;
their object/assembly checks passed. The existing scalar machine suite passed
43 forms and 301 corrupted-plan rejections; 10,000 allocation graphs with
1,000 mutations, 1,000 physical phi cycles, parallel copies, 353 constraints,
CTest and evidence-integrity checks passed. All 53 implementation modules
compiled to original target objects. The full 1,211-source and 2,422-CIR
regressions passed against the frozen ABI-plan binaries.

The pressure fixture had a call marker without a signature. This exposed a
null dereference in selection. Selection now rejects that input; the fixture
supplies real formal/actual signatures and retains its original pressure and
mutation counts. The failure observation is preserved privately.

Evidence: `.agent-local/mir-abi/`, `.agent-local/mir-abi-failure/` and
`.agent-local/selfhost-object-audit/`. Memory/conversion selection, typed HIR
and the remaining acceptance campaigns are still open.

The preceding selected-scalar checkpoint `24d9562` passed hosted
[native validation](https://github.com/Aneesh495/cinder/actions/runs/38014479847)
and [bootstrap](https://github.com/Aneesh495/cinder/actions/runs/38014554072).

## Selected machine representation

Normal allocation and scalar encoding now consume an owned selected machine
function. Integer forms carry original x86 byte templates, SSE arithmetic and
comparisons carry selected opcode/precision/parity plans, and definition banks
and clobber contracts reach allocation. Phi transfers read the owned machine
operands. `--dump-mir` exposes those actual plans. Shared opcode/type metadata
is separated from AST lowering for the standalone allocator/machine probes.
See [MACHINE_SELECTION.md](MACHINE_SELECTION.md).

Strict GCC/ASan builds passed 43 independently specified scalar forms, 301
malformed-machine rejections, 10,000 allocation graphs with 1,000 mutations,
parallel-copy checks, scalar/floating/ABI checks, aggregate/variadic source
fixtures, the full ten-pass proof and promotion guards. Cinder compiled all
53 implementation modules to original target objects. The complete regression
passed 1,211 source cases and 2,422 CIR round trips with 36 rejection mutations.
Every one of those 2,422 original objects is byte-identical to the preceding
frozen compiler. The Linux VM independently executed all 43 scalar probe
objects and 1,000 integer/float physical phi-cycle objects. Frozen source,
object and executable bindings were independently audited.

Evidence: `.agent-local/selected-mir/`, `.agent-local/allocation/`,
`.agent-local/selected-mir-byte-preservation.json`,
`.agent-local/linux-vm/share/mir-first/` and
`.agent-local/linux-vm/share/mir-ssa-first/`. Native hosted checks run after
publication. Memory/conversion target pseudos and a
separate typed HIR still require further representation work. Full acceptance
is incomplete.

The earlier `cb329bd` bootstrap and native validation completed successfully
([bootstrap](https://github.com/Aneesh495/cinder/actions/runs/38010977389),
[native validation](https://github.com/Aneesh495/cinder/actions/runs/38010888391)).
The downloaded complete bootstrap was independently reread against that exact
source archive: real stage objects/executables, 1,000 generated programs,
8,000 native and 4,000 interpreter outcomes, full authored stage commands and
20 corruption rejections passed. The acceptance-dispatch integration at
`2d0842d` also passed
[native CI](https://github.com/Aneesh495/cinder/actions/runs/38011643225).

## Acceptance reader dispatch

The nonvacuous-pass gate now calls the independently exercised raw-artifact
reader. Native generation retains the complete proof and reports the computed
ten-pass result; read-only verification reconstructs the outcome and rejects
inflated counts, different units, skipped status and escaped raw paths. The
native artifact package from `ac854e7` passed this dispatch independently with
forty actual native executions. macOS semantic/object checks cannot satisfy
the native gate. Nineteen other required full-campaign readers remain open, so
overall acceptance still fails honestly.

The `cb329bd` frozen builds subsequently completed all 1,211 source regression
cases, 2,422 CIR round trips with 36 malformed mutations, 100,000 typed IR
executions and 1,000 physical phi cycles. Current hosted bootstrap/CI results
are checked separately before claiming those native stage outcomes.

## Integer address authority and conditional initialization

Full-width integer address values now retain separate interpreter authority
through arithmetic, reversible bit operations, phis, calls, variadic arguments
and storage. Bounds, lifetime and const origin remain enforced when restored
pointers are accessed. Byte overwrites and narrowing cannot invent authority;
nonzero words mixing different domains are explicitly unsupported by the
interpreter. See [POINTER_WORDS.md](POINTER_WORDS.md).

The preceding optimizer commit `ac854e7` passed native Linux validation
[37887721615](https://github.com/Aneesh495/cinder/actions/runs/37887721615).
Its forty native pass objects and all ten actual stage pairs were independently
rechecked from downloaded raw artifacts against a `git archive` of that exact
commit. Bootstrap
[37887782994](https://github.com/Aneesh495/cinder/actions/runs/37887782994)
failed on a real raw-IR const store in `source/source.c`. The conditional
lowerer used an ordinary store for its internal result slot. Selected branches
now initialize that result with `local.init`; source assignments and the
const-store verifier restriction remain intact. The failed hosted artifact
and local reproducer are retained privately.

Current frozen GCC/ASan builds passed twenty-one authored integer-address cases
against both reference compilers, canonical CIR/object identity and eight
separate model guards. All 51 implementation modules compiled to original
target objects with verification before promotion. The Linux VM independently
executed eighty-four original/assembled objects and eighty-four GCC/Clang
reference programs; frozen source, object and executable hashes were audited.
ASan also passed the complete ten-pass proof and its 21 tamper controls,
promotion guards, 136 memory-undefined cases, 57 malformed mutations,
25 arithmetic-undefined cases and qualifier-origin contracts. The full source
regression and new 100,000-IR/1,000-phi campaigns are running against these
frozen binaries. Hosted validation and bootstrap are rerun after publication.

Evidence: `.agent-local/pointer_words/`,
`.agent-local/pointer-word-contracts/`,
`.agent-local/linux-vm/share/pointer-words-final/`,
`.agent-local/selfhost-object-audit/` and
`.agent-local/bootstrap-const-choice-failure/`. Full acceptance is incomplete.

## Verified optimizer records checkpoint

Normal source compilation now uses the ten-pass engine described in
[PASS_PIPELINE.md](PASS_PIPELINE.md). Each actual pass verifies its input and
output, records transformation events and dimensions, declares analysis
invalidation and measures algorithm CPU time. Atomic JSON and canonical CIR
traces are available through the CLI. Scalar promotion additionally preserves
retired-slot and indeterminate-write guards, existing phi operands and
repeated-pass byte identity.

Current frozen GCC/ASan builds passed all ten selected positive/negative pairs,
twelve independent CIR promotion guards, 21 pass-proof tamper rejections,
353 constraint cases, 136 memory-undefined cases and 57 malformed mutations,
25 arithmetic-undefined cases, all older optimizer fixtures, sixteen target
header probes and CTest smoke. The complete source regression passed 1,190
reference/interpreter/object cases and 2,380 CIR round trips with 36 rejection
mutations. ASan additionally passed 100,000 typed IR cases, 1,000 physical phi
cycles and 183,565 defined-input rewrite checks with 11,243 invalid inputs and
twelve independently rehashed proof mutations.

The owned Linux VM independently executed all forty isolated pass objects with
eighty GCC/Clang reference outcomes, twelve defined promotion guard objects,
and six clock-header original/assembled/parsed objects with four references.
Frozen source, compiler, object and executable hashes were checked afterward.
These VM observations establish target functional behavior; native hosted CI
and bootstrap are run separately after publication. They do not satisfy native
performance measurements. The public read-only pass reader requires actual
Linux x86-64 records by default and rejects altered or incomplete stage chains.

Evidence: `.agent-local/pass-pipeline/`, `.agent-local/promotion-guards/`,
`.agent-local/ir-campaign/`, `.agent-local/rewrite-checks/` and the corresponding
`pipeline-final`, `promotion-pipeline-final`, `clock-pipeline-final` directories
under `.agent-local/linux-vm/share/`. The original trace-path lifetime sanitizer
failure and its frozen reproducer are retained privately; the output path now
lives through atomic commit/abort and both final sanitizer suites pass.

Remaining work includes genuine HIR/MIR boundaries, debug information and
debugger recordings, dependency output and reproducible driver options,
substantial application fixtures, coverage-guided fuzz/recovery campaigns,
native performance measurements and complete acceptance runner/reader wiring.
`make acceptance` and `make verify` still reject incomplete full acceptance.
The older sections below retain the sequence of implementation checkpoints.

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


## Nonreturning function declarations

`_Noreturn` contracts now survive compatible file/block redeclarations,
semantic name resolution, canonical CIR schema 4, and original native
emission. A bounded diagnostic CFG walk warns on possible returns. The
independent interpreter classifies a reached marked return as undefined;
marked native returns and resumed marked calls emit `UD2`.
The authored `stdnoreturn.h` supplies the standard `noreturn` macro.

GCC Release and Clang ASan/UBSan passed 27 authored reference/object programs,
9 diagnostic paths, 6 defined libc exit programs, 9 exact trap inspections,
and 4 malformed contract rejections. No undefined returning-function native
program was executed. The sanitizer build passed all 1,494 source CIR
comparisons and 36 malformed CIR mutations, 206 source constraint rejections,
90 memory UB classifications and 57 corrupt memory contracts, 100,000 IR
comparisons, and 1,000 SSA cases. External process termination remains an
explicitly unsupported interpreter service; the native probes validate the
hosted libc boundary separately. All 132 defined native object/assembly probes passed using the final GCC
Release compiler in the Linux development VM. The first attempt linked and ran 48 probes before a link timeout; that
attempt and timeout remain recorded. The unchanged retry completed all probes.
See `docs/NORETURN.md`; full acceptance remains incomplete.


The standalone alignment checker repair `1afe95b` passed the full hosted Linux
workflow [37702523157](https://github.com/Aneesh495/cinder/actions/runs/37702523157),
including both complete 512-signature aligned aggregate and variadic callback
campaigns. Raw hosted artifacts are retained privately. The nonreturning
function increment `cad854a` passed the full hosted Linux workflow
[37704769941](https://github.com/Aneesh495/cinder/actions/runs/37704769941).


## Block storage increment

Block-scope static objects now use deterministic local ELF symbols and static
initialization, retain values across calls and loop re-entry, and preserve
requested alignment and read-only storage. Name expressions retain their
resolved declaration into lowering. Compatible block extern declarations share
translation-unit storage while visible source-point linkage and lexical
shadowing determine the target. Incomplete and unused extern declarations
remain real undefined symbols; missing interpreter storage is unsupported.

GCC Release and Clang ASan/UBSan passed 47 authored reference/object/assembly
programs, seven cross-unit contract profiles, and 229 source rejections.
The final compiler passed all 244 defined native object/assembly executions in
the Linux development VM. The sanitizer passed 92 memory UB classifications
and 57 malformed memory contracts, 100,000 IR execution/round-trip comparisons,
and 1,000 SSA cases. Two static read-only probes exercise UB solely in the
independent interpreter. A converted static boolean initializer failure and an
aligned extern definition failure are retained privately. The full CIR regression
also caught an explicit pointer-cast initializer affected by boolean unwrapping;
the narrowed conversion rule passed the complete initializer suite and a fresh
244-execution native storage run. An unused extern
void declaration initially mislabeled as invalid was checked against both
references and the standard, then moved to the authored positive ledger.
The final sanitizer also passed all 1,588 source CIR comparisons and 36 malformed
CIR mutations. The increment is published as `4359f22`; its full hosted Linux run passed:
[37707676446](https://github.com/Aneesh495/cinder/actions/runs/37707676446).
See `docs/BLOCK_STORAGE.md`. Full acceptance remains incomplete.


## Label and goto increment

Function-wide labels now resolve forward and backward jumps in a separate
namespace, diagnose duplicate and undefined targets, and preserve labels after
terminated statements. Lexical scope transitions end objects in departed scopes
and begin objects in entered scopes without executing skipped initializers.
Resolved declaration slots remain available when a jump bypasses a declaration.
Reached declarations reset indeterminate storage without changing object
identity; scalar SSA promotion represents those resets explicitly.

GCC Release and Clang ASan/UBSan passed 40 authored reference/object/assembly
programs and 80 canonical CIR execution/object-identity comparisons, four
malformed reset metadata rejections, and label/statement nesting boundaries.
All 240 defined native owned, assembled, and CIR-generated objects passed in
the Linux development VM. The sanitizer passed all 1,668 source CIR comparisons
and 36 malformed CIR mutations, 241 source rejections, 99 memory UB cases and
57 malformed memory contracts, 100,000 IR comparisons, and 1,000 SSA cases.
The GCC control regression passed 832 cases; the complete sanitizer CIR suite
includes all 834 current ledger entries. Static-storage and nonreturning
function regressions also passed. The seven new memory UB programs run only in
the independent interpreter. See `docs/JUMPS.md`. Full acceptance remains
incomplete; other language families and audited campaign readers remain open.


## Switch dispatch increment

Switch/case/default now implements integer promotions, converted constant
matching, duplicate detection, source-order fallthrough, and nearest-switch
association. Dispatch edges enter scopes without executing skipped initializers
or conditions. Nested loop and switch statements have separate break and
continue target/lifetime stacks. Cases inside loops, conditionals, and nested
blocks work, including Duff's device and function labels using typedef names.

GCC Release and Clang ASan/UBSan passed 46 authored reference/object/assembly
programs and 92 canonical CIR execution/object identities. All 276 defined
owned, assembled, and CIR-generated objects passed in the Linux development VM.
Six generated 1,024-case object paths also passed native execution at O0/O2.
A 1,024-case dispatch executed through canonical IR; 4,096 cases passed semantic
analysis, with rejection at 4,097 and beyond the 128 nested-label limit. The
sanitizer passed all 1,760 source CIR comparisons and 36 malformed mutations,
264 source constraints, 106 memory UB cases and 57 malformed memory contracts,
100,000 typed IR comparisons, and 1,000 SSA cases. The GCC regression passed all 880 current source observations. The existing
goto contract regression and read-only evidence integrity checks also passed.

An initial case-expression ownership defect was caught by ASan: the statement
and discarded-constant ledger both freed the same generic association vector.
Case statements now own their expressions and perform their full semantic
constant checking directly. The failure and original source snapshot remain in
the private audit. A new unselected-generic negative oracle was narrowed from
void indirection to floating remainder after reviewing the language constraint;
reference diagnostics for the earlier oracle remain retained.

See `docs/SWITCH.md`. Full acceptance remains incomplete, including the audited
source-bound native report readers and remaining original prompt requirements.


## Register addressability and variadic parameter increment

Register storage is retained on objects, parameters, and inherited aggregate
members. Source address requests and array pointer conversions are rejected,
while scalar updates, array size queries, pointer dereference/cancellation, and
aggregate ABI operations remain supported. Source-point constant expressions
retain declaration identity, including prototype parameter scopes.

Variadic builtins now resolve the actual final named parameter declaration.
Same-spelling shadows are rejected. Evaluated va_start uses reject register,
original array/function, and promotion-changing parameters; unused generic and
sizeof uses retain their unevaluated behavior. Prototype attributes do not
transfer to later definitions. Native ABI metadata is unchanged.

GCC Release and Clang ASan/UBSan passed 30 authored programs against both
references at O0/O2, 60 canonical CIR/object identities, 293 source rejections,
and 106 memory UB cases with 57 malformed memory metadata mutations. All 180
defined direct, assembled, and CIR-generated object executions passed in the
Linux development VM and their frozen object/compiler hashes were audited.
The GCC regression passed all 910 source observations. The sanitizer passed
1,820 source CIR comparisons with 36 malformed mutations,
100,000 typed IR comparisons, and 1,000 SSA cases. Evidence tampering checks
and the strict GCC smoke test passed.

Host diagnostics for inherited register-array conversions differ: Clang accepts
one invalid member conversion while Cinder rejects it under the documented
profile. The semantic UB rules and the 11 invalid variadic source cases have
exact source hashes and non-mandatory reference policies; the original reference
diagnostics are retained. No invalid native execution is used as evidence.
See `docs/REGISTER.md`. Full acceptance and its audited report readers remain
incomplete.


## Target offset constants and fundamental runtime headers

The authored `stddef.h` intrinsic resolves complete source-point aggregate
layouts and constant member/index paths into typed `size_t` constants. Every
index expression is semantically checked even when unevaluated. Bounds, enums,
assertions, cases, static initializers, direct native objects and serialized IR
use the same target layout. Owned `stdbool.h`, `stdint.h` and `limits.h` describe
the Linux LP64 profile. `max_align_t` promises alignment 16 with a private
representation; matching a libc-specific sizeof is not claimed. The remaining library surface and a verified self-host campaign remain open. See `docs/RUNTIME_HEADERS.md`.

Strict GCC and sanitizer builds passed 34 authored offset/header cases against
both references at both optimization levels, original object/assembly byte and
relocation checks, 68 canonical CIR/object identities, six parser-boundary
checks and seven invalid/profile designators. Both builds rejected all 306
constraint sources. The sanitizer passed 1,888 source CIR comparisons plus 36
mutations, 106 classified memory cases plus 57 mutations, 100,000 typed IR
comparisons, and 1,000 SSA cases. All 204 owned/assembled/parsed native runs
passed in the Linux development VM with independently audited object/compiler
hashes. The sanitizer smoke and evidence tampering/read-only checks passed.

Two initial test assertions incorrectly assumed the build host's integer typedef
and maximum-alignment choices matched the target. They were corrected to check
header type consistency and exact alignment on the declared target; the source
and failure audit remain private. Transient host reference startup timeouts
were retained; unchanged binaries and complete reruns subsequently passed.
No timeout is counted as a successful run. Full acceptance remains incomplete.
The preceding register/varargs increment passed the complete native Linux
workflow [37713251240](https://github.com/Aneesh495/cinder/actions/runs/37713251240).


## Authored compiler libc/POSIX interfaces

The owned runtime headers now declare the compiler's standard/POSIX calls,
opaque streams, Linux errno accessor, target time/permission/process types,
Linux `struct tm` layout, wait decoding, integer printf spellings, and authored
IEEE finite classification. `__unix__` is an explicit target predefined macro.
Both strict host builds passed 15 authored runtime probes with both references
and optimization levels, 30 canonical CIR/object identities, and independent
assembly section/relocation checks. The independent interpreter explicitly
classifies unavailable external calls; these are native probes rather than
additional source-interpreter successes. All 90 Cinder-owned native executions
and 60 Linux GCC/Clang system-header references passed in the development VM,
with frozen compiler and object hashes independently audited. ASan also passed
74 preprocessor checks and 306 source rejections. The preceding offset compiler
passed all 944 source regression observations.

The initial syntax audit passed 41 of 42 modules and identified the arena
flexible member as a real blocker. The flexible-array implementation below
removes that blocker without precompiling a module. The full stage 2/3
bootstrap campaign remains unverified. See `docs/RUNTIME_HEADERS.md`.


## Named flexible array storage

Legal final members now affect target alignment and tail padding while retaining
zero fixed size. Initializers and assignments operate on the fixed header;
System V classification omits the incomplete tail. Illegal struct/array
embedding is rejected, including recursive unions. Typed interpreter bounds
include fixed tail padding and containing union storage. Tail accesses preserve
initialization, const qualification and lifetime checks. Native allocations
exercise actual malloc/calloc/realloc storage with scalar, pointer, record,
multidimensional and string tails.

Strict GCC and sanitizer builds passed 34 authored fixed-header cases against
both references at both optimization levels, 68 canonical CIR/object identities,
11 malformed type graphs, eight cross-unit ABI profiles, and 12 allocation
probes. The sanitizer rejected 322 constraint sources and classified 113 memory
UB sources plus 57 malformed memory contracts. All 354 final owned/assembled/
parsed executions and 216 Linux reference executions passed in the development
VM with independently verified frozen source, compiler and object hashes.
Clang's small by-value flexible-header ABI differs from GCC's declared target
contract. Six incompatible case/level profiles remain explicitly ineligible,
with source hashes and independent LLVM signatures; the original failed native
call and providers are retained. Both references interoperate through tested
pointer interfaces and the compatible memory-return profile.

The complete regression ledger passed 978 defined source observations and 1,956
canonical source IR/object comparisons plus 36 mutations. The sanitizer also
passed 100,000 typed IR comparisons, 1,000 SSA cases, preprocessor provenance,
smoke and evidence tampering checks. See `docs/FLEXIBLE_ARRAYS.md`.

A frozen development bootstrap attempt compiled all 43 actual production
modules, linked a functioning Cinder compiler, and recompiled those sources into
43 byte-identical objects. This is preliminary evidence, not the final bootstrap
gate: the current source and both stages still need the complete authored suite
and required generated native subset. Full acceptance remains incomplete.


## Actual bootstrap runner and defined native generator

`make selfhost` now performs a fresh host stage 1 build, Cinder compilation of
every actual module into stages 2 and 3, exact object/executable comparisons,
both authored semantic suites, and 1,000 generated native programs. It retains
per-command and per-file artifacts, declared source-date/prefix settings,
startup/tool/library hashes and separate stage test workspaces. Unsupported
hosts return nonzero instead of presenting a version print as a bootstrap.
The manual native Linux workflow preserves full raw stage evidence.

The native workflow at commit `fc83de6` produced two byte-identical 43-module
object sets and identical compiler/IR-tool executables. Both stages passed
the authored suites and the 1,000-program subset: 8,000 native executions
and 4,000 independent interpreter executions. Downloaded manifests, input
snapshots, objects, executables and all 1,000 per-program reports were
independently hash-checked against that published source revision. This
is historical bootstrap evidence; later implementation changes require a
fresh campaign before final source-bound acceptance. An independent
pilot validated all nine generator families with 36 eligible programs, 216
GCC/Clang/Cinder native executions and 72 Cinder interpreter executions. These
pilot observations do not satisfy the 1,000-program bootstrap or separate
20,000-program differential gate. See `docs/SELFHOSTING.md`.

## Anonymous struct and union members

Anonymous members preserve their physical nested aggregate layout. Promoted
names resolve through a bounded member path shared by semantic checks,
constant offsets, initializers, static addresses and original IR lowering.
Duplicate promoted names and tagged/typedef/scalar bare declarations fail.
Qualifiers propagate through every anonymous container, and the expression
base is evaluated once. Flexible arrays count preceding promoted named
members, including the standard's anonymous-prefix example.

The authored ledger adds 32 independent translation units. Both strict host
builds pass GCC/Clang reference comparisons, original object/assembly checks,
canonical IR object identities, five malformed type graphs, 63/64 aggregate
nesting and rejection at 65/1,024 levels. Seven additional interpreter cases
check promoted bounds, indeterminate values, lifetime and read-only storage.
Six mixed-toolchain profiles cover integer, SSE, mixed and MEMORY records,
unions, returned aggregates and callbacks with register exhaustion. The frozen development Linux VM passed 268 original/assembled/parsed object
executions and 160 GCC/Clang reference executions; object, source, provider
and executable hashes were independently rechecked. This functional VM
evidence does not substitute for the native hosted acceptance profile.

Diagnostic references now record both GCC and Clang. Clang accepts the exact
const-qualified anonymous-container assignment that GCC and Cinder reject.
Its source-bound exception records a reference diagnostic discrepancy while
retaining the mandatory Cinder rejection. Register-array decay remains
separately classified as undefined behavior with a non-mandatory diagnostic.
The full acceptance report remains incomplete.

## Read-only native evidence readers

Bootstrap and generated native campaigns now preserve copied startup/tool/libc
artifacts and reference/original compiler binaries in addition to their recorded
hashes. Schema 2 readers rehash every source, object, executable and report;
require target ELF headers, full original module inventories, strict fresh host
stage 1 compile commands, complete authored commands and both native levels;
and regenerate the bounded input specification for every eligible program.
Generated failures, duplicate source programs, interpreter discrepancies,
missing compiler levels and host fallback cannot satisfy a reader. Raw schema 1
bootstrap evidence remains historical proof and cannot silently satisfy the
stronger schema 2 contract. No reader has been connected to final acceptance
until a complete real native positive run and evidence mutation checks pass.

## Local redundant memory elimination

The optimizer now caches exact integer accesses to known nonvolatile local
objects within a block. It forwards repeated loads and loads after retained
writes, and removes identical ordinary stores only after a retained ordinary
store has established write permission. Declaration volatility survives casts.
Calls, unknown roots, writes, bitfields, aggregate effects, variadic operations
and lifetime changes invalidate facts. Const initialization and reads never
prove permission for a subsequent write.

Strict GCC and Clang ASan/UBSan builds pass the 17 isolated source fixtures
and six typed storage guards. The earlier optimizer suites, 25 arithmetic
undefined cases, 136 memory classifications and 57 malformed memory contracts
pass. A frozen development Linux VM executed 51 original objects and 68
GCC/Clang reference programs; independent source, compiler, object, target ELF
and executable hashes were checked. This is emulated functional evidence.
`make test-optimizer`, CI and both native bootstrap stages run the new suite.

The published copy-cleanup revision passed native Linux validation and its
complete deterministic bootstrap. The complete 1,168-case source suite and 2,336 canonical-IR/backend
comparisons with 36 malformed-input rejections passed. A reference compiler
timeout was retained separately before the unchanged suite passed on rerun. The
full acceptance remains incomplete: loop motion and pass records, actual HIR
and machine representation, debugger, applications, fuzzing, recovery and
performance work remain open.

## Natural loops and conservative invariant motion

Dominance-checked backedges now define natural-loop membership. The optimizer
requires one verified existing preheader and moves only total integer
operations with defined invariant operands. It retains irreducible regions,
multiple-entry headers, effects, pointers, floating operations, loads, division,
signed arithmetic and unproved shifts. Moved operations preserve their original
SSA IDs and locations. The normal `-O2` pipeline and `--pass=loop-motion` use
the same implementation. See `docs/LOOP_MOTION.md`.

Both strict GCC and Clang ASan/UBSan builds pass 18 source motion/retention
fixtures and ten constructed zero-trip, trap and preheader guards. Existing
optimizer suites, arithmetic/memory undefined classifications and the complete
independent rewrite checker with evidence tampering tests pass. A frozen
development Linux VM executed 66 original normal/isolated/guard objects and
72 GCC/Clang references, with an independent source/object/ELF/executable audit.
This remains emulated functional evidence. Broader local regressions are still
running against frozen binaries. CI and both bootstrap stages include the suite.

The earlier memory revision finished all 1,168 source cases, 2,336 canonical-IR
object comparisons and 36 rejection mutations. Its exact published tip is
`5c7f6ca18183716c0d5dc2cc7e89b24763c652da`; native bootstrap is running at
`https://github.com/Aneesh495/cinder/actions/runs/37883644974`. Pass contracts,
timing and invalidation records, full representation boundaries and the
remaining acceptance campaigns are still open.
