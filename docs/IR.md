# Cinder IR and verification

Cinder's IR is an owned representation in `source/ir.c`, not an LLVM textual dialect. Values have stable numeric IDs, blocks have explicit terminators, and local memory is represented by `local.load` and `local.store` operations. Calls carry a symbol name and ordered argument IDs. CFG predecessor/successor lists are maintained as edges are created.

`source/ir_verify.c` checks function entry blocks, value ranges, local-slot ranges, call operands, terminator values, and branch targets. The driver can run it at each boundary with `-fverify-each`. A verifier failure is fatal and is never converted into a native success artifact.

`source/ir_interp.c` is an independent bounded evaluator. It owns value arrays, local objects, call frames, arithmetic definedness checks, and step limits. It can execute Cinder-defined functions without entering the native backend. External calls are explicitly classified as unsupported by the interpreter rather than returning a guessed value.

The IR now carries explicit phi incoming block IDs and values. Join-phi insertion is limited to locals with a concrete store in every predecessor; the interpreter selects the incoming value by predecessor, the verifier checks edge membership, and the native bootstrap materializes the same slot-backed value. Full SSA renaming, critical-edge parallel-copy lowering, and complete out-of-SSA allocation remain open gates.
