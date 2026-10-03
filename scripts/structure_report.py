#!/usr/bin/env python3
"""Structure report for src/ (docs/refactor-pass4/).

Four measurements:

  functions  Function lengths in src/kfx_*/src and src/main.cpp (tests,
             ftests and *_data.cpp tables excluded). Source-text based: a
             definition is a line that looks like a function head followed
             by a brace at column 0 (the tree's style); its length runs to
             the matching closing brace. Good enough to rank, not exact.
  symbols    From a build tree's object files (default out/linux): external
             functions no other object refers to ("file-local": used only in
             their own file, could be static) and, of those, the ones nothing
             refers to at all, not even a relocation in their own file
             ("unreferenced": dead in the game binary; may still be test
             hooks). Objects whose source file no longer exists (a build tree
             keeps them after a file is deleted) are skipped. Needs nm and
             readelf (ELF trees) or i686-w64-mingw32-nm and -objdump (mingw
             trees, *.obj; their C symbols' leading underscore is dropped so
             the names compare with Linux's); the tree must be built. On mingw
             objects only "file-local" is measured: a call within one COFF
             section needs no relocation, so uses inside the own file are
             invisible and "referenced nowhere" can't be told apart.
  globals    Mutable `extern` variables declared in each library's include/
             headers (non-const, not functions): state outside the
             per-library state structs.
  (--show)   The file-local or unreferenced symbols defined in files whose
             path contains the given text.
  (--check-local)  Refactor pass 4 S02's check: C functions and C++ free
             functions that are external but used only in their own file in
             every --build tree given (pass the functesting tree too: its
             #ifdef FUNCTESTING callers don't exist in the others), whose
             names appear nowhere upstream (--upstream, default
             origin/master: our own code, where `static` costs no merge
             conflicts) and in no unit test or ftest. Lists them and exits 1
             when there are any; they should be `static`.

Usage:
  scripts/structure_report.py                      # all four summaries
  scripts/structure_report.py --build out/linux    # another build tree
  scripts/structure_report.py --top 40             # longer function list
  scripts/structure_report.py --show thing_list    # symbols in one file
  scripts/structure_report.py --json out.json
  scripts/structure_report.py --check-local --build out/linux --build out/ft-editor
"""
import argparse
import json
import os
import re
import subprocess
import sys
from collections import Counter, defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

HEAD_RE = re.compile(r'^(?:static\s+|inline\s+|extern\s+|const\s+|unsigned\s+|struct\s+|enum\s+)*'
                     r'[A-Za-z_][\w\s\*:<>,&]*?\b([A-Za-z_~][\w:~]*)\s*\(')
NOT_HEAD = ('if', 'for', 'while', 'switch', 'return', 'else', 'do', '#', '//', '/*', '*', 'case')


def source_files():
    for base, _, files in os.walk(os.path.join(ROOT, 'src')):
        rel = os.path.relpath(base, ROOT)
        if '/tests' in rel or 'ftests' in rel:
            continue
        for f in files:
            if f.endswith(('.c', '.cpp')) and not f.endswith('_data.cpp'):
                yield os.path.join(rel, f)


def function_lengths():
    rows = []
    for path in source_files():
        with open(os.path.join(ROOT, path), errors='replace') as fh:
            lines = fh.read().split('\n')
        i = 0
        while i < len(lines):
            line = lines[i]
            m = HEAD_RE.match(line)
            if (m and not line.lstrip().startswith(NOT_HEAD) and not line.rstrip().endswith(';')
                    and line[:1] not in (' ', '\t')):
                # the body's opening brace: this line, or the first line starting with '{'
                j = i
                while j < len(lines) and j < i + 8 and '{' not in lines[j] and not lines[j].rstrip().endswith(';'):
                    j += 1
                if j < len(lines) and lines[j].startswith('{') or (j == i and line.rstrip().endswith('{')):
                    depth = 0
                    k = j
                    while k < len(lines):
                        depth += lines[k].count('{') - lines[k].count('}')
                        if depth <= 0 and '}' in lines[k]:
                            break
                        k += 1
                    rows.append((k - i + 1, path, m.group(1), i + 1))
                    i = k + 1
                    continue
            i += 1
    rows.sort(key=lambda r: (-r[0], r[1]))
    return rows


