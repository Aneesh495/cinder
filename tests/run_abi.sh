#!/bin/sh
set -eu
ccbin=${1:?compiler path}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cinder-abi.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
"$ccbin" -S examples/hello.c -o "$tmp/hello.s"
grep -q '^\.globl add' "$tmp/hello.s"
grep -q '^  \.byte' "$tmp/hello.s"
"$ccbin" --dump-regalloc examples/hello.c | grep -q 'allocation function=add'
