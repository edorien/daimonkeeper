#!/usr/bin/env python3
"""Post-build symbol-level layering audit for the KeeperFX src/kfx_*/ split.

check_layering.py (the CI-blocking, --strict check) only ever looks at
#include text edges between files -- it has no notion of function/global
*symbols* at all. That leaves a real blind spot: a lower-ranked library
can still reach into a higher-ranked one at the symbol level, by
hand-writing a bare `extern` forward-declaration for a function or global
instead of #include-ing the header that actually declares it. See
docs/refactor/todo/check-layering-symbol-level-blind-spot.md for the
concrete instances this caught (get_sprite, get_gameturn,
emulate_integer_overflow, prepare_file_path*, thing_is_invalid,
creature_code_name/creature_desc, and several units_per_pixel_* globals),
all since fixed by moving the code down or routing it through a
callback-struct.

Unlike check_layering.py, this script needs an actual build to already
exist: it runs `nm` over each kfx_* OBJECT library's compiled .o files,
collects each library's *defined* global symbols and *undefined*
(referenced-but-not-defined-here) symbols, and flags any undefined
symbol that resolves only to a symbol defined in a strictly
higher-ranked library. This catches the bare-extern-forward-declaration
pattern for free, without parsing declarations or maintaining a name
index by hand -- it's ground truth from the linker's point of view.

It complements, not replaces, check_layering.py: the #include-level
check runs on raw source (fast, no build needed, gates every push);
this one needs a build tree and is meant for periodic/CI-post-build runs
or local investigation while working on a specific library boundary.

Usage:
    KFX_OS=linux ./build-cmake.sh                      # build first
    python3 scripts/check_layering_symbols.py           # human-readable report
    python3 scripts/check_layering_symbols.py --json     # machine-readable
    python3 scripts/check_layering_symbols.py --strict   # exit 1 on violation
    python3 scripts/check_layering_symbols.py --build-dir out/windows
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_layering import LIBRARY_ORDER, RANK  # noqa: E402

# ---------------------------------------------------------------------------
# Accepted permanent residuals -- mirrors check_layering.py's
# ACCEPTED_VIOLATIONS. Each entry is (referencing_library, symbol,
# defining_library). --strict does not fail on these.
# ---------------------------------------------------------------------------
ACCEPTED_SYMBOL_VIOLATIONS: set[tuple[str, str, str]] = {
    # Empty: every known symbol-level residual has been fixed by moving
    # either the storage (get_packet/get_packet_direct/set_packet_action/
    # set_players_packet_action, creature_table_add -- see docs/refactor/
    # todo/remove-symbol-level-layering-residuals.md) or the code itself
    # (kfxmain -- kfx_platform's PlatformLinux.cpp/PlatformWindows.cpp used
    # to physically own the process main()/WinMain() and call up into
    # app_entry's kfxmain(); the native entry point moved down into
    # app_entry itself, src/native_entry.cpp, see docs/refactor/todo/
    # remove-kfxmain-symbol-residual.md) down to where it actually belongs.
}

OBJ_DIR_RE = re.compile(r"/src/(kfx_\w+)/CMakeFiles/(\w+)\.dir/")
# nm's per-line format: "<addr-or-blank> <type-char> <name>"
NM_LINE_RE = re.compile(r"^([0-9a-fA-F]*)\s+([A-Za-z])\s+(.+)$")


def find_build_dir(preferred: str | None) -> Path:
    candidates = [preferred] if preferred else ["out/linux", "out/windows"]
    for c in candidates:
        if c is None:
            continue
        p = REPO_ROOT / c
        if (p / "CMakeCache.txt").exists():
            return p
    raise SystemExit(
        "No build tree found (looked for CMakeCache.txt under "
        f"{', '.join(str(c) for c in candidates)}). Build first, e.g.:\n"
        "  KFX_OS=linux ./build-cmake.sh\n"
        "or pass --build-dir explicitly."
    )


def classify_object(path: Path, build_dir: Path) -> str | None:
    """Map a compiled .o file back to its owning kfx_* library (or
    app_entry), the same LIBRARY_ORDER classify() in check_layering.py
    uses for source files -- just reading it off the CMake OBJECT
    library's own build directory layout instead of the #include graph."""
    rel = "/" + str(path.relative_to(build_dir)).replace("\\", "/")
    m = OBJ_DIR_RE.search(rel)
    if m:
        return m.group(1)
    # Anything not under a src/kfx_*/ OBJECT library directory but still
    # part of the main keeperfx target is app_entry (main.cpp) --
    # excluding ftests/, a separate exempt tier (test code may depend on
    # anything), same as check_layering.py.
    if "/CMakeFiles/keeperfx" in rel and "/src/ftests/" not in rel:
        return "app_entry"
    return None


