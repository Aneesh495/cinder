#!/bin/sh
set -eu
exec python3 tests/test_applications.py "$@"
