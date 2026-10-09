#!/bin/sh
set -eu
ccbin=${1:?compiler path}
mkdir -p artifacts/benchmarks
/usr/bin/time -p "$ccbin" -O0 -c examples/hello.c -o artifacts/benchmarks/hello-o0.o 2>artifacts/benchmarks/o0.time
/usr/bin/time -p "$ccbin" -O2 -c examples/hello.c -o artifacts/benchmarks/hello-o2.o 2>artifacts/benchmarks/o2.time
