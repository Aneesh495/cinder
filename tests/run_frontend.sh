#!/bin/sh
set -eu
ccbin=${1:?compiler path}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cinder-frontend.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
"$ccbin" -fsyntax-only examples/include_demo.c
"$ccbin" --interpret examples/loop.c | grep -q '=> 3'
"$ccbin" -DCLI_VALUE=42 --interpret tests/cli_define.c | grep -q '=> 42'
"$ccbin" -E examples/macro_args.c | grep -q '20'
if "$ccbin" -c tests/invalid_undeclared.c -o "$tmp/bad.o" >/dev/null 2>"$tmp/undeclared.err"; then exit 1; fi
test ! -e "$tmp/bad.o"
grep -q "undeclared identifier" "$tmp/undeclared.err"
if "$ccbin" -fsyntax-only tests/invalid_lvalue.c >/dev/null 2>"$tmp/lvalue.err"; then exit 1; fi
grep -q 'lvalue' "$tmp/lvalue.err"
