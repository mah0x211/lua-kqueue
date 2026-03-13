#!/usr/bin/env sh

set -ex

# Check if .gcno files exist in src directory
if ! find ./src -name "*.gcno" | grep -q .; then
    echo "Warning: No .gcno files found in src directory. Skipping coverage generation."
    echo "Note: To generate coverage data, compile with coverage flags (e.g., --coverage or -fprofile-arcs -ftest-coverage)"
    exit 0
fi

mkdir -p ./coverage
lcov -c -d ./src -o coverage/lcov.info.all
lcov -r coverage/lcov.info.all '*/include/*' -o coverage/lcov.info.all

# Replace impl/ with actual source directory in the report
srcdir=$(readlink ./impl | tr -d '\n')
sed "s|impl/|${srcdir}|" coverage/lcov.info.all > coverage/lcov.info
# genhtml -o coverage/html coverage/lcov.info
