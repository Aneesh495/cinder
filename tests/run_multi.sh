#!/bin/sh
set -eu
ccbin=${1:?compiler path}
ccbin=$(CDPATH= cd -- "$(dirname -- "$ccbin")" && pwd)/$(basename -- "$ccbin")
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cinder-multi.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
(
    cd "$tmp"
    "$ccbin" -fverify-each -c "$root/examples/tu_a.c" "$root/examples/tu_b.c"
    file tu_a.o | grep -q 'ELF 64-bit.*x86-64'
    file tu_b.o | grep -q 'ELF 64-bit.*x86-64'
)
if [ "$(uname -s)" = "Linux" ] && [ "$(uname -m)" = "x86_64" ]; then
    "$ccbin" "$root/examples/tu_a.c" "$root/examples/tu_b.c" -o "$tmp/multi"
    set +e
    "$tmp/multi"
    status=$?
    set -e
    test "$status" -eq 9
else
    if "$ccbin" "$root/examples/tu_a.c" "$root/examples/tu_b.c" -o "$tmp/multi" >"$tmp/link.out" 2>"$tmp/link.err"; then
        exit 1
    fi
    grep -q 'linking is unavailable' "$tmp/link.err"
fi
