#!/usr/bin/env python3
"""Duplicate-code report for src/ (docs/refactor-pass3/).

Two measurements, both source-text based (no compiler):

  clones  Token-window clone detection. Every C/C++ file is tokenised
          (comments and preprocessor lines dropped), every window of
          --window tokens is hashed, and matching windows are extended
          into maximal pairs. A line counts as "cloned" when it lies in
          a pair of at least --min-lines lines. Exact tokens only: two
          functions that differ in one identifier split into two
          shorter clones, so an axis-swapped pair (x/y) shows up as
          many short pieces, not one long one.

  walks   Hand-written linked-list walks (the loop shape pass 3's S01
          replaces), counted by list kind and library.

Good for tracking the trend across a stage and for finding candidates,
not a proof: read the code before acting on any single pair.

Usage:
    python3 scripts/clone_report.py                  # summary
    python3 scripts/clone_report.py --files 40       # + top files and file pairs
    python3 scripts/clone_report.py --show creature_senses.c
    python3 scripts/clone_report.py --walks
    python3 scripts/clone_report.py --json out.json
"""
from __future__ import annotations

import argparse
import collections
import json
import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
SRC = REPO / 'src'
DEFAULT_EXCLUDE = r'/tests/|/ftests/|_data\.cpp$|\.spv\.h$'
# A window seen more than this many times is boilerplate (a table row shape,
# a log macro); pairing every occurrence costs time and adds nothing.
MAX_OCCURRENCES = 40

TOKEN = re.compile(r'''
  (?P<ws>\s+)
 |(?P<str>"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*')
 |(?P<num>0[xX][0-9a-fA-F]+[uUlL]*|\d+\.\d*(?:[eE][+-]?\d+)?[fF]?|\d+[uUlLfF]*)
 |(?P<id>[A-Za-z_]\w*)
 |(?P<op>->|\+\+|--|<<=|>>=|<<|>>|<=|>=|==|!=|&&|\|\||[-+*/%&|^!=<>]=|::|[{}()\[\];,.?:~!%^&*+\-/<>=|])
''', re.X)

# (label, pattern that advances a hand-written walk to its next element)
WALKS = [
    ('map block things', r'\bi\s*=\s*thing->next_on_mapblk\s*;'),
    ('things of class', r'\bi\s*=\s*thing->next_of_class\s*;'),
    ('player creatures', r'=\s*cctrl->players_next_creature_idx\s*;'),
    ('rooms of owner/kind', r'=\s*room->next_of_(?:owner|kind)\s*;'),
    ('room slabs', r'=\s*slb->next_in_room\s*;|=\s*get_next_slab_number_in_room\('),
]


def lib_of(rel: str) -> str:
    parts = rel.split('/')
    return parts[1] if len(parts) > 2 else 'app_entry'


def strip(text: str) -> str:
    """Drop comments and preprocessor lines, keeping line numbers."""
    text = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), text, flags=re.S)
    text = re.sub(r'//[^\n]*', '', text)
    out, cont = [], False
    for line in text.split('\n'):
        if cont or line.lstrip().startswith('#'):
            cont = line.rstrip().endswith('\\')
            out.append('')
        else:
            out.append(line)
    return '\n'.join(out)


def tokens(path: Path) -> list[tuple[str, int]]:
    toks, line = [], 1
    for m in TOKEN.finditer(strip(path.read_text(errors='replace'))):
        if m.lastgroup == 'ws':
            line += m.group(0).count('\n')
        else:
            toks.append((m.group(0), line))
    return toks


def source_files(exclude: str) -> list[Path]:
    files = []
    for p in sorted(SRC.rglob('*')):
        if p.suffix in ('.c', '.cpp', '.h', '.hpp') and p.is_file():
            rel = p.relative_to(REPO).as_posix()
            if not re.search(exclude, '/' + rel):
                files.append(p)
    return files


def find_pairs(files: list[Path], window: int) -> list[tuple[str, tuple[int, int], str, tuple[int, int]]]:
    ftoks, index = {}, collections.defaultdict(list)
    for p in files:
        rel = p.relative_to(REPO).as_posix()
        t = tokens(p)
        ftoks[rel] = t
        words = [w for w, _ in t]
        for i in range(len(words) - window + 1):
            index[hash(tuple(words[i:i + window]))].append((rel, i))
    pairs, seen = {}, set()
    for occ in index.values():
        if len(occ) < 2 or len(occ) > MAX_OCCURRENCES:
            continue
        for a in range(len(occ)):
            for b in range(a + 1, len(occ)):
                (pa, ia), (pb, ib) = occ[a], occ[b]
                if (pa, ia, pb, ib) in seen or (pa == pb and abs(ia - ib) < window):
                    continue
                ta, tb = ftoks[pa], ftoks[pb]
                while ia > 0 and ib > 0 and ta[ia - 1][0] == tb[ib - 1][0] and not (pa == pb and ia - 1 == ib):
                    ia, ib = ia - 1, ib - 1
                n = 0
                while ia + n < len(ta) and ib + n < len(tb) and ta[ia + n][0] == tb[ib + n][0]:
                    if pa == pb and ia + n == ib:
                        break
                    n += 1
                if n < window:
                    continue  # hash collision
                for k in range(n - window + 1):
                    seen.add((pa, ia + k, pb, ib + k))
                pairs[(pa, (ta[ia][1], ta[ia + n - 1][1]), pb, (tb[ib][1], tb[ib + n - 1][1]))] = n
    return list(pairs)


