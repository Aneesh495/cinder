#!/bin/sh
set -eu
ccbin=${1:?compiler path}
printf '%s\n' 'Self-hosting requires the declared Linux x86-64 profile; this macOS host records the gate as unverified.'
"$ccbin" --version
