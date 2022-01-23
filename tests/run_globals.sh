#!/bin/sh
set -eu
ccbin=${1:?compiler path}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cinder-globals.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
"$ccbin" -fverify-each --emit-ir -O0 examples/globals.c | grep -q 'global answer'
"$ccbin" -fsyntax-only examples/aggregate_layout.c
"$ccbin" -fverify-each -c examples/globals.c -o "$tmp/globals.o"
file "$tmp/globals.o" | grep -q 'ELF 64-bit.*x86-64'
python3 tests/check_elf_sections.py "$tmp/globals.o"
"$ccbin" --interpret examples/globals.c | grep -q '=> 42'