OBJ_RE = re.compile(r'src/([^/]+)/CMakeFiles/[^/]+\.dir/(.*)\.(?:o|obj)$')


def live_objects(build):
    objs = []
    for base, _, files in os.walk(os.path.join(build, 'src')):
        for f in files:
            if not f.endswith(('.o', '.obj')):
                continue
            obj = os.path.join(base, f)
            rel = os.path.relpath(obj, build)
            if '/tests/' in rel:
                continue
            m = OBJ_RE.match(rel)
            if m and os.path.isfile(os.path.join(ROOT, 'src', m.group(1), m.group(2))):
                objs.append(obj)
    for main_obj in ('CMakeFiles/keeperfx.dir/src/main.cpp.o', 'CMakeFiles/keeperfx.dir/src/main.cpp.obj'):
        if os.path.isfile(os.path.join(build, main_obj)):
            objs.append(os.path.join(build, main_obj))
    return objs


def coff_name(sym):
    """A mingw (i686) symbol as Linux names it: C symbols and C++ ones carry one leading underscore."""
    return sym[1:] if sym.startswith('_') else sym


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True, check=False).stdout


def symbols(build):
    objs = live_objects(build)
    if not objs:
        sys.exit(f'no object files under {build}/src: build the tree first')
    coff = objs[0].endswith('.obj')
    nm = 'i686-w64-mingw32-nm' if coff else 'nm'
    name = coff_name if coff else (lambda sym: sym)
    defined = {}
    undefined = set()
    for chunk in range(0, len(objs), 100):
        part = objs[chunk:chunk + 100]
        for line in run([nm, '-A', '-g', '--defined-only'] + part).splitlines():
            obj, _, rest = line.rpartition(':') if coff else line.partition(':')
            f = rest.split()
            if len(f) == 3 and f[1] == 'T':
                defined.setdefault(name(f[2]), obj)
        for line in run([nm, '-u'] + part).splitlines():
            f = line.split()
            if f:
                undefined.add(name(f[-1]))
    referenced = set()
    for obj in objs:
        if coff:
            continue  # see the docstring: not measurable on COFF
        for line in run(['readelf', '-rW', obj]).splitlines():
            f = line.split()
            if len(f) >= 5 and not f[4].startswith('.'):
                referenced.add(f[4].split('@')[0])
    local_only = {s: o for s, o in defined.items() if s not in undefined}
    unreferenced = {} if coff else {s: o for s, o in local_only.items() if s not in referenced}

    def src_of(obj):
        rel = os.path.relpath(obj, build)
        m = OBJ_RE.match(rel)
        return f'src/{m.group(1)}/{m.group(2)}' if m else 'src/main.cpp'
    return (len(objs), len(defined), {s: src_of(o) for s, o in local_only.items()},
            {s: src_of(o) for s, o in unreferenced.items()})


EXTERN_RE = re.compile(r'^extern\s+(?!const\b)(?!"C")[^(;]*;')


def globals_by_library():
    out = {}
    for lib in sorted(os.listdir(os.path.join(ROOT, 'src'))):
        inc = os.path.join(ROOT, 'src', lib, 'include')
        if not lib.startswith('kfx_') or not os.path.isdir(inc):
            continue
        names = []
        for base, _, files in os.walk(inc):
            for f in files:
                if not f.endswith(('.h', '.hpp')):
                    continue
                with open(os.path.join(base, f), errors='replace') as fh:
                    for line in fh:
                        if EXTERN_RE.match(line) and ' const ' not in line:
                            names.append((os.path.relpath(os.path.join(base, f), ROOT), line.strip()))
        out[lib] = names
    return out


def demangle(names):
    mangled = [n for n in names if n.startswith('_Z')]
    if not mangled:
        return {n: n for n in names}
    out = run(['c++filt'] + mangled).splitlines()
    table = dict(zip(mangled, out))
    return {n: table.get(n, n) for n in names}


