#!/usr/bin/env python3
"""Content-feature parity between upstream KeeperFX and this tree.

Campaigns and map packs written for KeeperFX use its level-script commands,
config keys, named values and Lua API. This game promises to run content
for a given KeeperFX release ("KFX 1.4", build/make/version.mk); this script
says how true that is, by diffing the feature tables in the two source trees:

  script command   command_desc[], dk1_command_desc[], subfunction_desc[]
  script name      static NamedCommand tables in lvl_script*/script files
                   (variables, players, operators, ...)
  config key       NamedField tables, the legacy key tables passed to
                   recognize_conf_command(), and TOML keys read through
                   CONDITIONAL_ASSIGN_*()/value_dict_get()
  config name      other static NamedCommand tables in config_* files
                   (named values: flags, properties, ...)
  lua              luaL_Reg tables and literal lua_register() names
  other name       static NamedCommand tables anywhere else
  settings         the user's own settings files (keeperfx.cfg, settings.toml,
                   campaign progress) -- listed, but not content, so never
                   "missing content"

It reads source text only (upstream via `git show`, no build), so it is a
table diff, not proof of behaviour: a feature can exist in both tables and
still behave differently, and features outside these tables (Lua object
fields resolved by strcmp, hard-coded parsing) aren't seen.

"missing" = upstream has it, this tree doesn't: content using it won't work
here. "fork-only" = only this tree has it: content using it won't work on
KeeperFX (the editor's "Force KeeperFX" save format must refuse these).
Against a release tag, "fork-only" also includes upstream's own post-release
(alpha) features merged here; the list of what is *really* only ours comes
from the default --upstream origin/master.

Usage:
    python3 scripts/kfx_parity.py                       # vs origin/master
    python3 scripts/kfx_parity.py --upstream v1.4.0     # vs a release tag
    python3 scripts/kfx_parity.py --json                # machine-readable
    python3 scripts/kfx_parity.py --fail-on-missing     # exit 1 if anything is missing
    python3 scripts/kfx_parity.py --fork-only-list      # one "category<TAB>table<TAB>name" per line

Run after every upstream merge (docs/Architecture/upstream-merge-workflow.md):
KFX_COMPAT_* in build/make/version.mk may only move to a release once
`--upstream <that release tag>` reports nothing missing.
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# Parts of this tree that aren't the game's content parsers: tests, the
# in-game editor and its content-schema library (which mirror the parsers'
# names for editing, and would otherwise count twice).
OURS_EXCLUDE = ("/tests/", "src/ftests/", "src/kfx_editor/", "src/kfx_content/")
UPSTREAM_EXCLUDE = ("/tests/", "src/ftests/")
SOURCE_SUFFIXES = (".c", ".cpp", ".h", ".hpp")

CATEGORIES = ("script command", "script name", "config key", "config name", "lua", "other name", "settings")
CONTENT_CATEGORIES = CATEGORIES[:-1]
# Tables whose category depends only on which file they're in; a table that moved
# file between the trees takes upstream's category (see canonicalise()).
NAME_CATEGORIES = ("script name", "config name", "other name", "settings")
# Matched case-insensitively by the game (strcasecmp/strnicmp), so compared that way.
CASE_INSENSITIVE = {"script command", "script name", "config key", "config name", "other name", "settings"}
# Files that parse the player's own settings, not campaign/map content.
SETTINGS_FILES = {"config_keeperfx.c", "config_settings.c", "config_settingschema.c", "game_campaign_progress.c"}

TABLE_RE = re.compile(
    r"\b(?:struct\s+)?(NamedField|NamedCommand|CommandDesc|luaL_Reg)\s+"
    r"([A-Za-z_]\w*)\s*\[[^\]]*\]\s*=\s*\{")
ENTRY_RE = re.compile(r"\{\s*\"([^\"\\]*)\"")
RECOGNIZE_RE = re.compile(r"recognize_conf_command\s*\((?:[^;]*?),\s*([A-Za-z_]\w*)\s*\)\s*;")
LUA_REGISTER_RE = re.compile(r"lua_register\s*\(\s*\w+\s*,\s*\"([^\"]+)\"")
TOML_KEY_RE = re.compile(r"\b(?:CONDITIONAL_ASSIGN_\w+|value_dict_get)\s*\(\s*[^,()]+,\s*\"([^\"]+)\"")
COMMAND_TABLES = ("command_desc", "dk1_command_desc", "subfunction_desc")


def strip_comments(text: str) -> str:
    """Remove // and /* */ comments, keeping string and char literals intact."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            out.append(" ")
            i = n if j < 0 else j + 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def table_body(text: str, open_brace: int) -> str:
    """Text between the brace at open_brace and its matching close (strings respected)."""
    depth, i, n = 0, open_brace, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            i += 1
            while i < n and text[i] != '"':
                i += 2 if text[i] == "\\" else 1
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace + 1:i]
        i += 1
    return text[open_brace + 1:]


