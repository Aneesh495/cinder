#!/bin/sh
set -eu
ccbin=${1:?compiler path}
if [ ! -f .agent-local/evidence/ACCEPTANCE.json ]; then
  printf '%s\n' 'No acceptance evidence has been generated; verification fails honestly.' >&2
  exit 1
fi
"$ccbin" --version >/dev/null