def check_local(builds, upstream):
    local = None
    unreferenced = set()
    for b in builds:
        _, _, fl, unref = symbols(b)
        local = dict(fl) if local is None else {s: p for s, p in local.items() if s in fl}
        unreferenced |= set(unref)
    upstream_names = set(run(['bash', '-c', f"git -C '{ROOT}' grep -h -o -w -E '[A-Za-z_][A-Za-z0-9_]*' "
                              f"'{upstream}' -- 'src/*.c' 'src/*.cpp' 'src/*.h' 'src/*.hpp' | sort -u"]).split())
    if not upstream_names:
        print(f'--check-local: no source at {upstream} (fetch it first)')
        return 2
    test_text = ''
    for base, _, files in os.walk(os.path.join(ROOT, 'src')):
        if '/tests' in base or '/ftests' in base or base.endswith('ftests'):
            for f in files:
                if f.endswith(('.c', '.cpp', '.h', '.hpp', '.inc')):
                    with open(os.path.join(base, f), errors='replace') as fh:
                        test_text += fh.read() + '\n'
    names = demangle(list(local))
    found = []
    for sym, path in sorted(local.items(), key=lambda kv: (kv[1], kv[0])):
        if sym in unreferenced or 'dr_mp3' in path or sym.startswith('drmp3'):
            continue  # pass 4 S01's business, or vendored
        head = names[sym].split('(')[0].replace('[abi:cxx11]', '')
        parts = head.split('::')
        if len(parts) > 1 and not re.search(r'\bnamespace[ \t]+' + re.escape(parts[-2]) + r'\b',
                                            open(os.path.join(ROOT, path), errors='replace').read()):
            continue  # a class member: linkage doesn't apply
        if parts[-1] in upstream_names or re.search(r'\b' + re.escape(parts[-1]) + r'\b', test_text):
            continue
        found.append((path, names[sym]))
    for path, name in found:
        print(f'  {path}: {name}')
    print(f'{len(found)} external function(s) of our own used only in their own file' + (': make them static' if found else ''))
    return 1 if found else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--build', action='append')
    ap.add_argument('--check-local', action='store_true')
    ap.add_argument('--upstream', default='origin/master')
    ap.add_argument('--top', type=int, default=25)
    ap.add_argument('--show')
    ap.add_argument('--json')
    args = ap.parse_args()
    builds = args.build or [os.path.join(ROOT, 'out/linux')]
    if args.check_local:
        sys.exit(check_local(builds, args.upstream))
    args.build = builds[0]

    funcs = function_lengths()
    print(f'Functions: {len(funcs)} in src/ (tests, ftests, *_data.cpp excluded)')
    for n in (100, 200, 300, 500):
        print(f'  over {n} lines: {sum(1 for r in funcs if r[0] > n)}')
    print(f'  longest {args.top}:')
    for length, path, name, line in funcs[:args.top]:
        print(f'    {length:5d}  {path}:{line}  {name}')

    nobj, ndef, local_only, unreferenced = symbols(args.build)
    coff = any(f.endswith('.obj') for f in live_objects(args.build)[:1])
    print(f'\nSymbols ({os.path.relpath(args.build, ROOT)}, {nobj} live objects): {ndef} external functions,'
          f' {len(local_only)} used only in their own file, '
          + ('"referenced nowhere" not measured on COFF objects' if coff else f'{len(unreferenced)} referenced nowhere'))
    by_lib = Counter(p.split('/')[1] for p in local_only.values())
    print('  file-local by library: ' + ', '.join(f'{k} {v}' for k, v in by_lib.most_common()))
    by_lib = Counter(p.split('/')[1] for p in unreferenced.values())
    print('  unreferenced by library: ' + ', '.join(f'{k} {v}' for k, v in by_lib.most_common()))

    glob = globals_by_library()
    print(f'\nMutable extern globals in include/ headers: {sum(len(v) for v in glob.values())}')
    print('  ' + ', '.join(f'{k} {len(v)}' for k, v in sorted(glob.items(), key=lambda kv: -len(kv[1])) if v))

    if args.show:
        names = demangle(list(local_only))
        print(f'\nIn files matching "{args.show}":')
        for sym, path in sorted(local_only.items(), key=lambda kv: (kv[1], names[kv[0]])):
            if args.show in path:
                print(f'  {"unreferenced" if sym in unreferenced else "file-local  "}  {path}  {names[sym]}')

    if args.json:
        with open(args.json, 'w') as fh:
            json.dump({'functions': [{'lines': r[0], 'file': r[1], 'name': r[2], 'line': r[3]} for r in funcs],
                       'file_local': local_only, 'unreferenced': unreferenced,
                       'globals': {k: [n for _, n in v] for k, v in glob.items()}}, fh, indent=1)


if __name__ == '__main__':
    main()
