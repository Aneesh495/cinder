#!/bin/sh
set -eu
ccbin=${1:?compiler path}
"$ccbin" --emit-ir -O2 examples/hello.c | grep -q 'cmp.eq'
"$ccbin" --emit-ir -O2 examples/hello.c | grep -q 'optimized IR'
"$ccbin" --dump-mir examples/loop.c | grep -q 'loop-header'
"$ccbin" --dump-mir -O2 examples/opt.c | grep -Eq 'dead=[1-9]'
"$ccbin" --dump-regalloc examples/hello.c | grep -q 'allocation function=main'
