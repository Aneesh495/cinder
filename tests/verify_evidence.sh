#!/bin/sh
set -eu
ccbin=${1:?compiler path}
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
exec python3 -B "$root/tools/verify_evidence.py" "$ccbin"