def report_clones(args) -> dict:
    files = source_files(args.exclude)
    pairs = [p for p in find_pairs(files, args.window) if p[1][1] - p[1][0] + 1 >= args.min_lines]
    covered = collections.defaultdict(set)
    shared = collections.defaultdict(set)
    for pa, la, pb, lb in pairs:
        ra, rb = set(range(la[0], la[1] + 1)), set(range(lb[0], lb[1] + 1))
        covered[pa] |= ra
        covered[pb] |= rb
        shared[tuple(sorted((pa, pb)))] |= {(pa, x) for x in ra} | {(pb, x) for x in rb}
    total_lines = {p.relative_to(REPO).as_posix(): p.read_text(errors='replace').count('\n') for p in files}
    by_lib = collections.Counter()
    for rel, lines in covered.items():
        by_lib[lib_of(rel)] += len(lines)
    result = {
        'window': args.window, 'min_lines': args.min_lines, 'files': len(files),
        'source_lines': sum(total_lines.values()),
        'cloned_lines': sum(len(s) for s in covered.values()),
        'by_library': dict(by_lib.most_common()),
        'by_file': {rel: [len(s), total_lines[rel]] for rel, s in sorted(covered.items(), key=lambda x: -len(x[1]))},
        'file_pairs': [[a, b, len(s) if a == b else len(s) // 2]
                       for (a, b), s in sorted(shared.items(), key=lambda x: -len(x[1]))],
        'pairs': [[pa, list(la), pb, list(lb)] for pa, la, pb, lb in pairs],
    }
    print(f"Clone report: {result['files']} files, {result['source_lines']} lines, "
          f"window {args.window} tokens, pairs of {args.min_lines}+ lines")
    print(f"  cloned lines: {result['cloned_lines']} "
          f"({100.0 * result['cloned_lines'] / max(1, result['source_lines']):.1f}%)")
    print('  by library:')
    for lib, n in by_lib.most_common():
        print(f'    {lib:<16} {n:6}')
    if args.files:
        print(f'\n  top {args.files} files (cloned / total lines):')
        for rel, (n, tot) in list(result['by_file'].items())[:args.files]:
            print(f'    {n:6} / {tot:6}  {rel}')
        print(f'\n  top {args.files} file pairs (lines shared):')
        for a, b, n in result['file_pairs'][:args.files]:
            print(f'    {n:6}  {a}' + ('  (within the file)' if a == b else f'  <->  {b}'))
    if args.show:
        rows = sorted(((la[1] - la[0] + 1, pa, la, pb, lb) for pa, la, pb, lb in pairs
                       if args.show in pa or args.show in pb), reverse=True)
        print(f'\n  pairs touching "{args.show}" ({len(rows)}):')
        for n, pa, la, pb, lb in rows[:args.limit]:
            print(f'    {n:4}  {pa}:{la[0]}-{la[1]}  ~  {pb}:{lb[0]}-{lb[1]}')
    return result


def report_walks(args) -> dict:
    counts = {label: collections.Counter() for label, _ in WALKS}
    for p in source_files(args.exclude):
        if p.suffix not in ('.c', '.cpp'):
            continue
        rel = p.relative_to(REPO).as_posix()
        text = strip(p.read_text(errors='replace'))
        for label, pat in WALKS:
            n = len(re.findall(pat, text))
            if n:
                counts[label][lib_of(rel)] += n
    print('Hand-written list walks (by the statement that steps to the next element):')
    total = 0
    for label, c in counts.items():
        total += sum(c.values())
        print(f'  {label:<20} {sum(c.values()):4}   ' + ' '.join(f'{lib}={n}' for lib, n in c.most_common()))
    print(f'  {"total":<20} {total:4}')
    return {label: dict(c) for label, c in counts.items()}


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--window', type=int, default=50, help='tokens per hashed window (default 50)')
    ap.add_argument('--min-lines', type=int, default=10, help='shortest pair counted, in lines (default 10)')
    ap.add_argument('--exclude', default=DEFAULT_EXCLUDE, help='regex over /src/... paths to skip')
    ap.add_argument('--files', type=int, default=0, help='also list the top N files and file pairs')
    ap.add_argument('--show', help='list the clone pairs touching files whose path contains this')
    ap.add_argument('--limit', type=int, default=40, help='rows for --show (default 40)')
    ap.add_argument('--walks', action='store_true', help='count hand-written list walks instead')
    ap.add_argument('--json', help='write the full result to this file')
    args = ap.parse_args()
    result = report_walks(args) if args.walks else report_clones(args)
    if args.json:
        Path(args.json).write_text(json.dumps(result, indent=1))


if __name__ == '__main__':
    main()
