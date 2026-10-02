#!/bin/sh
set -eu
ccbin=${1:?compiler path}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cinder-float.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
"$ccbin" -fsyntax-only examples/float.c
"$ccbin" --emit-ir -O0 examples/float.c | grep -q 'fadd'
"$ccbin" -S examples/float.c -o "$tmp/float.s"
clang -target x86_64-unknown-linux-gnu -c "$tmp/float.s" -o "$tmp/float-ref.o"
"$ccbin" -fverify-each -c examples/float.c -o "$tmp/float.o"
python3 tests/check_elf_sections.py "$tmp/float.o"
