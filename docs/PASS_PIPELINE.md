# Verified pass execution

Normal `-O2` source compilation executes these passes in order:

1. `mem2reg`
2. `constant-fold`
3. `cfg-simplify`
4. `sparse-constants`
5. `value-numbering`
6. `strength-reduction`
7. `copy-cleanup`
8. `local-memory`
9. `loop-motion`
10. `dead-code`

`-O1` runs promotion, constant folding, CFG simplification, copy cleanup, local
memory forwarding and dead-code elimination. Source `-O0` performs baseline
scalar promotion only. The textual CIR tool leaves `-O0` input unchanged; this
keeps independently constructed memory/lifetime guards observable before
promotion. An isolated `cinderir --pass=NAME` executes exactly the named pass.

Each boundary runs the full typed IR verifier. `--pass-stats FILE` atomically
publishes JSON containing contracts, function and transformation counts,
active operations and blocks, verification results, analysis masks and epochs.
`--dump-passes` prints the same record. `--pass-trace DIRECTORY` saves canonical
before/after CIR and a pipeline JSON index into an existing directory. Trace
and statistics paths currently require a single translation unit. Invalid
input is rejected before successful output publication.

For example:

```sh
mkdir -p artifacts/pass-example
build/cindercc -O2 --pass-stats artifacts/pass-example/stats.json \
  --pass-trace artifacts/pass-example --serialize-ir \
  tests/optimization/pipeline_mem2reg.c -o artifacts/pass-example/result.cir
build/cinderir --pass=constant-fold \
  artifacts/pass-example/01-constant-fold.before.cir -o artifacts/folded.cir
```

Transformation events count actual instruction rewrites, inserted definitions,
removed blocks or moved instructions according to the pass. They are not a
count of distinct instructions: an instruction can undergo multiple changes.
Operation counts exclude `nop`; block counts include retained unreachable
blocks until CFG simplification removes them. CPU time comes from `clock()`
around the algorithm execution, excluding verifier and trace output work.
Sub-clock-resolution passes can report zero seconds; unavailable clocks are
recorded as `null`, which the full pass proof rejects. This instrumentation
does not replace native end-to-end benchmark measurements.

The masks are CFG=1, dominance=2, loops=4, uses=8, effects=16 and liveness=32.
An unchanged pass preserves all six. Changed CFG/sparse passes invalidate all
six; other passes preserve CFG/dominance/loop shape and invalidate value-use,
effect and liveness facts. Each invalidation advances the module analysis
epoch. The current analyses build fresh local facts rather than sharing a
cache across passes. The explicit masks conservatively describe what a later
analysis must recompute.

Scalar promotion follows slot declaration identity and possible lifetime
states across the CFG. An access that may follow retirement prevents promotion
of that slot; a new `local.begin` renews its lifetime. Volatile or address-taken
storage, invalid ordinary const writes and stores of possibly indeterminate
values retain their memory guards. A virgin indeterminate read can become a
read of an `undef` SSA definition and still produces the same classification.
Only phis inserted by the current promotion run have their incoming slot
definitions rewritten. Existing valid phi metadata and operands survive.
Running promotion a second time produces identical canonical bytes.

`make test-passes` observes ten independent source pairs, using the real input
boundary immediately preceding each selected pass. It checks targeted opcode
reduction or actual invariant value movement, preserved negative cases,
independent interpreter results, original object output and complete stage
chains. Linux x86-64 also links and executes every before/after object.
Twelve independently constructed CIR cases exercise slot lifetime, undefined
reads/writes and existing phi ownership; malformed const writes and missing
phi slots must fail before replacing prior output.

`tools/pass_evidence.py` reads the saved proof without running workloads or
rewriting it. It checks exact source/tool/artifact bindings, all stage chains,
actual dimensions, contract masks/epochs, transformation progress, interpreter
command output and nonempty target ELF code. Its default requires actual
Linux x86-64 execution records for all forty objects. Local macOS tests can
validate artifact semantics without satisfying that native gate. Tamper tests
include consistently rehashed false counters, results, objects and commands.
The complete acceptance registry still requires the remaining campaign readers.

See [pass invalidation](diagrams/pass-invalidation.mmd) and
[promotion guards](diagrams/promotion-guards.mmd).
