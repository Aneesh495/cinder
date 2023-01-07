# Owned lexical scopes through native encoding

Status: accepted

C declarations can start in the middle of a block. A debugger must select an
outer name before an inner declarator and the inner name afterward. Assigning
every object the entire function range gives incorrect shadowing, even if its
value is marked unavailable.

HIR lowering creates IR-owned scope records and automatic-object descriptors.
Each C declaration starts a nested suffix scope. Subsequent statements in that
C scope inherit the suffix; a child block inherits the active parent suffix.
An enclosing block's later declarations do not change an earlier child's
parent. Parameters remain direct subprogram children. Local names are copied
into the IR arena; types belong to the independent type context.

Instructions and terminators carry their scope ID. Rewrites retain it, and
critical-edge branches inherit the predecessor's scope. Encoding records the
actual begin/end byte offset of every selected operation and terminator.
Zero-byte operations have empty ranges. A scope contains the nonempty ranges
of its own operations and descendants, with adjacent ranges merged.

DWARF4 lexical blocks use original `.debug_ranges` lists, explicit relocated
function bases and CU-relative section offsets. Each local declaration records
its name, type and exact declaration coordinates. Empty location lists make
values unavailable until a separate storage/availability analysis establishes
a current value. A reserved frame slot is insufficient evidence of one.

Construction verifies scope parent order, the 1023-level debug nesting bound,
declaration ownership and slot/type identity, and an exact, contiguous mapping
of all IR operations to actual encoded boundaries. It rejects malformed maps
before adding any source section. The independent reader checks range
containment, instruction boundaries, before/after shadow selection and loop
declarations. The source/API lifetime probe retires AST and HIR before debug
construction, then IR/types/sources before ELF publication.

Canonical CIR stays at schema 6 and omits source-only scope metadata.
Automatic value tracking, mutable parameter locations and global declarations
remain separate work. The debug gate remains open until supported values are
checked in the required native campaign.
