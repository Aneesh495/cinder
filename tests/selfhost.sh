#!/bin/sh
set -eu
ccbin=${1:?compiler path}
if [ "$(uname -s)" != Linux ] || [ "$(uname -m)" != x86_64 ]; then
    printf '%s\n' 'Self-hosting requires Linux x86-64, GCC, Clang, CMake, Python 3, and the declared libc/startup objects. This host cannot verify the gate.' >&2
    exit 2
fi
for dependency in gcc clang cmake python3; do
    command -v "$dependency" >/dev/null || { printf '%s\n' "Missing bootstrap dependency: $dependency" >&2; exit 2; }
done
campaign_root=".agent-local/selfhost/$(date -u +%Y%m%dT%H%M%SZ)-$$"
exec python3 tools/selfhost.py "$ccbin" --profile "${CINDER_EXECUTION_PROFILE:-native}" --output "$campaign_root"
