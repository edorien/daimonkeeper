#!/usr/bin/env python3
"""Lists the third-party libraries the game is built with, at the versions build/cmake/modules/Dependencies.cmake
pins, and asks the OSV database (api.osv.dev) for known vulnerabilities fixed in a later release (security pass S09).
Run it on demand and before a release; it needs network access.

    python3 scripts/check_dep_vulns.py              # report; exit 1 if a patch release of a pinned library fixes an advisory
    python3 scripts/check_dep_vulns.py --verbose    # also list the advisories only a newer minor/major release fixes
    python3 scripts/check_dep_vulns.py --offline    # only print what is pinned (and the cross-checks), no network
    python3 scripts/check_dep_vulns.py --json       # machine-readable report

How it works, and what it cannot tell you
  * The versions come out of Dependencies.cmake itself (one regex per library, see COMPONENTS), so a bump there is
    seen here. A library pinned in two places (the Windows and the Linux branch) must agree; if the two differ the
    script says so and exits 1.
  * OSV has no database of C-library releases as such. The Debian security tracker, which OSV imports, is the best
    source of per-release advisories for most of these libraries, so the pinned *upstream* version is asked of the
    "Debian" ecosystem and the answer is sorted into three kinds. Debian's fixed version, with its epoch and Debian
    revision removed, is compared with the pin: a fix in the *same minor series* (pin 7.1, fixed in 7.1.5) is a
    drop-in patch update and fails the run; a fix only in a newer minor/major series, and advisories with no fix
    listed, are counted and shown with --verbose. Debian records every range as "from version 0", so these are
    candidates to read, not verdicts: an advisory may concern code that is not built (ffmpeg here is configured
    with only the Smacker demuxer and decoders).
  * A library OSV knows nothing about (OpenResty's LuaJIT fork, libnatpmp, ENet6, the single-file libraries, ...)
    prints under "not covered": those need a look at the project's own release notes. A clean result for the
    others is "nothing known to the Debian tracker", not "no vulnerabilities".
  * The libraries that Linux builds take from the system (SDL3, openal, spng, minizip, luajit, ... when pkg-config
    finds them) are the distro's responsibility and are not checked here; this is about the versions we fetch.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
import urllib.error
import urllib.request
from functools import cmp_to_key
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEPS = ROOT / "build/cmake/modules/Dependencies.cmake"
OSV = "https://api.osv.dev/v1/query"
ECOSYSTEM = "Debian:13"

# name, [regexes with one version group each; all matches must agree], Debian source package (None = not in OSV),
# where to read release notes / advisories
COMPONENTS = [
    ("curl", [r"curl/archive/refs/tags/curl-(\d+)_(\d+)_(\d+)\.tar"], "curl",
     "https://curl.se/docs/security.html"),
    ("ffmpeg", [r"ffmpeg\.org/releases/ffmpeg-([\d.]+)\.tar"], "ffmpeg",
     "https://ffmpeg.org/security.html"),
    ("zlib / minizip", [r"set\(KFX_ZLIB_VER ([\d.]+)\)", r"madler/zlib/releases/download/v([\d.]+)/zlib-"], "zlib",
     "https://zlib.net/"),
    ("libspng", [r"libspng/archive/refs/tags/v([\d.]+)\.tar"], "libspng",
     "https://github.com/randy408/libspng/security/advisories"),
    ("openal-soft", [r"openal-soft/releases/download/([\d.]+)/"], "openal-soft",
     "https://github.com/kcat/openal-soft/releases"),
    ("miniupnpc", [r"miniupnpc_(\d+)_(\d+)_(\d+)\.tar"], "miniupnpc",
     "http://miniupnp.free.fr/ (changelog)"),
    ("SDL3", [r"set\(SDL3_VER\s+([\d.]+)\)"], None,
     "https://github.com/libsdl-org/SDL/releases"),
    ("SDL3_mixer", [r"set\(SDL3_MIX_VER\s+([\d.]+)\)"], None,
     "https://github.com/libsdl-org/SDL_mixer/releases"),
    ("SDL3_image", [r"set\(SDL3_IMG_VER\s+([\d.]+)\)"], None,
     "https://github.com/libsdl-org/SDL_image/releases"),
    ("LuaJIT (OpenResty fork)", [r"luajit2/archive/refs/tags/v([\d.\-]+)\.tar"], None,
     "https://github.com/openresty/luajit2/releases"),
    ("ENet6", [r"enet6/archive/refs/tags/v([\d.]+)\.tar"], None,
     "https://github.com/SirLynix/enet6/releases"),
    ("libnatpmp", [r"libnatpmp/archive/([0-9a-f]{40})\.tar"], None,
     "https://github.com/miniupnp/libnatpmp/commits/master"),
    ("centijson", [r"KFX_CENTIJSON_COMMIT ([0-9a-f]{40})"], None, "https://github.com/mity/centijson"),
    ("astronomy engine", [r"KFX_ASTRONOMY_COMMIT ([0-9a-f]{40})"], None, "https://github.com/cosinekitty/astronomy"),
]


def pinned(text: str, patterns: list[str]) -> tuple[str | None, list[str]]:
    """The version, joined from the regex groups (curl-8_22_0 -> 8.22.0), and a list of problems."""
    found: set[str] = set()
    for pat in patterns:
        for m in re.finditer(pat, text):
            found.add(".".join(m.groups()))
    if not found:
        return None, ["not found in Dependencies.cmake (pattern out of date?)"]
    if len(found) > 1:
        return sorted(found)[0], ["pinned to different versions in different places: " + ", ".join(sorted(found))]
    return found.pop(), []


def _order(c: str) -> int:
    if c == "~":
        return -1
    if c == "":
        return 0
    return ord(c) if c.isalpha() else ord(c) + 256


def vercmp(a: str, b: str) -> int:
    """dpkg's version comparison (without epoch/revision handling): '~' sorts before everything, even the end."""
    while a or b:
        first_diff = 0
        while (a and not a[0].isdigit()) or (b and not b[0].isdigit()):
            ac = _order(a[0]) if a and not a[0].isdigit() else 0
            bc = _order(b[0]) if b and not b[0].isdigit() else 0
            if ac != bc:
                return -1 if ac < bc else 1
            a = a[1:] if a and not a[0].isdigit() else a
            b = b[1:] if b and not b[0].isdigit() else b
        ma, mb = re.match(r"\d*", a), re.match(r"\d*", b)
        na, nb = int(ma.group() or 0), int(mb.group() or 0)
        a, b = a[ma.end():], b[mb.end():]
        if na != nb:
            return -1 if na < nb else 1
    return 0


