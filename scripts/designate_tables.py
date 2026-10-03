#!/usr/bin/env python3
"""S01 step D: rewrite positional callback-table initializers as designated ones.

Pairs every positional slot of every `struct *Callbacks` / `*Predicates`
table initializer under src/ (tests excluded, ReceiveCallbacks excluded)
with the struct member declared at the same position, and rewrites the
initializer as one `.member = value,` per line. Comments inside the
initializer are kept, except a trailing `/* member */` label that only
repeats the member name.

Before rewriting anything it prints every pair whose value does not
mention the member name. A mismatch is a *possible live miswire*: review
each one by hand. `--check` prints the report and changes nothing.

Usage:
    python3 scripts/designate_tables.py --check
    python3 scripts/designate_tables.py --write
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
SKIP_STRUCTS = {'ReceiveCallbacks'}


def blank_comments(text: str) -> str:
    """Replace comments with spaces (newlines kept) so offsets stay valid."""
    def rep(m):
        return re.sub(r'[^\n]', ' ', m.group(0))
    return re.sub(r'/\*.*?\*/|//[^\n]*', rep, text, flags=re.S)


def parse_structs(src: dict[str, str]) -> dict[str, tuple[str, list[str], list[str]]]:
    structs = {}
    for f, t in src.items():
        for m in re.finditer(r'struct\s+(\w*(?:Callbacks|Predicates))\s*\{(.*?)\n\}\s*;', t, re.S):
            names, odd = [], []
            for decl in blank_comments(m.group(2)).split(';'):
                d = ' '.join(decl.split())
                if not d:
                    continue
                x = re.search(r'\(\s*\*\s*(\w+)\s*\)\s*\(', d)
                if x:
                    names.append(x.group(1))
                else:
                    odd.append(d)
            structs[m.group(1)] = (f, names, odd)
    return structs


def find_close(text: str, open_idx: int) -> int:
    """Index of the '}' matching text[open_idx] == '{' (comments already blanked)."""
    depth = 0
    for i in range(open_idx, len(text)):
        c = text[i]
        if c in '({[':
            depth += 1
        elif c in ')}]':
            depth -= 1
            if depth == 0:
                return i
    raise ValueError('unbalanced')


def tokenize(body: str):
    """List of ('item', text) / ('comment', text, own_line) / ('blank',).

    own_line: the comment is the first thing on its source line (it
    introduces what follows); otherwise it trails the previous item.
    """
    toks = []
    i, n = 0, len(body)
    depth, start = 0, None
    line_has_code = False
    while i < n:
        if body.startswith('//', i) or body.startswith('/*', i):
            j = body.find('\n', i) if body.startswith('//', i) else body.find('*/', i) + 2
            j = n if j < 0 else j
            if start is not None and body[start:i].strip():
                raise ValueError(f'comment inside a slot value: {body[start:j]!r}')
            toks.append(('comment', body[i:j].rstrip(), not line_has_code))
            i = j
            continue
        c = body[i]
        if c == '\n':
            k = i + 1
            while k < n and body[k] in ' \t':
                k += 1
            if k < n and body[k] == '\n' and start is None and toks and toks[-1][0] != 'blank':
                toks.append(('blank',))
            line_has_code = False
            i += 1
            continue
        if c in '({[':
            depth += 1
        elif c in ')}]':
            depth -= 1
        if not c.isspace():
            line_has_code = True
            if start is None:
                start = i
        if c == ',' and depth == 0:
            toks.append(('item', body[start:i].strip()))
            start = None
        i += 1
    if start is not None and body[start:].strip():
        toks.append(('item', body[start:].strip()))
    return toks


def norm(s: str) -> str:
    return s.replace('_', '').lower()


def matches(member: str, value: str) -> bool:
    v = norm(value)
    return norm(member) in v


def main() -> int:
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument('--check', action='store_true')
    g.add_argument('--write', action='store_true')
    args = ap.parse_args()

    files = [f for f in subprocess.check_output(['git', 'ls-files', 'src'], cwd=REPO, text=True).split()
             if f.endswith(('.c', '.cpp', '.h', '.hpp')) and '/tests/' not in f]
    src = {f: (REPO / f).read_text() for f in files}
    structs = parse_structs(src)

    for s, (hdr, names, odd) in sorted(structs.items()):
        if odd:
            print(f'NOTE {s} ({hdr}) has non-function-pointer members: {odd}')

    snames = '|'.join(sorted(structs))
    init_re = re.compile(r'(?:struct\s+)?\b(' + snames + r')\s+(\w+)\s*=\s*\{')
    total_tables = total_slots = 0
    mismatches = []
    edits: dict[str, list[tuple[int, int, str]]] = {}
    for f, t in sorted(src.items()):
        bl = blank_comments(t)
        for m in init_re.finditer(bl):
            sname, var = m.group(1), m.group(2)
            if sname in SKIP_STRUCTS:
                continue
            open_idx = m.end() - 1
            close_idx = find_close(bl, open_idx)
            body = t[open_idx + 1:close_idx]
            toks = tokenize(body)
            items = [x for x in toks if x[0] == 'item']
            names = structs[sname][1]
            if items and items[0][1].startswith('.'):
                print(f'skip {f}:{var} (already designated)')
                continue
            if len(items) != len(names):
                print(f'ERROR {f}:{var}: {len(items)} slots, {sname} has {len(names)} members')
                return 1
            total_tables += 1
            total_slots += len(items)
            # indentation of the first item line
            first_line_start = t.rfind('\n', 0, open_idx) + 1
            decl_indent = re.match(r'[ \t]*', t[first_line_start:]).group(0)
            ind = decl_indent + '    '
            out, idx = [], 0
            pending_blank = False
            for tok in toks:
                if tok[0] == 'blank':
                    pending_blank = True
                    continue
                if tok[0] == 'comment':
                    text, own = tok[1], tok[2]
                    if not own and out and out[-1][0] == 'item':
                        label = text.strip('/* ').strip()
                        prev_member = names[idx - 1]
                        if label == prev_member:
                            continue
                        out[-1] = ('item', out[-1][1] + ' ' + text)
                        continue
                    if pending_blank and out:
                        out.append(('blank', ''))
                    pending_blank = False
                    out.append(('comment', ind + text.strip()))
                    continue
                member, value = names[idx], tok[1]
                if not matches(member, value):
                    mismatches.append((f, var, idx, member, value))
                if pending_blank and out:
                    out.append(('blank', ''))
                pending_blank = False
                out.append(('item', f'{ind}.{member} = {value},'))
                idx += 1
            new_body = '\n' + '\n'.join(x[1] for x in out) + '\n' + decl_indent
            edits.setdefault(f, []).append((open_idx + 1, close_idx, new_body))

    print(f'{total_tables} tables, {total_slots} slots')
    print(f'{len(mismatches)} slot(s) whose value does not mention the member name:')
    for f, var, idx, member, value in mismatches:
        print(f'  {f} {var}[{idx}] .{member} = {value}')

    if args.write:
        for f, es in edits.items():
            t = src[f]
            for a, b, new in sorted(es, reverse=True):
                t = t[:a] + new + t[b:]
            (REPO / f).write_text(t)
        print(f'rewrote {sum(len(v) for v in edits.values())} initializers in {len(edits)} files')
    return 0


if __name__ == '__main__':
    sys.exit(main())
