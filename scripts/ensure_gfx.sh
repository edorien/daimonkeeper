#!/usr/bin/env bash
#
# Makes sure gfx/ holds a complete checkout of dkfans/FXGraphics (the
# source art `make pkg-gfx`/`pkg-enginegfx` build sprite sheets from),
# fetching it only when it isn't already available. Run by make's
# gfx-checkout target (build/make/pkg_gfx.mk) once per invocation that
# touches gfx/, so it's quiet and cheap when there's nothing to do:
#
#   - gfx/ already a checkout: reused as-is. One with tracked files missing
#     (e.g. left by an interrupted clone from before this script: git
#     checks out LICENSE early, so its presence alone proves nothing) is
#     repaired from its own object store, no network needed.
#   - gfx/ absent in a linked git worktree (.claude/worktrees/*, `git
#     worktree add`) whose main checkout has one: symlinked to that,
#     instead of a fresh ~1 GB clone per worktree.
#   - otherwise (fresh clone, CI): `git clone --depth=1` into gfx.partial/,
#     renamed to gfx/ only once complete, so an interrupted clone never
#     passes for a finished one -- the next run just starts it over.
#
# An existing checkout is never updated unless asked:
#
#   REFRESH_GFX=1 make pkg-enginegfx    # or REFRESH_GFX=1 ./build-package.sh
#
# fetches upstream's latest commit and moves gfx/ to it (`git reset
# --keep`: aborts rather than overwrite local edits to files that changed
# upstream). Through a worktree's symlink this refreshes the main
# checkout's gfx/, which every worktree shares.
set -euo pipefail

GFX_URL="${GFX_URL:-https://github.com/dkfans/FXGraphics.git}"

cd "$(dirname "$0")/.."

# Marker written into the checkout's own .git once it's known complete, so
# later runs skip the full-tree scan.
stamp_path() {
    echo "$(git -C "$1" rev-parse --absolute-git-dir)/kfx-checkout-complete"
}

# True if $1 is the top of its own git repository with a resolvable HEAD
# -- not merely a directory inside this repo, which `git -C` would happily
# resolve to dk-fx-v2 itself.
is_checkout() {
    local top
    top=$(git -C "$1" rev-parse --show-toplevel 2>/dev/null) || return 1
    [ "$(cd "$1" && pwd -P)" = "$(cd "$top" && pwd -P)" ] || return 1
    git -C "$1" rev-parse --verify -q HEAD >/dev/null
}

# Restores tracked files missing from checkout $1 (only those -- local
# modifications are left alone). Skipped once stamped, unless $2 is "force".
ensure_complete() {
    local dir="$1" stamp missing
    stamp=$(stamp_path "$dir")
    if [ "${2:-}" != force ] && [ -f "$stamp" ]; then
        return 0
    fi
    missing=$(git -C "$dir" ls-files --deleted | wc -l)
    if [ "$missing" -gt 0 ]; then
        echo "==> $dir: $missing tracked file(s) missing, restoring them from its local object store"
        if ! git -C "$dir" ls-files -z --deleted | xargs -0 git -C "$dir" checkout --; then
            echo "error: $dir is damaged beyond a local repair; remove it (rm -rf $dir) and re-run to clone a fresh copy" >&2
            exit 1
        fi
    fi
    touch "$stamp"
}

refresh() {
    local dir="$1"
    echo "==> Refreshing $dir from upstream (REFRESH_GFX=1)"
    git -C "$dir" fetch --depth=1 origin
    git -C "$dir" reset --keep FETCH_HEAD
    ensure_complete "$dir" force
}

# The main checkout's root if this tree is a linked worktree of a
# non-bare repository; nothing otherwise (main checkout, or no git at all).
main_checkout() {
    local common top
    common=$(git rev-parse --git-common-dir 2>/dev/null) || return 0
    common=$(cd "$common" && pwd -P)
    top=$(cd "$(git rev-parse --show-toplevel)" && pwd -P)
    if [ "$(basename "$common")" = .git ] && [ "$(dirname "$common")" != "$top" ]; then
        dirname "$common"
    fi
}

# A symlink whose target has gone away is just a stale worktree share.
if [ -L gfx ] && [ ! -e gfx ]; then
    rm gfx
fi

if [ -e gfx ]; then
    if is_checkout gfx; then
        if [ "${REFRESH_GFX:-}" = 1 ]; then refresh gfx; fi
        ensure_complete gfx
        exit 0
    fi
    if [ -d gfx ] && [ -z "$(ls -A gfx)" ]; then
        rmdir gfx
    else
        echo "error: gfx/ exists but isn't a git checkout of FXGraphics; move it aside (or remove it) and re-run to clone one" >&2
        exit 1
    fi
fi

main=$(main_checkout)
if [ -n "$main" ] && [ -e "$main/gfx" ] && is_checkout "$main/gfx"; then
    echo "==> Reusing the main checkout's graphics: gfx -> $main/gfx"
    ln -s "$main/gfx" gfx
    if [ "${REFRESH_GFX:-}" = 1 ]; then refresh gfx; fi
    ensure_complete gfx
    exit 0
fi

echo "==> Cloning $GFX_URL into gfx/"
rm -rf gfx.partial
git clone --depth=1 "$GFX_URL" gfx.partial
touch "$(stamp_path gfx.partial)"
mv gfx.partial gfx
