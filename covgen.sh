#!/usr/bin/env sh

set -ex

mkdir -p ./coverage
lcov -c -d ./impl/ -o coverage/lcov.info.all
lcov -r coverage/lcov.info.all '*/include/*' -o coverage/lcov.info.all

srcdir=$(readlink ./impl | tr -d '\n')
sed "s|impl/|${srcdir}|" coverage/lcov.info.all > coverage/lcov.info
# genhtml -o coverage/html coverage/lcov.info
