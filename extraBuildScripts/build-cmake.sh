#!/usr/bin/env bash
#
# Build keeperfx with CMake, at parity with the hand Makefiles.
#
#   KFX_OS=windows (default)  32-bit MinGW-w64 (i686) Windows binary, using the
#                             same compiler/flags/prebuilt deps as `make`.
#   KFX_OS=linux              native x86_64 Linux ELF (system/pkg-config deps,
#                             falling back to a from-source build for anything
#                             not found on the system).
#
# Third-party deps are downloaded and built from source automatically on
# first run (build/cmake/modules/Dependencies.cmake); neither platform needs
# vcpkg or manually-fetched prebuilt archives. The Linux build additionally
# tries system/pkg-config packages first for the deps that are commonly
# packaged, only falling back to a source build when one isn't found.
#
# Usage:
# Legacy: superseded by ../build-cmake-linux.sh (same logic, defaults to
# KFX_OS=linux, also installs the mcp component). Run from the repo root --
# the configure step uses the current directory as the source tree.
#
#   extraBuildScripts/build-cmake.sh                     # Windows keeperfx
#   KFX_OS=linux extraBuildScripts/build-cmake.sh        # Linux keeperfx
#   (no separate heavy-log build any more: logging is the LOG_LEVEL option)
#   USE_DOCKER=1 extraBuildScripts/build-cmake.sh        # build in an Ubuntu 24.04 container
#   BUILD_DIR=out/foo extraBuildScripts/build-cmake.sh   # override the build directory (default: out/<KFX_OS>/)
#
# Requirements (native):
#   windows: a MinGW-w64 i686 toolchain (Ubuntu: g++-mingw-w64-i686), cmake, ninja
#   linux:   gcc/g++, cmake, ninja, pkg-config + the ffmpeg/openal/luajit/
#            spng/minizip/zlib/miniupnpc/natpmp/openssl/zstd -dev packages, plus
#            SDL3's build deps (X11/GL/audio + png/ogg/vorbis/flac/mpg123/opus)
#            since SDL3 is built from source on distros without libsdl3-dev.
#
set -euo pipefail

TARGET="${1:-keeperfx}"
if [ "$TARGET" = "keeperfx_hvlog" ]; then
    echo "keeperfx_hvlog is no longer a separate build (see LOG_LEVEL in daimonkeeper.cfg); building keeperfx." >&2
    TARGET=keeperfx
fi
KFX_OS="${KFX_OS:-windows}"
# One build tree per platform (out/linux/, out/windows/, git-ignored) --
# a CMakeCache.txt bakes in its compiler/toolchain, so linux and windows
# can't share a directory. build/ holds tracked CMake modules, separate
# from this generated out/ tree.
BUILD_DIR="${BUILD_DIR:-out/$KFX_OS}"

# Run the whole thing inside a container that mirrors upstream CI (Ubuntu 24.04).
if [ "${USE_DOCKER:-0}" = "1" ]; then
    if [ "$KFX_OS" = "linux" ]; then
        PKGS="build-essential pkg-config cmake ninja-build git curl ca-certificates \
              libavcodec-dev libavformat-dev libavutil-dev libswresample-dev \
              libopenal-dev libluajit-5.1-dev libspng-dev libminizip-dev zlib1g-dev \
              libminiupnpc-dev libnatpmp-dev libssl-dev libzstd-dev \
              libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
              libxfixes-dev libxss-dev libxtst-dev libxkbcommon-dev libdrm-dev \
              libgbm-dev libgl1-mesa-dev libegl1-mesa-dev libasound2-dev \
              libpulse-dev libdbus-1-dev libudev-dev \
              libpng-dev libjpeg-dev libogg-dev libvorbis-dev libflac-dev \
              libmpg123-dev libopusfile-dev"
    else
        PKGS="g++-mingw-w64-i686 cmake ninja-build git curl ca-certificates"
    fi
    exec docker run --rm -v "$PWD:/src" -w /src ubuntu:24.04 bash -c "
        set -eux
        export DEBIAN_FRONTEND=noninteractive
        apt-get update -qq
        apt-get install -y -qq $PKGS
        git config --global --add safe.directory /src || true
        KFX_OS='$KFX_OS' BUILD_DIR='$BUILD_DIR' bash extraBuildScripts/build-cmake.sh '$TARGET'
    "
fi

if [ "$KFX_OS" = "linux" ]; then
    cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
else
    if ! command -v i686-w64-mingw32-gcc >/dev/null 2>&1; then
        echo "error: i686-w64-mingw32-gcc not found on PATH." >&2
        echo "       Install a MinGW-w64 i686 toolchain (Ubuntu: 'sudo apt install g++-mingw-w64-i686')," >&2
        echo "       or re-run with USE_DOCKER=1, or KFX_OS=linux for a native Linux build." >&2
        exit 1
    fi
    cmake -S . -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=build/cmake/toolchains/mingw32.cmake \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo
fi

cmake --build "$BUILD_DIR" --target "$TARGET" -j"$(nproc 2>/dev/null || echo 4)"

# Copy the binary (+ its runtime SDL3 libs, if any were built from source)
# to dist/<platform>/ for easy access, alongside whatever's still in
# $BUILD_DIR from prior variant builds -- reuses the install(TARGETS ...)/
# install(FILES/DIRECTORY ...) rules in Packaging.cmake/Dependencies.cmake,
# so this stays in sync with what those actually produce. --component
# runtime restricts this to exactly the rules tagged COMPONENT runtime --
# without it, `cmake --install` also runs every install() rule the fetched
# SDL3/SDL3_image/SDL3_mixer subprojects register for themselves (headers,
# cmake config, docs, ...), which isn't what "copy the binary somewhere
# convenient" means.
DIST_DIR="dist/$KFX_OS"
cmake --install "$BUILD_DIR" --prefix "$DIST_DIR" --component runtime >/dev/null

echo
if [ "$KFX_OS" = "linux" ]; then
    echo "Built: $BUILD_DIR/$TARGET"
    echo "Copied to: $DIST_DIR/$TARGET"
else
    echo "Built: $BUILD_DIR/$TARGET.exe"
    echo "Copied to: $DIST_DIR/$TARGET.exe"
fi
