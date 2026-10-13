#!/usr/bin/env python3
"""Groups the crashes kfx_fuzz saved (security pass S08, fuzz/README.md).

    scripts/fuzz_triage.py [crash-dir ...]      default: every folder under out/scratch/fuzz/crashes

Each crash <name> has <name>.txt, the sanitizer report or backtrace of a rerun. Crashes are grouped by what happened
(the sanitizer's one-line summary, or the signal/timeout) and the first three frames in the game's own code.
Prints the groups, most common first, with the smallest input of each.
"""
import glob, os, re, sys, collections

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
dirs = sys.argv[1:] or sorted(glob.glob(os.path.join(root, "out/scratch/fuzz/crashes/*")))
groups = collections.defaultdict(list)
for d in dirs:
    for log in glob.glob(os.path.join(d, "*.txt")):
        text = open(log, errors="replace").read()
        kind = None
        m = re.search(r"(runtime error: [^\n]*)", text)
        m2 = re.search(r"ERROR: AddressSanitizer: (\S+)", text)
        if m2:
            kind = "ASan " + m2.group(1)
        elif m:
            kind = re.sub(r"0x[0-9a-f]+", "0x..", m.group(1))
            kind = re.sub(r"^.*?runtime error", "UBSan", kind)
        elif "FUZZ: timeout" in text:
            kind = "timeout"
        elif "Signal" in text or "signal" in text:
            kind = "signal"
        else:
            kind = "?"
        frames = re.findall(r"#\d+\s+0x[0-9a-f]+ in (\S+) (?:\S*?/)?((?:src|fuzz|deps)/\S+?):(\d+)", text)
        own = [(f, p, l) for f, p, l in frames if p.startswith(("src/", "fuzz/", "deps/centitoml"))][:3]
        if not own:  # timeout backtraces have no file names
            own = [(x, "", "") for x in re.findall(r"\(([A-Za-z_][A-Za-z0-9_]*)\+0x", text)[:4]]
        key = (os.path.basename(d), kind, tuple(own))
        input_path = log[:-4]
        groups[key].append(input_path)
for key, items in sorted(groups.items(), key=lambda kv: -len(kv[1])):
    d, kind, own = key
    smallest = min(items, key=lambda p: os.path.getsize(p))
    print("%3d  [%s] %s" % (len(items), d, kind))
    for f, p, l in own:
        print("       %s %s%s" % (f, p, (":" + l) if l else ""))
    print("       smallest: %s (%d bytes)" % (os.path.relpath(smallest, root), os.path.getsize(smallest)))
