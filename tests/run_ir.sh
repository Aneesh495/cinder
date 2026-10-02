#!/bin/sh
set -eu
ccbin=${1:?compiler path}
"$ccbin" --emit-ir -fverify-each -O2 examples/hello.c | grep -q 'cmp.eq'
"$ccbin" --dump-regalloc examples/hello.c | grep -q 'allocation function=main'
