# 0022: Selected materialization and bulk-memory programs

Status: accepted

## Context

Literal, copy and bulk-memory IR operations still selected opcodes inside the
encoder. Their typed choices and fixed-register effects were absent from the
selected representation. Zero-emission lifetime and phi operations also
remained implicit encoder cases.

## Decision

Select explicit materialization, bulk and marker roles before allocation.
Materialization owns integer and canonical binary64 literal bits, pool
relocation kind/addend, result bank and byte programs. Copies select their
source/result bank. Floating negation owns the sign mask and XOR sequence.
Undefined placeholders retain explicit roles without asserting definedness.

Bulk plans own a 64-bit extent and selected zero/copy program. Destination and
source staging remains allocation dependent; the selected contract declares
RDI/RSI uses, RCX progress and actual scratch/flag effects. The fixed `va_copy`
representation selects a 24-byte copy. Marker plans emit zero bytes.

The machine verifier seals every field against verified input. The encoder
consumes the selected role, bits, byte program and fixups. It no longer
expands these raw IR operations. Argument/call and variadic protocols retain
separate target expansion work.

## Validation

Hand-specified Python readers compare programs and register effects with actual
ELF text, floating-pool bytes and relocation sites. An API probe corrupts
fifteen plan/operand fields, requires verifier and encoder rejection before
output mutation, then restores plans for object identity. GCC/Clang and IR
executions cover defined source behavior at O0/O2; native Linux additionally
executes the original Cinder objects. Optional baseline checks compare complete
objects with the preceding compiler. Unique evidence runs bind compiler,
core, sources, probes, tools and artifacts before and after execution.
