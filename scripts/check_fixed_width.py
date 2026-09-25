#!/usr/bin/env python3
"""Every integer in src/ is int64_t/uint64_t and every float a double, except at explicit boundaries.

The game used to mix int, short, long, unsigned, uint32_t, ... whose widths differ between platforms (`long`) and
whose arithmetic wraps at different points; docs/refactor/defects/01-int64-double.md has the history. Now:

  * integers: int64_t / uint64_t (size_t/ptrdiff_t/bool/char/uint8_t stay for sizes, flags and bytes)
  * floating point: double
  * BOUNDARIES keep a narrow type and convert with a cast: third-party APIs (SDL, ImGui, Lua, curl, libc),
    file formats and wire formats of the original game, pixels/CPUID, and 32-bit algorithms (RNG, checksum).

This check is a ratchet: scripts/fixed_width_baseline.json records, per file, how many lines use a forbidden
type today (all of them boundaries). A file may go down, never up. New code therefore cannot reintroduce `int`,
`long`, `short`, `float`, `unsigned`, (u)int16_t/(u)int32_t; a genuinely new boundary is added with
`--update` and a note in the PR.

    python3 scripts/check_fixed_width.py            # report
    python3 scripts/check_fixed_width.py --strict   # exit 1 if any file exceeds its baseline (CI)
    python3 scripts/check_fixed_width.py --update   # rewrite the baseline (reviewed change)
"""
import json, os, re, subprocess, sys

SEG = re.compile(r'(//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\')', re.S)
BAD = re.compile(
    r'(?<![\w.])(?:'
    r'long(?!\s+(?:long|double))(?<!long long)'          # long, unsigned long, long int
    r'|short|float'
    r'|u?int(?:16|32)_t'
    r'|int(?!\w)'                                          # bare int, unsigned int, signed int
    r'|unsigned(?!\s+(?:char|long\s+long))(?=\s*[\w*&)(,;>]|\s*$)'   # bare `unsigned`, `unsigned int/short/long`
    r')(?![\w])')
RAWPARSE = re.compile(r'(?<![\w.])(?:strtol|atol)\s*\(')
BASELINE = os.path.join(os.path.dirname(__file__), 'fixed_width_baseline.json')

def counts():
    files = subprocess.check_output(['git', 'ls-files', 'src/*.c', 'src/*.cpp', 'src/*.h', 'src/*.hpp']).decode().split()
    out = {}
    for f in files:
        text = open(f, errors='replace').read()
        n = 0
        for i, part in enumerate(SEG.split(text)):
            if i % 2:
                continue
            for line in part.split('\n'):
                s = line.strip()
                if BAD.search(line) or (RAWPARSE.search(line) and not f.endswith('bflib_basics.c') and '/tests/' not in f):
                    n += 1
        if n:
            out[f] = n
    return out

def main():
    cur = counts()
    if '--update' in sys.argv:
        json.dump(cur, open(BASELINE, 'w'), indent=0, sort_keys=True)
        print(f'baseline updated: {sum(cur.values())} lines in {len(cur)} files')
        return 0
    base = json.load(open(BASELINE)) if os.path.exists(BASELINE) else {}
    bad = [(f, n, base.get(f, 0)) for f, n in sorted(cur.items()) if n > base.get(f, 0)]
    for f, n, b in bad:
        print(f'{f}: {n} line(s) with int/long/short/float/unsigned/(u)int16|32_t (baseline {b})')
    print(f'{sum(cur.values())} boundary line(s) in {len(cur)} files; {len(bad)} file(s) over baseline')
    return 1 if bad and '--strict' in sys.argv else 0

if __name__ == '__main__':
    sys.exit(main())
