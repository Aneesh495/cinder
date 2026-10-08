# Anonymous members

Cinder supports C17 anonymous struct and union members defined by untagged
struct/union specifiers. Members are visible recursively through anonymous
containers. Named members remain explicit access boundaries. Physical fields,
offsets and aggregate identities remain nested in canonical IR.

Member expressions evaluate their base once and lower every physical step.
Const and volatile qualifiers propagate from each containing aggregate.
The same path resolver serves offsetof, static address initializers, designated
initializers and inferred array bounds. Initializer continuation resumes at
the designated physical subobject, including nested anonymous structs and
unions. Prior anonymous named members satisfy the flexible-array prefix rule.

Aggregate definitions permit 64 nested levels. Generated boundary probes
execute 63 and 64 levels and preserve prior output when rejecting 65 and
1,024 levels. These generated boundaries do not inflate authored-suite counts.

Run `make test-anonymous`, `make test-constraints` and `make test-memory`.
The Linux workflow retains original, assembled and canonical-IR objects,
reference executables, native observations and input hashes. Six ABI profiles
exchange integer, floating, mixed and large records and callbacks with GCC and
Clang at both optimization levels. External provider calls are explicitly
unsupported by the independent interpreter and require native execution.

Clang's acceptance of assignment through an exact const-qualified anonymous
container reproducer is retained in `tests/constraints/reference_policy.json`.
Cinder and GCC reject the assignment. This exception remains a reference
constraint discrepancy, not a successful Cinder compilation. Unknown
reference acceptance and any changed reproducer hash fail the diagnostic
harness. Register array decay is a separate C17 undefined-behavior rule.

Sources: [C17-compatible draft N1570](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf),
sections 6.7.2.1, 6.5.2.3, 6.7.9 and 6.5.16;
[WG14 issue 492](https://www.open-std.org/JTC1/SC22/WG14/issues/c11c17/issue0492.html)
clarifies that tagged bare declarations do not create anonymous members.

All feature-family and final acceptance claims require the complete audited,
source-bound native reports. Passing this focused target alone does not satisfy
the full master-prompt gates.
