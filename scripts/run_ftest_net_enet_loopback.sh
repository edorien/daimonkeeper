#!/usr/bin/env bash
#
# Runs the Phase 2 real-ENet-loopback ftest pair
# (docs/refactor/todo/ftest-fake-multiplayer.md) -- net_enet_loopback_host
# and net_enet_loopback_join -- as two separate daimonkeeper processes talking
# over real 127.0.0.1 UDP. Exists because these two tests are only halves
# of one session: neither can pass run alone (see src/ftests/ftest_list.c's
# comment on why they live in long_running_tests_list, and each test file's
# own header comment).
#
# If keeperfx-dir is a KFX_TEST_COVERAGE build (detected via the presence of
# any .gcno file), this also merges the two processes' gcov data into that
# tree's coverage.info/coverage-html -- see "Coverage merge" below -- and,
# if out/coverage (the unit-test tree) already has its own coverage.info,
# refreshes the combined out/coverage-merged report the same way
# scripts/merge_coverage.sh does. Both are best-effort: a non-coverage build
# just runs the tests, and a missing unit-test coverage.info only skips the
# final combined-report step, neither is a hard failure.
#
# Usage:
#   scripts/run_ftest_net_enet_loopback.sh [keeperfx-dir]
#
# keeperfx-dir defaults to out/coverage-ftest (a KFX_FUNCTESTING build tree
# with staged game data -- see src/ftests/README.md). Must contain a built
# `daimonkeeper` binary and its staged fxdata/campgns/levels/etc.
#
# Known limitation (see the plan doc's "Phase 2 port allocation" note):
# FTEST_NET_ENET_LOOPBACK_PORT (src/ftests/tests/ftest_net_enet_loopback_shared.h)
# is a fixed port, not dynamically allocated -- a collision with something
# else already bound to it locally will make this fail in a way that looks
# like a real bug.

set -euo pipefail

KEEPERFX_DIR="${1:-out/coverage-ftest}"
if [ ! -x "$KEEPERFX_DIR/daimonkeeper" ]; then
    echo "error: no daimonkeeper binary at $KEEPERFX_DIR/daimonkeeper (build it first, or pass the right directory)" >&2
    exit 1
fi
KEEPERFX_DIR="$(cd "$KEEPERFX_DIR" && pwd)"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# GCOV_PREFIX-captured .gcda deliberately land *outside* KEEPERFX_DIR (a
# plain mktemp scratch dir, not e.g. KEEPERFX_DIR/.gcov-host): gcov-tool
# merge (below) walks the real tree as one of its two inputs, and an
# earlier version of this script that nested the prefix dirs inside
# KEEPERFX_DIR made that walk also pick up the mirrored copies living
# inside them as spurious extra pass-through files with no matching .gcno.
WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/ftest-net-enet-loopback.XXXXXX")"
trap 'rm -rf "$WORK_DIR"' EXIT
GCOV_HOST_DIR="$WORK_DIR/gcov-host"
GCOV_JOIN_DIR="$WORK_DIR/gcov-join"
mkdir -p "$GCOV_HOST_DIR" "$GCOV_JOIN_DIR"

HOST_LOG="keeperfx_ftest_net_enet_loopback_host.log"
JOIN_LOG="keeperfx_ftest_net_enet_loopback_join.log"

echo "Starting net_enet_loopback_host..."
(
    cd "$KEEPERFX_DIR"
    GCOV_PREFIX="$GCOV_HOST_DIR" GCOV_PREFIX_STRIP=0 \
        ./daimonkeeper -ftests net_enet_loopback_host -includelongtests -headless -exitonfailedtest -log "$HOST_LOG"
) &
HOST_PID=$!

# Give the host time to bind before the client tries to connect --
# bf_enet_join()'s own connect loop would eventually catch up regardless
# (it retries across TIMEOUT_CONNECT_DIRECT_IPV4, net_main.h), but starting
# in the right order avoids relying on that for a fast, deterministic run.
sleep 1

echo "Starting net_enet_loopback_join..."
JOIN_EXIT=0
(
    cd "$KEEPERFX_DIR"
    GCOV_PREFIX="$GCOV_JOIN_DIR" GCOV_PREFIX_STRIP=0 \
        ./daimonkeeper -ftests net_enet_loopback_join -includelongtests -headless -exitonfailedtest -log "$JOIN_LOG"
) || JOIN_EXIT=$?

HOST_EXIT=0
wait "$HOST_PID" || HOST_EXIT=$?

echo
echo "net_enet_loopback_host exit code: $HOST_EXIT (log: $KEEPERFX_DIR/$HOST_LOG)"
echo "net_enet_loopback_join exit code: $JOIN_EXIT (log: $KEEPERFX_DIR/$JOIN_LOG)"

if [ "$HOST_EXIT" -ne 0 ] || [ "$JOIN_EXIT" -ne 0 ]; then
    echo "FAIL: see the logs above for details"
    exit 1
