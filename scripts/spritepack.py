#!/usr/bin/env python3
"""HD sprite packs: the original frames and their content keys (docs/refactor/sprites/, git-ignored plan).

A pack (the game loads it as the sprites/ of an asset pack, fxdata/asset_packs/<pack>/sprites/, ASSET_PACK=<pack>) is
a folder in FXGraphics' sprites-* layout:

    <pack>/pack.toml                 name = "..."; scale = 2 | 3 | 4; use_original_at_1x = true | false
    <pack>/animlist.txt              the filelists the pack has (any subset of the reference's), one a line
    <pack>/<folder>/filelist_rotN.txt  a header line, then one PNG a line: each frame's whole canvas at scale x the
                                     original's FrameWidth x FrameHeight
    <pack>/keys/animlist.idx         generated: 16 bytes a frame line, in animlist order: the content key of the
                                     original frame (0: none), and its crop on its canvas (x, y, w, h; u16 each)

A pack's frame lines are matched to the originals by their filelist's path in the reference (gfx/enginefx/sprites-32,
whose animlist order is creature.tab's) and their position in it.

The original thing sprites are data/creature.tab (16 bytes a frame: data offset, SWidth, SHeight, FrameWidth,
FrameHeight, Rotable, FramesCount, FrameOffsW, FrameOffsH, offset_x, offset_y) and data/creature.jty (each frame's
rows: n > 0 then n palette indices, n < 0 skip -n, 0 ends the row). A frame's content key is the engine's
sprite_hd_frame_key() (engine_sprites_hd.c): FNV-1a 64 over its width and height (two bytes each, low byte first),
then its pixels row by row, the palette index or 0 where transparent.

Commands:
    dump <out> --data DIR [--ref DIR]
                       every original frame as a PNG of its whole FrameWidth x FrameHeight canvas (the frame at
                       FrameOffsW, FrameOffsH; transparent around it), named as in --ref's animlist (FXGraphics'
                       gfx/enginefx/sprites-32, whose order is the table's) or frames/NNNN.png, plus frames.csv:
                       index, path, sizes, offsets, key, and the first index with the same key
    keys <pack> --data DIR --ref DIR
                       writes keys/animlist.idx from the original frames the pack's art was made against
    upscale <out> --data DIR --ref DIR [--factor 4] [--only PREFIX] [--pattern] [--spill N] [--additive-log FILE]
            [--filter nearest|lanczos] [--sharpen 1.2]
                       a test pack made from the originals themselves (nearest neighbour). Drawn in parity mode
                       (KFX_SPRITE_HD_PARITY=1) the game must draw exactly what it draws without HD. --filter lanczos
                       makes draft art instead: Lanczos plus an unsharp mask (hd_resample.py), silhouettes cut at the
                       game's alpha 128
    check <pack> --data DIR --ref DIR
                       what the game would skip or mis-attach (no pack.toml, a bad scale, filelists or PNGs missing,
                       canvases not scale x the original's, keys missing or stale), and a list to look at: art whose
                       silhouette differs from the original's (IoU < 0.9), art spilling past the original's crop
    import-zip <out> <zip>... --data DIR [--scale 4] [--keys-only] [--filter nearest|lanczos] [--sharpen 1.2]
                       KeeperFX sprite zips' frames (sprites.json: each sprite's TD and FP views, rotations, frames)
                       laid out as a pack: one filelist per view and rotation, each frame's PNG (its whole image is
                       the frame) upscaled (nearest, or --filter lanczos) as the starting art, keyed as the game
                       keys the frame it quantises the PNG to (compress_raw(): DIR/png_conv_pal.dat, L1-nearest on
                       6-bit channels, alpha below 0x40 transparent). Wherever the game loads the zip, its frames
                       get the pack's art. With --keys-only, prints "<png> <w> <h> <key>" a frame instead
    template <out> --data DIR --ref DIR [--scale 4] [--only PREFIX]
                       an empty pack: pack.toml, animlist, filelists, keys, and FRAMES.txt with each frame's canvas,
                       the original's crop on it and the anchor, at the scale: what to draw
    sample <out> --big DIR --data DIR --ref DIR [--only PREFIX] [--additive-log FILE]
                       a x4 pack from FXGraphics' sprites-128 (a local aid; mind its licence): each frame's 128 art
                       found by path or in the folders the 128 set renamed (by silhouette), and kept where its
                       silhouette is the original's (IoU >= 0.9); the rest left as the originals (key 0)
    colourise <pack> --data DIR --ref DIR --spritecolors TOML [--objects CFG] [--zips ZIP...]
                       player colours: from the art of each spritecolors row's base (red) frames and a
                       <frame>_mask.png beside each (white = the player's colour), the other variants' frames, the
                       masked pixels through a colour map fitted from the originals (zip variants placed by silhouette)
    fit <mesh.obj> --data DIR --ref DIR --anim FOLDER [--frames 0,1] [--sets td,fp] [--out fit.json]
                       the sprite cameras fitted to a model (pixels a unit, pitch, anchor nudge per view set) by the
                       silhouette IoU of the mesh ({frame} in its name for one a frame) against the original frames.
                       The rig's axes: Z up, the model facing -Y, its ground point at the origin (--axes y-up for
                       Blender's default OBJ export)
    blender <out.py> --ref DIR --anim FOLDER --renders DIR [--fit fit.json] [--scale 4] [--mesh OBJ]
                       a Blender script (blender [model.blend] -b -P out.py) rendering the animation's views (TD and
                       FP, rotations 1-5, the original's frame count) through the fitted cameras at the pack's scale
    from-renders <renders> <pack> --data DIR --ref DIR --anim FOLDER [--scale 4]
                       the script's renders into a pack's frames of the animation, keyed
    keys-check <engine-keys> --data DIR
                       compares the engine's keys (ftest sprite_density with KFX_FTEST_SD_KEYS=<file>) with this
                       script's for every original frame; exit 1 on any difference
    selftest           the key function's test vector, shared with the Catch2 test [sprite_hd_key]

Stdlib only, except dump, upscale and check (Pillow).
"""
import argparse
import csv
import os
import struct
import sys

FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
SPILL_MAX = 8  # original texels a side the engine keeps past a frame's crop (SPRITE_HD_MAX_SPILL)
MASK64 = (1 << 64) - 1
TAB_ENTRY = struct.Struct('<IBBBBBBBBhh')

# The shared test vector (src/kfx_render/tests/engine_sprites_hd_test.cpp): a 5x3 frame with a skip, a literal
# index 0, a run past the end of a row and an empty row.
SELFTEST_RLE = bytes([2, 7, 0, 0xFE, 1, 3, 0,   0xFD, 3, 4, 5, 6, 0,   0])
SELFTEST_SIZE = (5, 3)


def frame_rows(rle, width, height):
    """The frame's pixels, a list of `height` bytearrays of `width` (0: transparent); None if the data ends early."""
    rows = []
    p = 0
    for _ in range(height):
        row = bytearray(width)
        x = 0
        while True:
            if p >= len(rle):
                return None
            n = rle[p] - 256 if rle[p] > 127 else rle[p]
            p += 1
            if n == 0:
                break
            if n < 0:
                x -= n
                continue
            if p + n > len(rle):
                return None
            for k in range(n):
                if 0 <= x < width:
                    row[x] = rle[p + k]
                x += 1
            p += n
        rows.append(row)
    return rows


def frame_key(rle, width, height):
    """sprite_hd_frame_key(): 0 for an empty, too wide or truncated frame."""
    if width <= 0 or height <= 0 or width > 1024 or width > 0xFFFF or height > 0xFFFF:
        return 0
    rows = frame_rows(rle, width, height)
    if rows is None:
        return 0
    h = FNV_OFFSET
    for b in (width & 0xFF, width >> 8, height & 0xFF, height >> 8):
        h = ((h ^ b) * FNV_PRIME) & MASK64
    for row in rows:
        for b in row:
            h = ((h ^ b) * FNV_PRIME) & MASK64
    return h


