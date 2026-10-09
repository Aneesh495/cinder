# Local integer translation validation

`make test-rewrites` exercises the production `cinderir -O2` optimizer using
typed, single-block fragments with an unknown argument. Constant folding cannot
replace the argument with a convenient test value. Before/after CIR and every
independent input/output/classification observation are retained under the
ignored `.agent-local/rewrite-checks` directory.

The checker in `tools/bitvector.py` uses unbounded Python arithmetic, then
explicit width truncation, signed bounds, and invalid-operation rules. It does
not call the production interpreter, optimizer, instruction selector, or
encoder to calculate an expected result. Its bounded grammar covers arguments,
constants, copies, multiplication, division, remainder, logical shifts, and
bitwise masks. Unsupported operations cause rejection.

| Preconditions | Original | Replacement |
| --- | --- | --- |
| Unsigned width `w`, `0 <= k < w` | `x * 2^k` | `x << k`, modulo `2^w` |
| Unsigned width `w`, `0 <= k < w` | `x / 2^k` | logical `x >> k` |
| Unsigned width `w`, `0 <= k < w` | `x % 2^k` | `x & (2^k - 1)` |
| Integer operand, factor one | `x * 1` or `x / 1` | copy of `x` |

The pass does not rewrite signed power-of-two division into an arithmetic shift.
For example, `-35 / 8` is `-4`, while an arithmetic shift would round toward
negative infinity. Signed multiplication can overflow and has different shift
definedness rules. Zero and non-power-of-two factors are negative fixtures.
Factors are normalized to the declared width before eligibility is checked:
an eight-bit constant with storage literal 256 is zero, not a shift by eight.

All byte inputs are exhausted for each fragment. A complete 16-bit domain
checks division by 256. Other 16-, 32-, and 64-bit fragments use boundary values
and a reproducible domain of 256 distinct inputs. Each fragment also checks an
indeterminate argument. Invalid signed overflow and zero division have distinct
outcomes, and indeterminate reads must remain invalid after a rewrite.

The report binds production source, checker/reader source, compiler binaries,
the original optimizer command, canonical IR bytes, and raw observations.
`tools/rewrite_evidence.py` reads these artifacts without invoking a compiler.
It reconstructs the entire expected fragment/input inventory, evaluates both
versions, and checks every recorded outcome. The reader tests alter totals,
source bindings, commands, fragment coverage, raw outcomes, and an actual shift
operand. Rehashing incorrect semantic data does not make it acceptable.

This is local translation validation for the supported integer fragments.
Sampled wider domains are not exhaustive proofs. It does not establish memory,
floating-point, control-flow, allocation, encoding, ABI, or whole-compiler
correctness. Those require separate execution and evidence gates. Native source
fixtures additionally check 32-/64-bit boundaries, volatile post-increment,
single calls, and signed arithmetic through the original encoder.
