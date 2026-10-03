#!/usr/bin/env bash
#
# Runs every registered ftest (src/ftests/ftest_list.c's tests_list, not long_running_tests_list) as its own
# process, for the KFX_FUNCTESTING+KFX_TEST_COVERAGE `coverage` target (refactor pass 5, S02).
#
# One process for the whole list (`-ftests -exitonfailedtest`) stopped at the first failing test, and the
# coverage of every test after it was silently missing from the report; each test also started from the state
# the ones before it left. Here a failure costs only that test: each runs alone, a line per test goes to the
# results file, and the counters every process adds to the build tree's .gcda files (gcov merges them under a
# lock) make one report.
#
# With FTEST_JOBS > 1, tests run that many at a time, each worker in its own run directory: worker 1 is the build
# tree itself, the others are copies of its staged data (tests write saves, maps and configs) with a copy of
# the binary, under <build-dir>/ftest-coverage-runs/. The binary writes its counters to the build tree's
# object paths wherever it runs from.
#
# Usage:
#   scripts/run_ftest_coverage.sh <build-dir> [results-file]     run the tests; exits 0 whatever they do
#   scripts/run_ftest_coverage.sh --check <results-file>         exit 1 (listing them) if any test failed
#
# Environment: FTEST_JOBS (default 2; build-coverage-core.sh -j sets it), FTEST_TIMEOUT seconds per test
# (default 1200), FTEST_ONLY=<regex> to run a subset (by name).

set -uo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [ "${1:-}" = "--check" ]; then
    RESULTS="${2:?usage: $0 --check <results-file>}"
    if [ ! -f "$RESULTS" ]; then
        echo "error: no results file $RESULTS" >&2
        exit 1
    fi
    FAILED=$(grep -v ' rc=0$' "$RESULTS" || true)
    TOTAL=$(wc -l < "$RESULTS")
    if [ -n "$FAILED" ]; then
        echo "ftest coverage run: $(echo "$FAILED" | wc -l) of $TOTAL tests failed:" >&2
        echo "$FAILED" >&2
        exit 1
    fi
    echo "ftest coverage run: all $TOTAL tests passed"
    exit 0
fi

BUILD_DIR="${1:?usage: $0 <build-dir> [results-file]}"
BUILD_DIR="$(cd "$BUILD_DIR" && pwd)"
RESULTS="${2:-$BUILD_DIR/ftest-coverage-results.txt}"
JOBS="${FTEST_JOBS:-2}"
FTEST_TIMEOUT="${FTEST_TIMEOUT:-1200}"
BINARY=daimonkeeper

if [ ! -x "$BUILD_DIR/$BINARY" ]; then
    echo "error: no $BINARY at $BUILD_DIR (build the keeperfx target first)" >&2
    exit 1
fi

# The registered tests: tests_list's entries, not the commented-out ones, not long_running_tests_list's.
mapfile -t TESTS < <(sed -n '/\.tests_list *= *{/,/\.long_running_tests_list *= *{/p' "$REPO_DIR/src/ftests/ftest_list.c" \
    | grep -v '^\s*//' | grep -oE 'test_name *= *"[^"]+"' | sed -E 's/.*"([^"]+)"/\1/')
if [ -n "${FTEST_ONLY:-}" ]; then
    mapfile -t TESTS < <(printf '%s\n' "${TESTS[@]}" | grep -E "$FTEST_ONLY")
fi
if [ "${#TESTS[@]}" -eq 0 ]; then
    echo "error: no tests found in src/ftests/ftest_list.c" >&2
    exit 1
fi

# Run directories: the build tree, then copies of its staged data.
RUN_DIRS=("$BUILD_DIR")
DATA_ENTRIES=(campgns creatrs data fxdata levels mods multiplayer daimonkeeper.cfg keeperfx.cfg)
for ((w = 2; w <= JOBS; w++)); do
    dir="$BUILD_DIR/ftest-coverage-runs/run$w"
    rm -rf "$dir"
    mkdir -p "$dir/save"
    for entry in "${DATA_ENTRIES[@]}"; do
        [ -e "$BUILD_DIR/$entry" ] && cp -a "$BUILD_DIR/$entry" "$dir/"
    done
    cp "$BUILD_DIR/$BINARY" "$dir/"
    RUN_DIRS+=("$dir")
done

LOG_DIR="$BUILD_DIR/ftest-coverage-logs"
mkdir -p "$LOG_DIR"
: > "$RESULTS"
echo "Running ${#TESTS[@]} ftests, one process each, $JOBS at a time (timeout ${FTEST_TIMEOUT}s each)"

# The next test to run, shared by the workers (a counter file under a lock): a worker takes one when it is
# free, so a few long tests don't leave the others idle.
QUEUE="$LOG_DIR/.queue"
echo 0 > "$QUEUE"
next_test() {
    exec 9<> "$QUEUE.lock"
    flock 9
    local i
    i=$(cat "$QUEUE")
    echo $((i + 1)) > "$QUEUE"
    flock -u 9
    exec 9>&-
    echo "$i"
}

# Worker w runs tests from its own run directory until none are left.
run_worker() {
    local w=$1 dir=${RUN_DIRS[$1]} i rc
    while i=$(next_test) && [ "$i" -lt "${#TESTS[@]}" ]; do
        local t=${TESTS[$i]}
        (cd "$dir" && timeout "$FTEST_TIMEOUT" "./$BINARY" -ftests "$t" -exitonfailedtest -headless \
            -log "ftcov_$t.log" > /dev/null 2>&1)
        rc=$?
        mv -f "$dir/ftcov_$t.log" "$LOG_DIR/" 2> /dev/null
        echo "$t rc=$rc" >> "$RESULTS"
        echo "  $t rc=$rc"
    done
}
for ((w = 0; w < JOBS; w++)); do
    run_worker "$w" &
done
wait

sort -o "$RESULTS" "$RESULTS"
rm -f "$QUEUE" "$QUEUE.lock"
FAILED=$(grep -vc ' rc=0$' "$RESULTS")
echo "Done: ${#TESTS[@]} tests, $FAILED failed (results: $RESULTS, logs: $LOG_DIR)"
exit 0
