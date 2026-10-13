#!/usr/bin/env bash
#
# Sanitizer and fuzz builds (security pass S08). Native Linux, gcc, in trees of their own with a dependency cache of
# their own: the shared deps/.cache-lin64 is rebuilt and relinked by every tree configured against it, so a build
# with other compiler flags must not use it (build/cmake/modules/Dependencies.cmake, KFX_DEP_CACHE_ROOT).
#
#   scripts/build-fuzz.sh asan     out/asan: the Catch2 suite under AddressSanitizer + UndefinedBehaviorSanitizer
#   scripts/build-fuzz.sh fuzz     out/fuzz: kfx_fuzz, the parser/state fuzz harnesses (fuzz/README.md), same
#                                  sanitizers plus coverage feedback from the kfx_* libraries
#   scripts/build-fuzz.sh asan-ft  out/asan-ft: the game with the in-game functional tests, same sanitizers; needs the
#                                  original game's data in core_files/ (run the ftests with scripts/run_ftest_coverage.sh
#                                  out/asan-ft <results-file>, FTEST_TIMEOUT=3600, FTEST_JOBS=8)
#
# Both share out/scratch/sanitize-deps (the dependencies are built once, with the sanitizers). Environment:
# KFX_FUZZ_JOBS (default: all cores), BUILD_DIR to override the tree.
set -euo pipefail
cd "$(dirname "$0")/.."

MODE="${1:-asan}"
JOBS="${KFX_FUZZ_JOBS:-$(nproc)}"
DEPS_ROOT="$PWD/out/scratch/sanitize-deps"

# -O1: the sanitizers' recommended level (usable speed, readable reports). The undefined-behaviour checks are the ones
# that find memory errors and wrong-size accesses; signed overflow and shifts are left out -- the game's hashes and
# random-number generators rely on wrapping, and they would bury everything else. Alignment is left out too: the
# state structs are #pragma pack(1) blobs, so every access to a member of an embedded struct is "misaligned" by design
# (harmless on x86, and the same bytes are what saves and resyncs carry). Each aborts rather than prints, so
# a fuzzer or a test run notices.
SAN="-fsanitize=address,undefined -fno-sanitize=signed-integer-overflow,shift,float-cast-overflow,alignment"
SAN="$SAN -fno-sanitize-recover=undefined -fno-omit-frame-pointer"
FLAGS="$SAN -O1 -g"

case "$MODE" in
  asan) DIR="${BUILD_DIR:-out/asan}"; EXTRA="-DKFX_BUILD_TESTS=ON" ;;
  fuzz) DIR="${BUILD_DIR:-out/fuzz}"; EXTRA="-DKFX_BUILD_FUZZ=ON" ;;
  asan-ft) DIR="${BUILD_DIR:-out/asan-ft}"; EXTRA="-DKFX_FUNCTESTING=ON -DKFX_FTEST_DATA_DIR=$PWD/core_files" ;;
  *) echo "usage: $0 asan|fuzz|asan-ft" >&2; exit 2 ;;
esac

mkdir -p "$DEPS_ROOT"
cmake -S . -B "$DIR" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_FLAGS_RELWITHDEBINFO="$FLAGS" -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="$FLAGS" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined" \
  -DKFX_DEP_CACHE_ROOT="$DEPS_ROOT" -DKFX_HARDEN=OFF $EXTRA
if [ "$MODE" = fuzz ]; then
  cmake --build "$DIR" --target kfx_fuzz -j "$JOBS"
elif [ "$MODE" = asan-ft ]; then
  cmake --build "$DIR" --target keeperfx -j "$JOBS"
else
  cmake --build "$DIR" -j "$JOBS"
fi
