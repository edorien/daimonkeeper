#!/usr/bin/env bash
#
# Native-Windows MinGW build + full package, run from an MSYS2 MINGW32 shell.
#
# Does exactly the Windows half of build-package.sh -- compiles keeperfx +
# keeperfx_hvlog with the 32-bit MinGW toolchain, assembles the game data,
# and produces both the .7z release archive (CPack) and the unpacked
# dist/windows/ tree. It deliberately does NOT do the Linux build or the
# coverage / unit-test pass.
#
# Unlike build-cmake.sh / build-package.sh (which cross-compile from Linux
# via build/cmake/toolchains/mingw32.cmake), this expects to already be
# running inside MSYS2's MINGW32 environment, where `gcc` IS the 32-bit
# mingw-w64 compiler -- so no toolchain file is passed; the plain WIN32
# branch of CMakeLists.txt / Dependencies.cmake (prebuilt i686 static libs)
# is what runs.
#
# Requirements (MSYS2 -- https://www.msys2.org):
#   pacman -S --needed git make p7zip curl unzip \
#     mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-ninja
#
# First run needs network access: Dependencies.cmake fetches the prebuilt
# third-party libs, and `make pkg-enginegfx` clones dkfans/FXGraphics and
# downloads the pngpal2raw tool.
#
# Usage (from the repo root, in an MSYS2 MINGW32 shell):
#   ./build-package-windows.sh
#   BUILD_NUMBER=1234 PACKAGE_SUFFIX=Alpha ./build-package-windows.sh
#
# Or launch it from an ordinary Command Prompt with build-package-windows.bat,
# which bootstraps the MSYS2 environment and calls this script.
#
set -euo pipefail

cd "$(dirname "$0")"

BUILD_NUMBER="${BUILD_NUMBER:-$(git rev-list --count HEAD)}"
PACKAGE_SUFFIX="${PACKAGE_SUFFIX:-}"
BUILD_DIR="out/windows"
DIST_DIR="dist/windows"
JOBS="$(nproc 2>/dev/null || echo 4)"

# --- Environment sanity ----------------------------------------------------
case "${MSYSTEM:-}" in
    MINGW32) ;;
    MINGW64|UCRT64|CLANG64)
        echo "error: this is the $MSYSTEM shell, but the prebuilt deps are 32-bit." >&2
        echo "       Start the 'MSYS2 MINGW32' shell and re-run." >&2
        exit 1 ;;
    *)
        echo "warning: \$MSYSTEM is '${MSYSTEM:-unset}', not MINGW32 -- continuing," >&2
        echo "         but the build only works with the 32-bit MinGW toolchain." >&2 ;;
esac

for tool in git make cmake ninja gcc; do
    command -v "$tool" >/dev/null 2>&1 || { echo "error: '$tool' not on PATH." >&2; exit 1; }
done

MK=(make BUILD_NUMBER="$BUILD_NUMBER" PACKAGE_SUFFIX="$PACKAGE_SUFFIX")

echo "==> Engine graphics (make pkg-enginegfx)"
"${MK[@]}" pkg-enginegfx

echo "==> Game data (make pkg-assemble)"
"${MK[@]}" pkg-assemble

echo "==> Configure (CMake, native MinGW32)"
cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_NUMBER="$BUILD_NUMBER" -DPACKAGE_SUFFIX="$PACKAGE_SUFFIX"

echo "==> Build keeperfx + keeperfx_hvlog"
cmake --build "$BUILD_DIR" --target keeperfx keeperfx_hvlog -j"$JOBS"

echo "==> Archive (CPack 7Z -> pkg/)"
cmake --build "$BUILD_DIR" --target package

# Two separate --component calls, not a plain `cmake --install`: the latter
# also runs every install() rule the fetched SDL3 subprojects register for
# themselves (headers, cmake config, docs, ...). See Packaging.cmake's
# COMPONENT runtime/gamedata comments.
echo "==> Install to $DIST_DIR/"
cmake --install "$BUILD_DIR" --prefix "$DIST_DIR" --component runtime >/dev/null
cmake --install "$BUILD_DIR" --prefix "$DIST_DIR" --component gamedata >/dev/null

echo
echo "===================================================================="
echo "Archive:  $(ls -1 pkg/keeperfx*.7z 2>/dev/null | tail -n1)"
echo "Unpacked: $DIST_DIR/"
echo "===================================================================="
