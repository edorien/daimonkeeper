#!/usr/bin/env bash
#
# Full local build: compiles the Windows and Linux packages (keeperfx,
# installed with the runtime libs + game data into
# dist/<platform>/) and the Catch2 test suite (instrumented for coverage,
# run under ctest, with an HTML report rendered). Generalizes what CI's
# release, unit-tests and coverage workflows do (.github/workflows/
# build-prototype.yml) for local iteration, reusing build-cmake-linux.sh's same
# out/<KFX_OS>/ build-tree convention plus a third out/coverage/ tree.
#
# Runs all three passes by default: Windows package, Linux package,
# coverage build+test+report. Any can be skipped individually:
#
#   SKIP_WINDOWS=1 ./build-package.sh   # Linux package + coverage only
#   SKIP_LINUX=1 ./build-package.sh     # Windows package + coverage only
#   SKIP_COVERAGE=1 ./build-package.sh  # both packages, no coverage pass
#
# A missing MinGW-w64 toolchain doesn't abort the whole run -- the
# Windows pass is skipped with a warning and the Linux + coverage passes
# still run, since they need nothing Windows-specific.
#
# The coverage pass is unit-test-only by default (out/coverage/). Setting
# KFX_FTEST_DATA_DIR to a real, proprietary KeeperFX install (not part of
# this repo -- docs/Architecture/testing-harness.md §7.3) additionally
# builds the ftest-driven coverage tree (out/coverage-ftest/, real
# gameplay simulation via src/ftests/ instead of isolated unit tests) and
# merges both into out/coverage-merged/ (scripts/merge_coverage.sh, §7.6)
# -- the two sources hit mostly different lines (§7.7), so the merged
# report is substantially more complete than either alone:
#
#   KFX_FTEST_DATA_DIR=/path/to/keeperfx/install ./build-package.sh
#
# Network access is needed: pkg-enginegfx clones dkfans/FXGraphics, and a
# first run fetches every third-party dependency (see build-cmake-linux.sh) --
# once per build tree (out/windows/, out/linux/, out/coverage/ each fetch
# their own, since a CMakeCache.txt bakes in its compiler/toolchain and
# trees can't share one).
#
# Usage:
#   ./build-package.sh                              # Windows + Linux packages, coverage report
#   BUILD_NUMBER=1234 PACKAGE_SUFFIX=Alpha ./build-package.sh
#
# Requirements: same as build-cmake-linux.sh (both platforms) plus a real
# `make` (mingw32-make's actual usual name; the asset pipeline's own
# Makefile is platform-agnostic) for the package passes; KFX_BUILD_TESTS'
# own requirements (native Linux, no cross-compile) for the coverage
# pass -- see docs/Architecture/testing-harness.md.
#
set -euo pipefail

BUILD_NUMBER="${BUILD_NUMBER:-$(git rev-list --count HEAD)}"
PACKAGE_SUFFIX="${PACKAGE_SUFFIX:-}"

# ---------------------------------------------------------------------------
# Shared game data (not platform-specific): built once, reused by both
# dist/windows/ and dist/linux/'s --component gamedata install below, via
# Packaging.cmake's install(CODE ...) block reading back this same pkg/ tree.
# ---------------------------------------------------------------------------
echo "==> Building engine graphics (make pkg-enginegfx)"
make BUILD_NUMBER="$BUILD_NUMBER" PACKAGE_SUFFIX="$PACKAGE_SUFFIX" pkg-enginegfx

echo "==> Assembling game data (make pkg-assemble)"
make BUILD_NUMBER="$BUILD_NUMBER" PACKAGE_SUFFIX="$PACKAGE_SUFFIX" pkg-assemble

build_platform_package() {
    local os="$1"
    local build_dir="out/$os"
    local dist_dir="dist/$os"

    echo "==> [$os] Configuring"
    if [ "$os" = "linux" ]; then
        cmake -S . -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            -DBUILD_NUMBER="$BUILD_NUMBER" -DPACKAGE_SUFFIX="$PACKAGE_SUFFIX"
    else
        cmake -S . -B "$build_dir" -G Ninja \
            -DCMAKE_TOOLCHAIN_FILE=build/cmake/toolchains/mingw32.cmake \
            -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            -DBUILD_NUMBER="$BUILD_NUMBER" -DPACKAGE_SUFFIX="$PACKAGE_SUFFIX"
    fi

    echo "==> [$os] Building keeperfx"
    cmake --build "$build_dir" --target keeperfx -j"$(nproc 2>/dev/null || echo 4)"

    # Three separate --component calls, not a plain `cmake --install`: the
    # latter would also run every install() rule the fetched SDL3
    # subprojects register for themselves (headers, cmake config, docs,
    # ...) -- see Packaging.cmake's COMPONENT runtime/gamedata/mcp comments.
    # mcp (the LLM/agent bridge, scripts/llm_bridge/) is plain Python and
    # identical either way, but still installed per-$os since dist/windows/
    # and dist/linux/ are each meant to be standalone, portable trees.
    echo "==> [$os] Installing to $dist_dir"
    cmake --install "$build_dir" --prefix "$dist_dir" --component runtime >/dev/null
    cmake --install "$build_dir" --prefix "$dist_dir" --component gamedata >/dev/null
    cmake --install "$build_dir" --prefix "$dist_dir" --component mcp >/dev/null

    echo "==> [$os] Packaged: $dist_dir/"
}

