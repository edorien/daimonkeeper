#!/usr/bin/env python3
"""HD terrain texture packs: keys, checks, and test packs (docs/newFeatures/texture-resolution/02-design.md §3.2, §5.6).

A pack (the game loads it as the textures/ of an asset pack, fxdata/asset_packs/<pack>/textures/, ASSET_PACK=<pack>)
uses FXGraphics' textures-* layout, plus keys:

    <pack>/pack.toml                 size = 64 | 128 | 256
    <pack>/filelist_tmapa005.txt     "textures_pack_005 8 68 128 128", then one "pack005/<sheet>.png x y w h" per block
    <pack>/pack005/<sheet>.png       sheets at the pack's size
    <pack>/keys/tmapa005.idx         one little-endian 64-bit key per filelist block: the FNV-1a hash of the
                                     32x32 block it upgrades (0: none). The game matches blocks by these keys.

Commands:
    keys <pack> --data DIR           (re)writes keys/*.idx from the 32x32 tmapXNNN.dat files in DIR -- the ones the
                                     art was made against (base data/, or a campaign's own files)
    check <pack> [--data DIR] [--ref DIR] [--palette FILE]
                                     reports what the game would skip or mis-attach: no pack.toml, bad size, filelists
                                     without keys, missing sheets, wrong crop sizes, crops off the sheet, keys that
                                     don't match DIR's blocks, names unlike the 32 filelists in --ref, blocks whose
                                     downsample doesn't look like their 32x32 block (a wrong sheet or position:
                                     brightness pattern correlation), keys with several HD versions
    sample <out> --big DIR --ref DIR --data DIR
                                     a pack from FXGraphics' textures-128: the 32 filelists' sheet names and layout,
                                     the 128 filelists' pixels block by block (a local aid; its art is FXGraphics')
    template <out> --ref DIR --data DIR [--size 128]
                                     an empty pack to paint: the filelists at --size, keys, pack.toml, and
                                     SHEETS.txt (each sheet's path and pixel size); no sheets
    upscale <out> --data DIR --ref DIR [--factor 4] [--palette FILE] [--filter nearest|lanczos] [--sharpen 1.2]
                                     a test pack made from the 32x32 data itself: the --ref filelists' sheets, each
                                     block nearest-neighbour upscaled through the palette (index 0 transparent: black).
                                     Drawn in parity mode (KFX_TEXTURE_HD_PARITY=1) it must draw exactly what the game
                                     draws without HD -- the file-loader version of KFX_TEXTURE_HD_SYNTH. --filter
                                     lanczos makes draft art instead: Lanczos plus an unsharp mask (hd_resample.py),
                                     each block filtered with what surrounds it: on each side its neighbour on the
                                     sheet where the edge runs on into it (a picture over several blocks), else its
                                     own opposite edge (a block that tiles).

Stdlib only, except upscale and check's downsample test (Pillow; numpy for --filter lanczos).
"""
import argparse
import os
import re
import struct
import sys

FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
MASK64 = (1 << 64) - 1
STRIDE = 256
BLOCKS_PER_ROW = 8
FILELIST_RE = re.compile(r'^filelist_(tmap[ab]\d{3})\.txt$')