def read_table(data_dir):
    """creature.tab's entries as dicts, and creature.jty's bytes."""
    with open(os.path.join(data_dir, 'creature.tab'), 'rb') as f:
        tab = f.read()
    with open(os.path.join(data_dir, 'creature.jty'), 'rb') as f:
        jty = f.read()
    entries = []
    for i in range(len(tab) // TAB_ENTRY.size):
        off, sw, sh, fw, fh, rot, fc, ow, oh, ox, oy = TAB_ENTRY.unpack_from(tab, i * TAB_ENTRY.size)
        entries.append(dict(offset=off, sw=sw, sh=sh, fw=fw, fh=fh, rotable=rot, frames=fc, offs_w=ow, offs_h=oh,
                            offset_x=ox, offset_y=oy))
    return entries, jty


def original_frames(data_dir):
    """(index, entry, rle) for every frame: every table entry but the last, which only ends the one before it
    (as the engine's load_single_frame() reads it)."""
    entries, jty = read_table(data_dir)
    for i in range(len(entries) - 1):
        e = entries[i]
        end = entries[i + 1]['offset']
        rle = jty[e['offset']:end] if end > e['offset'] else b''
        yield i, e, rle


def load_palette(path):
    with open(path, 'rb') as f:
        pal = f.read(768)
    # bflib_video.h chan6_to_8()
    return [tuple((pal[3 * i + c] & 63) * 255 // 63 for c in range(3)) for i in range(256)]


def ref_paths(ref_dir):
    """The frame PNG paths of FXGraphics' sprites-32 in animlist order (the table's order)."""
    paths = []
    with open(os.path.join(ref_dir, 'animlist.txt'), encoding='utf-8-sig') as f:
        filelists = [line.strip() for line in f if line.strip()]
    for fl in filelists:
        with open(os.path.join(ref_dir, fl), encoding='utf-8-sig') as f:
            lines = [line.split() for line in f if line.strip()]
        for words in lines[1:]:
            paths.append(os.path.join(os.path.dirname(fl), words[0]))
    return paths


def cmd_dump(args):
    from PIL import Image
    palette = load_palette(os.path.join(args.data, 'palette.dat'))
    paths = ref_paths(args.ref) if args.ref else None
    os.makedirs(args.out, exist_ok=True)
    first_of = {}
    rows_out = []
    for i, e, rle in original_frames(args.data):
        key = frame_key(rle, e['sw'], e['sh'])
        name = paths[i] if (paths is not None and i < len(paths)) else os.path.join('frames', f'{i:04d}.png')
        if e['fw'] > 0 and e['fh'] > 0:
            img = Image.new('RGBA', (e['fw'], e['fh']), (0, 0, 0, 0))
            rows = frame_rows(rle, e['sw'], e['sh']) if key else None
            if rows is not None:
                px = img.load()
                for y, row in enumerate(rows):
                    for x, idx in enumerate(row):
                        cx, cy = e['offs_w'] + x, e['offs_h'] + y
                        if idx and 0 <= cx < e['fw'] and 0 <= cy < e['fh']:
                            px[cx, cy] = palette[idx] + (255,)
            dest = os.path.join(args.out, name)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            img.save(dest)
        first = first_of.setdefault(key, i) if key else ''
        rows_out.append([i, name, e['sw'], e['sh'], e['fw'], e['fh'], e['offs_w'], e['offs_h'], e['offset_x'],
                         e['offset_y'], e['rotable'], e['frames'], f'{key:016x}', first if first != i else ''])
    with open(os.path.join(args.out, 'frames.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['index', 'path', 'width', 'height', 'canvas_w', 'canvas_h', 'crop_x', 'crop_y', 'offset_x',
                    'offset_y', 'rotable', 'frames', 'key', 'same_as'])
        w.writerows(rows_out)
    dups = sum(1 for r in rows_out if r[-1] != '')
    print(f'{len(rows_out)} frames written to {args.out} ({len(first_of)} distinct keys, {dups} repeats)')
    return 0


def cmd_keys_check(args):
    engine = {}
    with open(args.engine_keys) as f:
        for line in f:
            words = line.split()
            if len(words) == 5 and words[0] == 'orig':
                engine[int(words[1])] = (int(words[2]), int(words[3]), int(words[4], 16))
    mine = {i: (e['sw'], e['sh'], frame_key(rle, e['sw'], e['sh'])) for i, e, rle in original_frames(args.data)}
    missing = sorted(set(mine) - set(engine))
    extra = sorted(set(engine) - set(mine))
    diff = [i for i in sorted(set(mine) & set(engine)) if mine[i] != engine[i]]
    for i in diff[:20]:
        print(f'frame {i}: engine {engine[i][0]}x{engine[i][1]} {engine[i][2]:016x}, script {mine[i][0]}x{mine[i][1]} {mine[i][2]:016x}')
    print(f'{len(mine)} frames: {len(mine) - len(missing) - len(diff)} equal, {len(diff)} differ, '
          f'{len(missing)} not in the engine file, {len(extra)} only in it')
    return 1 if (diff or missing or extra) else 0


def read_pack_toml(pack):
    """pack.toml's name, scale and use_original_at_1x (a small reader: key = value lines)."""
    path = os.path.join(pack, 'pack.toml')
    if not os.path.exists(path):
        return None
    out = {'name': '', 'scale': 0, 'use_original_at_1x': True}
    with open(path, encoding='utf-8-sig') as f:
        for line in f:
            line = line.split('#', 1)[0].strip()
            if '=' not in line:
                continue
            k, v = (x.strip() for x in line.split('=', 1))
            if k == 'name':
                out['name'] = v.strip('"')
            elif k == 'scale':
                out['scale'] = int(v) if v.isdigit() else 0
            elif k == 'use_original_at_1x':
                out['use_original_at_1x'] = (v.lower() == 'true')
    return out


def read_filelist(path):
    """A filelist's header (list of words) and its PNG names."""
    with open(path, encoding='utf-8-sig') as f:
        lines = [line.split() for line in f if line.strip()]
    return (lines[0] if lines else []), [words[0] for words in lines[1:]]


def ref_filelists(ref):
    """The reference's filelists in animlist order: path -> (first table index, frame count)."""
    out = {}
    index = 0
    with open(os.path.join(ref, 'animlist.txt'), encoding='utf-8-sig') as f:
        filelists = [line.strip() for line in f if line.strip()]
    for fl in filelists:
        _, pngs = read_filelist(os.path.join(ref, fl))
        out[fl] = (index, len(pngs))
        index += len(pngs)
    return out


def pack_frames(pack):
    """The pack's frame lines in animlist order: (filelist, position in it, PNG path relative to the pack)."""
    frames = []
    with open(os.path.join(pack, 'animlist.txt'), encoding='utf-8-sig') as f:
        filelists = [line.strip() for line in f if line.strip() and not line.startswith('#')]
    for fl in filelists:
        path = os.path.join(pack, fl)
        if not os.path.exists(path):
            frames.append((fl, None, None))
            continue
        _, pngs = read_filelist(path)
        folder = os.path.dirname(fl)
        for i, png in enumerate(pngs):
            frames.append((fl, i, os.path.join(folder, png) if folder else png))
    return frames


def original_index(frame, refs):
    fl, pos, _ = frame
    if fl not in refs or pos is None or pos >= refs[fl][1]:
        return None
    return refs[fl][0] + pos


def key_records(pack, data_dir, ref):
    """The keys/animlist.idx records for a pack, and how many frame lines have no original."""
    refs = ref_filelists(ref)
    originals = {i: (e, rle) for i, e, rle in original_frames(data_dir)}
    records = []
    missing = 0
    for frame in pack_frames(pack):
        i = original_index(frame, refs)
        if i is None or i not in originals:
            missing += 1
            records.append(struct.pack('<QHHHH', 0, 0, 0, 0, 0))
            continue
        e, rle = originals[i]
        key = frame_key(rle, e['sw'], e['sh'])
        records.append(struct.pack('<QHHHH', key, e['offs_w'], e['offs_h'], e['sw'] if key else 0, e['sh'] if key else 0))
    return records, missing


def cmd_keys(args):
    records, missing = key_records(args.pack, args.data, args.ref)
    os.makedirs(os.path.join(args.pack, 'keys'), exist_ok=True)
    with open(os.path.join(args.pack, 'keys', 'animlist.idx'), 'wb') as f:
        f.write(b''.join(records))
    print(f'{len(records)} frame lines keyed ({missing} without an original in the reference)')
    return 0


# render_alpha_blend()'s ramps (bflib_render.h): index 1..64 is ramp (i-1)//8 at step (i-1)%8, each step adding
# these 6-bit amounts (the black ramp subtracting).
RAMP_DELTAS = ((4, 4, 4), (6, 4, 0), (6, 1, 1), (2, 2, 6), (2, 6, 2), (3, 0, 3), (-2, -2, -2), (6, 3, 1))


def ramp_delta(i):
    """What alpha-ramp index i adds to each channel (negative: subtracts), in 8-bit units, as the engine computes it;
    None for an index that isn't a ramp step (0, above 64)."""
    if not 1 <= i <= 64:
        return None
    d = RAMP_DELTAS[(i - 1) // 8]
    step = (i - 1) % 8
    return tuple(int(c * step * 255 / 63) for c in d) # C's division truncates toward zero, as int() does


def additive_filelists(refs, log_path):
    """The reference filelists holding any frame of an alpha-ramp log (KFX_SPRITE_ALPHA_LOG: a table index a line)."""
    if not log_path:
        return set()
    frames = set()
    with open(log_path) as f:
        for line in f:
            if line.strip().isdigit():
                frames.add(int(line))
    return {fl for fl, (first, n) in refs.items() if any(first <= i < first + n for i in frames)}


def lanczos_art(small, s, sharpen, additive=False, pad=4):
    """small (RGBA) at x s by hd_resample.upscale(), with `pad` transparent texels around it while filtering."""
    from PIL import Image
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import hd_resample
    w, h = small.size
    padded = Image.new('RGBA', (w + 2 * pad, h + 2 * pad), (0, 0, 0, 0))
    padded.paste(small, (pad, pad))
    big = hd_resample.upscale(padded, s, sharpen, additive=additive)
    return big.crop((pad * s, pad * s, (pad + w) * s, (pad + h) * s))


def cmd_upscale(args):
    from PIL import Image
    palette = load_palette(os.path.join(args.data, 'palette.dat'))
    s = args.factor
    lanczos = args.filter == 'lanczos'
    refs = ref_filelists(args.ref)
    originals = {i: (e, rle) for i, e, rle in original_frames(args.data)}
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, 'pack.toml'), 'w') as f:
        name = f'Lanczos x{s} (draft)' if lanczos else f'Upscale x{s}'
        f.write(f'name = "{name}"\nscale = {s}\nuse_original_at_1x = true\n')
    kept = [fl for fl in refs if args.only is None or fl.startswith(args.only)]
    with open(os.path.join(args.out, 'animlist.txt'), 'w') as f:
        f.write('\n'.join(kept) + '\n')
    additive = additive_filelists(refs, args.additive_log)
    written = 0
    for fl in kept:
        header, pngs = read_filelist(os.path.join(args.ref, fl))
        os.makedirs(os.path.join(args.out, os.path.dirname(fl)), exist_ok=True)
        with open(os.path.join(args.out, fl), 'w') as f:
            f.write(' '.join(header[:5] + (['additive'] if fl in additive else [])) + '\n' + '\n'.join(pngs) + '\n')
        first, _ = refs[fl]
        for pos, png in enumerate(pngs):
            e, rle = originals.get(first + pos, (None, None))
            if e is None or e['fw'] == 0 or e['fh'] == 0:
                continue
            img = Image.new('RGBA', (e['fw'] * s, e['fh'] * s), (0, 0, 0, 0))
            rows = frame_rows(rle, e['sw'], e['sh']) if e['sw'] and e['sh'] else None
            if rows is not None and fl in additive and lanczos:
                # Additive art from the ramp deltas at the original size, each image filtered as additive art.
                add = Image.new('RGBA', (e['sw'], e['sh']), (0, 0, 0, 0))
                sub = Image.new('RGBA', (e['sw'], e['sh']), (0, 0, 0, 0))
                ap, mp = add.load(), sub.load()
                for y, row in enumerate(rows):
                    for x, idx in enumerate(row):
                        d = ramp_delta(idx)
                        if d is not None:
                            ap[x, y] = tuple(max(c, 0) for c in d) + (255,)
                            mp[x, y] = tuple(max(-c, 0) for c in d) + (255,)
                at = (e['offs_w'] * s, e['offs_h'] * s)
                img.paste(lanczos_art(add, s, args.sharpen, additive=True), at)
                img.save(os.path.join(args.out, os.path.dirname(fl), png))
                sub_img = Image.new('RGBA', img.size, (0, 0, 0, 0))
                sub_img.paste(lanczos_art(sub, s, args.sharpen, additive=True), at)
                sub_img.save(os.path.join(args.out, os.path.dirname(fl), png[:-4] + '_sub.png'))
                written += 1
                continue
            if rows is not None and fl in additive:
                # Additive art (an alpha-ramp frame): each texel's ramp delta, what it adds here and what it
                # subtracts in <frame>_sub.png; not a ramp step (0, above 64): transparent.
                sub = Image.new('RGBA', img.size, (0, 0, 0, 0))
                ap, mp = img.load(), sub.load()
                off = (s - 1) // 2
                for y, row in enumerate(rows):
                    for x, idx in enumerate(row):
                        d = ramp_delta(idx)
                        if d is None:
                            continue
                        for dy in range(s):
                            for dx in range(s):
                                add = [max(c, 0) for c in d]
                                if args.pattern and (dx != off or dy != off):
                                    add = [255 - c for c in add]
                                X, Y = (e['offs_w'] + x) * s + dx, (e['offs_h'] + y) * s + dy
                                ap[X, Y] = tuple(add) + (255,)
                                mp[X, Y] = tuple(max(-c, 0) for c in d) + (255,)
                img.save(os.path.join(args.out, os.path.dirname(fl), png))
                sub.save(os.path.join(args.out, os.path.dirname(fl), png[:-4] + '_sub.png'))
                written += 1
                continue
            if rows is not None:
                small = Image.new('RGBA', (e['sw'], e['sh']), (0, 0, 0, 0))
                px = small.load()
                for y, row in enumerate(rows):
                    for x, idx in enumerate(row):
                        if idx:
                            px[x, y] = palette[idx] + (255,)
                big = lanczos_art(small, s, args.sharpen) if lanczos else small.resize((e['sw'] * s, e['sh'] * s), Image.NEAREST)
                if args.pattern:
                    # Every texel but the middle of each original texel's block inverted: parity mode must still draw
                    # the originals, precision mode mustn't (the negative control).
                    bp = big.load()
                    off = (s - 1) // 2
                    for y in range(e['sh'] * s):
                        for x in range(e['sw'] * s):
                            r, g, b, a = bp[x, y]
                            if a and (x % s != off or y % s != off):
                                bp[x, y] = (255 - r, 255 - g, 255 - b, a)
                if args.spill:
                    # A frame of opaque magenta `spill` original texels wide around the crop (inside the canvas): art
                    # past the crop, which precision mode draws beside the original and parity mode ignores.
                    m = args.spill * s
                    x0, y0 = e['offs_w'] * s, e['offs_h'] * s
                    img.paste((255, 0, 255, 255), (max(0, x0 - m), max(0, y0 - m),
                                                   min(img.size[0], x0 + e['sw'] * s + m), min(img.size[1], y0 + e['sh'] * s + m)))
                    img.paste((0, 0, 0, 0), (x0, y0, x0 + e['sw'] * s, y0 + e['sh'] * s))
                img.paste(big, (e['offs_w'] * s, e['offs_h'] * s))
            img.save(os.path.join(args.out, os.path.dirname(fl), png))
            written += 1
    records, missing = key_records(args.out, args.data, args.ref)
    os.makedirs(os.path.join(args.out, 'keys'), exist_ok=True)
    with open(os.path.join(args.out, 'keys', 'animlist.idx'), 'wb') as f:
        f.write(b''.join(records))
    print(f'{args.out}: x{s}, {len(kept)} filelists, {written} frames, {len(records)} keyed ({missing} without an original)')
    return 0


class ZipQuantiser:
    """custom_sprites.c's compress_raw() colours: each RGBA pixel's 6-bit channels to the nearest (L1, the first on a
    tie) of png_conv_pal.dat's 256 entries, as create_rgb_to_pal_table() builds it; alpha below 0x40 transparent."""

    def __init__(self, data_dir):
        with open(os.path.join(data_dir, 'png_conv_pal.dat'), 'rb') as f:
            pal = f.read(768)
        self.pal = [(pal[3 * i], pal[3 * i + 1], pal[3 * i + 2]) for i in range(256)]
        self.cache = {}

    def index(self, r, g, b):
        k = ((r >> 2) & 63, (g >> 2) & 63, (b >> 2) & 63)
        i = self.cache.get(k)
        if i is None:
            vr, vg, vb = k
            best, best_d = 0, 1 << 32
            for n, (pr, pg, pb) in enumerate(self.pal):
                d = abs(pr - vr) + abs(pg - vg) + abs(pb - vb)
                if d < best_d:
                    best, best_d = n, d
            i = self.cache[k] = best
        return i

    def key(self, rgba, w, h):
        """sprite_hd_frame_key() of the frame compress_raw() makes of these w x h RGBA bytes."""
        if w <= 0 or h <= 0 or w > 1024:
            return 0
        hv = FNV_OFFSET
        for b in (w & 0xFF, w >> 8, h & 0xFF, h >> 8):
            hv = ((hv ^ b) * FNV_PRIME) & MASK64
        for y in range(h):
            for x in range(w):
                o = (y * w + x) * 4
                i = 0 if rgba[o + 3] < 0x40 else self.index(rgba[o], rgba[o + 1], rgba[o + 2])
                hv = ((hv ^ i) * FNV_PRIME) & MASK64
        return hv


def zip_frames(zip_path):
    """(sprite name, view 'td'|'fp', rotation from 1, frame from 0, png name, rotatable) for each frame of a sprite
    zip's sprites.json, in the order the game loads them. Sprites made from Blender scene metadata are skipped."""
    import json
    import zipfile
    with zipfile.ZipFile(zip_path) as zf:
        if 'sprites.json' not in zf.namelist():
            return []
        sprites = json.loads(zf.read('sprites.json'))
    out = []
    for spr in sprites:
        if 'blender_scene' in spr:
            print(f'{zip_path}: {spr.get("name")}: made from Blender scene metadata, not imported', file=sys.stderr)
            continue
        rotatable = bool(spr.get('rotatable'))
        for view in ('td', 'fp'):
            for r, frames in enumerate((spr.get(view) or [])[:5 if rotatable else 1]):
                for n, itm in enumerate(frames):
                    out.append((spr['name'], view, r + 1, n, itm['file'], rotatable))
    return out


def cmd_import_zip(args):
    import io
    import zipfile
    from PIL import Image
    q = ZipQuantiser(args.data)
    s = args.scale
    lists = {}
    records = []
    for zip_path in args.zips:
        frames = zip_frames(zip_path)
        if not frames:
            print(f'{zip_path}: no sprites.json sprites', file=sys.stderr)
            continue
        zf = zipfile.ZipFile(zip_path)
        members = {n.lower(): n for n in zf.namelist()} # the game finds them ignoring case (fastUnzLocateFile())
        stem = os.path.splitext(os.path.basename(zip_path))[0]
        for name, view, rot, n, png, rotatable in frames:
            try:
                img = Image.open(io.BytesIO(zf.read(members.get(png.lower(), png)))).convert('RGBA')
            except (KeyError, OSError) as ex:
                print(f'{zip_path}: {png}: {ex}', file=sys.stderr)
                continue
            w, h = img.size
            key = q.key(img.tobytes(), w, h)
            if args.keys_only:
                print(f'{png} {w} {h} {key:016x}')
                continue
            fl = f'{stem}/{name}/{view}_rot{rot}.txt'
            out_png = f'{view}_r{rot}_f{n:02d}.png'
            lists.setdefault(fl, [rotatable, []])[1].append(out_png)
            # The starting art: the frame's own colours at x scale (nearest, or --filter lanczos), opaque where the game
            # draws it (alpha 0x40).
            art = img.copy()
            art.putalpha(img.getchannel('A').point(lambda a: 255 if a >= 0x40 else 0))
            art = lanczos_art(art, s, args.sharpen) if args.filter == 'lanczos' else art.resize((w * s, h * s), Image.NEAREST)
            os.makedirs(os.path.join(args.out, os.path.dirname(fl)), exist_ok=True)
            art.save(os.path.join(args.out, os.path.dirname(fl), out_png))
            records.append(struct.pack('<QHHHH', key, 0, 0, w, h))
    if args.keys_only:
        return 0
    if not records:
        return 1
    name = os.path.splitext(os.path.basename(args.zips[0]))[0] if len(args.zips) == 1 else f'{len(args.zips)} zips'
    with open(os.path.join(args.out, 'pack.toml'), 'w') as f:
        f.write(f'name = "{name} x{s}"\nscale = {s}\nuse_original_at_1x = true\n')
    with open(os.path.join(args.out, 'animlist.txt'), 'w') as f:
        f.write('\n'.join(lists) + '\n')
    for fl, (rotatable, pngs) in lists.items():
        with open(os.path.join(args.out, fl), 'w') as f:
            f.write(f'{os.path.basename(os.path.dirname(fl))} {2 if rotatable else 0} {len(pngs)} 0 0\n' + '\n'.join(pngs) + '\n')
    os.makedirs(os.path.join(args.out, 'keys'), exist_ok=True)
    with open(os.path.join(args.out, 'keys', 'animlist.idx'), 'wb') as f:
        f.write(b''.join(records))
    print(f'{args.out}: x{s}, {len(lists)} filelists, {len(records)} frames from {len(args.zips)} zip(s)')
    return 0


def write_pack_skeleton(out, ref, data_dir, scale, name, only=None):
    """pack.toml, animlist.txt and the reference's filelists (those under `only`, a path prefix) in a new pack.
    Returns the filelists."""
    refs = ref_filelists(ref)
    kept = [fl for fl in refs if only is None or fl.startswith(only)]
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, 'pack.toml'), 'w') as f:
        f.write(f'name = "{name}"\nscale = {scale}\nuse_original_at_1x = true\n')
    with open(os.path.join(out, 'animlist.txt'), 'w') as f:
        f.write('\n'.join(kept) + '\n')
    for fl in kept:
        header, pngs = read_filelist(os.path.join(ref, fl))
        os.makedirs(os.path.join(out, os.path.dirname(fl)), exist_ok=True)
        with open(os.path.join(out, fl), 'w') as f:
            f.write(' '.join(header) + '\n' + '\n'.join(pngs) + '\n')
    return kept


