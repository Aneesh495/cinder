#!/bin/sh
set -eu
ccbin=${1:?compiler path}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cinder-object.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
"$ccbin" -c examples/hello.c -o "$tmp/hello.o"
file "$tmp/hello.o" | grep -q 'ELF'
if command -v readelf >/dev/null 2>&1; then readelf -h "$tmp/hello.o" | grep -q 'ELF64'; fi