def block_key(atlas, index):
    """The game's content key (engine_textures_hd.c texture_hd_block_key) of block `index` of a raw 256-wide atlas;
    0 for a uniform block, which never gets HD, or one past the end of the file."""
    x0 = (index % BLOCKS_PER_ROW) * 32
    y0 = (index // BLOCKS_PER_ROW) * 32
    if (y0 + 32) * STRIDE > len(atlas):
        return 0
    h = FNV_OFFSET
    first = atlas[y0 * STRIDE + x0]
    uniform = True
    for y in range(32):
        row = atlas[(y0 + y) * STRIDE + x0:(y0 + y) * STRIDE + x0 + 32]
        for v in row:
            h = ((h ^ v) * FNV_PRIME) & MASK64
            if v != first:
                uniform = False
    return 0 if uniform else h


def block_indices(atlas, index):
    x0 = (index % BLOCKS_PER_ROW) * 32
    y0 = (index // BLOCKS_PER_ROW) * 32
    return [atlas[(y0 + y) * STRIDE + x0:(y0 + y) * STRIDE + x0 + 32] for y in range(32)]


def read_filelist(path):
    """(header fields, [(sheet, x, y, w, h)]) as the game parses it: BOM, tabs or spaces, blank lines skipped."""
    with open(path, 'rb') as f:
        text = f.read().decode('utf-8-sig', errors='replace')
    header = None
    lines = []
    for raw in text.splitlines():
        parts = raw.split()
        if len(parts) != 5:
            continue
        try:
            nums = [int(p) for p in parts[1:]]
        except ValueError:
            continue
        if header is None:
            header = (parts[0], *nums)
        else:
            lines.append((parts[0], *nums))
    return header, lines


def pack_size(pack):
    path = os.path.join(pack, 'pack.toml')
    if not os.path.exists(path):
        return None
    with open(path, encoding='utf-8') as f:
        for raw in f:
            m = re.match(r'\s*size\s*=\s*(\d+)', raw)
            if m:
                return int(m.group(1))
    return None


def filelists(pack):
    return sorted((m.group(1), os.path.join(pack, name)) for name in os.listdir(pack) if (m := FILELIST_RE.match(name)))


def read_dat(data_dir, stem):
    path = os.path.join(data_dir, stem + '.dat')
    if not os.path.exists(path):
        return None
    with open(path, 'rb') as f:
        return f.read()


def write_keys(pack, stem, keys):
    os.makedirs(os.path.join(pack, 'keys'), exist_ok=True)
    with open(os.path.join(pack, 'keys', stem + '.idx'), 'wb') as f:
        f.write(b''.join(struct.pack('<Q', k) for k in keys))


def read_keys(pack, stem):
    path = os.path.join(pack, 'keys', stem + '.idx')
    if not os.path.exists(path):
        return None
    with open(path, 'rb') as f:
        data = f.read()
    return [struct.unpack_from('<Q', data, i)[0] for i in range(0, len(data) - 7, 8)]


def png_size(path):
    """(w, h) from a PNG's IHDR, without decoding it; None if it isn't one."""
    with open(path, 'rb') as f:
        head = f.read(24)
    if len(head) < 24 or head[:8] != b'\x89PNG\r\n\x1a\n' or head[12:16] != b'IHDR':
        return None
    return struct.unpack('>II', head[16:24])


def cmd_keys(args):
    written = 0
    for stem, path in filelists(args.pack):
        atlas = read_dat(args.data, stem)
        if atlas is None:
            print(f'{stem}: no {stem}.dat in {args.data}; keys not written', file=sys.stderr)
            continue
        _, lines = read_filelist(path)
        keys = [block_key(atlas, i) for i in range(len(lines))]
        write_keys(args.pack, stem, keys)
        print(f'{stem}: {sum(1 for k in keys if k)} keys for {len(lines)} blocks')
        written += 1
    return 0 if written else 1


def load_palette(path):
    with open(path, 'rb') as f:
        pal = f.read(768)
    # bflib_video.h chan6_to_8()
    return [tuple((pal[3 * i + c] & 63) * 255 // 63 for c in range(3)) for i in range(256)]


def correlation(a, b):
    """Pearson correlation of two equal-length lists, and the second's standard deviation."""
    n = len(a)
    ma, mb = sum(a) / n, sum(b) / n
    va = sum((x - ma) ** 2 for x in a)
    vb = sum((y - mb) ** 2 for y in b)
    if va < 1e-9 or vb < 1e-9:
        return 1.0, (vb / n) ** 0.5
    return sum((x - ma) * (y - mb) for x, y in zip(a, b)) / (va * vb) ** 0.5, (vb / n) ** 0.5


def write_review_png(path, pack, data, palette, review, cols=7):
    """Each reviewed block's 32x32 original (nearest, at the pack's size) beside its HD version."""
    from PIL import Image, ImageDraw
    side = review[0][5]
    cw, rh = 2 * side + 12, side + 14
    grid = Image.new('RGB', (cols * cw, ((len(review) + cols - 1) // cols) * rh), (40, 40, 40))
    draw = ImageDraw.Draw(grid)
    for k, (stem, i, sheet, x, y, w, r) in enumerate(review):
        atlas = read_dat(data, stem)
        block = Image.new('RGB', (32, 32))
        block.putdata([palette[v] for row in block_indices(atlas, i) for v in row])
        hd = Image.open(os.path.join(pack, sheet)).convert('RGB').crop((x, y, x + w, y + w)).resize((side, side))
        cx, cy = (k % cols) * cw, (k // cols) * rh
        grid.paste(block.resize((side, side), Image.NEAREST), (cx, cy + 14))
        grid.paste(hd, (cx + side + 2, cy + 14))
        draw.text((cx + 2, cy), f'{stem[-3:]}:{i} {os.path.basename(sheet)[:18]} {r:.2f}', fill=(255, 255, 0))
    grid.save(path)


def cmd_check(args):
    problems = 0

    def report(msg):
        nonlocal problems
        problems += 1
        print(msg)

    size = pack_size(args.pack)
    if size is None:
        report('pack.toml missing, or has no size: the game ignores this pack')
        return 1
    if size not in (64, 128, 256):
        report(f'pack.toml: size {size}: must be 64, 128 or 256')
        return 1
    palette = load_palette(args.palette) if args.palette else None
    image_lib = None
    if palette is not None and args.data:
        try:
            from PIL import Image
            image_lib = Image
        except ImportError:
            print('(Pillow not installed: no downsample check)')
    seen_keys = {}
    sheets_open = {}
    images = {}
    shifted = 0
    review = []
    total = covered = 0
    for stem, path in filelists(args.pack):
        header, lines = read_filelist(path)
        if header is None:
            report(f'{stem}: no header line')
            continue
        keys = read_keys(args.pack, stem)
        if keys is None:
            report(f'{stem}: no keys/{stem}.idx: the game ignores this filelist (texpack.py keys)')
            continue
        if len(keys) < len(lines):
            report(f'{stem}: keys/{stem}.idx has {len(keys)} keys for {len(lines)} blocks: the rest get no HD')
        atlas = read_dat(args.data, stem) if args.data else None
        ref_lines = None
        if args.ref:
            ref_path = os.path.join(args.ref, f'filelist_{stem}.txt')
            if os.path.exists(ref_path):
                ref_lines = read_filelist(ref_path)[1]
        n_ok = 0
        for i, (sheet, x, y, w, h) in enumerate(lines):
            key = keys[i] if i < len(keys) else 0
            if key == 0:
                continue
            total += 1
            sheet_path = os.path.join(args.pack, sheet)
            if (w, h) != (size, size):
                report(f'{stem} block {i}: {sheet} crops {w}x{h}, the pack is {size}: skipped')
                continue
            if sheet not in sheets_open:
                sheets_open[sheet] = png_size(sheet_path) if os.path.exists(sheet_path) else None
                if sheets_open[sheet] is None and os.path.exists(sheet_path):
                    report(f'{sheet}: not a PNG')
            dims = sheets_open[sheet]
            if dims is None:
                continue  # a partial pack: no sheet, no HD for its blocks (counted below)
            if x < 0 or y < 0 or x + w > dims[0] or y + h > dims[1]:
                report(f'{stem} block {i}: {sheet} is {dims[0]}x{dims[1]}, its crop {x},{y} {w}x{h} is off it')
                continue
            if ref_lines is not None and i < len(ref_lines):
                ref_sheet = ref_lines[i][0]
                if os.path.basename(ref_sheet) != os.path.basename(sheet):
                    report(f'{stem} block {i}: sheet {sheet}, the 32 filelist names it {ref_sheet}')
            if atlas is not None and block_key(atlas, i) != key:
                report(f'{stem} block {i}: its key doesn\'t match {stem}.dat in {args.data} (stale keys? texpack.py keys)')
            seen_keys.setdefault(key, []).append((stem, i))
            n_ok += 1
            if image_lib is not None and atlas is not None:
                # Is this the art of that block? Its downsample's brightness pattern against the 32x32 block's:
                # repainted art keeps the pattern (high correlation) even when its colours move, a wrong sheet or
                # position doesn't. Blocks with almost no pattern (std < 4) can't tell.
                if sheet not in images:
                    images[sheet] = image_lib.open(sheet_path).convert('RGB')
                down = images[sheet].crop((x, y, x + w, y + h)).resize((32, 32), image_lib.BOX)
                a = [0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2] for p in down.getdata()]
                b = [0.299 * palette[v][0] + 0.587 * palette[v][1] + 0.114 * palette[v][2] for row in block_indices(atlas, i) for v in row]
                r, spread = correlation(a, b)
                if spread >= 4.0 and r < args.min_correlation:
                    # Not counted as a problem: repainted noise (lava and water edges) correlates as little as a
                    # wrong crop does. A list to look at (--review-png draws it).
                    review.append((stem, i, sheet, x, y, w, r))
                elif abs(sum(a) / len(a) - sum(b) / len(b)) > args.max_mean_diff:
                    shifted += 1
        covered += n_ok
        print(f'{stem}: {n_ok} of {sum(1 for k in keys[:len(lines)] if k)} keyed blocks have a sheet')
    for key, where in seen_keys.items():
        if len(where) > 1:
            print(f'key {key:016x}: {len(where)} HD versions ({", ".join(f"{s}:{i}" for s, i in where[:4])}'
                  f'{", ..." if len(where) > 4 else ""}): a slot gets its own, else the first')
    if shifted:
        print(f'{shifted} blocks are the right art but brighter or darker on average than their 32x32 block '
              f'(repainted; not a problem)')
    if review:
        print(f'{len(review)} blocks look unlike their 32x32 block (brightness pattern correlation below '
              f'{args.min_correlation}): a wrong sheet or position, or art redrawn freely -- to look at:')
        for stem, i, sheet, x, y, w, r in review:
            print(f'  {stem} block {i}: {sheet} at {x},{y} (correlation {r:.2f})')
        if args.review_png:
            write_review_png(args.review_png, args.pack, args.data, palette, review)
            print(f'  side by side (32x32 block, its HD version): {args.review_png}')
    print(f'{covered} of {total} keyed blocks loadable; {problems} problems')
    return 1 if problems else 0


LANCZOS_PAD = 4  # 32x32 texels around a block while it's filtered: Lanczos and the unsharp mask reach about 3


def lanczos_block(atlas, i, rect_block, sheet, x, y, palette, f, sharpen):
    """Block i of atlas at f x 32 by hd_resample.upscale(), filtered with what surrounds it: on each side the block
    next to it on its sheet (rect_block: (sheet, x, y) -> block index) if the edge runs on into that one more smoothly
    than into its own opposite edge, else its own opposite edge (it tiles); a corner as its two sides say (the
    diagonal block, one side's neighbour, or this block)."""
    import numpy as np
    from PIL import Image
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import hd_resample
    pal = np.array([(*c, 255) for c in palette], np.uint8)
    pal[0, 3] = 0

    def rgba(j):
        return pal[np.frombuffer(b"".join(bytes(r) for r in block_indices(atlas, j)), np.uint8).reshape(32, 32)]

    me = rgba(i)
    rgb = me[..., :3].astype(np.int32)

    def side(dx, dy):
        j = rect_block.get((sheet, x + 32 * dx, y + 32 * dy))
        if j is None:
            return me
        other = rgba(j)
        o = other[..., :3].astype(np.int32)
        if dx:
            edge, there, wrap = (rgb[:, -1], o[:, 0], rgb[:, 0]) if dx > 0 else (rgb[:, 0], o[:, -1], rgb[:, -1])
        else:
            edge, there, wrap = (rgb[-1], o[0], rgb[0]) if dy > 0 else (rgb[0], o[-1], rgb[-1])
        return other if np.abs(edge - there).mean() < np.abs(edge - wrap).mean() else me

    grid = {(dx, dy): side(dx, dy) for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1))}
    grid[(0, 0)] = me
    for dx in (-1, 1):
        for dy in (-1, 1):
            # Both sides run on: the diagonal block; one side: that side's neighbour (tiling across the other, as this
            # block does); neither: this block.
            j = rect_block.get((sheet, x + 32 * dx, y + 32 * dy))
            hx, vy = grid[(dx, 0)] is not me, grid[(0, dy)] is not me
            if hx and vy:
                grid[(dx, dy)] = rgba(j) if j is not None else grid[(dx, 0)]
            elif hx:
                grid[(dx, dy)] = grid[(dx, 0)]
            elif vy:
                grid[(dx, dy)] = grid[(0, dy)]
            else:
                grid[(dx, dy)] = me
    tile = np.concatenate([np.concatenate([grid[(dx, dy)] for dx in (-1, 0, 1)], axis=1) for dy in (-1, 0, 1)], axis=0)
    p = LANCZOS_PAD
    padded = Image.fromarray(np.ascontiguousarray(tile[32 - p:64 + p, 32 - p:64 + p]), 'RGBA')
    big = hd_resample.upscale(padded, f, sharpen)
    return big.crop((p * f, p * f, (p + 32) * f, (p + 32) * f))


def cmd_upscale(args):
    from PIL import Image
    palette = load_palette(args.palette)
    f = args.factor
    size = 32 * f
    lanczos = args.filter == 'lanczos'
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, 'pack.toml'), 'w', encoding='utf-8') as out:
        name = f'Lanczos {size} (draft)' if lanczos else 'Upscaled 32x32 data (test pack)'
        out.write(f'name = "{name}"\nsize = {size}\nuse_original_at_32 = true\n')
    # One set of sheets for all filelists: tmapa and tmapb filelists share some (pack000/player_brick_drape.png).
    sheets = {}
    owner = {}
    for stem, path in filelists(args.ref):
        atlas = read_dat(args.data, stem)
        if atlas is None:
            continue
        header, lines = read_filelist(path)
        keys = []
        used = set()
        rect_block = {}
        for i, (sheet, x, y, w, h) in enumerate(lines):
            if block_key(atlas, i):
                rect_block.setdefault((sheet, x, y), i)
        for i, (sheet, x, y, w, h) in enumerate(lines):
            key = block_key(atlas, i)
            if key == 0:
                keys.append(0)
                continue
            # Sheet size from the furthest crop, at the 32 size; grown as needed.
            img = sheets.get(sheet)
            need = ((x + w) * f, (y + h) * f)
            if img is None or img.size[0] < need[0] or img.size[1] < need[1]:
                grown = Image.new('RGBA', (max(need[0], img.size[0] if img else 0), max(need[1], img.size[1] if img else 0)))
                if img is not None:
                    grown.paste(img, (0, 0))
                img = sheets[sheet] = grown
            rows = block_indices(atlas, i)
            rect = (sheet, x, y)
            if rect in owner and owner[rect][1] != key:
                # Two blocks drawn from one rectangle with different art: this one gets no HD (its sheet holds the
                # other's), so the pack stays exact.
                keys.append(0)
                continue
            owner[rect] = (i, key)
            if lanczos:
                img.paste(lanczos_block(atlas, i, rect_block, sheet, x, y, palette, f, args.sharpen), (x * f, y * f))
                keys.append(key)
                used.add(sheet)
                continue
            pixels = []
            for yy in range(size):
                row = rows[yy // f]
                for xx in range(size):
                    v = row[xx // f]
                    pixels.append((0, 0, 0, 0) if v == 0 else (*palette[v], 255))
            block = Image.new('RGBA', (size, size))
            block.putdata(pixels)
            img.paste(block, (x * f, y * f))
            keys.append(key)
            used.add(sheet)
        name, cols, rows_, _, _ = header
        with open(os.path.join(args.out, f'filelist_{stem}.txt'), 'w', encoding='utf-8') as out:
            out.write('﻿' + f'{name}\t{cols}\t{rows_}\t{size}\t{size}\n')
            for sheet, x, y, w, h in lines:
                out.write(f'{sheet}\t{x * f}\t{y * f}\t{w * f}\t{h * f}\n')
        write_keys(args.out, stem, keys)
        print(f'{stem}: {sum(1 for k in keys if k)} blocks, {len(used)} sheets')
    for sheet, img in sheets.items():
        dst = os.path.join(args.out, sheet)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        img.save(dst)
    return 0


def cmd_template(args):
    """An empty pack to paint: pack.toml, every filelist of --ref scaled to --size, keys against --data, and
    SHEETS.txt -- the sheets the game will look for and their size. No sheets: a blank one would load as black."""
    f = args.size // 32
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, 'pack.toml'), 'w', encoding='utf-8') as out:
        out.write(f'name = "{args.name}"\nsize = {args.size}\n'
                  '# true: the original 32x32 blocks are drawn at 32 and below (art derived from the originals);\n'
                  '# false: the art of this pack, reduced, all the way down\nuse_original_at_32 = true\n')
    extents = {}
    for stem, ref_path in filelists(args.ref):
        atlas = read_dat(args.data, stem)
        if atlas is None:
            continue
        header, lines = read_filelist(ref_path)
        name, cols, rows_, _, _ = header
        with open(os.path.join(args.out, f'filelist_{stem}.txt'), 'w', encoding='utf-8') as out:
            out.write('\ufeff' + f'{name}\t{cols}\t{rows_}\t{args.size}\t{args.size}\n')
            for sheet, x, y, w, h in lines:
                out.write(f'{sheet}\t{x * f}\t{y * f}\t{w * f}\t{h * f}\n')
        keys = [block_key(atlas, i) for i in range(len(lines))]
        write_keys(args.out, stem, keys)
        for i, (sheet, x, y, w, h) in enumerate(lines):
            if keys[i]:
                ex = extents.setdefault(sheet, [0, 0, 0])
                ex[0] = max(ex[0], (x + w) * f)
                ex[1] = max(ex[1], (y + h) * f)
                ex[2] += 1
        print(f'{stem}: {sum(1 for k in keys if k)} blocks to paint')
    with open(os.path.join(args.out, 'SHEETS.txt'), 'w', encoding='utf-8') as out:
        out.write('# The sheets this pack is made of: path (relative to the pack), size in pixels, blocks on it.\n'
                  '# RGB or RGBA PNG; alpha below 128 draws black (as texture index 0 does). A missing sheet just\n'
                  '# leaves its blocks at 32x32. Check with: scripts/texpack.py check <pack> --data <dat folder>\n')
        for sheet in sorted(extents):
            w, h, n = extents[sheet]
            out.write(f'{sheet}\t{w}x{h}\t{n}\n')
    print(f'{len(extents)} sheets listed in SHEETS.txt')
    return 0


def cmd_sample(args):
    """A pack from FXGraphics' textures-128 (or -64), laid out and named as the 32 filelists are: every block
    takes its 32 entry's sheet name and rectangle (scaled), and its pixels from the big filelist's block at the
    same index. That fixes the sheets the big set still has under older names and layouts."""
    from PIL import Image
    os.makedirs(args.out, exist_ok=True)
    size = None
    sheets = {}
    opened = {}
    palette_path = os.path.join(args.data, 'palette.dat')
    palette = load_palette(palette_path) if os.path.exists(palette_path) else None
    unlike = []
    lum = lambda c: 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]
    for stem, big_path in filelists(args.big):
        ref_path = os.path.join(args.ref, f'filelist_{stem}.txt')
        if not os.path.exists(ref_path):
            continue
        atlas = read_dat(args.data, stem)
        if atlas is None:
            continue
        big_header, big_lines = read_filelist(big_path)
        ref_header, ref_lines = read_filelist(ref_path)
        size = big_header[3]
        f = size // 32
        keys = []
        for i, (sheet, x, y, w, h) in enumerate(ref_lines):
            key = block_key(atlas, i)
            if key == 0 or i >= len(big_lines):
                keys.append(0)
                continue
            bsheet, bx, by, bw, bh = big_lines[i]
            # FXGraphics' textures-128 filelists for packs 1-7 all name pack000/ sheets, though each pack has its own
            # art in its own folder (pack002/ground_graveyard.png differs from pack000's): use the pack's own sheet
            # when there is one, in the folder the 32 filelist uses.
            own = os.path.join(os.path.dirname(sheet), os.path.basename(bsheet))
            if os.path.dirname(bsheet) != os.path.dirname(sheet) and os.path.exists(os.path.join(args.big, own)):
                bsheet = own
            src = opened.get(bsheet)
            if src is None:
                src = opened[bsheet] = Image.open(os.path.join(args.big, bsheet)).convert('RGBA')
            block = src.crop((bx, by, bx + size, by + size))
            if palette is not None and not args.keep_unlike:
                # Some of FXGraphics' 128 sheets aren't the 32 art's (pack002's winter reliefs have another pack's
                # colours, pack005's relief_woman is laid out differently): such a block stays 32x32 (check's test).
                a = [lum(p) for p in block.convert('RGB').resize((32, 32), Image.BOX).getdata()]
                b = [lum(palette[v]) for row in block_indices(atlas, i) for v in row]
                r, spread = correlation(a, b)
                if spread >= 4.0 and r < 0.5:
                    unlike.append(f'{stem} block {i} ({os.path.basename(sheet)}, correlation {r:.2f})')
                    keys.append(0)
                    continue
            img = sheets.get(sheet)
            need = ((x + w) * f, (y + h) * f)
            if img is None or img.size[0] < need[0] or img.size[1] < need[1]:
                grown = Image.new('RGBA', (max(need[0], img.size[0] if img else 0), max(need[1], img.size[1] if img else 0)))
                if img is not None:
                    grown.paste(img, (0, 0))
                img = sheets[sheet] = grown
            img.paste(block, (x * f, y * f))
            keys.append(key)
        name, cols, rows_, _, _ = ref_header
        with open(os.path.join(args.out, f'filelist_{stem}.txt'), 'w', encoding='utf-8') as out:
            out.write('\ufeff' + f'{name}\t{cols}\t{rows_}\t{size}\t{size}\n')
            for sheet, x, y, w, h in ref_lines:
                out.write(f'{sheet}\t{x * f}\t{y * f}\t{w * f}\t{h * f}\n')
        write_keys(args.out, stem, keys)
        print(f'{stem}: {sum(1 for k in keys if k)} blocks')
    for sheet, img in sheets.items():
        dst = os.path.join(args.out, sheet)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        img.save(dst)
    with open(os.path.join(args.out, 'pack.toml'), 'w', encoding='utf-8') as out:
        out.write(f'name = "FXGraphics textures-{size} (local sample, not for redistribution)"\nsize = {size}\n'
                  'use_original_at_32 = true\n')
    if unlike:
        print(f'{len(unlike)} blocks left at 32x32: their big art doesn\'t look like the 32 art (--keep-unlike keeps them):')
        for u in unlike:
            print(f'  {u}')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('keys', help='write keys/*.idx from 32x32 .dat files')
    p.add_argument('pack')
    p.add_argument('--data', required=True, help='folder with the tmapXNNN.dat files the art was made against')
    p = sub.add_parser('check', help='validate a pack')
    p.add_argument('pack')
    p.add_argument('--data', help='folder with the tmapXNNN.dat files: keys and downsample checked against them')
    p.add_argument('--ref', help='the 32 filelists (gfx/enginefx/textures-32): sheet names checked against them')
    p.add_argument('--palette', help='data/palette.dat, for the downsample check')
    p.add_argument('--review-png', help='write the blocks to look at, side by side with their 32x32 originals')
    p.add_argument('--min-correlation', type=float, default=0.5,
                   help='a block whose downsample correlates less with its 32x32 block is reported')
    p.add_argument('--max-mean-diff', type=float, default=24.0, help='average brightness shift counted as repainted')
    p = sub.add_parser('upscale', help='a test pack from the 32x32 data itself')
    p.add_argument('out')
    p.add_argument('--data', required=True)
    p.add_argument('--ref', required=True, help='the 32 filelists (gfx/enginefx/textures-32)')
    p.add_argument('--palette', required=True, help='data/palette.dat')
    p.add_argument('--factor', type=int, default=4, choices=(2, 4, 8))
    p.add_argument('--filter', choices=('nearest', 'lanczos'), default='nearest',
                   help='how blocks are enlarged: nearest (exact texels, the test pack) or lanczos (Lanczos plus an unsharp mask: draft art)')
    p.add_argument('--sharpen', type=float, default=1.2, help='with --filter lanczos: the unsharp mask amount (0: none)')
    p = sub.add_parser('template', help='an empty pack to paint: filelists, keys, the sheet list')
    p.add_argument('out')
    p.add_argument('--ref', required=True, help='the 32 filelists (gfx/enginefx/textures-32)')
    p.add_argument('--data', required=True, help='the tmapXNNN.dat files the art is for')
    p.add_argument('--size', type=int, default=128, choices=(64, 128, 256))
    p.add_argument('--name', default='My HD textures')
    p = sub.add_parser('sample', help='a pack from FXGraphics textures-128, with the 32 names and layout')
    p.add_argument('out')
    p.add_argument('--big', required=True, help='gfx/enginefx/textures-128')
    p.add_argument('--ref', required=True, help='gfx/enginefx/textures-32')
    p.add_argument('--data', required=True, help='the tmapXNNN.dat files the keys are for (and palette.dat)')
    p.add_argument('--keep-unlike', action='store_true', help='keep blocks whose big art doesn\'t look like the 32 art')
    args = parser.parse_args()
    return {'keys': cmd_keys, 'check': cmd_check, 'upscale': cmd_upscale, 'sample': cmd_sample,
            'template': cmd_template}[args.cmd](args)


if __name__ == '__main__':
    sys.exit(main())
