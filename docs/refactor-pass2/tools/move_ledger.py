#!/usr/bin/env python3
"""Move ledger tooling for refactor pass 2 (docs/refactor-pass2/function-moves.md).

Commands:
    check                     verify every ledger row against the current tree:
                              'planned' and 'dropped' rows must still be at `from`, 'done' rows must be at `to`.
    lookup SYMBOL...          where each symbol is defined/declared now, plus any ledger rows about it.
    upstream PATH [--ref R]   for an upstream (flat-layout) file, list every function it defines and
                              where that function lives in this fork now (default ref: origin/master).
                              Use during upstream-merge-workflow.md step 2.
    detect RANGE              list functions whose defining file changed across a git range
                              (e.g. HEAD~5..HEAD) and flag the ones missing from the ledger.
                              Run before committing a stage's moves.

Source-text heuristics (regex, no compiler); good enough to catch drift and missed rows.
"""
from __future__ import annotations

import argparse
import collections
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
LEDGER = Path(__file__).resolve().parents[1] / 'function-moves.tsv'
FUNC_DEF = re.compile(r'^(?:static\s+)?(?:inline\s+)?[A-Za-z_][\w \*\t]*?\b(\w+)\s*\([^;{]*\)\s*(?:const\s*)?\{', re.M)
KEYWORDS = {'if', 'while', 'for', 'switch', 'return', 'sizeof'}


def git(*args: str) -> str:
    return subprocess.check_output(['git', *args], cwd=REPO, text=True, errors='replace',
                                   stderr=subprocess.DEVNULL)


def strip_comments(text: str) -> str:
    text = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def defined_functions(text: str) -> set[str]:
    return {m.group(1) for m in FUNC_DEF.finditer(strip_comments(text))} - KEYWORDS


def source_files(rev: str | None = None) -> list[str]:
    out = git('ls-tree', '-r', '--name-only', rev, 'src') if rev else git('ls-files', 'src')
    return [f for f in out.split() if f.endswith(('.c', '.cpp', '.h', '.hpp')) and '/tests/' not in f]


def read(path: str, rev: str | None = None) -> str:
    if rev:
        try:
            return git('show', f'{rev}:{path}')
        except subprocess.CalledProcessError:
            return ''
    p = REPO / path
    return p.read_text(errors='replace') if p.exists() else ''


def load_ledger() -> list[dict]:
    rows = []
    for line in LEDGER.read_text().splitlines():
        if not line.strip() or line.startswith('#'):
            continue
        cols = (line.split('\t') + [''] * 8)[:8]
        rows.append(dict(zip(['stage', 'kind', 'symbol', 'from', 'to', 'status', 'commit', 'notes'], cols)))
    return rows


def definition_index() -> dict[str, list[str]]:
    idx = collections.defaultdict(list)
    for f in source_files():
        if f.endswith(('.c', '.cpp')):
            for name in defined_functions(read(f)):
                idx[name].append(f)
    return idx


def symbol_present(kind: str, symbol: str, path: str) -> bool:
    if path.startswith('(') or not path:
        return True  # "(deleted)" / "(direct call)" targets are not checked
    first, _, renamed = path.split(' + ')[0].split('{')[0].partition('#')  # path#new_name: renamed on the way
    if kind == 'file':
        return any(Path(REPO).glob(first.rstrip('*') + '*')) if '*' in path else (REPO / first).exists()
    text = strip_comments(read(first))
    if not text:
        return False
    name = renamed or symbol.split('.')[-1].replace('struct ', '')
    if kind == 'callback-entry':
        if first.endswith('.def'):  # refactor pass 2, S15: a port's entry list
            return re.search(r'^KFX_PORT_\w+\((?:[^,\n]*,\s*)?' + re.escape(name) + r'\s*,\s*\(', text, re.M) is not None
        return re.search(r'\(\s*\*\s*' + re.escape(name) + r'\s*\)', text) is not None
    if kind == 'table-type' and first.endswith('.def'):
        return True
    if kind == 'type':
        return re.search(r'struct\s+' + re.escape(name) + r'\s*\{', text) is not None
    if kind == 'function':
        return name in defined_functions(text)
    return re.search(r'\b' + re.escape(name) + r'\b', text) is not None


