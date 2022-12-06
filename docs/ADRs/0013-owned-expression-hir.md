# Own expression HIR before moving control and storage plans

The prior CFG lowerer consumed semantic AST expressions directly. Introducing
an inspection-only copy would not create an actual representation boundary.
The expression consumer now reads a distinct owned graph with explicit place
loads, addresses, conversions, selected generic associations and variadic
operations. The retained ownership probe distinguishes AST mutation from HIR
mutation through actual interpreted CFG behavior.

Semantic target types and binding metadata stay borrowed while declaration,
scope-lifetime, label/switch and static-address plans are transferred separately.
Structured control and object initialization remain visible dependencies, not
an advertised complete HIR stage. The driver destroys HIR before its semantic
unit and target-type arena. Independent IR readers need neither HIR nor AST.

Stable IDs follow construction order. The AST-to-HIR lookup uses address-keyed
storage internally but never prints those addresses or uses hash traversal to
order output. Owned strings/operand arrays prevent expression storage from
aliasing the parser arena. Full graph verification precedes CFG lowering.
