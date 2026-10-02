# Floating-point scalar slice

Cinder now lexes decimal floating constants, preserves target-rounded `double` values in tokens and AST nodes, types floating expressions as `double`, lowers floating arithmetic/comparisons into dedicated IR operations, interprets them independently, classifies simple pure-SSE function parameters and calls, and emits a stack-backed SSE2 implementation for constants, arithmetic, comparisons, and floating returns.

The target contract is IEEE-754 binary32/binary64 as exposed by the declared Linux x86-64 profile. The current implementation uses `double` values for the supported `double` path and rejects no floating literal solely because the host integer parser cannot consume its spelling. Simple all-SSE parameter/result calls are covered; mixed integer/SSE calls, hexadecimal float spelling, explicit `float` narrowing, NaN/infinity spelling, exact exception behavior, SSE register allocation, callbacks, and full mixed integer/SSE ABI classification remain incomplete.

`tests/run_float.sh` checks frontend typing, IR operations, direct encoded bytes assembled by an independent cross-target Clang oracle, ELF output, and an independent interpreter comparison for a floating condition. It is scalar coverage, not the 500-combination ABI gate.
