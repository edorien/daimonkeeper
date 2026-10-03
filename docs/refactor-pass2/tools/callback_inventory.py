#!/usr/bin/env python3
"""Inventory of the cross-layer callback tables (docs/refactor-pass2/).

For every `struct *Callbacks` / `struct *Predicates` / `struct *Port` table under src/ it
reports: entry count, which library each entry's implementation lives in
(resolved through src/main.cpp's table initializer + its static
wrapper bodies), which libraries call the entry, entries with no callers
outside tests, entries whose every caller already ranks at or above the
implementer (the callback is unnecessary), and implementations exposed
through more than one entry.

Heuristic, source-text based (no compiler): good for tracking the trend
across a refactor, not a proof. Verify any single finding by hand before
acting on it.

Usage:
    python3 docs/refactor-pass2/tools/callback_inventory.py            # summary
    python3 docs/refactor-pass2/tools/callback_inventory.py --entries  # + per-entry rows
    python3 docs/refactor-pass2/tools/callback_inventory.py --json out.json
"""
from __future__ import annotations

import argparse
import collections
import json
import re
import subprocess
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
RANK = ['kfx_platform', 'kfx_config', 'kfx_content', 'kfx_model', 'kfx_pathfinding', 'kfx_sim', 'kfx_render',
        'kfx_net', 'kfx_game', 'kfx_frontend', 'kfx_script', 'kfx_apploop',
        'kfx_editor', 'app_entry']


def lib_of(path: str) -> str:
    parts = path.split('/')
    if parts[1] == 'ftests':
        return 'ftests'
    return parts[1] if len(parts) > 2 else 'app_entry'


def rank(lib: str) -> int:
    return RANK.index(lib) if lib in RANK else 99


