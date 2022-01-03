#!/bin/sh
set -eu
ccbin=${1:?compiler path}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cinder-smoke.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
"$ccbin" --version
"$ccbin" -fsyntax-only examples/hello.c
"$ccbin" --dump-tokens examples/hello.c >"$tmp/tokens"
grep -q 'identifier' "$tmp/tokens"
"$ccbin" --emit-ir -O2 examples/hello.c >"$tmp/ir"
grep -q 'function main' "$tmp/ir"
"$ccbin" -S examples/hello.c -o "$tmp/hello.s"
grep -q 'add:' "$tmp/hello.s"
"$ccbin" -c examples/hello.c -o "$tmp/hello.o"
file "$tmp/hello.o" | grep -q 'ELF'
"$ccbin" -E examples/preprocess.c | grep -q 'int answer'