fi
echo "PASS: real ENet loopback round trip succeeded on both sides"

# --- Coverage merge (only for a KFX_TEST_COVERAGE build) -------------------
if [ -z "$(find "$KEEPERFX_DIR" -name '*.gcno' -print -quit 2>/dev/null)" ]; then
    echo
    echo "No .gcno found under $KEEPERFX_DIR -- not a coverage build, skipping coverage merge."
    exit 0
fi
if ! command -v gcov-tool >/dev/null 2>&1; then
    echo
    echo "warning: coverage-instrumented build detected but gcov-tool is not on PATH -- skipping coverage merge" >&2
    exit 0
fi

echo
echo "Merging loopback coverage into $KEEPERFX_DIR..."
# gcov-tool merge <dir1> <dir2> matches files by their path *relative to
# each dir*, so dir1=KEEPERFX_DIR (relative root: CMakeFiles/...) and
# dir2=<prefix>$KEEPERFX_DIR (GCOV_PREFIX_STRIP=0 mirrors the full absolute
# path, so its own relative root from there is also CMakeFiles/...) line up
# and get summed rather than treated as unrelated files. Two sequential
# 2-way merges (host into the real tree, then join into that result) since
# the tool only takes a pair at a time.
MERGE_STAGE1="$WORK_DIR/merge-stage1"
MERGE_STAGE2="$WORK_DIR/merge-stage2"
gcov-tool merge "$KEEPERFX_DIR" "$GCOV_HOST_DIR$KEEPERFX_DIR" -o "$MERGE_STAGE1" >/dev/null
gcov-tool merge "$MERGE_STAGE1" "$GCOV_JOIN_DIR$KEEPERFX_DIR" -o "$MERGE_STAGE2" >/dev/null
# Write the merged counts back over the real in-place .gcda tree, so the
# lcov capture below (and any later normal `cmake --build --target
# coverage` re-run) picks them up transparently.
cp -a "$MERGE_STAGE2/." "$KEEPERFX_DIR/"

echo "Refreshing $KEEPERFX_DIR/coverage.info..."
# Mirrors CMakeLists.txt's KFX_FUNCTESTING `coverage` custom target's own
# lcov capture/extract/genhtml commands exactly (same tool, same flags, same
# exclusion pattern) so this drop-in-refreshes the same coverage.info/
# coverage-html a normal `cmake --build <dir> --target coverage` run
# produces -- just with the loopback pair's contribution folded in too.
# kfx_fetch() (Dependencies.cmake) caches lcov under deps/.cache-lin64/
# prebuilt/, not per-build-tree -- deliberately outside out/<platform> so
# deleting/recreating a build tree doesn't force a re-download.
LCOV_BIN="$REPO_ROOT/deps/.cache-lin64/prebuilt/lcov/lcov-1.16/bin/lcov"
GENHTML_BIN="$REPO_ROOT/deps/.cache-lin64/prebuilt/lcov/lcov-1.16/bin/genhtml"
if [ ! -x "$LCOV_BIN" ]; then
    echo "warning: no fetched lcov at $LCOV_BIN -- run 'cmake --build $KEEPERFX_DIR --target coverage' once first to fetch it, then re-run this script" >&2
    exit 0
fi
KFX_LCOV_EXCL_LINE_PATTERN='LCOV_EXCL_LINE|\b(ERRORLOG|WARNLOG|WARNMSG|SYNCDBG|JUSTMSG|SCRPTWRNLOG|SCRPTERRLOG)\s*\('

perl "$LCOV_BIN" --capture --directory "$KEEPERFX_DIR" \
    --output-file "$KEEPERFX_DIR/coverage.raw.info" --gcov-tool gcov --quiet \
    --rc "lcov_excl_line=$KFX_LCOV_EXCL_LINE_PATTERN"
perl "$LCOV_BIN" --extract "$KEEPERFX_DIR/coverage.raw.info" \
    "$REPO_ROOT/src/kfx_"'*'"/src/*" \
    --output-file "$KEEPERFX_DIR/coverage.info" --quiet
perl "$GENHTML_BIN" "$KEEPERFX_DIR/coverage.info" \
    --output-directory "$KEEPERFX_DIR/coverage-html" --quiet
echo "Refreshed: $KEEPERFX_DIR/coverage-html/index.html"

# --- Full merged report (only if the unit-test tree already has its own) --
UNIT_TREE="$REPO_ROOT/out/coverage"
if [ -f "$UNIT_TREE/coverage.info" ]; then
    echo
    echo "Refreshing the combined report via scripts/merge_coverage.sh..."
    "$REPO_ROOT/scripts/merge_coverage.sh" "$UNIT_TREE" "$KEEPERFX_DIR" "$REPO_ROOT/out/coverage-merged"
else
    echo
    echo "$UNIT_TREE/coverage.info not found -- skipping the combined out/coverage-merged report" \
         "(run 'cmake --build $UNIT_TREE --target coverage' first if you want it, then re-run this script)."
fi
