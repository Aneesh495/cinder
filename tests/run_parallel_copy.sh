#!/bin/sh
set -eu
ccbin=${1:?compiler path}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cinder-copy.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
cc -std=c17 -I source tests/parallel_copy_probe.c source/regalloc.c source/arena.c source/diag.c source/source.c -o "$tmp/probe"
"$tmp/probe"
"$ccbin" --dump-regalloc examples/phi.c | grep -q 'parallel-copy'
