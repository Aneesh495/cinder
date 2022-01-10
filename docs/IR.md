# Cinder IR and verification

Cinder's IR is an owned representation in `source/ir.c`, not an LLVM textual dialect. Values have stable numeric IDs, blocks have explicit terminators, and local memory is represented by `local.load` and `local.store` operations. Calls carry a symbol name and ordered argument IDs. CFG predecessor/successor lists are maintained as edges are created.

`source/ir_verify.c` checks function entry blocks, value ranges, local-slot ranges, call operands, terminator values, and branch targets. The driver can run it at each boundary with `-fverify-each`. A verifier failure is fatal and is never converted into a native success artifact.

`source/ir_interp.c` is an independent bounded evaluator. It owns value arrays, local objects, call frames, arithmetic definedness checks, and step limits. It can execute Cinder-defined functions without entering the native backend. External calls are explicitly classified as unsupported by the interpreter rather than returning a guessed value.

The first CFG analysis increment now computes reachable reverse postorder, immediate dominators, and conservative back-edge loop headers. `cinder_forward_local_memory` forwards a local load to a prior same-block stored value without removing the store, preserving cross-block semantics. A dead-code pass removes unused pure integer operations while retaining calls, stores, loads, and other effectful operations. Optimization statistics include forwarded loads and removed pure instructions.
