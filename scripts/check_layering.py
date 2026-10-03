#!/usr/bin/env python3
"""Dependency-layering checker for the KeeperFX src/kfx_*/ split.

Stage 13 of docs/refactor/: a mandatory, CI-blocking check that turns
"does library X depend on library Y the wrong way round" into something
the build can answer, instead of something read out of hundreds of files
by eye.

By this stage every file has a real src/<name>/{src,include} home (see
docs/refactor/stage-13-enforce-and-document.md), so classification is just
"which src/<name>/ directory is this file under" -- the physical layout
*is* the CMake target membership, since every src/<name>/CMakeLists.txt
globs its own src/*.c|*.cpp into exactly one OBJECT library (see e.g.
src/kfx_platform/CMakeLists.txt). No hand-maintained per-file table to
keep in sync with the code anymore. Every kfx_* library lives directly
under src/ (unified there after the SDL3 merge, not a separate top-level
libs/ tree), alongside main.cpp/ftests/.

Usage:
    python3 scripts/check_layering.py            # human-readable report
    python3 scripts/check_layering.py --json      # machine-readable report
    python3 scripts/check_layering.py --strict    # exit 1 on any violation

Exit code is always 0 unless --strict is passed. CI passes --strict (see
.github/workflows/*.yml) so a back-edge fails the build, not just a
code-review nitpick.
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = REPO_ROOT / "src"

# ---------------------------------------------------------------------------
# Library order, lowest (fewest allowed dependencies) to highest.
# An edge "A includes B" is only valid if RANK[lib(B)] <= RANK[lib(A)].
# kfx_script, kfx_apploop, and app_entry are allowed to depend on anything
# (see docs/refactor/stage-11-kfx-script.md and
# stage-12-slim-app-target.md) so they're ranked above everything else;
# nothing should depend on *them*, which the checker will also flag if it
# happens. kfx_apploop (stage 12.5) holds the top-level game/frontend
# session loop (game_loop/wait_at_frontend/keeper_gameplay_loop/update()
# and the frame-pacing helpers around them) -- by design it ties every
# other layer together each frame, so forcing it through per-call
# callback injection (the pattern every other "layer X needs layer Y"
# case in this refactor uses) would mean adding on the order of 40 new
# GameCallbacks/RenderOverlayCallbacks entries for what is genuinely the
# app's main loop, not domain logic. It's its own top-ranked library
# instead, kept out of the app target (stage 12's exit criterion).
#
# This list is also the authoritative "which physical directory maps to
# which library" answer -- each entry names a src/<entry>/ directory
# except app_entry, which has no src/<name>/ directory of its own (it's
# whatever's left directly under src/: main.cpp today).
# ---------------------------------------------------------------------------
LIBRARY_ORDER = [
    "kfx_platform",
    "kfx_config",
    # refactor pass 2, S04: the cfgc_* content layer, split out of kfx_config.
    # Only kfx_editor (and the exempt ftests) use it.
    "kfx_content",
    # refactor pass 2, S08: header-only type layouts (struct Thing, struct Map,
    # struct SlabMap) the pathfinder reads directly. Headers only: see
    # check_model_headers() below.
    "kfx_model",
    "kfx_pathfinding",
    "kfx_sim",
    # refactor pass 2, S14: the computer-player AI, a client of the sim.
    # kfx_sim reaches it only through kfx_config's AiPort.
    "kfx_ai",
    "kfx_render",
    "kfx_net",
    "kfx_game",
    "kfx_frontend",
    "kfx_script",
    "kfx_apploop",
    # docs/refactor/editor/00-overview.md §5.1 -- the in-game level editor's
    # own library. Ranked immediately below app_entry (the highest of any
    # kfx_* library): it may depend on everything below it; nothing below
    # depends on it back. Its only inbound edge is main.cpp (app_entry),
    # which already includes every layer as the composition root.
    "kfx_editor",
    "app_entry",
]
RANK = {lib: i for i, lib in enumerate(LIBRARY_ORDER)}
LIB_DIR_NAMES = set(LIBRARY_ORDER) - {"app_entry"}

# ---------------------------------------------------------------------------
# Accepted permanent residuals (see docs/refactor/stage-13-enforce-and-
# document.md's Tier 4 Group D). Each was investigated and found to have no
# viable fix without a deeper redesign that's out of scope for this
# refactor (an ABI-shared struct, an intentional raw-blob wire format, or a
# single legitimate direct call). --strict does not fail on these; it
# fails on anything NOT in this list, so a genuinely new violation still
# blocks CI. Remove an entry here if a future change actually resolves it
# -- don't let this list grow to paper over new violations.
#
# Empty as of docs/refactor/todo/remove-remaining-layering-violations.md:
# both prior entries here (net_resync.cpp's kfx_frontend_state.h/
# kfx_game_state.h/game_legacy.h raw-blob resync includes, and
# bflib_enet.cpp's net_main.h include) turned out to be fixable once the
# ftest coverage built in docs/refactor/todo/ftest-fake-multiplayer.md
# made attempting the fix safe to verify. net_resync.cpp now exports/
# imports the upper-layer structs via NetCallbacks (same pattern the file
# already used for Lua's resync payload) instead of #include'ing them
# directly; bflib_enet.cpp's struct NetSP contract moved down to
# kfx_platform's bflib_netsp.h, the layer that actually implements it.
# ---------------------------------------------------------------------------
ACCEPTED_VIOLATIONS: set[tuple[str, str]] = set()

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"([^"]+)"')


def classify(path: Path) -> str | None:
    """Classify a file by the src/<name>/ directory it physically lives
    under (that directory IS the CMake OBJECT library's source set), or
    app_entry for anything directly under src/ (not src/ftests/, which is
    a separate exempt tier -- test code may depend on anything)."""
    try:
        rel = path.relative_to(SRC_DIR)
    except ValueError:
        return None
    top = rel.parts[0]
    if top in LIB_DIR_NAMES:
        return top
    if top == "ftests":
        return None
    return "app_entry"


# Maps #include "foo.h" -> the file it resolves to, so an edge can be
# classified even when two libraries both happen to have a file with the
# same stem (classification used to be by stem alone; a real path lookup
# avoids that ambiguity).
def build_stem_index(files: list[Path]) -> dict[str, list[Path]]:
    index: dict[str, list[Path]] = {}
    for p in files:
        index.setdefault(p.name, []).append(p)
    return index


def find_project_files() -> list[Path]:
    exts = (".c", ".cpp", ".h", ".hpp")
    # git ls-files, not a filesystem glob: src/ accumulates
    # gitignored, build-generated files alongside real source (e.g.
    # src/ver_defs.h, written by configure_file()/the Makefiles' version
    # step) which aren't part of the architecture and would otherwise be
    # misclassified as app_entry and produce false-positive violations.
    out = subprocess.run(
        ["git", "-C", str(REPO_ROOT), "ls-files", "--", "src"],
        capture_output=True, text=True, check=True,
    ).stdout
    files = [REPO_ROOT / line for line in out.splitlines() if Path(line).suffix in exts]
    # ftests/ and tests/ are a separate, exempt tier (test code may depend on
    # anything); excluding them keeps the report focused on production code.
    return [p for p in files if "ftests" not in p.parts and "tests" not in p.parts]


def extract_includes(path: Path) -> list[str]:
    includes = []
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return includes
    for line in text.splitlines():
        m = INCLUDE_RE.match(line)
        if m:
            includes.append(Path(m.group(1)).name)
    return includes


MODEL_INCLUDE_DIR = REPO_ROOT / "src" / "kfx_model" / "include"


def check_model_headers() -> list[str]:
    """kfx_model is header-only (refactor pass 2, S08): its headers may declare
    types, macros and static inline functions, but no non-inline function
    prototypes and no extern data. That keeps it from ever creating a symbol
    reference to a higher library. Returns one message per offending line."""
    problems = []
    for hdr in sorted(MODEL_INCLUDE_DIR.glob("*.h")):
        text = hdr.read_text(errors="replace")
        text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
        text = re.sub(r"//[^\n]*", "", text)
        # Blank out the #ifdef __cplusplus / extern "C" { ... } wrappers, so
        # their braces don't make the whole header look nested.
        text = re.sub(r"#ifdef __cplusplus[^\n]*\n.*?#endif",
                      lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
        depth = 0
        for lineno, line in enumerate(text.split("\n"), 1):
            stripped = line.strip()
            if depth == 0 and stripped and not stripped.startswith("#"):
                if re.match(r"extern\b(?!\s*\"C\")", stripped):
                    problems.append(f"{hdr.relative_to(REPO_ROOT)}:{lineno}: extern data in kfx_model")
                elif (stripped.endswith(");") and "(" in stripped
                      and not stripped.startswith(("static", "typedef", "#"))
                      and not re.search(r"\(\s*\*", stripped)):
                    problems.append(f"{hdr.relative_to(REPO_ROOT)}:{lineno}: function prototype in kfx_model")
            depth += line.count("{") - line.count("}")
    return problems


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    ap.add_argument("--strict", action="store_true", help="exit 1 if any violation found")
    ap.add_argument("--show-unclassified", action="store_true",
                     help="also list files not resolved to a library")
    args = ap.parse_args()

    files = find_project_files()
    stem_index = build_stem_index(files)
    violations = []
    unclassified = set()
    edge_count = 0

    for path in files:
        src_lib = classify(path)
        if src_lib is None:
            unclassified.add(str(path.relative_to(REPO_ROOT)))
            continue
        for inc_name in extract_includes(path):
            candidates = stem_index.get(inc_name)
            if not candidates:
                continue  # system/third_party header, not part of this tree
            # A header name should only resolve to one library in practice
            # (headers aren't duplicated by name across src/kfx_*/include/);
            # if it somehow does, treat it as a violation only if *every*
            # candidate agrees it's one, to avoid false positives from an
            # incidental name collision.
            dst_libs = {classify(c) for c in candidates}
            dst_libs.discard(None)
            if not dst_libs:
                continue
            if src_lib in dst_libs:
                continue
            edge_count += 1
            if all(RANK[d] > RANK[src_lib] for d in dst_libs):
                violations.append({
                    "file": str(path.relative_to(REPO_ROOT)),
                    "from_lib": src_lib,
                    "includes": inc_name,
                    "to_lib": min(dst_libs, key=lambda d: RANK[d]),
                })

    for v in violations:
        v["accepted"] = (v["file"], v["includes"]) in ACCEPTED_VIOLATIONS
    violations.sort(key=lambda v: (v["from_lib"], v["to_lib"], v["file"]))
    new_violations = [v for v in violations if not v["accepted"]]
    accepted = [v for v in violations if v["accepted"]]

    def _print_group(vs: list[dict]) -> None:
        last_pair = None
        for v in vs:
            pair = (v["from_lib"], v["to_lib"])
            if pair != last_pair:
                print(f"\n  {v['from_lib']} -> {v['to_lib']}:")
                last_pair = pair
            print(f"    {v['file']}  includes  {v['includes']}")

    if args.json:
        print(json.dumps({
            "violations": new_violations,
            "accepted_violations": accepted,
            "edges_checked": edge_count,
            "files_scanned": len(files),
            "unclassified_count": len(unclassified),
        }, indent=2))
    else:
        print(f"Scanned {len(files)} files, checked {edge_count} classified "
              f"#include edges, {len(unclassified)} files unclassified.")
        if not new_violations:
            print("No new layering violations found among classified files.")
        else:
            print(f"\n{len(new_violations)} layering violation(s) "
                  f"(lower library reaching into a higher one):\n")
            _print_group(new_violations)
        if accepted:
            print(f"\n{len(accepted)} accepted permanent residual(s) "
                  f"(see ACCEPTED_VIOLATIONS in this script), not counted "
                  f"against --strict:\n")
            _print_group(accepted)
        if args.show_unclassified and unclassified:
            print(f"\nUnclassified files ({len(unclassified)}), skipped: "
                  f"{', '.join(sorted(unclassified))}")

    model_problems = check_model_headers()
    if not args.json:
        for msg in model_problems:
            print(msg)
    if args.strict and (new_violations or model_problems):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
