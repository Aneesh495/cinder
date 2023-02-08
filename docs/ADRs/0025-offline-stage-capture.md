# 0025: Offline stage capture

Status: accepted

## Decision

Capture normal compiler objects at their existing stage boundaries into owned
streams. The driver holds one opaque Explorer context per translation unit.
AST/HIR/IR destruction does not invalidate captured text. Source/provenance
objects remain alive through final HTML rendering. Allocation capture records
the actual selected plans, physical locations and computed block liveness.

Track actual encoder PCs independently of DWARF publication. Build source/IR
links from those positions and generate CFG SVG from actual successor lists.
Use numeric function/block anchors so arbitrary C identifiers never become
HTML attributes. Escape source and diagnostics as data. Embed full pass CIR
snapshots and client controls in one offline document.

Retain requested pass directories; otherwise use an owned temporary directory
and clean known artifacts after rendering. Publish through the existing atomic
output boundary. A failed source report uses separate publication diagnostics
without clearing the compilation's fatal errors. Report failures return
nonzero and cannot overwrite an input source.

## Validation

Require byte-identical ordinary objects with/without Explorer, exact captured
stage and snapshot contents, independently read instruction boundaries and
`.text` ranges, valid fragment links, escaping and failed-source/output tests.
Browser checks cover SVG navigation, logical source links, searchable changes,
keyboard tabs and console errors. Native runs additionally execute the original
report-producing objects; cross-host checks are labeled separately.
