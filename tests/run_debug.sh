#!/bin/sh
set -eu
ccbin=${1:?compiler path}
exec python3 tests/test_unwind.py "$ccbin"
