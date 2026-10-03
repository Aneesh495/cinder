# Preprocessing and source provenance

The preprocessor runs before semantic lexing and uses original preprocessing
tokens. `pp_tokens.c` handles splicing, comments, literals, preprocessing
numbers, longest-match punctuators, and digraphs. `pp_expand.c` owns macro
replacement and hide sets. `pp_eval.c` evaluates conditional expressions.
`pp.c` coordinates directives, include search, file identity, and TU state.
No preprocessing operation invokes a host compiler or `cpp`.

## Macro replacement

Object and function replacements are token sequences. Function invocation
collects parentheses-balanced arguments while keeping string and character
literals opaque. Ordinary parameters use argument prescan; `#` and `##` use
raw arguments. Stringification collapses argument whitespace and escapes
literal spelling. Pasting must form exactly one preprocessing token, handles
empty-argument placemarkers, and rescans the result with following input.
Variadic arguments retain their separating commas in `__VA_ARGS__`.

Each token carries an immutable hide set. Object replacement adds the macro
identity to the invocation hide set. Function replacement uses the
intersection of the invocation and closing-parenthesis hide sets before
adding the current macro. Substituted argument tokens keep their own hide
sets. Self-reference and mutual recursion therefore stop as unavailable
identifiers rather than reaching a recursive-text limit. Argument prescan is
bounded separately. Macro redefinitions must match the parameter list,
replacement spelling, and separating whitespace. GNU comma deletion and
`__VA_OPT__` are excluded extensions.

## Directives and expression evaluation

Supported directives are `#include`, `#define`, `#undef`, `#if`, `#ifdef`,
`#ifndef`, `#elif`, `#else`, `#endif`, `#error`, `#line`, and `#pragma`.
Conditional state tracks parent activation, previously selected branches,
and whether `#else` occurred. Unmatched or repeated structural directives
fail. Inactive branches cannot change macros or resolve includes.

`#if` uses the target 64-bit `intmax_t`/`uintmax_t` model, integer suffixes,
character escapes, precedence, `defined`, unary operators, comparisons,
shifts, arithmetic, bitwise operators, and the conditional operator. Logical
and conditional operators parse unselected operands without evaluating
invalid division or shifts. Evaluated signed overflow, invalid shifts, zero
division, malformed constants, and trailing tokens are errors. Remaining
identifiers evaluate to zero after macro expansion.

Quoted includes search the including directory first, then `-I` directories
in order. Angle includes search `-I` directories. Header operands may be
macro-expanded. Canonical paths identify loaded files; ordinary guarded
headers and the `#pragma once` extension can suppress repeated contents.
Include depth, expression depth, input tokens, expansion work, and produced
tokens have explicit limits with nonzero diagnostics.

`_Pragma` destringizes its expanded operand and uses the same pragma handler.
`STDC FP_CONTRACT` is accepted without introducing contraction.
`FENV_ACCESS ON`, packing changes, and unsupported STDC configurations are
errors. Unknown pragmas produce a warning. The internal token stream omits
handled pragmas; `-E` currently emits the resulting ordinary tokens without
reconstructing accepted STDC pragma lines.

## Locations and reproducibility

Rendered tokens have source spans recording expansion, spelling, and macro
definition locations. The semantic lexer maps rendered offsets back to those
spans. Included tokens retain the included file location. Diagnostics for a
replacement token point to its invocation and add its macro definition.
This is a single-definition note, not a complete nested expansion backtrace.
`#line` changes logical line and filename display while retaining physical
byte offsets. Dependency-file output and complete include/macro backtraces
remain open.

Predefined macros describe the Linux x86-64 LP64 target and the C17 profile,
including optional-facility exclusions. `__FILE__` and `__LINE__` use logical
source locations. `__DATE__` and `__TIME__` use UTC compilation time by
default. Setting `SOURCE_DATE_EPOCH` fixes both for deterministic campaigns;
invalid values fail compilation. Prefix mapping remains open.

`make test-preprocessor` compares authored valid token observations with a
C17 host preprocessor, checks independent negative inputs, and verifies macro
and included-source provenance. Raw observations are saved under
`.agent-local/preprocessor/`. The corpus and comparison normalization are in
`tests/preprocessor/` and `tests/test_preprocessor.py`.
