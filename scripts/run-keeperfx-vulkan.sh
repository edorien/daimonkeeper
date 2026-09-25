#!/usr/bin/env bash
# Launches keeperfx with RENDERER=VULKAN working on systems where a driver
# package shadows the system libwayland-client.
#
# Symptom: the log says "SDL_CreateGPUDevice (vulkan) failed: SDL_HINT_GPU_DRIVER
# vulkan unsupported!" and the game falls back to the software renderer, while
# `vulkaninfo` prints "undefined symbol: wl_fixes_interface" for every ICD.
# Cause: the AMDGPU driver package puts its own, older libwayland-client.so.0
# under /opt/amdgpu/lib ahead of the system one in the loader cache; current
# Mesa Vulkan drivers need wl_fixes_interface, which only the system copy has.
# Fix: preload the system copy -- done here only when the shadowing is detected,
# so on healthy systems this is just a plain launch.
#
# Usage: scripts/run-keeperfx-vulkan.sh [path/to/keeperfx] [game args...]
#        (default binary: ./keeperfx, i.e. run it from the game directory)
set -u

sys_wl=""
for d in /usr/lib/x86_64-linux-gnu /usr/lib64 /usr/lib; do
    [ -e "$d/libwayland-client.so.0" ] && { sys_wl="$d/libwayland-client.so.0"; break; }
done

first_wl="$(ldconfig -p 2>/dev/null | awk '/libwayland-client\.so\.0 \(libc6,x86-64\)/ {print $NF; exit}')"

has_fixes() { nm -D "$1" 2>/dev/null | grep -q wl_fixes_interface; }

if [ -n "$sys_wl" ] && [ -n "$first_wl" ] \
   && [ "$(readlink -f "$first_wl")" != "$(readlink -f "$sys_wl")" ] \
   && ! has_fixes "$first_wl" && has_fixes "$sys_wl"; then
    echo "run-keeperfx-vulkan: $first_wl shadows the system libwayland-client and lacks wl_fixes_interface; preloading $sys_wl" >&2
    export LD_PRELOAD="$sys_wl${LD_PRELOAD:+:$LD_PRELOAD}"
fi

bin="${1:-./keeperfx}"
[ $# -gt 0 ] && shift
exec "$bin" "$@"
