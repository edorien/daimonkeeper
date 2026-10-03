#!/usr/bin/env python3
"""Writes THIRD_PARTY_NOTICES.txt: the licence and copyright notice of every
third-party library built into or shipped with the game.

Every text is copied verbatim from the library's own source (licence file, or
the licence block of a single-file library), so the notices stay exactly what
the licences require. The fetched dependency sources come from the build's
dependency caches (deps/.cache-lin64/, deps/.cache-mingw32/), which a normal
build fills -- run a build first if this complains a file is missing.

Update COMPONENTS when a dependency is added, removed or bumped (versions are
in build/cmake/modules/Dependencies.cmake), then:

    python3 scripts/gen_third_party_notices.py          # rewrite THIRD_PARTY_NOTICES.txt
    python3 scripts/gen_third_party_notices.py --check  # exit 1 if it's out of date

Test-only dependencies (Catch2, CUnit) aren't shipped and aren't listed.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "THIRD_PARTY_NOTICES.txt"
LIN = "deps/.cache-lin64/_deps"

# MinGW-w64 winpthreads, statically linked into the Windows build (-static winpthread).
# No source of it is fetched; this is its licence as published by the mingw-w64 project
# (COPYING of mingw-w64-libraries/winpthreads).
WINPTHREADS = """\
Copyright (c) 2011 mingw-w64 project

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

/*
 * Parts of this library are derived by:
 *
 * Posix Threads library for Microsoft Windows
 *
 * Use at own risk, there is no implied warranty to this code.
 * It uses undocumented features of Microsoft Windows that can change
 * at any time in the future.
 *
 * (C) 2010 Lockless Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of Lockless Inc. nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AN
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
"""

# (name, where it's used, licence, homepage, [text sources])
# A text source is a path, or (path, first_line_marker, last_line_marker[, keep_last]) to take
# the block from the line containing the first marker to the line containing the last one
# (keep_last=False leaves that last line out, e.g. a closing "*/").
COMPONENTS = [
    ("SDL3 3.4.12", "both", "zlib", "https://libsdl.org",
     [f"{LIN}/sdl3-src/LICENSE.txt"]),
    ("SDL3_image 3.4.4", "both", "zlib", "https://github.com/libsdl-org/SDL_image",
     [f"{LIN}/sdl3_image-src/LICENSE.txt"]),
    ("SDL3_mixer 3.2.4", "both", "zlib", "https://github.com/libsdl-org/SDL_mixer",
     [f"{LIN}/sdl3_mixer-src/LICENSE.txt"]),
    ("OpenAL Soft 1.24.3", "both", "GNU LGPL version 2 or later (PFFFT part: BSD-style)", "https://openal-soft.org",
     [f"{LIN}/openal_soft-src/COPYING", f"{LIN}/openal_soft-src/LICENSE-pffft"]),
    ("FFmpeg 7.1 (Smacker video/audio decoding only; built without GPL or non-free parts)", "both",
     "GNU LGPL version 2.1 or later", "https://ffmpeg.org",
     ["deps/.cache-lin64/ffmpeg/src/ffmpeg_build/COPYING.LGPLv2.1"]),
    ("LuaJIT (OpenResty luajit2 v2.1-20260724)", "both", "MIT", "https://github.com/openresty/luajit2",
     [f"{LIN}/luajit_src-src/COPYRIGHT"]),
    ("libcurl 8.22.0", "both", "curl licence (MIT-style)", "https://curl.se",
     [f"{LIN}/curl_src-src/COPYING"]),
    ("zlib 1.3.1 and minizip", "both", "zlib", "https://zlib.net",
     [f"{LIN}/zlib_minizip_src-src/LICENSE"]),
    ("libspng 0.7.4", "both", "BSD 2-Clause", "https://libspng.org",
     [f"{LIN}/spng-src/LICENSE"]),
    ("ENet6 6.1.3", "both", "MIT", "https://github.com/SirLynix/enet6",
     [f"{LIN}/enet6_src-src/LICENSE"]),
    ("miniupnpc 2.3.3", "both", "BSD 3-Clause", "https://miniupnp.tuxfamily.org",
     [f"{LIN}/miniupnp_src-src/LICENSE"]),
    ("libnatpmp", "both", "BSD 3-Clause", "https://miniupnp.tuxfamily.org/libnatpmp.html",
     [f"{LIN}/natpmp_src-src/LICENSE"]),
    ("Astronomy Engine", "both", "MIT", "https://github.com/cosinekitty/astronomy",
     [f"{LIN}/astronomy_src-src/LICENSE"]),
    ("CentiJSON", "both", "MIT", "https://github.com/mity/centijson",
     [f"{LIN}/centijson_src-src/LICENSE.md"]),
    ("CentiTOML", "both", "MIT", "https://github.com/SimLV/centitoml",
     [("deps/centitoml/toml.h", "Copyright (c) 2022 CK Tan, TheSim", "  SOFTWARE.")]),
    ("Dear ImGui", "both", "MIT", "https://github.com/ocornut/imgui",
     ["deps/imgui/LICENSE.txt"]),
    ("ImGuiColorTextEdit", "both", "MIT", "https://github.com/BalazsJako/ImGuiColorTextEdit",
     ["deps/ImGuiColorTextEdit/LICENSE"]),
    ("dr_mp3", "both", "Public domain (Unlicense) or MIT No Attribution", "https://github.com/mackron/dr_libs",
     [("deps/dr_mp3.h", "This software is available as a choice of the following licenses", "*/", False)]),
    ("tinyfiledialogs", "both", "zlib", "https://sourceforge.net/projects/tinyfiledialogs",
     [("deps/tinyfiledialogs/tinyfiledialogs.h", "Copyright (c) 2014", "Copyright (c) 2014"),
      ("deps/tinyfiledialogs/tinyfiledialogs.h", "This software is provided 'as-is'",
       "3. This notice may not be removed or altered from any source distribution.")]),
    ("MinGW-w64 winpthreads", "Windows", "MIT, with parts under BSD 3-Clause", "https://www.mingw-w64.org",
     [WINPTHREADS]),
]


