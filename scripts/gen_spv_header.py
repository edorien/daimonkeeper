#!/usr/bin/env python3
"""Regenerates a precompiled SPIR-V header (src/kfx_platform/include/renderer/shaders/<name>.spv.h) from its .spv.

The headers stand in for a shader-compile build step (none is wired in; see each header's comment). This rewrites
only the array and its size line; the comment block above them is kept as it is. Words are written six to a line,
as the existing headers are, so regenerating an unchanged shader reproduces its header byte for byte.

Usage:
  scripts/gen_spv_header.py <in.spv> <out.spv.h>          write the header
  scripts/gen_spv_header.py --check <in.spv> <out.spv.h>  exit 1 if the header doesn't match the .spv
Normally run through scripts/compile_shaders.sh.
"""
import os
import re
import struct
import sys


def render(spv_path, header_path):
    data = open(spv_path, "rb").read()
    if len(data) % 4 != 0 or data[:4] != b"\x03\x02\x23\x07":
        sys.exit(f"{spv_path}: not a SPIR-V module")
    words = struct.unpack(f"<{len(data) // 4}I", data)
    name = "g_" + re.sub(r"[^A-Za-z0-9]", "_", os.path.basename(spv_path)[: -len(".spv")]) + "_spv"
    old = open(header_path).read() if os.path.exists(header_path) else ""
    m = re.search(r"^static const uint32_t ", old, re.M)
    preamble = old[: m.start()] if m else "#pragma once\n#include <stdint.h>\n\n"
    lines = [f"static const uint32_t {name}[] = {{"]
    for i in range(0, len(words), 6):
        lines.append("    " + " ".join(f"0x{w:08x}u," for w in words[i : i + 6]))
    lines.append("};")
    lines.append(f"static const uint32_t {name}_size = sizeof({name});")
    return preamble + "\n".join(lines) + "\n"


def main(argv):
    check = argv[:1] == ["--check"]
    if check:
        argv = argv[1:]
    if len(argv) != 2:
        sys.exit(__doc__)
    spv_path, header_path = argv
    text = render(spv_path, header_path)
    if check:
        same = os.path.exists(header_path) and open(header_path).read() == text
        print(f"{header_path}: {'up to date' if same else 'OUT OF DATE'}")
        return 0 if same else 1
    open(header_path, "w").write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
