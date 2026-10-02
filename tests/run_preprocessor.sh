#!/bin/sh
set -eu
ccbin=${1:?compiler path}
out=$("$ccbin" -E examples/preprocess.c)
printf '%s\n' "$out" | grep -q 'int answer'
if printf '%s\n' "$out" | grep -q 'inactive branch'; then exit 1; fi
