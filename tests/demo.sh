#!/bin/sh
set -eu
ccbin=${1:?compiler path}
mkdir -p artifacts/demo
"$ccbin" -c examples/hello.c -o artifacts/demo/hello.o
"$ccbin" --emit-ir examples/hello.c > artifacts/demo/hello.ir
"$ccbin" --dump-tokens examples/hello.c > artifacts/demo/hello.tokens
