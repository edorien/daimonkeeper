#!/usr/bin/env bash
# Launches the game with RENDERER=VULKAN working on systems where a driver
# package shadows system libraries that Mesa's Vulkan drivers need.
#
# Symptoms:
#  - the log says "SDL_CreateGPUDevice (vulkan) failed: SDL_HINT_GPU_DRIVER
#    vulkan unsupported!" and the game falls back to the software renderer,
#    while `vulkaninfo` prints "undefined symbol: wl_fixes_interface" for every ICD;
#  - or, once that is fixed, the log says "SDL_GPU device ready (llvmpipe ...)":
#    the GPU's own driver (RADV) failed ("failed to query GPU info" in
#    `vulkaninfo`) and only the CPU rasteriser was left.
# Cause: the AMDGPU driver package puts its own, older libwayland-client.so.0,
# libdrm.so.2 and libdrm_amdgpu.so.1 under /opt/amdgpu/lib ahead of the system
# ones in the loader cache; current Mesa needs the system copies.
# Fix: preload the system copy of each library a driver package under /opt
# shadows -- done here only when that shadowing is detected, so on healthy
# systems this is just a plain launch.
#
# Usage: scripts/run-keeperfx-vulkan.sh [path/to/daimonkeeper] [game args...]
#        (default binary: ./daimonkeeper, i.e. run it from the game directory)
set -u

sys_lib_dir=""
for d in /usr/lib/x86_64-linux-gnu /usr/lib64 /usr/lib; do
    [ -e "$d/libwayland-client.so.0" ] && { sys_lib_dir="$d"; break; }
done

preload=""
if [ -n "$sys_lib_dir" ]; then
    for lib in libwayland-client.so.0 libdrm.so.2 libdrm_amdgpu.so.1; do
        sys="$sys_lib_dir/$lib"
        [ -e "$sys" ] || continue
        first="$(ldconfig -p 2>/dev/null | awk -v l="$lib" '$1 == l && /\(libc6,x86-64\)/ {print $NF; exit}')"
        [ -n "$first" ] || continue
        [ "$(readlink -f "$first")" = "$(readlink -f "$sys")" ] && continue
        case "$first" in
            /opt/*) ;;
            *) continue ;; # shadowed by something other than a driver package: leave it alone
        esac
        echo "run-keeperfx-vulkan: $first shadows the system $lib; preloading $sys" >&2
        preload="${preload:+$preload:}$sys"
    done
fi
[ -n "$preload" ] && export LD_PRELOAD="$preload${LD_PRELOAD:+:$LD_PRELOAD}"

bin="${1:-./daimonkeeper}"
[ $# -gt 0 ] && shift
exec "$bin" "$@"
