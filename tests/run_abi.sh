#!/bin/sh
set -eu
ccbin=${1:?compiler path}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cinder-abi.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
"$ccbin" -S examples/hello.c -o "$tmp/hello.s"
grep -q 'call' "$tmp/hello.s"