build_coverage() {
    local build_dir="out/coverage"
    local utest_targets="kfx_platform_utest kfx_config_utest kfx_content_utest kfx_pathfinding_utest kfx_sim_utest kfx_ai_utest kfx_render_utest kfx_net_utest kfx_game_utest kfx_frontend_utest kfx_script_utest kfx_apploop_utest kfx_editor_utest"

    echo "==> [coverage] Configuring (native Linux, instrumented)"
    cmake -S . -B "$build_dir" -G Ninja -DKFX_OS=linux -DCMAKE_BUILD_TYPE=Debug \
        -DKFX_BUILD_TESTS=ON -DKFX_TEST_COVERAGE=ON

    echo "==> [coverage] Building test binaries"
    # shellcheck disable=SC2086
    cmake --build "$build_dir" --target $utest_targets -j"$(nproc 2>/dev/null || echo 4)"

    # Stale .gcda from a prior instrumented build of the same source trips
    # gcov's "overwriting an existing profile data with a different
    # checksum" warning -- harmless, but clear it so a rebuild after local
    # source changes doesn't carry it forward into the fresh capture below.
    find "$build_dir" -name "*.gcda" -delete

    echo "==> [coverage] Running tests (ctest)"
    ctest --test-dir "$build_dir" --output-on-failure

    echo "==> [coverage] Generating report"
    cmake --build "$build_dir" --target coverage

    echo "==> [coverage] Report: $build_dir/coverage-html/index.html"
}

# docs/Architecture/testing-harness.md §7: a second, independent coverage
# source -- runs the real compiled game through src/ftests/'s registered
# functional tests instead of Catch2 unit tests -- merged with the
# unit-test tree's report by scripts/merge_coverage.sh (§7.6) into one
# combined out/coverage-merged report, since the two sources hit mostly
# different lines (§7.7: 6.34%/36.2% unit-only/merged, last measured).
# Needs KFX_FUNCTESTING+KFX_TEST_COVERAGE in their own tree (out/
# coverage-ftest, not out/coverage: §7.1, the two options are mutually
# exclusive in one configure) and KFX_FTEST_DATA_DIR pointing at a real,
# proprietary KeeperFX install (§7.3) -- gated on that being set (see the
# main flow below), since most local runs and every CI run don't have one.
build_ftest_coverage() {
    local build_dir="out/coverage-ftest"

    echo "==> [coverage-ftest] Configuring (native Linux, instrumented)"
    cmake -S . -B "$build_dir" -G Ninja -DKFX_OS=linux -DCMAKE_BUILD_TYPE=Debug \
        -DKFX_BUILD_TESTS=OFF -DKFX_FUNCTESTING=ON -DKFX_TEST_COVERAGE=ON \
        -DKFX_FTEST_DATA_DIR="$KFX_FTEST_DATA_DIR"

    # `coverage` here is a single target (§7.4): it stages the ftest data
    # subset, runs `keeperfx -ftests -headless -exitonfailedtest` itself,
    # then captures/renders -- no separate ctest step, unlike build_coverage
    # above.
    echo "==> [coverage-ftest] Building keeperfx, staging data, running ftests, generating report"
    cmake --build "$build_dir" --target coverage

    echo "==> [coverage-ftest] Report: $build_dir/coverage-html/index.html"
}

WINDOWS_STATUS="skipped (SKIP_WINDOWS=1)"
LINUX_STATUS="skipped (SKIP_LINUX=1)"
COVERAGE_STATUS="skipped (SKIP_COVERAGE=1)"

if [ "${SKIP_WINDOWS:-0}" != "1" ]; then
    if command -v i686-w64-mingw32-gcc >/dev/null 2>&1; then
        build_platform_package windows
        WINDOWS_STATUS="built -> dist/windows/"
    else
        echo "==> [windows] SKIPPING: i686-w64-mingw32-gcc not found on PATH." >&2
        echo "              Install a MinGW-w64 i686 toolchain (Ubuntu: 'sudo apt install g++-mingw-w64-i686')" >&2
        echo "              to include the Windows package, or set SKIP_WINDOWS=1 to silence this." >&2
        WINDOWS_STATUS="SKIPPED (no i686-w64-mingw32-gcc on PATH)"
    fi
fi

if [ "${SKIP_LINUX:-0}" != "1" ]; then
    build_platform_package linux
    LINUX_STATUS="built -> dist/linux/"
fi

if [ "${SKIP_COVERAGE:-0}" != "1" ]; then
    build_coverage
    COVERAGE_STATUS="built -> out/coverage/coverage-html/index.html (unit tests only; set KFX_FTEST_DATA_DIR for a merged report)"

    if [ -n "${KFX_FTEST_DATA_DIR:-}" ]; then
        build_ftest_coverage
        echo "==> [coverage-merged] Merging unit-test + ftest reports"
        scripts/merge_coverage.sh out/coverage out/coverage-ftest out/coverage-merged
        COVERAGE_STATUS="built -> out/coverage-merged/coverage-html/index.html (merged unit-test + ftest)"
    fi
fi

echo
echo "===================================================================="
echo "Windows package: $WINDOWS_STATUS"
echo "Linux package:   $LINUX_STATUS"
echo "Coverage report: $COVERAGE_STATUS"
echo "===================================================================="
