#!/bin/sh
set -eu
ccbin=${1:?compiler path}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cinder-varargs.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
"$ccbin" -fsyntax-only examples/varargs.c
"$ccbin" --emit-ir -O0 examples/varargs.c | grep -q 'va_arg'
"$ccbin" --interpret -O0 examples/varargs.c | grep -q '=> 42'
"$ccbin" -S examples/varargs.c -o "$tmp/varargs.s"
clang -target x86_64-unknown-linux-gnu -c "$tmp/varargs.s" -o "$tmp/varargs-ref.o"
"$ccbin" -fverify-each -c examples/varargs.c -o "$tmp/varargs.o"
"$ccbin" --interpret examples/stack_args.c | grep -q '=> 36'
"$ccbin" -S examples/stack_args.c -o "$tmp/stack.s"
clang -target x86_64-unknown-linux-gnu -c "$tmp/stack.s" -o "$tmp/stack-ref.o"