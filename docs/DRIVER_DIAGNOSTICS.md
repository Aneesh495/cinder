# Dependencies and diagnostic output

`-M` uses the original preprocessor to emit a dependency rule without parsing
the C body. `-MD` stages a dependency file and publishes it after successful
compilation. `-MF PATH` chooses that file; `-MQ TARGET` supplies one quoted
target. The default target is the object output, assembly output, or source
unit's default object name. The default dependency file replaces the target's
extension with `.d`.

Dependencies list the canonical root first and actual included files in sorted
order. Guarded repeated includes appear once. Inactive includes are absent.
Make escaping covers spaces, tabs, hashes, dollars, backslashes and colons.
Paths with line breaks receive an error. Dependency output cannot overwrite a
source or collide with the primary output. Existing source/output aliases and
directory destinations are checked before publication. Each output uses an
owned temporary file and atomic replacement; separate files are separate
publications.

```sh
build/cindercc -M -MQ 'build/app.o' examples/hello.c
build/cindercc -c -MD -MF build/app.d examples/hello.c -o build/app.o
```

Multiple source units use independent default dependency files. An explicit
`-MF` requires one unit. `-M` also rejects mixed stage-inspection output.
`--` ends option parsing for filenames that start with a dash.

`-std=c17-core`, `-std=c17` and `--std=c17-core` select the same documented
core profile. Other language modes receive a nonzero diagnostic.

`--diagnostics=json` writes schema-one diagnostic batches to stderr. Each
translation unit can produce one newline-delimited batch; requested stage or
artifact output stays on its normal stream. `--diagnostics=text` selects the
readable default. A diagnostic includes severity, message, its bounded source
range, spelling range, macro-definition range and actual include backtrace.
Repeated inclusion has distinct frame IDs and `#line` state. Source tokens
retain their exact preprocessed origin rather than searching by physical
filename/offset alone. Offsets are zero-based bytes;
lines and columns are one-based. Unknown locations are null. UTF-8 paths and
JSON control characters are escaped consistently. Source-read/UTF-8 failures
use the same formatter as preprocessing and semantic errors. `--dump-ast`
now runs semantic analysis before printing the typed tree.

```sh
build/cindercc --diagnostics=json -fsyntax-only examples/hello.c
```

`make test-driver` retains 41 actual commands, uses GNU Make to parse emitted
rules, checks the active include inventory, diagnostic token ranges and macro
definitions, and verifies preservation of source/previous outputs on failures.
`make test-runtime-headers` includes the opaque POSIX directory-handle ABI used
by destination checks. `make test-source-instances` checks repeated-header
logical locations, macro definitions, twelve nested semantic/preprocessor/UTF-8
failure chains and logical-line execution. [Prefix mapping](REPRODUCIBILITY.md)
has separate file/macro options, one-pass substitution and physical dependency
identities. Debug output and the full product gate still require further work.

See [the editable publication flow](diagrams/driver-publication.mmd).
