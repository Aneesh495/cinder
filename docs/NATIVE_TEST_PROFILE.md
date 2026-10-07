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

The Ubuntu 24.04 hosted profile uses its GCC 13 compiler and Clang 22 from
the [official LLVM Noble repository](https://apt.llvm.org/). Installation
checks the repository key fingerprint and uses a dedicated signed-by keyring.
The selected major version is explicit; raw campaigns record actual compiler
versions. LLVM supplies reference compilation, assembly comparison, and the
sanitizer host build. Cinder continues to own source compilation and encoding.

Run 37696602274 passed 639 native source observations, 1,278 CIR comparisons,
and the IR and SSA campaigns, then Clang 18 rejected
`tests/compound_literals/enum_unevaluated_effects.c`. Its complete literal
occurs inside `sizeof` in a block enumeration and does not evaluate its
initializers. The original failure is retained under
`.agent-local/native-failure-fba74e1`. GCC and Clang 22 independently compiled
and executed the unchanged unevaluated-source regressions at O0/O2 on the
development Linux VM. Upgrading the declared reference compiler preserves
all source payloads, reference equality checks, and required campaign sizes.
