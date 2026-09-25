#!/usr/bin/env bash
#
# Verifies the harness_setup_failure ftest (src/ftests/tests/ftest_harness_setup_failure.c):
# a pre_start_func that fails via FTEST_FAIL_TEST must abort/move on immediately, not wait for
# FTF_LevelLoaded and run init_func/actions anyway. That's what used to make skirmish_setup_override
# hang forever whenever its own pre_start_func bailed out early (docs/refactor/skirmish/) -- the level
# it targets went on to load anyway, without the override its pre_start_func never got to install, and
# never finished loading.
#
# harness_setup_failure can't assert this from inside its own actions -- the whole point is that
# nothing after pre_start_func should run -- so this script drives it externally and checks both the
# process's behaviour (bounded run time under -exitonfailedtest) and the log content (proves init/actions
# genuinely never ran, not just that the process happened to exit).
#
# Usage:
#   scripts/run_ftest_harness_setup_failure.sh [keeperfx-dir]
#
# keeperfx-dir defaults to out/coverage-ftest (a KFX_FUNCTESTING build tree with staged game data --
# see src/ftests/README.md). Must contain a built `keeperfx` binary and its staged campgns/levels/etc.

set -euo pipefail

KEEPERFX_DIR="${1:-out/coverage-ftest}"
if [ ! -x "$KEEPERFX_DIR/keeperfx" ]; then
    echo "error: no keeperfx binary at $KEEPERFX_DIR/keeperfx (build it first, or pass the right directory)" >&2
    exit 1
fi
KEEPERFX_DIR="$(cd "$KEEPERFX_DIR" && pwd)"

LOG="keeperfx_ftest_harness_setup_failure.log"
TIMEOUT_SECS=60

echo "Running harness_setup_failure (timeout ${TIMEOUT_SECS}s)..."
EXIT_CODE=0
(
    cd "$KEEPERFX_DIR"
    timeout "$TIMEOUT_SECS" ./keeperfx -ftests harness_setup_failure -headless -exitonfailedtest -log "$LOG"
) || EXIT_CODE=$?

LOG_PATH="$KEEPERFX_DIR/$LOG"
FAIL=0

if [ "$EXIT_CODE" -eq 124 ]; then
    echo "FAIL: process did not exit within ${TIMEOUT_SECS}s -- the setup-failure hang has regressed"
    FAIL=1
elif [ "$EXIT_CODE" -ne 255 ]; then
    # kfxmain() returns -1 for a failed test under -exitonfailedtest, which the shell reports as 255.
    echo "FAIL: expected exit code -1 (255) for a failed test under -exitonfailedtest, got $EXIT_CODE"
    FAIL=1
else
    echo "OK: process exited promptly with the expected failed-test exit code ($EXIT_CODE)"
fi

if [ ! -f "$LOG_PATH" ]; then
    echo "FAIL: no log found at $LOG_PATH"
    FAIL=1
else
    if ! grep -q "Deliberate pre_start failure" "$LOG_PATH"; then
        echo "FAIL: log does not show the deliberate pre_start_func failure -- test didn't run as expected"
        FAIL=1
    else
        echo "OK: pre_start_func's deliberate failure is in the log"
    fi

    if grep -q "regression: action ran despite pre_start_func" "$LOG_PATH"; then
        echo "FAIL: the canary action ran -- the harness went on to run init_func/actions after" \
             "pre_start_func already failed the test (see src/ftests/ftest.c's FTSt_TestIsProcessingActions handling)"
        FAIL=1
    else
        echo "OK: the canary action never ran"
    fi

    if grep -q "Initializing Functional Test harness_setup_failure" "$LOG_PATH"; then
        echo "FAIL: init_func ran for harness_setup_failure despite its pre_start_func already failing the test"
        FAIL=1
    else
        echo "OK: init_func never ran for harness_setup_failure"
    fi
fi

if [ "$FAIL" -ne 0 ]; then
    echo
    echo "FAIL: see $LOG_PATH for details"
    exit 1
fi

echo
echo "PASS: a failed pre_start_func aborts/moves on immediately instead of hanging"
