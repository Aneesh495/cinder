# Native test process profile

Authored source, IR round-trip, and initializer native runners declare a
64 MiB Linux process stack budget before launching subprocesses. GCC, Clang,
and Cinder executables inherit the same limit. The soft limit before and
after configuration and the hard limit appear in raw observations. A host
whose hard limit prevents this budget fails the runner. Compiler frame
validation retains its existing bounded allocation rules.

This budget matters for large unevaluated compound literals. Ubuntu GCC 13
at O0 reserves and probes a 16 MiB frame for `sizeof (char[16777216]){1}`.
Its reference executable exhausted the default 8 MiB stack, while Cinder's
owned object and IR round trip passed. The original failure and executable
are retained from run 37695187093. Disassembly shows the page-probe loop;
GCC 15 with the same frame-probe option independently reproduced failure
at 8 MiB and success at 64 MiB on the development x86 Linux VM.

The source, initializer payload, optimization levels, expected values,
execution timeouts, reference agreement rules, and required campaign sizes
remain unchanged. Stack exhaustion receives no accepted reference outcome.
Raw reproduction artifacts are under
`.agent-local/linux-vm/share/stack-profile`; hosted artifacts are under
`.agent-local/native-failure-4ed6e32`.
