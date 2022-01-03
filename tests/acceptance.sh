#!/bin/sh
set -eu
ccbin=${1:?compiler path}
mkdir -p .agent-local/evidence
"$ccbin" --version > .agent-local/evidence/compiler.version
"$ccbin" -fverify-each -O2 -c examples/hello.c -o .agent-local/evidence/hello.o
file .agent-local/evidence/hello.o > .agent-local/evidence/object.file
"$ccbin" --emit-ir -O2 examples/hello.c > .agent-local/evidence/hello.ir
"$ccbin" --dump-tokens examples/hello.c > .agent-local/evidence/hello.tokens