def top_level_entry_names(body: str) -> list[str]:
    """First string of each top-level {"name", ...} entry of a table body."""
    names, depth, i, n = [], 0, 0, len(body)
    while i < n:
        c = body[i]
        if c == '"':
            j = i + 1
            while j < n and body[j] != '"':
                j += 2 if body[j] == "\\" else 1
            i = j + 1
            continue
        if c == "{":
            if depth == 0:
                m = ENTRY_RE.match(body, i)
                if m and m.group(1):
                    names.append(m.group(1))
            depth += 1
        elif c == "}":
            depth -= 1
        i += 1
    return names


@dataclass
class Surface:
    """category -> table -> set of names (tables are 'where the name lives')."""
    label: str
    tables: dict[str, dict[str, set[str]]] = field(default_factory=lambda: {c: {} for c in CATEGORIES})

    def add(self, category: str, table: str, name: str) -> None:
        if category in CASE_INSENSITIVE:
            name = name.upper()
        self.tables[category].setdefault(table, set()).add(name)

    def names(self, category: str) -> set[tuple[str, str]]:
        return {(t, n) for t, ns in self.tables[category].items() for n in ns}


def classify_named_command(path: str, table: str, legacy_key_tables: set[str]) -> str:
    base = Path(path).name
    if base in SETTINGS_FILES:
        return "settings"
    if table in legacy_key_tables:
        return "config key"
    if "lvl_script" in base or "script" in base:
        return "script name"
    if base.startswith("config"):
        return "config name"
    return "other name"


def scan_sources(files: dict[str, str], label: str) -> Surface:
    surface = Surface(label)
    stripped = {path: strip_comments(text) for path, text in files.items()}
    legacy_key_tables: set[str] = set()
    for text in stripped.values():
        legacy_key_tables.update(RECOGNIZE_RE.findall(text))
    for path, text in stripped.items():
        base = Path(path).name
        for m in TABLE_RE.finditer(text):
            kind, table = m.group(1), m.group(2)
            names = top_level_entry_names(table_body(text, m.end() - 1))
            if kind == "CommandDesc":
                category = "script command" if table in COMMAND_TABLES else "script name"
            elif kind == "NamedField":
                category = "settings" if base in SETTINGS_FILES else "config key"
            elif kind == "luaL_Reg":
                category = "lua"
            else:
                category = classify_named_command(path, table, legacy_key_tables)
            for name in names:
                surface.add(category, table, name)
        for name in LUA_REGISTER_RE.findall(text):
            surface.add("lua", "<global>", name)
        if base.startswith("config"):
            category = "settings" if base in SETTINGS_FILES else "config key"
            for name in TOML_KEY_RE.findall(text):
                surface.add(category, f"<toml:{Path(base).stem}>", name)
    return surface


def canonicalise(upstream: Surface, ours: Surface) -> None:
    """Put each of our name tables in the category upstream files it under.

    Tables moved between files in this tree's library split (player_desc went from
    lvl_script.h to config_players.c, the function-name tables to config_funcnames.c),
    and a file-based category would then report one table as missing in one category
    and fork-only in another."""
    upstream_category = {t: c for c in NAME_CATEGORIES for t in upstream.tables[c]}
    for cat in NAME_CATEGORIES:
        for table in list(ours.tables[cat]):
            target = upstream_category.get(table, cat)
            if target != cat:
                ours.tables[target].setdefault(table, set()).update(ours.tables[cat].pop(table))


def is_source(path: str, exclude: tuple[str, ...]) -> bool:
    return path.startswith("src/") and path.endswith(SOURCE_SUFFIXES) and not any(x in path for x in exclude)


def git(*args: str) -> str:
    return subprocess.run(["git", "-C", str(REPO_ROOT), *args], check=True,
                          capture_output=True, text=True, errors="replace").stdout


def read_ref(ref: str, exclude: tuple[str, ...]) -> dict[str, str]:
    paths = [p for p in git("ls-tree", "-r", "--name-only", ref, "--", "src").splitlines() if is_source(p, exclude)]
    batch = subprocess.run(["git", "-C", str(REPO_ROOT), "cat-file", "--batch"],
                           input="".join(f"{ref}:{p}\n" for p in paths).encode(),
                           check=True, capture_output=True).stdout
    files, pos = {}, 0
    for path in paths:
        header_end = batch.index(b"\n", pos)
        size = int(batch[pos:header_end].split()[2])
        files[path] = batch[header_end + 1:header_end + 1 + size].decode("utf-8", errors="replace")
        pos = header_end + 1 + size + 1
    return files


def read_worktree(exclude: tuple[str, ...]) -> dict[str, str]:
    files = {}
    for p in (REPO_ROOT / "src").rglob("*"):
        rel = p.relative_to(REPO_ROOT).as_posix()
        if p.is_file() and is_source(rel, exclude):
            files[rel] = p.read_text(encoding="utf-8", errors="replace")
    return files