def series(v: str) -> str:
    """The minor series of a version: 7.1.2 -> 7.1, 8.22.0~rc2 -> 8.22."""
    return ".".join(re.split(r"[^\d]", v)[:2]) if re.match(r"\d", v) else v


def upstream_part(debian_version: str) -> str:
    v = debian_version.split(":", 1)[-1]       # epoch
    v = re.sub(r"-[^-]*$", "", v) if "-" in v else v   # Debian revision
    return re.sub(r"[+.]?dfsg.*$", "", v)


def query(package: str, version: str) -> list[dict]:
    body = json.dumps({"version": version, "package": {"name": package, "ecosystem": ECOSYSTEM}}).encode()
    vulns: list[dict] = []
    while True:
        req = urllib.request.Request(OSV, body, {"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=30) as r:
            data = json.load(r)
        vulns += data.get("vulns", [])
        token = data.get("next_page_token")
        if not token:
            return vulns
        body = json.dumps({"version": version, "package": {"name": package, "ecosystem": ECOSYSTEM},
                           "page_token": token}).encode()


def relevant(vulns: list[dict], package: str, pin: str) -> dict[str, list[dict]]:
    """Sorts the advisories not fixed in the pinned release into patch (a fix in the pin's own minor series),
    later (fixed only in a newer series) and unfixed (no fix listed)."""
    out: dict[str, list[dict]] = {"patch": [], "later": [], "unfixed": []}
    for v in vulns:
        fixed_in: list[str] = []
        for a in v.get("affected", []):
            if a.get("package", {}).get("name") != package:
                continue
            fixed_in += [e["fixed"] for r in a.get("ranges", []) for e in r.get("events", []) if "fixed" in e]
        later = sorted({upstream_part(f) for f in fixed_in if vercmp(upstream_part(f), pin) > 0},
                       key=cmp_to_key(vercmp))
        if fixed_in and not later:
            continue                                  # fixed in a release at or before the pin
        text = (v.get("summary") or v.get("details") or "").strip()
        item = {"id": v["id"], "cve": [x for x in v.get("aliases", []) if x.startswith("CVE-")][:2],
                "summary": text.splitlines()[0][:110] if text else "", "fixed": later[0] if later else "no fix listed"}
        kind = "unfixed" if not later else ("patch" if series(later[0]) == series(pin) else "later")
        out[kind].append(item)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--offline", action="store_true", help="print the pinned versions only")
    ap.add_argument("--verbose", action="store_true", help="also list advisories only a newer series fixes")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()
    text = DEPS.read_text()
    report = []
    bad = False
    for name, patterns, package, notes in COMPONENTS:
        pin, problems = pinned(text, patterns)
        entry = {"name": name, "version": pin, "problems": problems, "notes": notes, "advisories": None}
        if problems:
            bad = True
        if pin and package and not args.offline:
            try:
                entry["advisories"] = relevant(query(package, pin), package, pin)
            except (urllib.error.URLError, TimeoutError) as e:
                entry["problems"].append(f"OSV query failed: {e}")
                bad = True
        report.append(entry)
    if args.json:
        json.dump(report, sys.stdout, indent=2)
        print()
    else:
        for e in report:
            adv = e["advisories"]
            head = f"{e['name']:<26} {(e['version'] or '?')[:16]:<18}"
            if args.offline:
                print(head + " (not queried)")
            elif adv is not None:
                counts = f"{len(adv['patch'])} fixed by a patch release, {len(adv['later'])} only in a newer series, " \
                         f"{len(adv['unfixed'])} with no fix listed"
                print(head + (counts if any(adv.values()) else "none known to the Debian tracker"))
                for kind in ("patch", "later", "unfixed") if args.verbose else ("patch",):
                    shown = adv[kind] if args.verbose else adv[kind][:5]
                    for a in shown:
                        print(f"    {kind:<8} {a['id']:<22} {' '.join(a['cve']):<16} {a['fixed']:<16} {a['summary']}")
                    if len(shown) < len(adv[kind]):
                        print(f"    ... {len(adv[kind]) - len(shown)} more {kind} (--verbose lists them all)")
            else:
                print(head + "not covered by OSV -- check " + e["notes"])
            for p in e["problems"]:
                print("    PROBLEM: " + p)
    if any(e["advisories"] and e["advisories"]["patch"] for e in report):
        bad = True
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