def write_keys(out, records):
    os.makedirs(os.path.join(out, 'keys'), exist_ok=True)
    with open(os.path.join(out, 'keys', 'animlist.idx'), 'wb') as f:
        f.write(b''.join(records))


def cmd_template(args):
    """An empty pack: everything but the art, and FRAMES.txt saying what each canvas must be."""
    s = args.scale
    kept = write_pack_skeleton(args.out, args.ref, args.data, s, f'New pack x{s}', args.only)
    records, missing = key_records(args.out, args.data, args.ref)
    write_keys(args.out, records)
    refs = ref_filelists(args.ref)
    originals = {i: e for i, e, _ in original_frames(args.data)}
    with open(os.path.join(args.out, 'FRAMES.txt'), 'w') as f:
        f.write(f'# Each frame line of the pack at x{s}: its PNG (to draw), the whole canvas, the original\'s crop on it (where\n'
                f'# the art goes; art past it is drawn too, up to 8 original texels a side, see spritepack.py check), and the\n'
                f'# anchor: the canvas pixel the creature\'s ground point is on (the animation\'s offsets, filelist header).\n'
                f'# png canvas_w canvas_h crop_x crop_y crop_w crop_h anchor_x anchor_y\n')
        for fl in kept:
            header, pngs = read_filelist(os.path.join(args.ref, fl))
            ax, ay = (-int(header[3]) * s, -int(header[4]) * s) if len(header) >= 5 else (0, 0)
            first = refs[fl][0]
            for n, png in enumerate(pngs):
                e = originals.get(first + n)
                path = os.path.join(os.path.dirname(fl), png)
                if e is None:
                    f.write(f'{path} - - - - - - - -\n')
                    continue
                f.write(f'{path} {e["fw"] * s} {e["fh"] * s} {e["offs_w"] * s} {e["offs_h"] * s} {e["sw"] * s} {e["sh"] * s} {ax} {ay}\n')
    print(f'{args.out}: x{s}, {len(kept)} filelists, {len(records)} frame lines ({missing} without an original); FRAMES.txt lists the canvases')
    return 0


