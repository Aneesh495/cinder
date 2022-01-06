# Cinder IR and verification

Cinder's IR is an owned representation in `source/ir.c`, not an LLVM textual dialect. Values have stable numeric IDs, blocks have explicit terminators, and local memory is represented by `local.load` and `local.store` operations. Calls carry a symbol name and ordered argument IDs. CFG predecessor/successor lists are maintained as edges are created.

`source/ir_verify.c` checks function entry blocks, value ranges, local-slot ranges, call operands, terminator values, and branch targets. The driver can run it at each boundary with `-fverify-each`. A verifier failure is fatal and is never converted into a native success artifact.

`source/ir_interp.c` is an independent bounded evaluator. It owns value arrays, local objects, call frames, arithmetic definedness checks, and step limits. It can execute Cinder-defined functions without entering the native backend. External calls are explicitly classified as unsupported by the interpreter rather than returning a guessed value.

The first optimizer increment contains constant folding for integer operations and copy propagation. It reports changed functions, changed instructions, and folded constants. Division by zero, signed division overflow, and invalid shifts are not folded. The pass pipeline is deliberately conservative; loop motion, SSA promotion, alias analysis, and translation validation are not marked complete until their independent checks exist.