def find_object_files(build_dir: Path) -> list[Path]:
    # Skips objects of the retired keeperfx_hvlog variant that an older
    # build tree may still hold (same source, same layering shape).
    return [p for p in build_dir.rglob("*.o") if "_hvlog" not in str(p)]


def nm_symbols(obj: Path) -> tuple[set[str], set[str]]:
    """Returns (defined_global_symbols, undefined_symbols) for one .o file."""
    result = subprocess.run(["nm", str(obj)], capture_output=True, text=True)
    defined: set[str] = set()
    undefined: set[str] = set()
    for line in result.stdout.splitlines():
        m = NM_LINE_RE.match(line)
        if not m:
            continue
        _addr, typ, name = m.groups()
        name = name.strip()
        if typ == "U":
            undefined.add(name)
        elif typ.isupper():
            # Uppercase type letter = externally-visible (global/weak)
            # definition; lowercase = local/static, not linker-visible
            # to other translation units, so it can't be what a
            # cross-library bare extern resolves to.
            defined.add(name)
    return defined, undefined


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build-dir", help="CMake build tree (default: out/linux or out/windows)")
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    ap.add_argument("--strict", action="store_true", help="exit 1 if any violation found")
    args = ap.parse_args()

    build_dir = find_build_dir(args.build_dir)
    objects = find_object_files(build_dir)

    lib_defined: dict[str, set[str]] = {lib: set() for lib in LIBRARY_ORDER}
    # symbol -> owning library (first writer wins; a symbol genuinely
    # defined in more than one library would be a link error already, so
    # collisions here are essentially just same-name-different-library
    # coincidences worth trusting the first classification for).
    symbol_owner: dict[str, str] = {}
    # (library, objfile) -> undefined symbols referenced there
    lib_undefined: dict[str, list[tuple[Path, set[str]]]] = {lib: [] for lib in LIBRARY_ORDER}

    unclassified = 0
    for obj in objects:
        lib = classify_object(obj, build_dir)
        if lib is None:
            unclassified += 1
            continue
        defined, undefined = nm_symbols(obj)
        lib_defined[lib].update(defined)
        for s in defined:
            symbol_owner.setdefault(s, lib)
        lib_undefined[lib].append((obj, undefined))

    violations = []
    for lib, entries in lib_undefined.items():
        for obj, undefined in entries:
            for sym in undefined:
                owner = symbol_owner.get(sym)
                if owner is None or owner == lib:
                    continue
                if RANK[owner] > RANK[lib]:
                    violations.append({
                        "from_lib": lib,
                        "symbol": sym,
                        "to_lib": owner,
                        "object": str(obj.relative_to(REPO_ROOT)),
                    })

    for v in violations:
        v["accepted"] = (v["from_lib"], v["symbol"], v["to_lib"]) in ACCEPTED_SYMBOL_VIOLATIONS
    violations.sort(key=lambda v: (v["from_lib"], v["to_lib"], v["symbol"], v["object"]))
    new_violations = [v for v in violations if not v["accepted"]]
    accepted = [v for v in violations if v["accepted"]]

    def _print_group(vs: list[dict]) -> None:
        last_pair = None
        for v in vs:
            pair = (v["from_lib"], v["to_lib"])
            if pair != last_pair:
                print(f"\n  {v['from_lib']} -> {v['to_lib']}:")
                last_pair = pair
            print(f"    {v['symbol']}  (referenced from {v['object']})")

    if args.json:
        print(json.dumps({
            "build_dir": str(build_dir.relative_to(REPO_ROOT)),
            "violations": new_violations,
            "accepted_violations": accepted,
            "objects_scanned": len(objects),
            "unclassified_count": unclassified,
        }, indent=2))
    else:
        print(f"Scanned {len(objects)} object files under {build_dir.relative_to(REPO_ROOT)}, "
              f"{unclassified} unclassified.")
        if not new_violations:
            print("No new symbol-level layering violations found.")
        else:
            print(f"\n{len(new_violations)} symbol-level layering violation(s) "
                  f"(lower library's undefined symbol resolved only by a higher one):\n")
            _print_group(new_violations)
        if accepted:
            print(f"\n{len(accepted)} accepted permanent residual(s) "
                  f"(see ACCEPTED_SYMBOL_VIOLATIONS in this script), not counted "
                  f"against --strict:\n")
            _print_group(accepted)

    if args.strict and new_violations:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
