# Diagnostics guide

Diagnostics are accumulated in `CinderDiagnostics` with severity, source range, and owned message text. Source locations use immutable file IDs, byte offsets, lengths, and computed line/column values. Lexical errors, malformed directives, unsupported aggregate declarations, undeclared names, incompatible calls/returns, invalid lvalues, and illegal loop control produce errors and nonzero status.

Diagnostics are emitted on stderr separately from `-E`, dumps, assembly, and object output. The driver checks the error count before every native boundary. A syntax or semantic failure cannot be turned into an empty object or a successful link.

Current limitations are intentionally visible: expanded tokens retain the primary file location but do not yet print a full macro-definition backtrace; JSON diagnostics are not yet exposed; line directives and system-header classification are not yet implemented. These are tracked as product work rather than simulated in output.
