#!/bin/sh
set -eu
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cinder-storage.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
clang -std=c17 -Wall -Wextra -Werror -fsanitize=address,undefined -I source tests/arena_probe.c source/arena.c -o "$tmp/probe"
"$tmp/probe"
