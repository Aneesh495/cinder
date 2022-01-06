# Preprocessing and token provenance

Cinder's preprocessor is implemented in `source/pp.c`. It owns macro state, conditional activation, include search, and replacement rescanning before `source/lex.c` assigns parser tokens. It does not call `cpp`, perform a process-wide text replacement, or hand user source to a host compiler.

## Current contract

Object-like and function-like macros are stored as named replacement token text. Function arguments are collected with balanced parentheses, expanded before substitution, and rescanned with a bounded recursion limit. `#`, `##`, placemarkers, variadic comma elision, and full macro-origin chains are not yet promoted to the complete profile and are diagnosed or reserved for the next preprocessor increment.

Conditional directives maintain an active stack. Inactive ordinary source is not expanded or emitted. `#define`, `#undef`, `#include`, `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif`, and `#error` are recognized. Include nesting is bounded and include paths are searched relative to the including file and then in `-I` order. `-D` definitions enter the same macro table as source definitions.

The preprocessed text is retained for `-E`. Tokens retain the source file ID and byte range, while line/column mapping is recomputed against the immutable source file. The current implementation records the primary file for expanded text; macro definition/argument backtraces are a documented incomplete diagnostic feature rather than fabricated locations.

## Reproducibility

No wall-clock predefined macros are injected. The preprocessing output is deterministic for fixed source bytes, include bytes, `-D` values, and `-I` ordering. Include identity canonicalization and dependency-file emission remain explicit follow-up work.
