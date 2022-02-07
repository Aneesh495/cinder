#!/bin/sh
set -eu
ccbin=${1:?compiler path}
count=0
for source in examples/apps/*.c; do
    "$ccbin" -fverify-each -O2 --interpret "$source" | grep -q 'interpret main => '
    "$ccbin" -fverify-each -c "$source" -o /tmp/cinder-app-$$.o
    rm -f /tmp/cinder-app-$$.o
    count=$((count + 1))
done
printf '{"applications":%s,"status":"pass"}\n' "$count"
