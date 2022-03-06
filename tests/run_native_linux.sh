#!/bin/sh
set -eu
ccbin=${1:?compiler path}
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ "$(uname -s)" != "Linux" ] || [ "$(uname -m)" != "x86_64" ]; then
    printf '%s\n' 'native Linux x86-64 smoke requires Linux x86_64' >&2
    exit 2
fi
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cinder-native.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
"$ccbin" -O2 -c "$root/examples/hello.c" -o "$tmp/hello.o"
readelf -h "$tmp/hello.o" | grep -q 'ELF64'
readelf -S "$tmp/hello.o" | grep -q '.rela.text'
"$ccbin" -O2 "$root/examples/hello.c" -o "$tmp/hello"
set +e
"$tmp/hello"
status=$?
set -e
test "$status" -eq 42
"$ccbin" -O2 "$root/examples/globals.c" -o "$tmp/globals"
set +e
"$tmp/globals"
status=$?
set -e
test "$status" -eq 42
(
    cd "$tmp"
    "$ccbin" -c "$root/examples/tu_a.c" "$root/examples/tu_b.c"
    cc tu_a.o tu_b.o -o multi
)
set +e
"$tmp/multi"
status=$?
set -e
test "$status" -eq 9