def superseded(rows: list[dict], i: int, where: str) -> bool:
    """A later row picks the symbol up from where this row left it."""
    name = rows[i]['symbol'].split('.')[-1]
    return any(r['from'] == where and r['symbol'].split('.')[-1] == name for r in rows[i + 1:])


def cmd_check(_args) -> int:
    bad = 0
    rows = load_ledger()
    for i, r in enumerate(rows):
        where = r['from'] if r['status'] in ('planned', 'dropped') else r['to']
        if r['status'] not in ('planned', 'done', 'dropped'):
            print(f"?? {r['stage']} {r['symbol']}: unknown status '{r['status']}'")
            bad += 1
        elif not symbol_present(r['kind'], r['symbol'], where) and not superseded(rows, i, where):
            print(f"DRIFT {r['stage']} {r['kind']} {r['symbol']}: expected at {where} ({r['status']})")
            bad += 1
    print(f"{len(load_ledger())} rows checked, {bad} problem(s)")
    return 1 if bad else 0


def cmd_lookup(args) -> int:
    idx = definition_index()
    rows = load_ledger()
    for sym in args.symbols:
        print(f"== {sym}")
        for f in idx.get(sym, []):
            print(f"   defined: {f}")
        if sym not in idx:
            decl = [f for f in source_files() if f.endswith(('.h', '.hpp'))
                    and re.search(r'\b' + re.escape(sym) + r'\b', strip_comments(read(f)))]
            print('   not defined as a function; mentioned in headers: ' + (', '.join(decl[:6]) or 'none'))
        for r in rows:
            if sym == r['symbol'].split('.')[-1] or re.search(r'\b' + re.escape(sym) + r'\b', r['notes']):
                print(f"   ledger: {r['stage']} {r['kind']} {r['from']} -> {r['to']} [{r['status']}] {r['notes']}")
    return 0


def cmd_upstream(args) -> int:
    text = read(args.path, args.ref)
    if not text:
        print(f"{args.ref}:{args.path} not found", file=sys.stderr)
        return 1
    idx = definition_index()
    ledger = {r['symbol'].split('.')[-1]: r for r in load_ledger()}
    base = Path(args.path).stem
    for name in sorted(defined_functions(text)):
        homes = idx.get(name, [])
        moved = [h for h in homes if Path(h).stem != base]
        tag = 'MISSING' if not homes else ('moved' if moved and len(moved) == len(homes) else 'same-file')
        note = f"  ledger: {ledger[name]['stage']} {ledger[name]['notes']}" if name in ledger else ''
        print(f"{tag:9} {name:48} {', '.join(homes) or '-'}{note}")
    return 0


def cmd_detect(args) -> int:
    base, head = args.range.split('..')
    changed = [f for f in git('diff', '--name-only', args.range, '--', 'src').split()
               if f.endswith(('.c', '.cpp')) and '/tests/' not in f]
    lost, gained = collections.defaultdict(set), collections.defaultdict(set)
    for f in changed:
        old, new = defined_functions(read(f, base)), defined_functions(read(f, head or 'HEAD'))
        for n in old - new:
            lost[n].add(f)
        for n in new - old:
            gained[n].add(f)
    known = {r['symbol'].split('.')[-1] for r in load_ledger()}
    known |= {w for r in load_ledger() for w in re.findall(r'\b\w+\b', r['notes'])}
    moves = sorted(n for n in lost if n in gained)
    for n in moves:
        flag = '' if n in known else '   <-- not in ledger'
        print(f"{n:48} {','.join(sorted(lost[n]))} -> {','.join(sorted(gained[n]))}{flag}")
    print(f"{len(moves)} moved function(s) in {args.range}")
    return 1 if any(n not in known for n in moves) and args.strict else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('check')
    p = sub.add_parser('lookup'); p.add_argument('symbols', nargs='+')
    p = sub.add_parser('upstream'); p.add_argument('path'); p.add_argument('--ref', default='origin/master')
    p = sub.add_parser('detect'); p.add_argument('range'); p.add_argument('--strict', action='store_true')
    args = ap.parse_args()
    return {'check': cmd_check, 'lookup': cmd_lookup, 'upstream': cmd_upstream, 'detect': cmd_detect}[args.cmd](args)


if __name__ == '__main__':
    sys.exit(main())
