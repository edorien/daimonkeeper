"""Starting art for HD packs: Lanczos upscaling plus an unsharp mask, for spritepack.py and texpack.py (--filter lanczos).
The game does the same in C for what no pack covers (ASSET_PACK_FILL, src/kfx_render/src/engine_hd_generate.c).

The resampling is Pillow's Lanczos at an integer factor, then UnsharpMask(radius 1, percent sharpen * 100, threshold 3)
at the new size -- the routine of the user's batch_upscale.py -- with what game art needs on top:

- Cut-out art (alpha 0 or 255): colours are spread into the transparent texels around the art before filtering (each
  takes the mean of its opaque neighbours, ring by ring), so the edges don't pick up black; alpha is filtered on its
  own, softened by a Gaussian of `soften` original texels so the silhouette's steps round off, and cut at 128,
  which is where the game cuts it; the middle of every opaque original texel stays opaque, so no thin detail (a bow
  string, a single texel) disappears or shrinks to a dot.
- Additive art (what a texel adds to the screen): transparent texels add nothing, so they filter as black.
- The caller pads the image with what lies around it (transparent for a sprite, the neighbouring blocks for a terrain
  block) and crops the result: the filters reach about 3 texels across an edge.
"""
import numpy as np


def _bleed(rgb, opaque, rings):
    """rgb with each transparent texel within `rings` of the art given its opaque neighbours' mean colour, ring by ring
    outwards; the rest the art's mean colour."""
    rgb = rgb.astype(np.float32)
    filled = opaque.copy()
    if not filled.any():
        return rgb
    h, w = filled.shape
    for _ in range(rings):
        acc = np.zeros_like(rgb)
        cnt = np.zeros(filled.shape, np.float32)
        p = np.pad(rgb * filled[..., None], ((1, 1), (1, 1), (0, 0)))
        m = np.pad(filled.astype(np.float32), 1)
        for dy in (0, 1, 2):
            for dx in (0, 1, 2):
                if dy == 1 and dx == 1:
                    continue
                acc += p[dy:dy + h, dx:dx + w]
                cnt += m[dy:dy + h, dx:dx + w]
        grow = (~filled) & (cnt > 0)
        if not grow.any():
            break
        rgb[grow] = acc[grow] / cnt[grow][:, None]
        filled |= grow
    rgb[~filled] = rgb[opaque].mean(axis=0)
    return rgb


def _resize_rgb(rgb, factor, sharpen):
    from PIL import Image, ImageFilter
    img = Image.fromarray(np.clip(np.rint(rgb), 0, 255).astype(np.uint8), 'RGB')
    big = img.resize((img.width * factor, img.height * factor), Image.LANCZOS)
    if sharpen > 0:
        big = big.filter(ImageFilter.UnsharpMask(radius=1, percent=int(sharpen * 100), threshold=3))
    return np.asarray(big)


def _resize_alpha(alpha, factor):
    from PIL import Image
    img = Image.fromarray(alpha.astype(np.uint8), 'L')
    return np.asarray(img.resize((img.width * factor, img.height * factor), Image.LANCZOS))


def _core(opaque, factor):
    """The middle (factor - 1) x (factor - 1) texels (at least 1) of each opaque texel's block, at factor x: a corner
    rounds off by one texel at most, and a single texel keeps most of its area."""
    k = max(1, factor - 1)
    lo = (factor - k) // 2
    sub = np.zeros((factor, factor), bool)
    sub[lo:lo + k, lo:lo + k] = True
    return np.kron(opaque, sub).astype(bool)


def upscale(img, factor, sharpen=1.2, additive=False, soften=0.5):
    """An RGBA image (Pillow) `factor` times its size: Lanczos, unsharp mask, alpha softened and cut at 128 (0 or 255
    out). Cut-out art by default (alpha below 128 is transparent); additive=True for additive art (RGB is what it
    adds)."""
    from PIL import Image, ImageFilter
    a = np.asarray(img.convert('RGBA'))
    opaque = a[..., 3] >= 128
    rgb = a[..., :3].astype(np.float32)
    if additive:
        rgb[~opaque] = 0
    else:
        rgb = _bleed(rgb, opaque, rings=4)
    out_rgb = _resize_rgb(rgb, factor, sharpen)
    mask = _resize_alpha(np.where(opaque, 255, 0), factor)
    if soften > 0:
        mask = np.asarray(Image.fromarray(mask, 'L').filter(ImageFilter.GaussianBlur(soften * factor)))
    out_a = np.where((mask >= 128) | _core(opaque, factor), 255, 0).astype(np.uint8)
    out = np.dstack([np.where(out_a[..., None] > 0, out_rgb, 0).astype(np.uint8), out_a])
    return Image.fromarray(out, 'RGBA')
