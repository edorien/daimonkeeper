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
# (default 1200), FTEST_ONLY=<regex> to run a subset (by name). GOLDEN_SHARDS=<n> (default 1) runs each *_golden test
# as n processes, each with a share of the cases (KFX_FTEST_GOLDEN_SHARD, ftest_golden.h): the cases are independent,
# so the shards are one golden's run split up. Its results line is the worst shard's; its log is the shards' logs,
# then (with KFX_FTEST_GOLDEN_PRINT) their printed hashes merged back into the cases' order as plain GOLDEN: lines.
# FTEST_THEN=<test> runs each selected test followed by <test> in the same process ("-ftests <t>,<test>"; the
# selected tests must come before it in ftest_list.c): which test leaves state behind that changes <test>? A line's
# rc is the pair's. The goldens and the continuity tests, the longest, are queued first. GOLDEN_UNSHARDED=<regex> (default: none)
# names goldens to run whole -- for a golden whose cases turn out not to be independent (a case's hash then depends on
# the cases before it: refactor pass 5's P5-F31 was found that way).

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

# The queue: a test, or a golden's shard (<test>@<k>); the long ones first, so they don't finish last.
SHARDS="${GOLDEN_SHARDS:-1}"
ITEMS=()
for pass in long rest; do
    for t in "${TESTS[@]}"; do
        if [[ "$t" =~ golden|continuity ]]; then [ "$pass" = long ] || continue; else [ "$pass" = rest ] || continue; fi
        if [[ "$t" =~ _golden$ ]] && [ "$SHARDS" -gt 1 ] && ! [[ -n "${GOLDEN_UNSHARDED:-}" && "$t" =~ ${GOLDEN_UNSHARDED} ]]; then
            for ((k = 0; k < SHARDS; k++)); do ITEMS+=("$t@$k"); done
        else
            ITEMS+=("$t")
        fi
    done
done

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
echo "Running ${#TESTS[@]} ftests (${#ITEMS[@]} processes: goldens in $SHARDS shards), $JOBS at a time (timeout ${FTEST_TIMEOUT}s each)"

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
    while i=$(next_test) && [ "$i" -lt "${#ITEMS[@]}" ]; do
        local item=${ITEMS[$i]} t log shard=""
        t=${item%@*}
        log="ftcov_$t.log"
        if [ "$item" != "$t" ]; then
            shard="${item#*@}/$SHARDS"
            log="ftcov_$t.shard${item#*@}.log"
        fi
        local run="$t"
        [ -n "${FTEST_THEN:-}" ] && run="$t,$FTEST_THEN"
        (cd "$dir" && KFX_FTEST_GOLDEN_SHARD="$shard" timeout "$FTEST_TIMEOUT" "./$BINARY" -ftests "$run" \
            -exitonfailedtest -headless -log "$log" > /dev/null 2>&1)
        rc=$?
        mv -f "$dir/$log" "$LOG_DIR/" 2> /dev/null
        echo "$item rc=$rc" >> "$RESULTS"
        echo "  $item rc=$rc"
    done
}
for ((w = 0; w < JOBS; w++)); do
    run_worker "$w" &
done
wait

# A sharded golden: one results line (the worst shard's), one log (the shards', then their printed hashes in the
# cases' order).
if [ "$SHARDS" -gt 1 ]; then
    for t in "${TESTS[@]}"; do
        [[ "$t" =~ _golden$ ]] || continue
        grep -q "^$t@0 rc=" "$RESULTS" || continue
        rc=0
        for ((k = 0; k < SHARDS; k++)); do
            r=$(grep -m1 "^$t@$k rc=" "$RESULTS" | sed 's/.* rc=//')
            [ "${r:-1}" != 0 ] && [ "$rc" = 0 ] && rc=${r:-1}
        done
        sed -i "/^$t@[0-9]* rc=/d" "$RESULTS"
        echo "$t rc=$rc" >> "$RESULTS"
        merged="$LOG_DIR/ftcov_$t.log"
        : > "$merged"
        for ((k = 0; k < SHARDS; k++)); do
            cat "$LOG_DIR/ftcov_$t.shard$k.log" >> "$merged" 2> /dev/null
            rm -f "$LOG_DIR/ftcov_$t.shard$k.log"
        done
        grep -ho 'GOLDEN#[0-9.]*:    {.*' "$merged" | sort | sed 's/^GOLDEN#[0-9.]*:/FTest: [merged] GOLDEN:/' >> "$merged.sorted"
        cat "$merged.sorted" >> "$merged" 2> /dev/null
        rm -f "$merged.sorted"
    done
fi

sort -o "$RESULTS" "$RESULTS"
rm -f "$QUEUE" "$QUEUE.lock"
FAILED=$(grep -vc ' rc=0$' "$RESULTS")
echo "Done: ${#TESTS[@]} tests, $FAILED failed (results: $RESULTS, logs: $LOG_DIR)"
exit 0
