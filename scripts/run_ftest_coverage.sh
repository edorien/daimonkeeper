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
# object paths wherever it runs from. Each test starts with an empty save/: the settings a test changes and saves
# (settings.toml: the shadows level, the minimap zoom, ...) would otherwise be the next test's in that directory.
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
#
# FTEST_ARGS: more launch arguments for every game process (e.g. -framehash, the frame channel: refactor pass 5, S10).
#
# Multiplayer pairs: a long_running_tests_list entry named <base>_host with .net_role=FTNet_Host and its <base>_join
# partner are one two-player game (ftest.h's net_role): a worker runs the two as two processes, the client in a
# directory of its own, on port FTEST_NET_PORT_BASE (default 5600) + the pair's index (KFX_FTEST_NET_PORT; a second
# runner at the same time needs another base). The pair's line, "<base> rc=", fails when either half does or when
# their "NETHASH <turn> <hash>" lines differ (rc=3: the machines' games went apart).
#
# Progress: each test prints a "start" line when a worker takes it and a "[done/total]" line with its rc and
# duration when it finishes, and every FTEST_HEARTBEAT seconds (default 60; 0 = off) a line lists what is still
# running and for how long -- a golden or continuity test can take many minutes with nothing else to show.
#
# A process that leaves no log (-log names one in its run directory) gets rc=4, whatever it exited with: the game
# runs on without one when it can't set it up (a full path past its limit, an unwritable directory), and a test
# without its log -- no FTest:, GOLDEN or NETHASH lines -- has shown nothing. Its stderr says why.

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
# The multiplayer pairs (long_running_tests_list's <base>_host entries with a <base>_join partner).
LONG_LIST=$(sed -n '/\.long_running_tests_list *= *{/,$p' "$REPO_DIR/src/ftests/ftest_list.c" | grep -v '^\s*//')
mapfile -t PAIRS < <(echo "$LONG_LIST" | grep 'net_role *= *FTNet_Host' | grep -oE 'test_name *= *"[^"]+_host"' \
    | sed -E 's/.*"([^"]+)_host"/\1/' | while read -r b; do echo "$LONG_LIST" | grep -q "\"${b}_join\"" && echo "$b"; done)
if [ -n "${FTEST_ONLY:-}" ] && [ "${#PAIRS[@]}" -gt 0 ]; then
    mapfile -t PAIRS < <(printf '%s\n' "${PAIRS[@]}" | grep -E "$FTEST_ONLY")
fi
if [ "${#TESTS[@]}" -eq 0 ] && [ "${#PAIRS[@]}" -eq 0 ]; then
    echo "error: no tests found in src/ftests/ftest_list.c" >&2
    exit 1
fi

# The queue: a test, or a golden's shard (<test>@<k>); the long ones first, so they don't finish last.
SHARDS="${GOLDEN_SHARDS:-1}"
ITEMS=()
for b in "${PAIRS[@]}"; do ITEMS+=("$b@pair"); done
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
DATA_ENTRIES=(campgns creatrs data fxdata levels mods multiplayer ldata daimonkeeper.cfg keeperfx.cfg)
make_run_dir() {
    local dir=$1 entry
    rm -rf "$dir"
    mkdir -p "$dir/save"
    for entry in "${DATA_ENTRIES[@]}"; do
        [ -e "$BUILD_DIR/$entry" ] && cp -a "$BUILD_DIR/$entry" "$dir/"
    done
    cp "$BUILD_DIR/$BINARY" "$dir/"
}
# save/ is the game's output only (settings, saves, campaign progress): emptied before each test
empty_save_dir() {
    rm -rf "$1/save"
    mkdir -p "$1/save"
}
for ((w = 2; w <= JOBS; w++)); do
    make_run_dir "$BUILD_DIR/ftest-coverage-runs/run$w"
    RUN_DIRS+=("$BUILD_DIR/ftest-coverage-runs/run$w")
