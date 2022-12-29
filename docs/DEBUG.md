# Debug information profile

`-g` emits original DWARF version 4 frame records, line tables, compilation
units, subprogram entries and their referenced types. The profile supports source breakpoints,
stepping, type inspection, unchanged scalar parameters and backtraces across
translation units. Automatic/global variable locations and parameter writes
remain open. The complete debugging acceptance campaign also
requires accurate supported value locations in native debugger sessions.

The profile uses DWARF32 lengths, eight-byte target addresses, little-endian
serialization, code alignment 1 and data alignment -8. It follows the
[June 10, 2010 DWARF 4 specification](https://dwarfstd.org/doc/DWARF4.pdf),
sections 3.1, 3.3, 6.2 and 6.4. `.debug_frame` is a non-allocated debugger section. This is not an
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

## Source rows and compilation units

HIR function declarations carry their scalar source location into IR. The
encoder records the actual byte offset before each emitted instruction and
terminator. Zero-byte lifetime markers, phi nodes and removed operations do
not create rows. Rows at the same address collapse to the final source
location. Each function starts a separate line sequence and ends at its exact
symbol extent. The post-prologue row follows frame setup and incoming ABI
register staging; parameter stores can still follow that row. It does not
claim that parameter values are already available.

The line program records logical file, line and column, statement changes and
the prologue boundary. Negative line deltas use signed LEB128 with defined
division, avoiding an implementation-defined signed shift. Included files and
`#line` directives use the source manager's expansion locations. Macro
instructions refer to their invocation, rather than attributing executable
instructions to a macro definition in a header.

`.debug_info` contains a DWARF4 compilation unit and a child subprogram entry
for each emitted function. Entries record declaration coordinates, external
visibility, relocated start address, exact byte extent, return type and a frame-base
expression using `DW_OP_call_frame_cfa`. Section offsets relocate to
`.debug_abbrev` and `.debug_line`, allowing the linker to concatenate units.
The CU language is DWARF4's C99 profile identifier. This does not change the
compiler's documented C17 source profile.

The CU compilation directory is the resolved physical working directory.
`-ffile-prefix-map` maps this directory and source names; macro-only mapping
does not change DWARF. Both absolute source names and compilation directories
must map consistently for byte-identical relocated objects. Dependency output
continues to name physical inputs. Logical names introduced by `#line` may
require GDB source-path substitution to display physical source text.

Construction rejects incomplete function maps, invalid file/include-frame
identities, unordered or out-of-function rows, absent prologue rows and
repeated finalization before adding source sections. The object owns all
serialized bytes and fixup names after source construction returns.

## Types and parameter declarations

`debug_type.c` emits basic target types, const/volatile/restrict qualification,
pointers, arrays, structures, unions, bitfields, enumerations and function
pointer signatures. Complete records use the semantic layout's byte size,
member offsets and DWARF4 data-bit offsets. Incomplete records use declaration
entries; flexible arrays have an open subrange. Enum names and signed values
belong to the type context, so they survive AST retirement. Named parameters
carry the actual declarator token coordinates through HIR and IR.

Type references are CU-relative four-byte offsets. A queue registers types
before emitting their referenced children, then patches forward references.
This handles recursive records without recursive serialization. Qualified
variants are keyed by their actual type record and remaining qualifiers,
including separately allocated qualified copies with the same semantic tag
identity. Empty function signatures use a childless abbreviation.

Function and parameter types are retained even when optimization removes their
uses. Parameters assigned in the body or used through their object address
have no location attribute and remain unavailable. Local/global variable
entries, parameter write tracking, lexical value ranges and retained typedef
aliases remain open. The current type graph starts
from emitted function signatures, rather than advertising every source typedef
as a separately retained debug declaration.

Debug construction borrows the IR module, source manager and type context only
while serializing. The type ownership probe destroys AST/sema before HIR
lowering and HIR before machine/debug construction. It then destroys IR, types
and source storage before publishing ELF from the owned bytes and fixups.
Canonical CIR remains schema 6; source-only declaration coordinates and enum
debug names are not serialized, and `cinderir` does not accept `-g`.

## Unchanged scalar parameter locations

Before optimization, IR construction marks a parameter unavailable if its slot
is assigned or its address is formed. The backend records locations only for
the remaining scalar parameters. This is a conservative analysis of the
parameter object; it does not assume that a pointer's pointee stays unchanged.
Aggregate parameter locations remain open.

Each location list begins with an explicit base-selection entry relocated to
the function. Address pairs are offsets relative to that base. Register
arguments use their real System V GPR/SSE register until incoming staging has
completed, then a `DW_OP_fbreg` location in the actual private ABI save area.
Stack arguments use their incoming CFA-relative address throughout. The
canonical frame address is the same caller stack address before and after
frame setup and `leave`, so the saved home remains addressable at the return
instruction. Function-symbol extents bound every list.

The private incoming save slots are distinct from local objects, spills and
outgoing call storage. They preserve an unchanged parameter even when its IR
loads are promoted or removed. An assigned or escaped parameter cannot use
that original home as its current value. No stale original value is supplied
for those parameters.

## Verification and use

`make test-debug` runs frame, source-table and type checks. It requires LLVM's `llvm-dwarfdump`, `llvm-objdump` and
`llvm-mc`, plus GCC and Clang reference drivers. On Linux x86-64 it also
requires GDB. The test checks O0/O2 frame tables against independently decoded
instruction boundaries, actual saved-register store/load bytes, all return
paths, original/assembled section identity and relocation meaning. GCC/Clang
execute the single-unit and two-unit fixtures. The Linux path links original
objects and records GDB backtraces with the DWARF unwinder selected.

The source reader independently interprets the line program and abbreviation
forms, resolves original relocation meaning, and checks every row against
disassembled instruction boundaries. It verifies subprogram symbol extents,
header and macro origins, backward logical line numbers, empty units,
noreturn functions and prefix-map ordering. Every debug section and relocation
is compared with LLVM-assembled output. Two relocated checkout trees must
produce identical debug objects. On Linux, six additional GDB sessions check
source breakpoints, stepping and caller chains at O0/O2.

Twelve malformed section/relocation records must fail both output writers.
The object writer preserves a complete previous artifact; the assembly writer
emits no bytes for the rejected records. Debug-disabled objects preserve the
normal section layout and text bytes. Raw commands, object hashes, tool hashes,
recordings and reports live under `.agent-local/unwind/` and
`.agent-local/debug-lines/`. A second API probe rejects twenty-two malformed
source/ABI maps and repeated finalization without adding sections or fixups.
Type reports under `.agent-local/debug-types/` check independently specified
record sizes, field/bit positions, array bounds, signed enum values, recursive
references, qualifier chains and callback signatures. Location-list decoding
checks function bases/ranges, ABI register numbers and CFA homes against actual
staging store bytes. Six corruptions must
fail the independent layout assertions. GCC and Clang execute the fixture at
both levels. The native path checks GDB type inspection, integer/enum/SSE and
pointer values, fields reached through a record pointer, and an explicitly
unavailable assigned parameter, alongside the separate lifetime probe.
Explicit `--execution-profile emulated` runs preserve that classification;
they do not count as native debugger acceptance.

On a Linux x86-64 host:

```sh
make build test-debug
build/cindercc -O0 -g tests/debug/unwind.c -o unwind-example
gdb -ex 'break unwind.c:3' -ex run -ex bt -ex next ./unwind-example
```

The full debug gate still requires accurate supported
variable locations across its native debugger sessions. These source and
frame tests do not replace that gate.
