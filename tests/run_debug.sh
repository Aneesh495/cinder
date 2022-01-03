#!/bin/sh
set -eu
ccbin=${1:?compiler path}
"$ccbin" -g -S examples/hello.c >/dev/null
