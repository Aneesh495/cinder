#!/bin/sh
set -eu
ccbin=${1:?compiler path}
mkdir -p .agent-local/benchmarks
/usr/bin/time -p "$ccbin" -O0 -c examples/hello.c -o .agent-local/benchmarks/hello-o0.o 2>.agent-local/benchmarks/o0.time
/usr/bin/time -p "$ccbin" -O2 -c examples/hello.c -o .agent-local/benchmarks/hello-o2.o 2>.agent-local/benchmarks/o2.time
