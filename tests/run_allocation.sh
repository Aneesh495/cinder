#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cinder-allocation.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
cc -std=c17 -O2 -Wall -Wextra -Werror -I source tests/allocation_probe.c source/regalloc.c source/stack_layout.c source/liveness.c source/alloc_check.c source/type.c source/arena.c source/diag.c source/source.c source/literal.c -o "$tmp/probe"
mkdir -p .agent-local/allocation
"$tmp/probe" 10000 > "$tmp/observations.jsonl"
mv "$tmp/observations.jsonl" .agent-local/allocation/observations.jsonl
