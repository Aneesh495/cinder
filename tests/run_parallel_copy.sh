#!/bin/sh
set -eu
ccbin=${1:?compiler path}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cinder-copy.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
cc -std=c17 -I source tests/parallel_copy_probe.c source/regalloc.c source/mir_select.c source/ir_ops.c source/stack_layout.c source/liveness.c source/alloc_check.c source/type.c source/arena.c source/diag.c source/source.c source/literal.c -o "$tmp/probe"
"$tmp/probe"
"$ccbin" --dump-regalloc examples/phi.c | grep -q 'parallel-copy'