def compare(upstream: Surface, ours: Surface) -> dict:
    result = {"categories": {}}
    for cat in CATEGORIES:
        up_tables, our_tables = upstream.tables[cat], ours.tables[cat]
        shared = sorted(set(up_tables) & set(our_tables))
        missing = {t: sorted(up_tables[t] - our_tables[t]) for t in shared if up_tables[t] - our_tables[t]}
        fork_only = {t: sorted(our_tables[t] - up_tables[t]) for t in shared if our_tables[t] - up_tables[t]}
        # A table on one side only is usually a rename or a move: compare its names
        # against everything else in the category before calling them missing.
        up_all = {n for ns in up_tables.values() for n in ns}
        our_all = {n for ns in our_tables.values() for n in ns}
        for t in sorted(set(up_tables) - set(our_tables)):
            gone = sorted(up_tables[t] - our_all)
            if gone:
                missing[t] = gone
        for t in sorted(set(our_tables) - set(up_tables)):
            new = sorted(our_tables[t] - up_all)
            if new:
                fork_only[t] = new
        result["categories"][cat] = {
            "upstream": len(upstream.names(cat)),
            "ours": len(ours.names(cat)),
            "missing": missing,
            "fork_only": fork_only,
            "tables_only_upstream": sorted(set(up_tables) - set(our_tables)),
            "tables_only_ours": sorted(set(our_tables) - set(up_tables)),
        }
    return result


def count(entries: dict[str, list[str]]) -> int:
    return sum(len(v) for v in entries.values())


def print_report(result: dict, upstream_label: str, ours_label: str, verbose_tables: bool) -> None:
    print(f"KeeperFX content parity: upstream {upstream_label}  vs  {ours_label}\n")
    print(f"{'':16}{'upstream':>10}{'ours':>8}{'missing':>9}{'fork-only':>11}")
    for cat, r in result["categories"].items():
        print(f"{cat:16}{r['upstream']:>10}{r['ours']:>8}{count(r['missing']):>9}{count(r['fork_only']):>11}")
    for key, title in (("missing", "Missing here (upstream has it; content using it won't work in this build)"),
                       ("fork_only", "Fork-only (content using it won't work on KeeperFX)")):
        print(f"\n== {title} ==")
        any_entries = False
        for cat, r in result["categories"].items():
            if cat == "settings":
                continue
            for table, names in r[key].items():
                any_entries = True
                print(f"[{cat}] {table}: {', '.join(names)}")
        if not any_entries:
            print("(none)")
    settings = result["categories"]["settings"]
    if settings["missing"] or settings["fork_only"]:
        print("\n== Settings (the player's own config files -- not content) ==")
        for table, names in settings["missing"].items():
            print(f"[upstream only] {table}: {', '.join(names)}")
        for table, names in settings["fork_only"].items():
            print(f"[ours only] {table}: {', '.join(names)}")
    if verbose_tables:
        print("\n== Tables on one side only (renamed/moved tables are matched by name above) ==")
        for cat, r in result["categories"].items():
            if r["tables_only_upstream"] or r["tables_only_ours"]:
                print(f"[{cat}] upstream only: {', '.join(r['tables_only_upstream']) or '-'}")
                print(f"[{cat}] ours only:     {', '.join(r['tables_only_ours']) or '-'}")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--upstream", default="origin/master", help="upstream git ref (branch or release tag)")
    ap.add_argument("--ours", default=None, help="compare this git ref instead of the working tree")
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    ap.add_argument("--fork-only-list", action="store_true", help="print fork-only names, one per line")
    ap.add_argument("--tables", action="store_true", help="also list tables present on one side only")
    ap.add_argument("--fail-on-missing", action="store_true", help="exit 1 if anything upstream has is missing")
    args = ap.parse_args(argv)

    try:
        upstream_desc = git("log", "-1", "--format=%h %cs", args.upstream).strip()
        upstream_files = read_ref(args.upstream, UPSTREAM_EXCLUDE)
        if args.ours:
            ours_label = f"{args.ours} ({git('log', '-1', '--format=%h %cs', args.ours).strip()})"
            ours_files = read_ref(args.ours, OURS_EXCLUDE)
        else:
            ours_label = "working tree"
            ours_files = read_worktree(OURS_EXCLUDE)
    except subprocess.CalledProcessError as e:
        sys.stderr.write(f"git failed: {' '.join(e.cmd)}\n{e.stderr}\n")
        return 2
    upstream_label = f"{args.upstream} ({upstream_desc})"
    upstream, ours = scan_sources(upstream_files, upstream_label), scan_sources(ours_files, ours_label)
    canonicalise(upstream, ours)
    result = compare(upstream, ours)

    if args.json:
        json.dump({"upstream": upstream_label, "ours": ours_label, **result}, sys.stdout, indent=2)
        print()
    elif args.fork_only_list:
        for cat, r in result["categories"].items():
            for table, names in r["fork_only"].items():
                for name in names:
                    print(f"{cat}\t{table}\t{name}")
    else:
        print_report(result, upstream_label, ours_label, args.tables)

    missing_total = sum(count(result["categories"][c]["missing"]) for c in CONTENT_CATEGORIES)
    return 1 if (args.fail_on_missing and missing_total) else 0


if __name__ == "__main__":
    sys.exit(main())
