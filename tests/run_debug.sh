#!/bin/sh
set -eu
ccbin=${1:?compiler path}
python3 tests/test_unwind.py "$ccbin"
python3 tests/test_debug_lines.py "$ccbin"
exec python3 tests/test_debug_types.py "$ccbin"
