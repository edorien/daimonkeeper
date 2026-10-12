#!/usr/bin/env bash
#
# Compiles the GPU renderer's shaders (src/kfx_platform/shaders/*.glsl) to SPIR-V and regenerates their precompiled
# headers (src/kfx_platform/include/renderer/shaders/*.spv.h, scripts/gen_spv_header.py). The build doesn't compile
# shaders itself (each header's comment); run this after editing a .glsl and commit the .spv and .spv.h with it.
#
# The compiler is glslang 15.4.0, the one the checked-in .spv files were built with (it reproduces them byte for
# byte): `glslang` or `glslangValidator` on PATH if it is that version, otherwise the Freedesktop SDK 25.08's, through
# flatpak (flatpak install flathub org.freedesktop.Sdk//25.08). GLSLANG=<command> overrides the choice.
#
# Usage:
#   scripts/compile_shaders.sh           compile every shader, regenerate every header
#   scripts/compile_shaders.sh --check   compile to a temporary directory; exit 1 if a .spv or .spv.h is out of date
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_DIR="$REPO_DIR/src/kfx_platform/shaders"
HDR_DIR="$REPO_DIR/src/kfx_platform/include/renderer/shaders"
SHADERS=(world_polygon.vert world_polygon.frag world_sprite.vert world_sprite.frag)
WANT_VERSION="15.4.0"

CHECK=0
[ "${1:-}" = "--check" ] && CHECK=1

pick_glslang() {
    if [ -n "${GLSLANG:-}" ]; then echo "$GLSLANG"; return; fi
    local c
    for c in glslang glslangValidator; do
        if command -v "$c" > /dev/null && "$c" --version 2> /dev/null | grep -q "glslang Khronos. $WANT_VERSION"; then
            echo "$c"; return
        fi
    done
    if command -v flatpak > /dev/null && flatpak info org.freedesktop.Sdk//25.08 > /dev/null 2>&1; then
        echo "flatpak run --command=glslang --filesystem=$REPO_DIR org.freedesktop.Sdk//25.08"; return
    fi
    echo "error: no glslang $WANT_VERSION (on PATH, or the flatpak org.freedesktop.Sdk//25.08); set GLSLANG" >&2
    exit 1
}
GLSLANG_CMD=$(pick_glslang)

OUT_DIR="$SRC_DIR"
LOG_DIR="$REPO_DIR/out/scratch/compile_shaders"
mkdir -p "$LOG_DIR"
if [ "$CHECK" = 1 ]; then
    OUT_DIR="$LOG_DIR"
fi

status=0
for s in "${SHADERS[@]}"; do
    stage="${s##*.}"
    # The compiler reads includes (lighting_common.glsl) relative to the source.
    $GLSLANG_CMD --target-env vulkan1.0 -V -S "$stage" -o "$OUT_DIR/$s.spv" "$SRC_DIR/$s.glsl" > "$LOG_DIR/$s.log" 2>&1 || {
        echo "error: $s.glsl doesn't compile:" >&2; cat "$LOG_DIR/$s.log" >&2; exit 1; }
    if [ "$CHECK" = 1 ]; then
        if ! cmp -s "$OUT_DIR/$s.spv" "$SRC_DIR/$s.spv"; then
            echo "$s.spv: OUT OF DATE (recompile: scripts/compile_shaders.sh)"; status=1
        fi
        python3 "$REPO_DIR/scripts/gen_spv_header.py" --check "$SRC_DIR/$s.spv" "$HDR_DIR/$s.spv.h" || status=1
    else
        python3 "$REPO_DIR/scripts/gen_spv_header.py" "$SRC_DIR/$s.spv" "$HDR_DIR/$s.spv.h"
        echo "$s: compiled, header written"
    fi
done
exit $status