def strip_comments(text: str) -> str:
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def split_top_level(body: str) -> list[str]:
    items, depth, cur = [], 0, ''
    for ch in body:
        if ch in '({[':
            depth += 1
        elif ch in ')}]':
            depth -= 1
        if ch == ',' and depth == 0:
            items.append(cur)
            cur = ''
        else:
            cur += ch
    if cur.strip():
        items.append(cur)
    return [i.strip().lstrip('&').strip() for i in items if i.strip()]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('--entries', action='store_true')
    ap.add_argument('--json')
    args = ap.parse_args()

    files = [f for f in subprocess.check_output(['git', 'ls-files', 'src'], cwd=REPO, text=True).split()
             if f.endswith(('.c', '.cpp', '.h', '.hpp'))]
    src = {f: (REPO / f).read_text(errors='replace') for f in files}
    prod = {f: t for f, t in src.items() if '/tests/' not in f}

    structs = {}
    for f, t in prod.items():
        for m in re.finditer(r'struct\s+(\w*(?:Callbacks|Predicates|Port))\s*\{(.*?)\n\};', t, re.S):
            names = []
            # refactor pass 2, S15: a port lists its entries in a .def file
            for inc in re.findall(r'#include\s+"(ports/\w+\.def)"', m.group(2)):
                d = (REPO / Path(f).parent.parent / inc).read_text()  # <lib>/include/ports/<x>.def
                names += re.findall(r'^KFX_PORT_(?:VOID|RET)[XS]?\((?:[^,]*,\s*)?(\w+)\s*,\s*\(', strip_comments(d), re.M)
            for decl in strip_comments(m.group(2)).split(';'):
                x = re.search(r'\(\s*\*\s*(\w+)\s*\)\s*\(', decl)
                if x:
                    names.append(x.group(1))
            if m.group(1) == 'ReceiveCallbacks':
                # not a port: kfx_platform's ServiceProvider takes it per
                # instance, and its callers and implementation are all in
                # kfx_platform (refactor pass 2, S15)
                continue
            structs[m.group(1)] = (f, names)

    defs = collections.defaultdict(set)
    for f, t in prod.items():
        if f.endswith(('.c', '.cpp')):
            for m in re.finditer(r'^(?:extern "C"\s+)?[A-Za-z_][\w \*\t]*?\b(\w+)\s*\([^;{]*\)\s*\{', strip_comments(t), re.M):
                defs[m.group(1)].add(lib_of(f))

    # extern globals, so a wrapper that reads one counts its owner as an implementer
    globals_ = collections.defaultdict(set)
    for f, t in prod.items():
        if f.endswith(('.h', '.hpp')) and lib_of(f) != 'app_entry':
            for m in re.finditer(r'^\s*extern\s+(?!"C")[^;()]*?\b(\w+)\s*(?:\[[^\]]*\]\s*)*;', strip_comments(t), re.M):
                globals_[m.group(1)].add(lib_of(f))

    main_src = strip_comments(src['src/main.cpp'])
    wrappers = {m.group(1): m.group(2) for m in re.finditer(
        r'^static[^\n;{=]*?\b(\w+)\s*\([^;{]*\)\s*\{(.*?)^\}', main_src, re.M | re.S)}

    def impl_libs(fn: str) -> set[str]:
        if fn in wrappers:
            body = wrappers[fn]
            libs = {l for c in re.findall(r'\b(\w+)\s*\(', body) for l in defs.get(c, ()) if l != 'app_entry'}
            libs |= {'kfx_' + s for s in re.findall(r'kfx_(\w+?)_state\b', body)}
            libs |= {l for w in re.findall(r'\b(\w+)\b(?!\s*\()', body) for l in globals_.get(w, ())}
            return libs or {'app_entry'}
        return set(defs.get(fn, ())) or {'?'}

    report = {}
    for sname, (hdr, names) in sorted(structs.items(), key=lambda kv: -len(kv[1][1])):
        m = re.search(r'struct\s+' + sname + r'\s+\w+\s*=\s*\{(.*?)\n\s*\};', main_src, re.S)
        if not m:
            # refactor pass 2, S15: a port's table lives in its provider library
            for f, t in prod.items():
                if f.endswith(('.c', '.cpp')):
                    # skip the generated unwired defaults (<stem>_defaults)
                    m = re.search(r'const\s+struct\s+' + sname + r'\s+(?!\w*_defaults\b)\w+\s*=\s*\{(.*?)\n\s*\};', strip_comments(t), re.S)
                    if m:
                        break
        # ...and its callers use generated wrappers, PREFIX##name
        pm = re.search(r'static inline void (\w+)##name', src.get(hdr, ''))
        prefix = pm.group(1) if pm else None
        impls = split_top_level(m.group(1)) if m else ['?'] * len(names)
        if impls and impls[0].startswith('.'):
            # designated initializer (S01 step D onwards): pair by name
            named = dict(tuple(x.strip().lstrip('&').strip() for x in i[1:].split('=', 1)) for i in impls)
            impls = [named.get(n, '?') for n in names]
        rows = []
        for name, fn in zip(names, impls):
            pat = re.compile(r'(?:->|\.)\s*' + name + r'\b(?!\s*=[^=])' + (r'|\b' + prefix + name + r'\s*\(' if prefix else ''))
            callers = collections.Counter()
            for f, t in prod.items():
                if f in ('src/main.cpp', hdr):
                    continue
                n = len(pat.findall(strip_comments(t)))
                if n:
                    callers[lib_of(f)] += n
            rows.append({'entry': name, 'impl': fn, 'impl_libs': sorted(impl_libs(fn)), 'callers': dict(callers)})
        report[sname] = {'header': hdr, 'wired': bool(m), 'rows': rows}

    total = sum(len(v['rows']) for v in report.values())
    print(f"{len(report)} callback tables, {total} entries\n")
    print(f"{'table':32} {'n':>4}  implementer libs")
    for sname, v in report.items():
        impl = collections.Counter(l for r in v['rows'] for l in r['impl_libs'])
        print(f"{sname:32} {len(v['rows']):4}  {dict(impl)}")

    print("\nEntries with no non-test callers:")
    for sname, v in report.items():
        for r in v['rows']:
            if not r['callers']:
                print(f"  {sname}.{r['entry']}")

    print("\nEntries whose every caller ranks >= the implementer (callback unnecessary):")
    for sname, v in report.items():
        for r in v['rows']:
            il = [l for l in r['impl_libs'] if l in RANK]
            if r['callers'] and il and len(il) == len(r['impl_libs']) \
                    and all(rank(c) >= max(rank(l) for l in il) for c in r['callers']):
                print(f"  {sname}.{r['entry']}  impl={il} callers={r['callers']}")

    by_impl = collections.defaultdict(list)
    for sname, v in report.items():
        for r in v['rows']:
            if r['impl'] != '?':
                by_impl[r['impl']].append(f"{sname}.{r['entry']}")
    dups = {k: v for k, v in by_impl.items() if len(v) > 1}
    print(f"\n{len(dups)} implementations exposed through more than one entry:")
    for k, v in sorted(dups.items()):
        print(f"  {k}: {', '.join(v)}")

    if args.entries:
        for sname, v in report.items():
            print(f"\n== {sname} ({v['header']})")
            for r in v['rows']:
                print(f"  {r['entry']:44} <- {r['impl']:48} impl={','.join(r['impl_libs']):24} callers={r['callers']}")
    if args.json:
        Path(args.json).write_text(json.dumps(report, indent=1))


if __name__ == '__main__':
    main()
