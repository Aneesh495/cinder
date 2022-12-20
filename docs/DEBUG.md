# Debug information profile

`-g` currently emits original DWARF version 4 stack-unwind records in
`.debug_frame`. These records support debugger backtraces and function
breakpoints. Source line tables, type entries and variable locations remain
open. A function name in a backtrace does not establish source-level debugging
or the complete debugging acceptance campaign.

The profile uses DWARF32 lengths, eight-byte target addresses, little-endian
serialization, code alignment 1 and data alignment -8. It follows the
[June 10, 2010 DWARF 4 specification](https://dwarfstd.org/doc/DWARF4.pdf),
section 6.4. `.debug_frame` is a non-allocated debugger section. This is not an
exception-unwinding runtime or an `.eh_frame` implementation.

## Actual frame rules

The canonical frame address is the caller's stack pointer at the call site.
At function entry it is `rsp + 8`; the return address is stored at `CFA - 8`.
After `push rbp`, the CFA becomes `rsp + 16`, with the old RBP at `CFA - 16`.
After `mov rbp,rsp`, the CFA becomes `rbp + 16`. Stack allocation and outgoing
call storage do not change that stable base.

The encoder records each selected R12-R15 save after its actual store. A slot
at `rbp + offset` is described as `CFA + offset - 16`. Each restore changes the
register's rule to `same_value` after the restoring move. After `leave`, the
CFA becomes `rsp + 8` and RBP is restored. A remember/restore-state pair wraps
every epilogue, so a later laid-out CFG block again has the function body's
rules. Noreturn traps retain body rules throughout their range.

Each function has an FDE covering its actual text-symbol size. Its initial
address has an `R_X86_64_64` relocation to the function. Its CIE offset has an
`R_X86_64_32` relocation to the debug section. The second relocation lets the
linker combine multiple translation units without leaving FDEs tied to the
first unit's CIE. `-S` emits the same bytes and relocations with local section
labels. Normal `-c` uses the original ELF writer.

See [the encoder mapping](diagrams/debug-frame.mmd).

## Verification and use

`make test-unwind` requires LLVM's `llvm-dwarfdump`, `llvm-objdump` and
`llvm-mc`, plus GCC and Clang reference drivers. On Linux x86-64 it also
requires GDB. The test checks O0/O2 frame tables against independently decoded
instruction boundaries, actual saved-register store/load bytes, all return
paths, original/assembled section identity and relocation meaning. GCC/Clang
execute the single-unit and two-unit fixtures. The Linux path links original
objects and records GDB backtraces with the DWARF unwinder selected.

Twelve malformed section/relocation records must fail both output writers.
The object writer preserves a complete previous artifact; the assembly writer
emits no bytes for the rejected records. Debug-disabled objects preserve the
normal section layout and text bytes. Raw commands, object hashes, tool hashes,
recordings and reports live under `.agent-local/unwind/`.

On a Linux x86-64 host:

```sh
make build test-unwind
build/cindercc -O0 -g tests/debug/unwind.c -o unwind-example
gdb -ex 'break unwind_leaf' -ex run -ex bt ./unwind-example
```

The full debug gate still requires source breakpoints, stepping and accurate
supported variable locations across its native debugger sessions. The unwind
test does not replace that gate.
