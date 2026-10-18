#!/usr/bin/env bash
#
# Coverage-only variant of build-package.sh, pre-pointed at the local game
# data in core_files/ (git-ignored real KeeperFX install, see
# src/ftests/README.md). Runs:
#
#   1. unit-test coverage   -> out/coverage/          (Catch2 + ctest)
#   2. ftest coverage       -> out/coverage-ftest/    (real game, -ftests -headless, a process per test)
#   3. merged report        -> out/coverage-merged/coverage-html/index.html
#
# Skips build-package.sh's package/asset passes (no make pkg-*, no
# dist/ output, no FXGraphics clone). Use build-package.sh for those.
#
#   ./build-coverage-core.sh
#   CORE_FILES=/other/install ./build-coverage-core.sh   # different data dir
#   SKIP_UNIT=1 ./build-coverage-core.sh                 # reuse an existing out/coverage/, redo ftest + merge
#   SKIP_FTEST=1 ./build-coverage-core.sh                # unit pass only
#   ./build-coverage-core.sh -j 8                        # run 8 tests at a time (default 4)
#
# -j N (or TEST_JOBS=N): how many tests run at once -- ctest's unit tests, and
# the ftests, each a game process of its own (scripts/run_ftest_coverage.sh).
# Compiling always uses every core.
#
# After a fully successful run out/coverage and out/coverage-ftest are deleted (several GB); the merged report in
# out/coverage-merged stays (SKIP_FTEST=1 keeps its unit-only report in out/coverage-unit). A failing ftest leaves
# both trees in place for inspection. So SKIP_UNIT=1 needs out/coverage to exist (e.g. after a failed run).
#
set -euo pipefail

TEST_JOBS="${TEST_JOBS:-4}"
while getopts "j:" opt; do
    case "$opt" in
        j) TEST_JOBS="$OPTARG" ;;
        *) echo "usage: $0 [-j test-jobs]" >&2; exit 1 ;;
    esac
done
if ! [[ "$TEST_JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "error: -j needs a positive number of jobs, not '$TEST_JOBS'" >&2
    exit 1
fi

cd "$(dirname "$0")"

CORE_FILES="$(realpath "${CORE_FILES:-core_files}")"
if { [ ! -f "$CORE_FILES/daimonkeeper.cfg" ] && [ ! -f "$CORE_FILES/keeperfx.cfg" ]; } || [ ! -d "$CORE_FILES/data" ]; then
    echo "error: '$CORE_FILES' doesn't look like a game-data install (need daimonkeeper.cfg or keeperfx.cfg, and data/)." >&2
    exit 1
fi

JOBS="$(nproc 2>/dev/null || echo 4)"
UNIT_TARGETS="kfx_platform_utest kfx_config_utest kfx_content_utest kfx_pathfinding_utest kfx_sim_utest kfx_ai_utest kfx_render_utest kfx_net_utest kfx_game_utest kfx_frontend_utest kfx_script_utest kfx_apploop_utest kfx_editor_utest"

if [ "${SKIP_UNIT:-0}" != "1" ]; then
    echo "==> [coverage] Configuring (native Linux, instrumented)"
    cmake -S . -B out/coverage -G Ninja -DKFX_OS=linux -DCMAKE_BUILD_TYPE=Debug \
        -DKFX_BUILD_TESTS=ON -DKFX_TEST_COVERAGE=ON

    echo "==> [coverage] Building test binaries"
    # shellcheck disable=SC2086
    cmake --build out/coverage --target $UNIT_TARGETS -j"$JOBS"

    # Stale .gcda from an earlier build trips gcov's checksum warning.
    find out/coverage -name "*.gcda" -delete

    echo "==> [coverage] Running tests (ctest, $TEST_JOBS at a time)"
    ctest --test-dir out/coverage --output-on-failure -j"$TEST_JOBS"

    echo "==> [coverage] Generating report"
    cmake --build out/coverage --target coverage
fi

if [ "${SKIP_FTEST:-0}" = "1" ]; then
    # A unit-only report lives inside the tree: keep it, drop the multi-GB tree around it.
    rm -rf out/coverage-unit
    mkdir -p out/coverage-unit
    mv out/coverage/coverage-html out/coverage-unit/coverage-html
    rm -rf out/coverage
    echo "Unit-only report: out/coverage-unit/coverage-html/index.html"
    exit 0
fi

echo "==> [coverage-ftest] Configuring (data: $CORE_FILES)"
cmake -S . -B out/coverage-ftest -G Ninja -DKFX_OS=linux -DCMAKE_BUILD_TYPE=Debug \
    -DKFX_BUILD_TESTS=OFF -DKFX_FUNCTESTING=ON -DKFX_TEST_COVERAGE=ON \
    -DKFX_FTEST_DATA_DIR="$CORE_FILES"

echo "==> [coverage-ftest] Building keeperfx, staging data, running ftests ($TEST_JOBS at a time), generating report"
# A failed ftest fails the target after its report is written: merge anyway, and fail at the end.
FTEST_FAILED=0
rm -f out/coverage-ftest/ftest-coverage-results.txt
FTEST_JOBS="$TEST_JOBS" cmake --build out/coverage-ftest --target coverage || FTEST_FAILED=1
if [ "$FTEST_FAILED" = "1" ] && [ ! -f out/coverage-ftest/ftest-coverage-results.txt ]; then
    echo "error: the ftest coverage build or run failed before any test ran" >&2
    exit 1
fi

echo "==> [coverage-merged] Merging unit-test + ftest reports"
scripts/merge_coverage.sh out/coverage out/coverage-ftest out/coverage-merged

echo
echo "===================================================================="
echo "Merged report: out/coverage-merged/coverage-html/index.html"
if [ "$FTEST_FAILED" = "1" ]; then
    # Keep the trees: the failing test's log and the per-tree reports are in them.
    echo "Unit report:   out/coverage/coverage-html/index.html"
    echo "Ftest report:  out/coverage-ftest/coverage-html/index.html"
    echo "===================================================================="
    echo "Some ftests failed (out/coverage-ftest/ftest-coverage-results.txt; logs in out/coverage-ftest/ftest-coverage-logs/)." >&2
    exit 1
fi
# Everything passed and the merged report is standalone: the instrumented trees are only needed to make it.
rm -rf out/coverage out/coverage-ftest out/coverage-unit
echo "(instrumented build trees out/coverage and out/coverage-ftest removed)"
echo "===================================================================="
