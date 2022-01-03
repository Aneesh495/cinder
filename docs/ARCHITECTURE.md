# Architecture

```mermaid
flowchart LR
  C[C source] --> PP[Source manager and preprocessor]
  PP --> TOK[Provenance tokens]
  TOK --> AST[Parser AST]
  AST --> SEMA[Typed semantic model]
  SEMA --> HIR[Typed HIR]
  HIR --> IR[CFG and SSA IR]
  IR --> OPT[Verified conservative passes]
  OPT --> MIR[x86-64 MIR]
  MIR --> RA[Linear-scan allocation]
  RA --> ENC[Original encoder]
  ENC --> ELF[ELF64 relocatable object]
  ELF --> LINK[Declared system linker]
  LINK --> RUN[Native Linux execution]
  IR --> INT[Independent IR interpreter]
  TOK --> INS[Inspection artifacts]
  OPT --> INS
  RA --> INS
```

Source files are arranged by invariants rather than by generated framework layers. `source/source.*` owns immutable input and locations; `token.*` and `lex.*` own token identity; `pp.*` owns macro state; `type.*`, `ast.*`, `parser.*`, and `sema.*` own the typed frontend; `ir.*` and `ir_verify.*` own the representation; `opt.*` and `regalloc.*` own transformations and location validity; `x86_64.*` and `elf64.*` own native output; `driver.*` and `inspect.*` own user-facing orchestration.

The current architecture records contracts before each incomplete boundary is promoted to a complete feature. In particular, a frontend-only parse result is never treated as a native compiler result, and a requested unsupported construct is a fatal diagnostic rather than an omitted AST node.
