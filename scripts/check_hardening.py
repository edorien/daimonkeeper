#!/usr/bin/env python3
"""Checks that a finished game binary carries the exploit mitigations build/cmake/modules/Hardening.cmake asks for
(security pass S09). A stand-in for checksec / winchecksec that needs only binutils.

    python3 scripts/check_hardening.py out/linux/daimonkeeper
    python3 scripts/check_hardening.py out/windows/daimonkeeper.exe
    python3 scripts/check_hardening.py --list FILE      # print every check with its result, pass or fail

Exit status 1 if any check fails. The format (ELF or PE) is read from the file's magic number, so the same call works
for both. Needs `readelf` and `objdump` on PATH (any binutils; the mingw-w64 cross objdump is not required, the host
one reads PE).

ELF:  PIE, full RELRO (GNU_RELRO + BIND_NOW), non-executable stack, no writable+executable segment, no text
      relocations, stack protector (imports __stack_chk_fail), fortify (imports at least one *_chk function),
      and, as information only, the CET mark in .note.gnu.property.
PE:   ASLR (DYNAMIC_BASE) with a relocation table, high-entropy ASLR (HIGH_ENTROPY_VA with an image base above
      4 GiB), DEP (NX_COMPAT), stack protector (__stack_chk_fail), fortify (a *_chk import or a linked-in *_chk).
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys


def run(*cmd: str) -> str:
    return subprocess.run(cmd, check=True, capture_output=True, text=True, errors="replace").stdout


def check_elf(path: str) -> list[tuple[str, bool | None, str]]:
    hdr = run("readelf", "-hW", path)
    seg = run("readelf", "-lW", path)
    dyn = run("readelf", "-dW", path)
    syms = run("readelf", "--dyn-syms", "-W", path)
    notes = run("readelf", "-nW", path)
    out: list[tuple[str, bool, str]] = []

    out.append(("PIE", bool(re.search(r"Type:\s+DYN", hdr)) and ("PIE" in dyn or "DYNAMIC" in seg),
                "position-independent executable (ASLR for the code)"))
    relro = "GNU_RELRO" in seg
    bind_now = "BIND_NOW" in dyn or re.search(r"FLAGS_1.*\bNOW\b", dyn) is not None
    out.append(("RELRO", relro, "GNU_RELRO segment (relocation tables read-only after start-up)"))
    out.append(("BIND_NOW", bind_now, "all symbols bound at load (full RELRO; the GOT is then read-only)"))
    stack = re.search(r"GNU_STACK\s+\S+\s+\S+\s+\S+\s+\S+\s+\S+\s+(\w+)", seg)
    out.append(("NX stack", bool(stack) and "E" not in stack.group(1), "GNU_STACK is not executable"))
    wx = []
    for m in re.finditer(r"^\s*LOAD\s.*$", seg, re.M):
        flg = re.search(r"\s([RWE ]+?)\s+0x[0-9a-f]+\s*$", m.group(0))
        if flg and "W" in flg.group(1) and "E" in flg.group(1):
            wx.append(m.group(0))
    out.append(("no W+X segment", not wx, "no LOAD segment that is both writable and executable"))
    out.append(("no TEXTREL", "TEXTREL" not in dyn, "no text relocations (code is not patched at load)"))
    out.append(("stack protector", "__stack_chk_fail" in syms, "imports __stack_chk_fail (-fstack-protector-*)"))
    chk = sorted(set(re.findall(r"\b(__\w+_chk)\b", syms)))
    out.append(("fortify", bool(chk), "imports checked libc functions (-D_FORTIFY_SOURCE): "
                + (", ".join(chk[:4]) + (", ..." if len(chk) > 4 else "") if chk else "none")))
    # Informational: the linker only keeps the CET property if every input object has it, and the fetched
    # dependencies (LuaJIT's generated code, ffmpeg's assembly) are not built with it, so the whole-file mark is
    # usually absent even though the game's own code is compiled with -fcf-protection=full.
    cet = "IBT" in notes and "SHSTK" in notes
    out.append(("(info) CET mark", cet or None, "file marked for indirect-branch tracking and shadow stack"))
    return out


def check_pe(path: str) -> list[tuple[str, bool, str]]:
    head = run("objdump", "-p", path)
    sections = run("objdump", "-h", path)
    syms = run("objdump", "-t", path)
    out: list[tuple[str, bool, str]] = []
    dll = re.search(r"DllCharacteristics\s+([0-9a-fA-F]+)", head)
    flags = int(dll.group(1), 16) if dll else 0
    base = re.search(r"ImageBase\s+([0-9a-fA-F]+)", head)
    image_base = int(base.group(1), 16) if base else 0
    out.append(("ASLR (DYNAMIC_BASE)", bool(flags & 0x40), "image can be relocated at load"))
    out.append(("relocations", ".reloc" in sections, ".reloc section present (ASLR needs it)"))
    out.append(("high-entropy ASLR", bool(flags & 0x20) and image_base > 0xFFFFFFFF,
                "HIGH_ENTROPY_VA with image base 0x%x (above 4 GiB)" % image_base))
    out.append(("DEP (NX_COMPAT)", bool(flags & 0x100), "data pages are non-executable"))
    out.append(("stack protector", "__stack_chk_fail" in syms, "links __stack_chk_fail (-fstack-protector-*)"))
    chk = sorted(set(re.findall(r"\b(__\w+_chk)\b", syms)))
    out.append(("fortify", bool(chk), "checked libc functions linked in (-D_FORTIFY_SOURCE): "
                + (", ".join(chk[:4]) + (", ..." if len(chk) > 4 else "") if chk else "none")))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("binary")
    ap.add_argument("--list", action="store_true", help="print every check, not only the failures")
    args = ap.parse_args()
    with open(args.binary, "rb") as f:
        magic = f.read(4)
    if magic == b"\x7fELF":
        results = check_elf(args.binary)
    elif magic[:2] == b"MZ":
        results = check_pe(args.binary)
    else:
        print(f"{args.binary}: neither an ELF nor a PE file", file=sys.stderr)
        return 2
    failed = 0
    counted = 0
    for name, ok, why in results:
        if ok is None:  # informational, not counted
            if args.list:
                print(f"  note {name:<22} {why}: no")
            continue
        counted += 1
        if not ok:
            failed += 1
        if args.list or not ok:
            print(f"  {'ok  ' if ok else 'FAIL'} {name:<22} {why}")
    print(f"{args.binary}: {counted - failed}/{counted} hardening checks pass")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
