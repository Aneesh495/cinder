#!/bin/sh
set -eu
ccbin=${1:?compiler path}
mkdir -p .agent-local/demo
"$ccbin" -c examples/hello.c -o .agent-local/demo/hello.o
"$ccbin" --emit-ir examples/hello.c > .agent-local/demo/hello.ir
"$ccbin" --dump-tokens examples/hello.c > .agent-local/demo/hello.tokens