done
# a pair's client runs in a directory of its own
for ((p = 0; p < ${#PAIRS[@]}; p++)); do
    make_run_dir "$BUILD_DIR/ftest-coverage-runs/pair$p"
done

LOG_DIR="$BUILD_DIR/ftest-coverage-logs"
mkdir -p "$LOG_DIR"
: > "$RESULTS"
echo "Running ${#TESTS[@]} ftests and ${#PAIRS[@]} multiplayer pairs (${#ITEMS[@]} queue items: goldens in $SHARDS shards), $JOBS at a time (timeout ${FTEST_TIMEOUT}s each)"

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

# Progress reporting. A worker records what it is running in $RUNNING/<worker> ("<item> <start-seconds>"); a
# background heartbeat lists those files.
RUNNING="$LOG_DIR/.running"
rm -rf "$RUNNING"
mkdir -p "$RUNNING"
TOTAL=${#ITEMS[@]}
RUN_START=$SECONDS
fmt_dur() { printf '%dm%02ds' $(($1 / 60)) $(($1 % 60)); }
done_count() { wc -l < "$RESULTS"; }
progress_start() { # worker item
    echo "$2 $SECONDS" > "$RUNNING/$1"
    echo "  [$(done_count)/$TOTAL] start $2"
}
progress_done() { # worker item rc
    local began
    read -r _ began < "$RUNNING/$1"
    rm -f "$RUNNING/$1"
    echo "  [$(done_count)/$TOTAL] done  $2 rc=$3 ($(fmt_dur $((SECONDS - began))))"
}
heartbeat() {
    local f item began list
    while sleep "${FTEST_HEARTBEAT:-60}"; do
        list=""
        for f in "$RUNNING"/*; do
            [ -f "$f" ] || continue
            read -r item began < "$f"
            list+=" $item($(fmt_dur $((SECONDS - began))))"
        done
        echo "  ... [$(done_count)/$TOTAL] elapsed $(fmt_dur $((SECONDS - RUN_START))), running:${list:- (none)}"
    done
}

# Moves a finished process's log (<run-dir>/<log>) to $LOG_DIR. No log: says so, with the process's stderr
# (<log>.stderr, removed either way), and fails.
collect_log() {
    local dir=$1 log=$2
    if [ -f "$dir/$log" ]; then
        mv -f "$dir/$log" "$LOG_DIR/"
        rm -f "$dir/$log.stderr"
        return 0
    fi
    echo "  no log written: $dir/$log" >&2
    [ -s "$dir/$log.stderr" ] && tail -n 5 "$dir/$log.stderr" | sed 's/^/    /' >&2
    rm -f "$dir/$log.stderr"
    return 1
}

# A multiplayer pair: the host in the worker's directory, the client in the pair's own, then their NETHASH lines.
run_pair() {
    local dir=$1 b=$2 w=$3 p port pdir hrc jrc rc
    for ((p = 0; p < ${#PAIRS[@]}; p++)); do [ "${PAIRS[$p]}" = "$b" ] && break; done
    port=$((${FTEST_NET_PORT_BASE:-5600} + p))
    progress_start "$w" "$b@pair"
    pdir="$BUILD_DIR/ftest-coverage-runs/pair$p"
    empty_save_dir "$dir"
    empty_save_dir "$pdir"
    rm -f "$dir/ftcov_${b}_host.log" "$pdir/ftcov_${b}_join.log"
    (cd "$dir" && KFX_FTEST_NET_PORT=$port timeout "$FTEST_TIMEOUT" "./$BINARY" -ftests "${b}_host" -includelongtests \
        -exitonfailedtest -headless ${FTEST_ARGS:-} -log "ftcov_${b}_host.log" > /dev/null 2> "ftcov_${b}_host.log.stderr") &
    local hpid=$!
    # The client tries to connect once (5 s) and doesn't retry: start it when the host takes players (its lobby,
    # after startup and the NAT probes -- a fixed 2 s wasn't always enough under load), or when the host has quit.
    local waited=0
    until grep -aq 'into 6 (FeSt_NET_START)' "$dir/ftcov_${b}_host.log" 2> /dev/null || ! kill -0 "$hpid" 2> /dev/null \
            || [ "$waited" -ge 600 ]; do
        sleep 0.5
        waited=$((waited + 1))
    done
    (cd "$pdir" && KFX_FTEST_NET_PORT=$port timeout "$FTEST_TIMEOUT" "./$BINARY" -ftests "${b}_join" -includelongtests \
        -exitonfailedtest -headless ${FTEST_ARGS:-} -log "ftcov_${b}_join.log" > /dev/null 2> "ftcov_${b}_join.log.stderr")
    jrc=$?
    wait "$hpid"
    hrc=$?
    collect_log "$dir" "ftcov_${b}_host.log" || hrc=4
    collect_log "$pdir" "ftcov_${b}_join.log" || jrc=4
    rc=$hrc
    [ "$rc" = 0 ] && rc=$jrc
    if [ "$rc" = 0 ] && ! diff -q <(grep -ao 'NETHASH .*' "$LOG_DIR/ftcov_${b}_host.log") \
            <(grep -ao 'NETHASH .*' "$LOG_DIR/ftcov_${b}_join.log") > /dev/null; then
        rc=3
    fi
    if [ "$rc" = 0 ] && ! grep -aq 'NETHASH' "$LOG_DIR/ftcov_${b}_host.log"; then
        rc=3
    fi
    echo "$b rc=$rc" >> "$RESULTS"
    progress_done "$w" "$b@pair" "$rc (host $hrc, client $jrc)"
}

# Worker w runs tests from its own run directory until none are left.
run_worker() {
    local w=$1 dir=${RUN_DIRS[$1]} i rc
    while i=$(next_test) && [ "$i" -lt "${#ITEMS[@]}" ]; do
        local item=${ITEMS[$i]} t log shard=""
        t=${item%@*}
        if [ "$item" = "$t@pair" ]; then
            run_pair "$dir" "$t" "$w"
            continue
        fi
        log="ftcov_$t.log"
        if [ "$item" != "$t" ]; then
            shard="${item#*@}/$SHARDS"
            log="ftcov_$t.shard${item#*@}.log"
        fi
        local run="$t"
        [ -n "${FTEST_THEN:-}" ] && run="$t,$FTEST_THEN"
        empty_save_dir "$dir"
        rm -f "$dir/$log"
        progress_start "$w" "$item"
        (cd "$dir" && KFX_FTEST_GOLDEN_SHARD="$shard" timeout "$FTEST_TIMEOUT" "./$BINARY" -ftests "$run" \
            -exitonfailedtest -headless ${FTEST_ARGS:-} -log "$log" > /dev/null 2> "$log.stderr")
        rc=$?
        collect_log "$dir" "$log" || rc=4
        echo "$item rc=$rc" >> "$RESULTS"
        progress_done "$w" "$item" "$rc"
    done
}
HEARTBEAT_PID=""
if [ "${FTEST_HEARTBEAT:-60}" != 0 ]; then
    heartbeat &
    HEARTBEAT_PID=$!
fi
WORKER_PIDS=()
for ((w = 0; w < JOBS; w++)); do
    run_worker "$w" &
    WORKER_PIDS+=($!)
done
wait "${WORKER_PIDS[@]}"
[ -n "$HEARTBEAT_PID" ] && kill "$HEARTBEAT_PID" 2> /dev/null
rm -rf "$RUNNING"

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
echo "Done: ${#TESTS[@]} tests and ${#PAIRS[@]} pairs, $FAILED failed (results: $RESULTS, logs: $LOG_DIR)"
exit 0