def silhouette_iou(small_rgba, big_rgba, factor):
    """IoU of a frame's mask (alpha >= 128) with its `factor` x art's, box-downsampled (opaque where half is)."""
    import numpy as np
    m = np.asarray(small_rgba)[..., 3] >= 128
    b = (np.asarray(big_rgba)[..., 3] >= 128).astype(np.float32)
    h, w = m.shape
    if b.shape != (h * factor, w * factor):
        return 0.0
    d = b.reshape(h, factor, w, factor).mean(axis=(1, 3)) >= 0.5
    union = (m | d).sum()
    return float((m & d).sum()) / union if union else 1.0


def write_additive_from_colours(img, pal64, out_path, original=None):
    """Additive art from a picture drawn in alpha-ramp indices' palette colours (pal64: indices 1..64): each opaque
    texel the ramp delta of the index whose colour is nearest, what it adds in out_path, what it subtracts beside.
    With the original frame's picture, only the ramps it uses are candidates (a smoothed edge's in-between colours
    would otherwise land on other ramps' steps)."""
    import numpy as np
    from PIL import Image
    px = np.asarray(img.convert('RGBA')).astype(np.int64)
    opaque = px[..., 3] >= 128
    rgb = px[..., :3][opaque]
    candidates = np.arange(64)
    if original is not None:
        o = np.asarray(original.convert('RGBA')).astype(np.int64)
        orgb = o[..., :3][o[..., 3] >= 128]
        if len(orgb):
            ramps = set((((orgb[:, None, :] - pal64[None, :, :]) ** 2).sum(-1).argmin(1) // 8).tolist())
            candidates = np.array([i for i in range(64) if i // 8 in ramps])
    nearest = candidates[((rgb[:, None, :] - pal64[None, candidates, :]) ** 2).sum(-1).argmin(1)] + 1
    deltas = np.array([ramp_delta(i) for i in range(1, 65)], np.int64)
    d = deltas[nearest - 1]
    add = np.zeros(px.shape, np.uint8)
    sub = np.zeros(px.shape, np.uint8)
    add[opaque] = np.concatenate([np.clip(d, 0, 255), np.full((len(d), 1), 255)], 1)
    sub[opaque] = np.concatenate([np.clip(-d, 0, 255), np.full((len(d), 1), 255)], 1)
    Image.fromarray(add, 'RGBA').save(out_path)
    Image.fromarray(sub, 'RGBA').save(out_path[:-4] + '_sub.png')


def cmd_sample(args):
    """A pack from FXGraphics' sprites-128 (x4): each frame's 128 art where it is the 32 art upscaled (silhouette IoU
    >= 0.9), found by path, else in the 128 set's renamed folders (anim####, and swapped ones) of the same parent,
    matched by silhouette. The rest stay the originals (key 0)."""
    import shutil
    from PIL import Image
    s = 4
    kept = write_pack_skeleton(args.out, args.ref, args.data, s, 'FXGraphics sprites-128 (sample)', args.only)
    # Alpha-ramp frames (--additive-log): the 128 art shows each texel in its ramp index's palette colour; the pack
    # gets additive art instead, each texel the ramp delta of the nearest of those colours.
    additive = additive_filelists(ref_filelists(args.ref), args.additive_log)
    import numpy as np
    pal64 = np.array(load_palette(os.path.join(args.data, 'palette.dat'))[1:65], np.int64)
    records, _ = key_records(args.out, args.data, args.ref)
    cache = {}

    def load(path):
        if path not in cache:
            cache[path] = Image.open(path).convert('RGBA') if os.path.exists(path) else None
        return cache[path]

    def dir_score(d32, pngs, d128):
        """Mean silhouette IoU of a 32 folder's frames against the same names in a 128 folder."""
        sc = []
        for png in pngs:
            a, b = load(os.path.join(args.ref, d32, png)), load(os.path.join(args.big, d128, png))
            sc.append(silhouette_iou(a, b, s) if (a is not None and b is not None) else 0.0)
        return sum(sc) / max(1, len(sc))

    # Where each 32 folder's art is in the 128 set: its own path, or the best-matching folder of the same parent.
    folders = {}
    for fl in kept:
        folders.setdefault(os.path.dirname(fl), []).extend(read_filelist(os.path.join(args.ref, fl))[1])
    big_dirs = {}
    for root, _, files in os.walk(args.big):
        if any(f.endswith('.png') for f in files):
            big_dirs.setdefault(os.path.dirname(os.path.relpath(root, args.big)), []).append(os.path.relpath(root, args.big))
    where = {}
    remapped = 0
    for d32, pngs in folders.items():
        own = dir_score(d32, pngs, d32)
        if own >= 0.9:
            where[d32] = d32
            continue
        best = (own, d32)
        for cand in big_dirs.get(os.path.dirname(d32), []):
            if cand != d32:
                best = max(best, (dir_score(d32, pngs, cand), cand))
        where[d32] = best[1]
        remapped += (best[1] != d32)
    frames = pack_frames(args.out)
    used = left = 0
    for n, (fl, pos, path) in enumerate(frames):
        if path is None:
            continue
        d32, png = os.path.dirname(path), os.path.basename(path)
        a, b = load(os.path.join(args.ref, path)), load(os.path.join(args.big, where[d32], png))
        if a is None or b is None or silhouette_iou(a, b, s) < 0.9:
            records[n] = struct.pack('<QHHHH', 0, 0, 0, 0, 0)  # not the 32 art: the original stays
            left += 1
            continue
        if fl in additive:
            write_additive_from_colours(b, pal64, os.path.join(args.out, path), original=a)
        else:
            shutil.copyfile(os.path.join(args.big, where[d32], png), os.path.join(args.out, path))
        used += 1
    for fl in additive & set(kept):
        header, pngs = read_filelist(os.path.join(args.out, fl))
        with open(os.path.join(args.out, fl), 'w') as f:
            f.write(' '.join(header[:5] + ['additive']) + '\n' + '\n'.join(pngs) + '\n')
    write_keys(args.out, records)
    print(f'{args.out}: x4, {used} frames from {args.big} ({remapped} folders found under other names), {left} left as the originals'
          + (f'; {len(additive & set(kept))} filelists as additive art' if additive else ''))
    return 0


def read_creature_list(path):
    """The engine's creature_list (keeper_sprites.c): animation id -> the table index of its first frame."""
    import re
    src = open(path).read()
    i = src.index('creature_list[CREATURE_FRAMELIST_LENGTH] = {')
    j = src.index('};', i)
    return [int(x) for x in re.findall(r'-?\d+', src[src.index('{', i) + 1:j])]


def read_spritecolors(path):
    """spritecolors.toml's animationIds and objects rows: lists of 10 entries (ints or names)."""
    import ast
    import re
    text = open(path, encoding='utf-8-sig').read()
    rows = {}
    for key in ('animationIds', 'objects'):
        m = re.search(key + r'\s*=\s*\[(.*?)\n\]', text, re.S)
        if not m:
            continue
        body = re.sub(r'#[^\n]*', '', m.group(1))
        rows[key] = ast.literal_eval('[' + body + ']')
    return rows


def read_object_animations(path):
    """objects.cfg: object model -> its AnimationID (an int or a custom sprite name)."""
    out, model = {}, None
    for line in open(path, encoding='utf-8-sig', errors='replace'):
        line = line.split(';', 1)[0].strip()
        if line.startswith('[object') and line.endswith(']'):
            model = int(line[7:-1]) if line[7:-1].isdigit() else None
        elif model is not None and '=' in line:
            k, v = (x.strip() for x in line.split('=', 1))
            if k.lower() == 'animationid':
                out[model] = int(v) if v.lstrip('-').isdigit() else v
    return out


def cmd_colourise(args):
    """Player colours from one art: the pack's frames of each row's base variant (the first colour after the
    reference, red) and a <frame>_mask.png beside each (white where the player's colour is) make the frames of
    the row's other variants: the masked pixels go through an affine colour map fitted, over the whole animation,
    from the base original's colours to that variant original's under the mask; the rest is the base art. A
    variant that is a zip sprite (named in the row) is laid out as import-zip does and placed by its silhouette."""
    import io
    import zipfile
    import numpy as np
    from PIL import Image
    meta = read_pack_toml(args.pack)
    if meta is None:
        print(f'{args.pack}: no pack.toml')
        return 1
    s = meta['scale']
    here = os.path.dirname(os.path.abspath(__file__))
    clist = read_creature_list(args.keeper_sprites or os.path.join(here, '..', 'src', 'kfx_render', 'src', 'keeper_sprites.c'))
    rows = read_spritecolors(args.spritecolors)
    objanim = read_object_animations(args.objects) if args.objects else {}
    palette = load_palette(os.path.join(args.data, 'palette.dat'))
    pal = np.array(palette, np.uint8)
    entries, jty = read_table(args.data)
    refs = ref_filelists(args.ref)
    q = ZipQuantiser(args.data)
    zips = {}
    for z in args.zips or []:
        zf = zipfile.ZipFile(z)
        members = {n.lower(): n for n in zf.namelist()}
        if 'sprites.json' not in members:
            continue
        import json
        for spr in json.loads(zf.read(members['sprites.json'])):
            zips.setdefault(spr['name'], (zf, members, spr))

    def original_rgba(i):
        e = entries[i]
        img = np.zeros((e['fh'], e['fw'], 4), np.uint8)
        end = entries[i + 1]['offset']
        rows_ = frame_rows(jty[e['offset']:end], e['sw'], e['sh']) if e['sw'] and e['sh'] else None
        for y, row in enumerate(rows_ or []):
            idx = np.frombuffer(bytes(row), np.uint8)
            px = img[e['offs_h'] + y, e['offs_w']:e['offs_w'] + e['sw']]
            px[:, :3] = pal[idx]
            px[:, 3] = np.where(idx != 0, 255, 0)
        return img

    def views_of(entry):
        """{'td'|'fp': [frame dict]} of a row entry: an animation id (its folder and its _td/_fp sibling) or a zip
        sprite's name."""
        out = {}
        if isinstance(entry, str):
            if entry not in zips:
                return None
            zf, members, spr = zips[entry]
            for view in ('td', 'fp'):
                for r, frames in enumerate((spr.get(view) or [])[:5 if spr.get('rotatable') else 1]):
                    for n, itm in enumerate(frames):
                        im = np.asarray(Image.open(io.BytesIO(zf.read(members.get(itm['file'].lower(), itm['file'])))).convert('RGBA')).copy()
                        im[..., 3] = np.where(im[..., 3] >= 0x40, 255, 0)
                        fl = f'zips/{entry}/{view}_rot{r + 1}.txt'
                        out.setdefault(view, []).append(dict(rgba=im, fl=fl, png=f'zips/{entry}/{view}_r{r + 1}_f{n:02d}.png',
                            key=q.key(im.tobytes(), im.shape[1], im.shape[0]), rot=r))
            return out
        if not isinstance(entry, int) or entry <= 0 or entry >= len(clist):
            return None
        t = clist[entry]
        e = entries[t]
        count = e['frames'] * (5 if e['rotable'] == 2 else 1)
        folders = sorted({os.path.dirname(fl) for fl, (first, n) in refs.items() if t <= first < t + count})
        for d in list(folders):
            for a_, b_ in (('_fp', '_td'), ('_td', '_fp')):
                if d.endswith(a_) and any(os.path.dirname(fl) == d[:-3] + b_ for fl in refs):
                    folders.append(d[:-3] + b_)
        for d in folders:
            view = 'fp' if d.endswith('_fp') else 'td'
            for fl, (first, n) in refs.items():
                if os.path.dirname(fl) != d:
                    continue
                _, pngs = read_filelist(os.path.join(args.ref, fl))
                for k, png in enumerate(pngs):
                    out.setdefault(view, []).append(dict(rgba=original_rgba(first + k), fl=fl, png=os.path.join(d, png), index=first + k))
        return out

    def place(a, b):
        """The offset (dx, dy) of canvas b against canvas a that overlaps their opaque pixels best."""
        ma, mb = a[..., 3] > 0, b[..., 3] > 0
        if ma.shape == mb.shape:
            return 0, 0
        ya, xa = np.nonzero(ma)
        yb, xb = np.nonzero(mb)
        if len(ya) == 0 or len(yb) == 0:
            return 0, 0
        # start from the boxes' bottom centres (the anchor is at the feet), then search around
        best = None
        cx = int(round((xa.min() + xa.max()) / 2 - (xb.min() + xb.max()) / 2))
        cy = int(ya.max() - yb.max())
        for dy in range(cy - 8, cy + 9):
            for dx in range(cx - 8, cx + 9):
                sh = np.zeros_like(ma)
                y0, x0 = max(0, dy), max(0, dx)
                y1, x1 = min(ma.shape[0], dy + mb.shape[0]), min(ma.shape[1], dx + mb.shape[1])
                if y1 <= y0 or x1 <= x0:
                    continue
                sh[y0:y1, x0:x1] = mb[y0 - dy:y1 - dy, x0 - dx:x1 - dx]
                score = (ma & sh).sum() / max(1, (ma | sh).sum())
                if best is None or score > best[0]:
                    best = (score, dx, dy)
        return best[1], best[2]

    pack_lists = [l.strip() for l in open(os.path.join(args.pack, 'animlist.txt'), encoding='utf-8-sig') if l.strip()]
    new_lists = {}
    zip_keys = {}
    made = 0
    for key in ('animationIds', 'objects'):
        for row in rows.get(key, []):
            ids = [objanim.get(x, 0) if key == 'objects' else x for x in row]
            base_id = ids[1]
            base = views_of(base_id)
            if not base:
                print(f'{key} row {row[0]}: base {base_id} not found', file=sys.stderr)
                continue
            missing = [f['png'] for v in base.values() for f in v
                       if not (os.path.exists(os.path.join(args.pack, f['png'])) and
                               os.path.exists(os.path.join(args.pack, f['png'][:-4] + '_mask.png')))]
            if missing:
                print(f'{key} row {row[0]}: no art or mask for {len(missing)} base frames (e.g. {missing[0]}): skipped')
                continue
            arts = {f['png']: (np.asarray(Image.open(os.path.join(args.pack, f['png'])).convert('RGBA')).copy(),
                               np.asarray(Image.open(os.path.join(args.pack, f['png'][:-4] + '_mask.png')).convert('L')) >= 128)
                    for v in base.values() for f in v}
            for vid in ids[2:]:
                if vid in (0, base_id):
                    continue
                var = views_of(vid)
                if not var:
                    print(f'{key} row {row[0]}: variant {vid} not found', file=sys.stderr)
                    continue
                for view, frames in var.items():
                    # A base in one folder without _td/_fp (effects, flames) serves both of a zip variant's views.
                    bframes = base.get(view) or (next(iter(base.values())) if len(base) == 1 else None)
                    if not bframes or len(bframes) != len(frames):
                        print(f'{key} row {row[0]}: variant {vid} {view}: {len(frames)} frames, base {len(bframes or [])}: skipped')
                        continue
                    # The colour map: base original -> variant original, under the mask, the whole animation.
                    X, Y, offs = [], [], []
                    for bf, vf in zip(bframes, frames):
                        art, mask = arts[bf['png']]
                        h, w = bf['rgba'].shape[:2]
                        m1 = mask[:h * s, :w * s].reshape(h, s, w, s).mean(axis=(1, 3)) >= 0.5
                        dx, dy = place(bf['rgba'], vf['rgba'])
                        offs.append((dx, dy))
                        for y, x in zip(*np.nonzero(m1 & (bf['rgba'][..., 3] > 0))):
                            vy, vx = y - dy, x - dx
                            if 0 <= vy < vf['rgba'].shape[0] and 0 <= vx < vf['rgba'].shape[1] and vf['rgba'][vy, vx, 3] > 0:
                                X.append(bf['rgba'][y, x, :3])
                                Y.append(vf['rgba'][vy, vx, :3])
                    if len(X) < 8:
                        print(f'{key} row {row[0]}: variant {vid} {view}: too few masked pixels to measure: skipped')
                        continue
                    A = np.hstack([np.asarray(X, np.float64), np.ones((len(X), 1))])
                    M, *_ = np.linalg.lstsq(A, np.asarray(Y, np.float64), rcond=None)
                    for (bf, vf), (dx, dy) in zip(zip(bframes, frames), offs):
                        art, mask = arts[bf['png']]
                        rgb = art[..., :3].reshape(-1, 3).astype(np.float64)
                        mapped = np.clip(np.hstack([rgb, np.ones((len(rgb), 1))]) @ M, 0, 255).reshape(art.shape[0], art.shape[1], 3)
                        outp = art.copy()
                        outp[..., :3] = np.where(mask[..., None], mapped.round().astype(np.uint8), art[..., :3])
                        vh, vw = vf['rgba'].shape[:2]
                        canvas = np.zeros((vh * s, vw * s, 4), np.uint8)
                        # base canvas pixel (x, y) is variant pixel (x - dx, y - dy)
                        sy0, sx0 = max(0, dy * s), max(0, dx * s)
                        sy1, sx1 = min(outp.shape[0], (dy + vh) * s), min(outp.shape[1], (dx + vw) * s)
                        if sy1 > sy0 and sx1 > sx0:
                            canvas[sy0 - dy * s:sy1 - dy * s, sx0 - dx * s:sx1 - dx * s] = outp[sy0:sy1, sx0:sx1]
                        os.makedirs(os.path.join(args.pack, os.path.dirname(vf['png'])), exist_ok=True)
                        Image.fromarray(canvas, 'RGBA').save(os.path.join(args.pack, vf['png']))
                        lst = new_lists.setdefault(vf['fl'], [])
                        lst.append(os.path.basename(vf['png']))
                        if 'key' in vf:
                            zip_keys[vf['png']] = struct.pack('<QHHHH', vf['key'], 0, 0, vw, vh)
                        made += 1
    # The new filelists: the reference's own header and lines for originals; import-zip's layout for zip sprites.
    for fl, pngs in new_lists.items():
        if fl in refs:
            header, ref_pngs = read_filelist(os.path.join(args.ref, fl))
            pngs = ref_pngs
        else:
            header = [os.path.basename(os.path.dirname(fl)), '0', str(len(pngs)), '0', '0']
        with open(os.path.join(args.pack, fl), 'w') as f:
            f.write(' '.join(header) + '\n' + '\n'.join(pngs) + '\n')
        if fl not in pack_lists:
            pack_lists.append(fl)
    with open(os.path.join(args.pack, 'animlist.txt'), 'w') as f:
        f.write('\n'.join(pack_lists) + '\n')
    records, _ = key_records(args.pack, args.data, args.ref)
    for n, (fl, pos, path) in enumerate(pack_frames(args.pack)):
        if path in zip_keys:
            records[n] = zip_keys[path]
    write_keys(args.pack, records)
    print(f'{args.pack}: {made} frames coloured, {len(new_lists)} filelists written')
    return 0


# The sprite cameras (02-design.md section 12), fitted on the archer (out/scratch/voxinv/): orthographic, the TD set
# pitched about 32 degrees, FP about 3; five rendered azimuths 45 degrees apart (rotation 1 faces the camera, 5 faces
# away; 6-8 are the engine's mirrors of 4-2). In the rig's world -- Blender's: Z up, the model facing -Y (Blender's
# front view), its ground point at the origin -- rotation r (1..5) at pitch e and s pixels a unit (at the original's
# size) puts point p at canvas pixel (ax + s * R.p, ay - s * U.p), (ax, ay) the animation's anchor (minus its filelist
# header's offsets) and
#     R = (cos a, sin a, 0), U = (-sin a sin e, cos a sin e, cos e), a = -45 (r - 1) degrees.
# The camera looks along -D, D = R x U = (sin a cos e, -cos a cos e, sin e), from above the model.
RIG_TD_PITCH, RIG_FP_PITCH, RIG_FP_SCALE = 32.0, 3.0, 1.35


def rig_axes(r, e):
    import math
    a, e = math.radians(-45.0 * (r - 1)), math.radians(e)
    R = (math.cos(a), math.sin(a), 0.0)
    U = (-math.sin(a) * math.sin(e), math.cos(a) * math.sin(e), math.cos(e))
    D = (math.sin(a) * math.cos(e), -math.cos(a) * math.cos(e), math.sin(e))
    return R, U, D


def rig_views(ref, anim, sets):
    """The views of an animation (`anim`: a reference folder without _td/_fp, e.g. creatures/creature_archer/walk):
    [(set, rotation, frame count, anchor x, anchor y, canvas w, canvas h, folder)] at the original's size."""
    from PIL import Image
    out = []
    for st in sets:
        folder = f'{anim}_{st}'
        for r in range(1, 6):
            fl = os.path.join(ref, folder, f'filelist_rot{r}.txt')
            if not os.path.exists(fl):
                break
            header, pngs = read_filelist(fl)
            w, h = Image.open(os.path.join(ref, folder, pngs[0])).size
            out.append((st, r, len(pngs), -int(header[3]), -int(header[4]), w, h, folder))
    return out


def load_obj(path):
    """An OBJ's vertices and triangles (polygons fanned), in the file's own axes."""
    import numpy as np
    vs, tris = [], []
    with open(path) as f:
        for line in f:
            w = line.split()
            if not w:
                continue
            if w[0] == 'v':
                vs.append([float(x) for x in w[1:4]])
            elif w[0] == 'f':
                idx = [int(x.split('/')[0]) for x in w[1:]]
                idx = [i - 1 if i > 0 else len(vs) + i for i in idx]
                for k in range(1, len(idx) - 1):
                    tris.append((idx[0], idx[k], idx[k + 1]))
    return np.asarray(vs, np.float64), np.asarray(tris, np.int64)


def surface_points(V, T, density):
    """Points over a mesh's surface, about `density` a square unit (at least one a triangle)."""
    import numpy as np
    rng = np.random.default_rng(1)
    a, b, c = V[T[:, 0]], V[T[:, 1]], V[T[:, 2]]
    area = 0.5 * np.linalg.norm(np.cross(b - a, c - a), axis=1)
    n = np.maximum(1, np.ceil(area * density)).astype(np.int64)
    tri = np.repeat(np.arange(len(T)), n)
    u, v = rng.random(len(tri)), rng.random(len(tri))
    flip = u + v > 1
    u[flip], v[flip] = 1 - u[flip], 1 - v[flip]
    return a[tri] + u[:, None] * (b[tri] - a[tri]) + v[:, None] * (c[tri] - a[tri])


def rig_mask(P, r, e, s, ax, ay, w, h, sup=4):
    """The silhouette of points P through rotation r's camera on a w x h canvas: covered where at least half of
    the sup x sup sub-pixels are."""
    import numpy as np
    R, U, _ = rig_axes(r, e)
    u = (ax + s * (P @ np.asarray(R))) * sup
    v = (ay - s * (P @ np.asarray(U))) * sup
    ui, vi = np.floor(u).astype(np.int64), np.floor(v).astype(np.int64)
    ok = (ui >= 0) & (ui < w * sup) & (vi >= 0) & (vi < h * sup)
    m = np.zeros((h * sup, w * sup), bool)
    m[vi[ok], ui[ok]] = True
    return m.reshape(h, sup, w, sup).mean(axis=(1, 3)) >= 0.5


def cmd_fit(args):
    """Fits the rig to a model: pixels a unit, pitch and an anchor nudge per view set, by the silhouette IoU of the
    mesh (OBJ in the rig's axes; a {frame} in the path for a mesh a frame) against the original frames."""
    import json
    import numpy as np
    from PIL import Image
    sets = args.sets.split(',')
    views = rig_views(args.ref, args.anim, sets)
    if not views:
        print(f'{args.anim}: no _td/_fp folders in {args.ref}')
        return 1
    frames = [int(x) for x in args.frames.split(',')]
    meshes = {}
    for fr in frames:
        V, T = load_obj(args.mesh.format(frame=fr))
        if args.axes == 'y-up':
            V = V[:, [0, 2, 1]] * np.array([1.0, -1.0, 1.0]) # OBJ Y up, -Z forward -> the rig's Z up, -Y forward
        meshes[fr] = (V, T)
    result = {}
    s_td = None
    for st in sets:
        vs = [v for v in views if v[0] == st]
        targets = []
        for (_, r, n, ax, ay, w, h, folder) in vs:
            for fr in frames:
                png = os.path.join(args.ref, folder, f'r{r}frame{fr + 1:02d}.png')
                targets.append((r, ax, ay, w, h, fr, np.asarray(Image.open(png).convert('RGBA'))[..., 3] >= 128))
        # Points dense enough for the largest scale tried (a point a sub-pixel or closer).
        height = max(np.ptp(V[:, 2]) for V, _ in meshes.values())
        s0 = None
        e0 = RIG_TD_PITCH if st == 'td' else RIG_FP_PITCH
        if st == 'fp' and s_td is not None:
            s0 = s_td * RIG_FP_SCALE
        if s0 is None:
            # pixels a unit from the silhouettes' heights (the model's height at pitch e0)
            hs = [np.ptp(np.nonzero(m)[0]) + 1 for (*_, m) in targets if m.any()]
            s0 = float(np.median(hs)) / max(1e-6, height * np.cos(np.radians(e0)))
        pts = {fr: surface_points(V, T, (s0 * 1.5 * 4) ** 2 * 2) for fr, (V, T) in meshes.items()}

        def score(p):
            s_, e_, dx, dy = p
            ious = []
            for (r, ax, ay, w, h, fr, m) in targets:
                k = rig_mask(pts[fr], r, e_, s_, ax + dx, ay + dy, w, h)
                ious.append(float((m & k).sum()) / max(1, (m | k).sum()))
            return float(np.mean(ious)), ious

        # Pattern search from (s0, e0, 0, 0): each parameter in turn, steps halving.
        p = [s0, e0, 0.0, 0.0]
        steps = [s0 * 0.08, 4.0, 1.0, 1.0]
        best, _ = score(p)
        for _ in range(6):
            improved = True
            while improved:
                improved = False
                for i in range(4):
                    for sgn in (1, -1):
                        q = list(p)
                        q[i] += sgn * steps[i]
                        if i == 0 and q[0] <= 0:
                            continue
                        sc, _ = score(q)
                        if sc > best + 1e-6:
                            best, p, improved = sc, q, True
            steps = [x / 2 for x in steps]
        sc, ious = score(p)
        if st == 'td':
            s_td = p[0]
        result[st] = dict(px_per_unit=p[0], pitch=p[1], dx=p[2], dy=p[3], mean_iou=sc, min_iou=min(ious),
                          iou=[round(x, 4) for x in ious])
        print(f'{st}: {p[0]:.4f} px/unit, pitch {p[1]:.2f}, anchor {p[2]:+.2f},{p[3]:+.2f}: IoU mean {sc:.4f}, min {min(ious):.4f} '
              f'({len(ious)} views)')
    result['anim'] = args.anim
    with open(args.out, 'w') as f:
        json.dump(result, f, indent=1)
    return 0


BLENDER_RIG = r'''# Written by spritepack.py blender: renders an animation's sprite views for an HD sprite pack (dAImon Keeper,
# docs/refactor/sprites/ section 12 of 02-design.md). Run:
#     blender [your.blend] -b -P THIS.py
# The model: Z up, facing -Y (Blender's front), its ground point at the origin, the action on the scene's timeline;
# or, with MESH set below, one OBJ a frame. Renders go to RENDERS/<set>/r<rotation>/f<frame>.png, at PLAN's canvases.
import bpy, json, math, os
from mathutils import Matrix, Vector
PLAN = json.loads(%(plan)r)
RENDERS = %(renders)r
MESH = %(mesh)r
ENGINE = %(engine)r
sc = bpy.context.scene
if MESH:
    for ob in list(sc.objects):
        if ob.type == 'MESH':
            bpy.data.objects.remove(ob)
try:
    sc.render.engine = ENGINE
except TypeError:
    sc.render.engine = 'BLENDER_EEVEE_NEXT' if ENGINE == 'BLENDER_EEVEE' else ENGINE
sc.render.film_transparent = True
sc.render.resolution_percentage = 100
sc.render.image_settings.file_format = 'PNG'
sc.render.image_settings.color_mode = 'RGBA'
sc.render.image_settings.color_depth = '8'
sc.view_settings.view_transform = 'Standard'
cd = bpy.data.cameras.new('sprite_cam')
cd.type = 'ORTHO'
cam = bpy.data.objects.new('sprite_cam', cd)
sc.collection.objects.link(cam)
sc.camera = cam
f0, f1 = sc.frame_start, sc.frame_end
current = None
for v in PLAN['views']:
    R, U, D = Vector(v['R']), Vector(v['U']), Vector(v['D'])
    W, H, s = v['w'], v['h'], v['s']
    sc.render.resolution_x, sc.render.resolution_y = W, H
    cd.ortho_scale = max(W, H) / s
    cd.sensor_fit = 'HORIZONTAL' if W >= H else 'VERTICAL'
    cd.clip_start, cd.clip_end = 0.1, 10000.0
    centre = R * ((W / 2 - v['ax']) / s) + U * ((v['ay'] - H / 2) / s)
    cam.matrix_world = Matrix.Translation(centre + D * 1000.0) @ Matrix((R, U, D)).transposed().to_4x4()
    for n in range(v['frames']):
        if MESH:
            path = MESH.format(frame=n)
            if current != path:
                for ob in list(sc.objects):
                    if ob.type == 'MESH':
                        bpy.data.objects.remove(ob)
                bpy.ops.wm.obj_import(filepath=path, forward_axis='Y', up_axis='Z') if PLAN['axes'] == 'z-up' else bpy.ops.wm.obj_import(filepath=path)
                current = path
        else:
            sc.frame_set(f0 + (n * (f1 - f0 + 1)) // v['frames'])
        sc.render.filepath = os.path.join(RENDERS, v['set'], 'r%%d' %% v['rot'], 'f%%02d.png' %% (n + 1))
        bpy.ops.render.render(write_still=True)
print('sprite rig: rendered', sum(v['frames'] for v in PLAN['views']), 'frames to', RENDERS)
'''


def cmd_blender(args):
    """A Blender script rendering an animation's views (both sets, rotations 1-5, the original's frame count) at a
    pack's scale through the rig fitted to the model (fit's JSON; without one, the archer's)."""
    import json
    fitted = json.load(open(args.fit)) if args.fit else {}
    views = rig_views(args.ref, args.anim, args.sets.split(','))
    if not views:
        print(f'{args.anim}: no _td/_fp folders in {args.ref}')
        return 1
    k = args.scale
    plan = {'axes': args.axes, 'views': []}
    for (st, r, n, ax, ay, w, h, folder) in views:
        f = fitted.get(st, {})
        e = f.get('pitch', RIG_TD_PITCH if st == 'td' else RIG_FP_PITCH)
        s = f.get('px_per_unit', 1.0 if st == 'td' else RIG_FP_SCALE)
        R, U, D = rig_axes(r, e)
        plan['views'].append(dict(set=st, rot=r, frames=n, w=w * k, h=h * k, s=s * k, ax=(ax + f.get('dx', 0.0)) * k,
                                  ay=(ay + f.get('dy', 0.0)) * k, R=R, U=U, D=D))
    with open(args.out, 'w') as fo:
        fo.write(BLENDER_RIG % dict(plan=json.dumps(plan), renders=os.path.abspath(args.renders),
                                    mesh=os.path.abspath(args.mesh) if args.mesh else '', engine=args.engine))
    print(f'{args.out}: {len(plan["views"])} views, {sum(v["frames"] for v in plan["views"])} renders at x{k} into {args.renders}')
    return 0


def cmd_from_renders(args):
    """Renders (RENDERS/<set>/r<rotation>/f<frame>.png, as blender's script writes them) into a pack's frames of an
    animation: the reference's filelists and names, keyed. Makes the pack if it isn't there."""
    import shutil
    from PIL import Image
    if read_pack_toml(args.pack) is None:
        os.makedirs(args.pack, exist_ok=True)
        with open(os.path.join(args.pack, 'pack.toml'), 'w') as f:
            f.write(f'name = "Rendered x{args.scale}"\nscale = {args.scale}\nuse_original_at_1x = true\n')
        open(os.path.join(args.pack, 'animlist.txt'), 'w').close()
    s = read_pack_toml(args.pack)['scale']
    lists = [l.strip() for l in open(os.path.join(args.pack, 'animlist.txt'), encoding='utf-8-sig') if l.strip()]
    done, problems = 0, []
    for (st, r, n, ax, ay, w, h, folder) in rig_views(args.ref, args.anim, args.sets.split(',')):
        fl = os.path.join(folder, f'filelist_rot{r}.txt')
        header, pngs = read_filelist(os.path.join(args.ref, fl))
        os.makedirs(os.path.join(args.pack, folder), exist_ok=True)
        for i, png in enumerate(pngs):
            src = os.path.join(args.renders, st, f'r{r}', f'f{i + 1:02d}.png')
            if not os.path.exists(src):
                problems.append(f'{src}: missing')
                continue
            size = Image.open(src).size
            if size != (w * s, h * s):
                problems.append(f'{src}: {size[0]}x{size[1]}, not {w * s}x{h * s}')
                continue
            shutil.copyfile(src, os.path.join(args.pack, folder, png))
            done += 1
        with open(os.path.join(args.pack, fl), 'w') as f:
            f.write(' '.join(header) + '\n' + '\n'.join(pngs) + '\n')
        if fl not in lists:
            lists.append(fl)
    with open(os.path.join(args.pack, 'animlist.txt'), 'w') as f:
        f.write('\n'.join(lists) + '\n')
    records, _ = key_records(args.pack, args.data, args.ref)
    write_keys(args.pack, records)
    for p in problems[:20]:
        print('PROBLEM', p)
    print(f'{args.pack}: {done} frames from {args.renders}, {len(problems)} problems')
    return 1 if problems else 0


def cmd_check(args):
    from PIL import Image
    problems, review = [], []
    meta = read_pack_toml(args.pack)
    if meta is None:
        print('no pack.toml: the game ignores this folder')
        return 1
    s = meta['scale']
    if s not in (2, 3, 4):
        problems.append(f'pack.toml: scale must be 2, 3 or 4, not {s}')
    if not os.path.exists(os.path.join(args.pack, 'animlist.txt')):
        print('no animlist.txt: the game ignores this pack')
        return 1
    refs = ref_filelists(args.ref)
    originals = {i: (e, rle) for i, e, rle in original_frames(args.data)}
    frames = pack_frames(args.pack)
    keys_path = os.path.join(args.pack, 'keys', 'animlist.idx')
    if not os.path.exists(keys_path):
        problems.append('keys/animlist.idx missing (spritepack.py keys): the game ignores this pack')
        keys = b''
    else:
        with open(keys_path, 'rb') as f:
            keys = f.read()
        if len(keys) != 16 * len(frames):
            problems.append(f'keys/animlist.idx has {len(keys) // 16} records for {len(frames)} frame lines (rerun keys): the game ignores this pack')
    want, _ = key_records(args.pack, args.data, args.ref)
    # A record of key 0 is a frame left as the original on purpose (sample, a partial pack): not stale.
    zero = struct.pack('<QHHHH', 0, 0, 0, 0, 0)
    skipped = {k for k in range(len(frames)) if 16 * (k + 1) <= len(keys) and keys[16 * k:16 * k + 16] == zero}
    stale = sum(1 for k, r in enumerate(want) if 16 * (k + 1) <= len(keys) and k not in skipped and keys[16 * k:16 * k + 16] != r)
    if stale:
        problems.append(f'{stale} keys differ from the originals in --data (rerun keys, or the art is for other data)')
    shown = 0
    checked = 0
    left = 0
    for n, frame in enumerate(frames):
        fl, pos, png = frame
        if n in skipped:
            left += 1
            continue
        if pos is None:
            problems.append(f'{fl}: listed in animlist.txt, not there')
            continue
        if fl not in refs:
            problems.append(f'{fl}: not a filelist of the reference (its frames get no key)')
            continue
        i = original_index(frame, refs)
        if i is None or i not in originals:
            continue
        e, rle = originals[i]
        path = os.path.join(args.pack, png)
        if not os.path.exists(path):
            problems.append(f'{png}: missing (that frame stays the original)')
            continue
        img = Image.open(path).convert('RGBA')
        if img.size != (e['fw'] * s, e['fh'] * s):
            problems.append(f'{png}: {img.size[0]}x{img.size[1]}, not {e["fw"] * s}x{e["fh"] * s} (x{s} the original canvas)')
            continue
        sub_path = path[:-4] + '_sub.png'
        if os.path.exists(sub_path) and Image.open(sub_path).size != img.size:
            problems.append(f'{png[:-4]}_sub.png: not the size of {png} (the game subtracts nothing for this frame)')
        checked += 1
        if not (e['sw'] and e['sh']):
            continue
        rows = frame_rows(rle, e['sw'], e['sh'])
        if rows is None:
            continue
        alpha = img.getchannel('A')
        x0, y0, x1, y1 = e['offs_w'] * s, e['offs_h'] * s, (e['offs_w'] + e['sw']) * s, (e['offs_h'] + e['sh']) * s
        whole = alpha.point(lambda a: 255 if a >= 128 else 0)
        inside = whole.crop((x0, y0, x1, y1))
        spill = sum(whole.histogram()[255:]) - sum(inside.histogram()[255:])
        margins = None
        box = whole.getbbox()
        if spill and box:
            # As the engine reads it (engine_sprites_hd_pack.c): whole original texels a side, up to SPILL_MAX.
            bx0, by0, bx1, by1 = box
            need = [-(-(x0 - bx0) // s) if bx0 < x0 else 0, -(-(y0 - by0) // s) if by0 < y0 else 0,
                    -(-(bx1 - x1) // s) if bx1 > x1 else 0, -(-(by1 - y1) // s) if by1 > y1 else 0]
            margins = (need, any(n > SPILL_MAX for n in need))
        small = inside.resize((e['sw'], e['sh']), Image.BOX).load()
        both = union = 0
        for y, row in enumerate(rows):
            for x, idx in enumerate(row):
                a = small[x, y] >= 128
                o = idx != 0
                both += a and o
                union += a or o
        iou = both / union if union else 1.0
        if iou < 0.9 or spill:
            review.append((png, iou, spill, margins))
    for p in problems[:50]:
        print('PROBLEM', p)
    if len(problems) > 50:
        print(f'... {len(problems) - 50} more problems')
    for png, iou, spill, margins in review[:50]:
        past = ''
        if spill:
            past = f', {spill} opaque pixels past the crop'
            if margins:
                need, cut = margins
                past += f' (widened by {need[0]}/{need[1]}/{need[2]}/{need[3]} texels left/top/right/bottom'
                past += f'; art past {SPILL_MAX} is cut off)' if cut else ')'
        print(f'REVIEW {png}: silhouette IoU {iou:.2f}' + past)
    print(f'{len(frames)} frame lines, {checked} canvases checked, {left} left as the originals (key 0): {len(problems)} problems, {len(review)} to review')
    return 1 if problems else 0


def cmd_selftest(args):
    key = frame_key(SELFTEST_RLE, *SELFTEST_SIZE)
    rows = frame_rows(SELFTEST_RLE, *SELFTEST_SIZE)
    print('rows:', [list(r) for r in rows])
    print(f'key: 0x{key:016x}')
    expected = 0x7a0d6190d491bb54  # the value engine_sprites_hd_test.cpp pins
    if key != expected:
        print(f'MISMATCH: expected 0x{expected:016x}')
        return 1
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('dump', help='the original frames as canvas PNGs, with frames.csv')
    p.add_argument('out')
    p.add_argument('--data', required=True, help='folder with creature.tab, creature.jty and palette.dat')
    p.add_argument('--ref', help='gfx/enginefx/sprites-32: name the frames as its animlist does')
    p = sub.add_parser('keys-check', help="compare the engine's frame keys with this script's")
    p.add_argument('engine_keys', help='the file ftest sprite_density wrote (KFX_FTEST_SD_KEYS)')
    p.add_argument('--data', required=True, help='the creature.tab/.jty the engine loaded')
    p = sub.add_parser('keys', help='write keys/animlist.idx')
    p.add_argument('pack')
    p.add_argument('--data', required=True, help='the creature.tab/.jty the art was made against')
    p.add_argument('--ref', required=True, help='gfx/enginefx/sprites-32')
    p = sub.add_parser('upscale', help='a test pack from the originals themselves')
    p.add_argument('out')
    p.add_argument('--data', required=True)
    p.add_argument('--ref', required=True)
    p.add_argument('--factor', type=int, default=4, choices=(2, 3, 4))
    p.add_argument('--only', help='only the filelists whose path starts with this')
    p.add_argument('--pattern', action='store_true', help='invert every texel but the one parity mode samples (a negative control)')
    p.add_argument('--spill', type=int, default=0, help='surround each crop with this many original texels of magenta (art past the crop)')
    p.add_argument('--additive-log', help='KFX_SPRITE_ALPHA_LOG output: the filelists with those frames become additive art (ramp deltas)')
    p.add_argument('--filter', choices=('nearest', 'lanczos'), default='nearest',
                   help='how the art is enlarged: nearest (exact texels, the test packs) or lanczos (Lanczos plus an unsharp mask: draft art, hd_resample.py)')
    p.add_argument('--sharpen', type=float, default=1.2, help='with --filter lanczos: the unsharp mask amount (0: none)')
    p = sub.add_parser('template', help='an empty pack: filelists, keys and FRAMES.txt, no art')
    p.add_argument('out')
    p.add_argument('--data', required=True)
    p.add_argument('--ref', required=True)
    p.add_argument('--scale', type=int, default=4, choices=(2, 3, 4))
    p.add_argument('--only', help='only the filelists whose path starts with this')
    p = sub.add_parser('sample', help="a x4 pack from FXGraphics' sprites-128 (a local aid: mind its licence)")
    p.add_argument('out')
    p.add_argument('--big', required=True, help='FXGraphics enginefx/sprites-128')
    p.add_argument('--additive-log', help='KFX_SPRITE_ALPHA_LOG output: those filelists become additive art')
    p.add_argument('--data', required=True)
    p.add_argument('--ref', required=True)
    p.add_argument('--only', help='only the filelists whose path starts with this')
    p = sub.add_parser('colourise', help="player colours: the other variants' frames from the base's art and masks")
    p.add_argument('pack')
    p.add_argument('--data', required=True)
    p.add_argument('--ref', required=True)
    p.add_argument('--spritecolors', required=True, help='fxdata/spritecolors.toml')
    p.add_argument('--objects', help='fxdata/objects.cfg (for its objects rows)')
    p.add_argument('--zips', nargs='*', help="sprite zips with the rows' named variants (colored_sprites.zip)")
    p.add_argument('--keeper-sprites', help="the engine's keeper_sprites.c (creature_list); default: this checkout's")
    p = sub.add_parser('fit', help='fit the sprite cameras to a model by silhouette IoU')
    p.add_argument('mesh', help='OBJ in the rig axes (Z up, facing -Y); {frame} in the name for a mesh a frame')
    p.add_argument('--data', required=True)
    p.add_argument('--ref', required=True)
    p.add_argument('--anim', required=True, help='a reference folder without _td/_fp, e.g. creatures/creature_archer/walk')
    p.add_argument('--frames', default='0', help='frames the mesh(es) pose, from 0: 0,3')
    p.add_argument('--sets', default='td,fp')
    p.add_argument('--axes', default='z-up', choices=('z-up', 'y-up'), help="the OBJ's axes (y-up: Blender's default OBJ export)")
    p.add_argument('--out', default='fit.json')
    p = sub.add_parser('blender', help="a Blender script rendering an animation's views through the fitted rig")
    p.add_argument('out')
    p.add_argument('--ref', required=True)
    p.add_argument('--anim', required=True)
    p.add_argument('--fit', help="fit's JSON (default: the archer's cameras)")
    p.add_argument('--scale', type=int, default=4, choices=(2, 3, 4))
    p.add_argument('--renders', required=True, help='where the script writes the renders')
    p.add_argument('--mesh', help='OBJ a frame instead of the scene ({frame} in the name)')
    p.add_argument('--axes', default='z-up', choices=('z-up', 'y-up'))
    p.add_argument('--sets', default='td,fp')
    p.add_argument('--engine', default='BLENDER_EEVEE', help='BLENDER_EEVEE, CYCLES or BLENDER_WORKBENCH')
    p = sub.add_parser('from-renders', help="the rig's renders into a pack")
    p.add_argument('renders')
    p.add_argument('pack')
    p.add_argument('--data', required=True)
    p.add_argument('--ref', required=True)
    p.add_argument('--anim', required=True)
    p.add_argument('--scale', type=int, default=4, choices=(2, 3, 4), help='for a new pack')
    p.add_argument('--sets', default='td,fp')
    p = sub.add_parser('import-zip', help="KeeperFX sprite zips' frames as a pack")
    p.add_argument('out')
    p.add_argument('zips', nargs='+')
    p.add_argument('--data', required=True, help='the folder with png_conv_pal.dat (the game\'s data/)')
    p.add_argument('--scale', type=int, default=4, choices=(2, 3, 4))
    p.add_argument('--keys-only', action='store_true')
    p.add_argument('--filter', choices=('nearest', 'lanczos'), default='nearest',
                   help='how the art is enlarged: nearest (exact texels, the test packs) or lanczos (Lanczos plus an unsharp mask: draft art, hd_resample.py)')
    p.add_argument('--sharpen', type=float, default=1.2, help='with --filter lanczos: the unsharp mask amount (0: none)')
    p = sub.add_parser('check', help='validate a pack')
    p.add_argument('pack')
    p.add_argument('--data', required=True)
    p.add_argument('--ref', required=True)
    sub.add_parser('selftest', help="the key function's shared test vector")
    args = parser.parse_args()
    return {'dump': cmd_dump, 'keys-check': cmd_keys_check, 'selftest': cmd_selftest, 'keys': cmd_keys,
            'upscale': cmd_upscale, 'check': cmd_check, 'import-zip': cmd_import_zip,
            'template': cmd_template, 'sample': cmd_sample, 'colourise': cmd_colourise, 'fit': cmd_fit,
            'blender': cmd_blender, 'from-renders': cmd_from_renders}[args.cmd](args)


if __name__ == '__main__':
    sys.exit(main())
