#!/usr/bin/env bash
#
# Native-Windows MinGW build + full package, run from an MSYS2 MINGW64 shell.
#
# Does exactly the Windows half of build-package.sh -- compiles keeperfx
# with the 64-bit (x86_64) MinGW toolchain, assembles the game data,
# and produces both the .7z release archive (CPack) and the unpacked
# dist/windows/ tree. It deliberately does NOT do the Linux build or the
# coverage / unit-test pass.
#
# Unlike build-cmake-linux.sh / build-package.sh (which cross-compile from Linux
# via build/cmake/toolchains/mingw64.cmake), this expects to already be
# running inside MSYS2's MINGW64 environment, where `gcc` IS the x86_64
# mingw-w64 compiler -- so no toolchain file is passed; the plain WIN32
# branch of CMakeLists.txt / Dependencies.cmake (third-party libs built from
# source, SDL3's prebuilt x86_64 mingw DLLs) is what runs. 32-bit Windows
# (MINGW32) is no longer a target.
#
# Requirements (MSYS2 -- https://www.msys2.org):
#   pacman -S --needed git make p7zip curl unzip \
#     mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja
#
# First run needs network access: Dependencies.cmake fetches the
# third-party libs (sources, plus SDL3's prebuilt DLLs), and `make pkg-enginegfx` clones dkfans/FXGraphics and
# downloads the pngpal2raw tool.
#
# Usage (in an MSYS2 MINGW64 shell; the script cd's to the repo root itself,
# so it can be run from any directory):
#   extraBuildScripts/build-package-windows.sh
#   BUILD_NUMBER=1234 PACKAGE_SUFFIX=Alpha extraBuildScripts/build-package-windows.sh
#
# Or launch it from an ordinary Command Prompt with build-package-windows.bat,
# which bootstraps the MSYS2 environment and calls this script.
#
set -euo pipefail

# This script lives in extraBuildScripts/; everything below (make, cmake -S .,
# out/, dist/, pkg/) is relative to the repo root one level up.
cd "$(dirname "$0")/.."

BUILD_NUMBER="${BUILD_NUMBER:-$(git rev-list --count HEAD)}"
PACKAGE_SUFFIX="${PACKAGE_SUFFIX:-}"
BUILD_DIR="out/windows"
DIST_DIR="dist/windows"
JOBS="$(nproc 2>/dev/null || echo 4)"

# --- Environment sanity ----------------------------------------------------
case "${MSYSTEM:-}" in
    MINGW64) ;;
    MINGW32)
        echo "error: this is the MINGW32 (32-bit) shell; the Windows build is 64-bit now." >&2
        echo "       Start the 'MSYS2 MINGW64' shell and re-run." >&2
        exit 1 ;;
    UCRT64|CLANG64)
        echo "error: this is the $MSYSTEM shell; the build (and the Linux cross-compile" >&2
        echo "       it mirrors) uses MINGW64's msvcrt GCC toolchain." >&2
        echo "       Start the 'MSYS2 MINGW64' shell and re-run." >&2
        exit 1 ;;
    *)
        echo "warning: \$MSYSTEM is '${MSYSTEM:-unset}', not MINGW64 -- continuing," >&2
        echo "         but the build only works with the x86_64 MinGW toolchain." >&2 ;;
esac

for tool in git make cmake ninja gcc; do
    command -v "$tool" >/dev/null 2>&1 || { echo "error: '$tool' not on PATH." >&2; exit 1; }
done

MK=(make BUILD_NUMBER="$BUILD_NUMBER" PACKAGE_SUFFIX="$PACKAGE_SUFFIX")

echo "==> Engine graphics (make pkg-enginegfx)"
"${MK[@]}" pkg-enginegfx

echo "==> Game data (make pkg-assemble)"
"${MK[@]}" pkg-assemble

# A tree configured from the old MINGW32 shell has the 32-bit compiler baked
# into its CMakeCache.txt -- reset it so this configure picks up MINGW64's.
if [ -f "$BUILD_DIR/CMakeCache.txt" ] && grep -q -E 'i686-w64-mingw32|/mingw32/bin/' "$BUILD_DIR/CMakeCache.txt"; then
    echo "note: $BUILD_DIR was configured for 32-bit Windows; resetting its CMake cache." >&2
    rm -rf "$BUILD_DIR/CMakeCache.txt" "$BUILD_DIR/CMakeFiles"
fi

echo "==> Configure (CMake, native MinGW64)"
cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_NUMBER="$BUILD_NUMBER" -DPACKAGE_SUFFIX="$PACKAGE_SUFFIX"

echo "==> Build keeperfx"
cmake --build "$BUILD_DIR" --target keeperfx -j"$JOBS"

echo "==> Archive (CPack 7Z -> pkg/)"
cmake --build "$BUILD_DIR" --target package

# Three separate --component calls, not a plain `cmake --install`: the latter
# also runs every install() rule the fetched SDL3 subprojects register for
# themselves (headers, cmake config, docs, ...). See Packaging.cmake's
# COMPONENT runtime/gamedata/mcp comments. mcp (the LLM/agent bridge,
# scripts/llm_bridge/) is plain Python -- same as build-package.sh installs it.
echo "==> Install to $DIST_DIR/"
cmake --install "$BUILD_DIR" --prefix "$DIST_DIR" --component runtime >/dev/null
cmake --install "$BUILD_DIR" --prefix "$DIST_DIR" --component gamedata >/dev/null
cmake --install "$BUILD_DIR" --prefix "$DIST_DIR" --component mcp >/dev/null

echo
echo "===================================================================="
echo "Archive:    $(ls -1 pkg/daimonkeeper*.7z 2>/dev/null | tail -n1)"
echo "Unpacked:   $DIST_DIR/"
echo "MCP bridge: $DIST_DIR/mcp/ (see $DIST_DIR/mcp/README.md)"
echo "===================================================================="
