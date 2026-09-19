#!/usr/bin/env bash
#
# Merges the two independent coverage.info files this repo can produce --
# the Catch2 unit-test harness's (KFX_BUILD_TESTS+KFX_TEST_COVERAGE) and
# src/ftests/'s in-game functional-test harness's
# (KFX_FUNCTESTING+KFX_TEST_COVERAGE) -- into one combined lcov report.
# They're generated from separate build trees (the two options are
# mutually exclusive in a single configure -- see CMakeLists.txt's
# KFX_FUNCTESTING option, or docs/Architecture/testing-harness.md §7.1),
# so combining them is a post-hoc lcov step, not a CMake target: lcov
# --add-tracefile merges LF/LH/FNF/FNH per source line across the two
# inputs, so a line either suite hits counts as hit in the merged report.
#
# Usage:
#   scripts/merge_coverage.sh [unit-test-tree] [ftest-tree] [output-dir]
#
# Defaults match docs/Architecture/testing-harness.md's own conventions:
#   unit-test-tree: out/coverage       (KFX_BUILD_TESTS+KFX_TEST_COVERAGE)
#   ftest-tree:     out/coverage-ftest (KFX_FUNCTESTING+KFX_TEST_COVERAGE)
#   output-dir:     out/coverage-merged
#
# Each tree must already have a coverage.info in it -- i.e. you've already
# run `cmake --build <tree> --target coverage` in both. This script only
# combines already-generated reports; it doesn't build or run anything.
set -euo pipefail

UNIT_TREE="${1:-out/coverage}"
FTEST_TREE="${2:-out/coverage-ftest}"
OUT_DIR="${3:-out/coverage-merged}"

UNIT_INFO="$UNIT_TREE/coverage.info"
FTEST_INFO="$FTEST_TREE/coverage.info"

for f in "$UNIT_INFO" "$FTEST_INFO"; do
    if [ ! -f "$f" ]; then
        echo "merge_coverage.sh: '$f' does not exist -- run" \
             "'cmake --build <tree> --target coverage' in its build tree first." >&2
        exit 1
    fi
done

# kfx_fetch() (build/cmake/modules/Dependencies.cmake) caches lcov under
# deps/.cache-lin64/prebuilt/, not per-build-tree -- deliberately outside
# out/<platform> so deleting/recreating a build tree doesn't force a
# re-download, same reasoning as every other dependency there. One shared
# copy regardless of which tree (or both) actually triggered the fetch.
LCOV="deps/.cache-lin64/prebuilt/lcov/lcov-1.16/bin/lcov"
GENHTML="deps/.cache-lin64/prebuilt/lcov/lcov-1.16/bin/genhtml"
if [ ! -x "$LCOV" ]; then
    echo "merge_coverage.sh: couldn't find a fetched lcov at $LCOV --" \
         "configure at least one tree with -DKFX_TEST_COVERAGE=ON first." >&2
    exit 1
fi

mkdir -p "$OUT_DIR"
perl "$LCOV" --add-tracefile "$UNIT_INFO" --add-tracefile "$FTEST_INFO" \
    --output-file "$OUT_DIR/coverage.info" --quiet
perl "$GENHTML" "$OUT_DIR/coverage.info" \
    --output-directory "$OUT_DIR/coverage-html" --quiet

echo "Merged report: $OUT_DIR/coverage-html/index.html"