def read_source(src) -> str:
    if isinstance(src, str) and "\n" in src:
        return src  # embedded text
    if isinstance(src, str):
        return (ROOT / src).read_text(encoding="utf-8", errors="replace")
    path, first, last, *rest = src
    keep_last = rest[0] if rest else True
    lines = (ROOT / path).read_text(encoding="utf-8", errors="replace").splitlines()
    start = next(i for i, l in enumerate(lines) if first in l)
    end = next(i for i in range(start, len(lines)) if last in lines[i])
    return "\n".join(lines[start:end + 1 if keep_last else end]) + "\n"


def render() -> str:
    out = [
        "Third-party notices",
        "===================",
        "",
        "dAImon Keeper is built with the libraries below. Each is the work of its",
        "authors and is used under its own licence, reproduced here as its licence",
        "requires. \"Windows\" marks a library used only in the Windows build; on",
        "Linux some of these may come from the system instead of being bundled.",
        "",
        "The game's own licence is in LICENSE, and its origins in NOTICE.",
        "",
        "The C/C++ runtime libraries of the compiler (libgcc, libstdc++) are used",
        "under the GCC Runtime Library Exception, which requires no notice.",
        "",
        "Contents:",
    ]
    out += [f"  - {name} ({lic})" for name, _, lic, _, _ in COMPONENTS]
    for name, where, lic, url, sources in COMPONENTS:
        out += ["", "", "=" * 78, name, "=" * 78,
                f"Licence: {lic}" + ("" if where == "both" else f"   (used in: {where})"),
                f"Homepage: {url}", ""]
        for i, src in enumerate(sources):
            if i:
                out += ["", "-" * 40, ""]
            out.append(read_source(src).rstrip("\n"))
    return "\n".join(out) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true", help="exit 1 if THIRD_PARTY_NOTICES.txt is out of date")
    args = ap.parse_args()
    try:
        text = render()
    except (OSError, StopIteration) as e:
        sys.stderr.write(f"can't read a licence source ({e}); run a build first so the dependency caches exist\n")
        return 2
    if args.check:
        current = OUT.read_text(encoding="utf-8") if OUT.exists() else ""
        if current != text:
            sys.stderr.write(f"{OUT.name} is out of date; run scripts/gen_third_party_notices.py\n")
            return 1
        return 0
    OUT.write_text(text, encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)} ({len(COMPONENTS)} components, {len(text)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
