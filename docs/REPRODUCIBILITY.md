# Reproducible source names

`-ffile-prefix-map=OLD=NEW` rewrites source names shown by diagnostics and
token inspection, and string literals produced by `__FILE__`.
`-fmacro-prefix-map=OLD=NEW` rewrites only `__FILE__`. These are literal byte
prefixes, including logical filenames established by `#line`. The last
matching option wins across both option kinds for macro names; macro-only
options do not participate in ordinary display names. Substitution happens
once, with no recursive remapping. Empty prefixes and replacements are valid.
The first equals sign after the option separates OLD from NEW, so NEW may
contain equals signs.

```sh
SOURCE_DATE_EPOCH=0 build/cindercc -O2 -c \
  "-ffile-prefix-map=$PWD=/src" examples/hello.c -o build/hello.o
```

Include opening, canonical identities, dependency rules, source byte offsets,
and output collision checks use physical names. Mapping a path does not change
which file is read. Source-date settings continue to control `__DATE__` and
`__TIME__`; default builds use the documented current-date policy. Each source
manager owns its rules and cached names. Independent translation units cannot
inherit logical filenames or macros from an earlier unit.

`make test-prefix-maps` checks two relocated source trees with root/header
`__FILE__`, interpreter results and identical original ELF objects at O0/O2.
It checks physical dependencies, overlapping/order-sensitive rules, logical
line controls, text/JSON/include/macro diagnostic names, token names, invalid
options preserving previous outputs, and independent units in one driver
process. Raw commands and observations remain under `.agent-local/prefix-maps/`.

There is no debug-prefix option yet. `-fdebug-prefix-map` receives an unsupported
option error until a usable debug profile is implemented.

See [the source-name flow](diagrams/source-names.mmd).
