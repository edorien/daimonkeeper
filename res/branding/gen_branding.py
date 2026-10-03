#!/usr/bin/env python3
"""dAImon Keeper branding artwork, generated as self-contained SVG.

The theme: the dungeon heart rebuilt as flesh and iron with one red machine eye, lit like
the original game's FMVs (a cold shaft from a ceiling grate, torches, blood-red underglow).

    python3 res/branding/gen_branding.py      # writes res/branding/svg/*.svg
    res/branding/make_assets.sh               # + rasterise and install into the tree

All text is converted to outlines (fontTools), so the SVGs need no fonts to render.
Wordmark: Cinzel Black/Bold (OFL, config/fxdata/font/Cinzel). Body text: Lato (OFL) --
must be installed to regenerate (Debian/Ubuntu: fonts-lato), not to render.
"""
import math
import os
import random

from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
OUT = os.path.join(HERE, 'svg')


def n(v):
    return ('%.1f' % v).rstrip('0').rstrip('.')


# --------------------------------------------------------------------------- fonts

class Font:
    def __init__(self, path):
        self.tt = TTFont(path)
        self.gs = self.tt.getGlyphSet()
        self.cmap = self.tt.getBestCmap()
        self.upm = self.tt['head'].unitsPerEm
        self.hmtx = self.tt['hmtx']
        self.kst = []
        if 'GPOS' in self.tt:
            for lk in self.tt['GPOS'].table.LookupList.Lookup:
                for st in lk.SubTable:
                    if lk.LookupType == 9:
                        if st.ExtensionLookupType != 2:
                            continue
                        st = st.ExtSubTable
                    elif lk.LookupType != 2:
                        continue
                    self.kst.append((set(st.Coverage.glyphs), st))

    def kern(self, a, b):
        for cov, st in self.kst:
            if a not in cov:
                continue
            if st.Format == 1:
                ps = st.PairSet[st.Coverage.glyphs.index(a)]
                for r in ps.PairValueRecord:
                    if r.SecondGlyph == b:
                        return getattr(r.Value1, 'XAdvance', 0) or 0
            else:
                c1 = st.ClassDef1.classDefs.get(a, 0)
                c2 = st.ClassDef2.classDefs.get(b, 0)
                v = st.Class1Record[c1].Class2Record[c2].Value1
                return (getattr(v, 'XAdvance', 0) or 0) if v else 0
        return 0

    def layout(self, text, size, tracking=0.0):
        sc = size / self.upm
        names = [self.cmap[ord(c)] for c in text]
        x, out = 0.0, []
        for i, (c, g) in enumerate(zip(text, names)):
            out.append((c, g, x))
            adv = self.hmtx[g][0]
            if i + 1 < len(names):
                adv += self.kern(g, names[i + 1])
            x += adv * sc + tracking * size
        return out, x - tracking * size

    def glyph_path(self, g, size, x, y):
        sc = size / self.upm
        pen = SVGPathPen(self.gs, ntos=n)
        self.gs[g].draw(TransformPen(pen, (sc, 0, 0, -sc, x, y)))
        return pen.getCommands()

    def glyph_bounds(self, g, size, x, y):
        sc = size / self.upm
        bp = BoundsPen(self.gs)
        self.gs[g].draw(bp)
        if bp.bounds is None:
            return None
        x0, y0, x1, y1 = bp.bounds
        return (x + x0 * sc, y - y1 * sc, x + x1 * sc, y - y0 * sc)

    def width(self, text, size, tracking=0.0):
        return self.layout(text, size, tracking)[1]

    def text(self, text, size, x, y, anchor='start', tracking=0.0):
        """Outlined text as one path 'd' string."""
        gl, w = self.layout(text, size, tracking)
        x0 = x - (w / 2 if anchor == 'middle' else w if anchor == 'end' else 0)
        return ''.join(self.glyph_path(g, size, x0 + gx, y) for c, g, gx in gl if c != ' ')


def find_font(*cands):
    for c in cands:
        p = c if os.path.isabs(c) else os.path.join(ROOT, c)
        if os.path.exists(p):
            return p
    raise SystemExit('font not found: %s' % (cands,))


CINZEL_BLACK = Font(find_font('config/fxdata/font/Cinzel/static/Cinzel-Black.ttf',
                              'core_files/fxdata/font/Cinzel/static/Cinzel-Black.ttf'))
CINZEL_BOLD = Font(find_font('config/fxdata/font/Cinzel/static/Cinzel-Bold.ttf',
                             'core_files/fxdata/font/Cinzel/static/Cinzel-Bold.ttf'))
LATO = Font(find_font('/usr/share/fonts/truetype/lato/Lato-Regular.ttf',
                      '/usr/share/fonts/lato/Lato-Regular.ttf',
                      os.path.expanduser('~/.local/share/fonts/Lato-Regular.ttf')))


# --------------------------------------------------------------------------- geometry helpers

def smooth_closed(pts, t=0.5):
    """Catmull-Rom through pts, closed, as cubic bezier path."""
    k = len(pts)
    d = 'M%s,%s' % (n(pts[0][0]), n(pts[0][1]))
    for i in range(k):
        p0, p1, p2, p3 = pts[i - 1], pts[i], pts[(i + 1) % k], pts[(i + 2) % k]
        c1 = (p1[0] + (p2[0] - p0[0]) * t / 3, p1[1] + (p2[1] - p0[1]) * t / 3)
        c2 = (p2[0] - (p3[0] - p1[0]) * t / 3, p2[1] - (p3[1] - p1[1]) * t / 3)
        d += 'C%s,%s %s,%s %s,%s' % tuple(n(v) for v in (*c1, *c2, *p2))
    return d + 'Z'


def smooth_open(pts, t=0.5):
    k = len(pts)
    d = 'M%s,%s' % (n(pts[0][0]), n(pts[0][1]))
    for i in range(k - 1):
        p0 = pts[max(i - 1, 0)]
        p1, p2 = pts[i], pts[i + 1]
        p3 = pts[min(i + 2, k - 1)]
        c1 = (p1[0] + (p2[0] - p0[0]) * t / 3, p1[1] + (p2[1] - p0[1]) * t / 3)
        c2 = (p2[0] - (p3[0] - p1[0]) * t / 3, p2[1] - (p3[1] - p1[1]) * t / 3)
        d += 'C%s,%s %s,%s %s,%s' % tuple(n(v) for v in (*c1, *c2, *p2))
    return d


def bez(p0, p1, p2, p3, t):
    u = 1 - t
    return tuple(u * u * u * a + 3 * u * u * t * b + 3 * u * t * t * c + t * t * t * d
                 for a, b, c, d in zip(p0, p1, p2, p3))


def tapered(p0, p1, p2, p3, w0, w1, steps=28, wfun=None):
    """Filled polygon along a cubic with width going w0 -> w1."""
    left, right = [], []
    for i in range(steps + 1):
        t = i / steps
        a = bez(p0, p1, p2, p3, max(t - .01, 0))
        b = bez(p0, p1, p2, p3, min(t + .01, 1))
        p = bez(p0, p1, p2, p3, t)
        dx, dy = b[0] - a[0], b[1] - a[1]
        ln = math.hypot(dx, dy) or 1
        nx, ny = -dy / ln, dx / ln
        w = (wfun(t) if wfun else (w0 + (w1 - w0) * t)) / 2
        left.append((p[0] + nx * w, p[1] + ny * w))
        right.append((p[0] - nx * w, p[1] - ny * w))
    pts = left + right[::-1]
    return 'M' + ' L'.join('%s,%s' % (n(x), n(y)) for x, y in pts) + 'Z'


# --------------------------------------------------------------------------- shared defs

def defs_common():
    return '''
<radialGradient id="vignette" cx=".5" cy=".5" r=".75">
  <stop offset=".45" stop-color="#000" stop-opacity="0"/><stop offset="1" stop-color="#000" stop-opacity=".92"/>
</radialGradient>
<radialGradient id="torchGlow"><stop offset="0" stop-color="#ffb24a" stop-opacity=".75"/>
  <stop offset=".35" stop-color="#d9601a" stop-opacity=".3"/><stop offset="1" stop-color="#d9601a" stop-opacity="0"/></radialGradient>
<filter id="blur6" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="6"/></filter>
<filter id="blur14" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="14"/></filter>
<filter id="blur30" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="30"/></filter>
<filter id="grit" x="0" y="0" width="100%" height="100%">
  <feTurbulence type="fractalNoise" baseFrequency=".85" numOctaves="3" seed="7"/>
  <feColorMatrix values="0 0 0 0 0  0 0 0 0 0  0 0 0 0 0  1.9 0 0 0 -.75"/>
</filter>
<filter id="mottle" x="0" y="0" width="100%" height="100%">
  <feTurbulence type="fractalNoise" baseFrequency=".012" numOctaves="3" seed="3"/>
  <feColorMatrix values="0 0 0 0 0  0 0 0 0 0  0 0 0 0 0  1.4 0 0 0 -.45"/>
</filter>
''' + ART_DEFS


def svg(w, h, body, bg=None, view=None):
    bgr = '<rect width="%s" height="%s" fill="%s"/>' % (n(w), n(h), bg) if bg else ''
    vw, vh = view or (w, h)
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="%s" height="%s" viewBox="0 0 %s %s">\n'
            '<defs>%s</defs>\n%s%s\n</svg>\n' % (n(w), n(h), n(vw), n(vh), defs_common(), bgr, body))


# --------------------------------------------------------------------------- wordmark

def wordmark(cx, baseline, size, uid, max_width=None):
    """'dAImon Keeper' in Cinzel Black: weathered bronze, the AI in glowing ember with etched traces."""
    f = CINZEL_BLACK
    tr = 0.015
    if max_width:
        w = f.width('dAImon Keeper', size, tr)
        if w > max_width:
            size *= max_width / w
    gl, w = f.layout('dAImon Keeper', size, tr)
    x0 = cx - w / 2
    metal, ai, boxes = [], [], []
    for i, (c, g, gx) in enumerate(gl):
        if c == ' ':
            continue
        d = f.glyph_path(g, size, x0 + gx, baseline)
        if i in (1, 2):
            ai.append(d)
            boxes.append(f.glyph_bounds(g, size, x0 + gx, baseline))
        else:
            metal.append(d)
    sw = size * .03
    out = ['<g class="wordmark">',
           '<path d="%s" fill="#ff4a14" opacity=".35" filter="url(#blur14)"/>' % ''.join(metal + ai),
           '<path d="%s" fill="#000" opacity=".75" transform="translate(%s,%s)" filter="url(#blur6)"/>'
           % (''.join(metal + ai), n(size * .03), n(size * .05)),
           '<path d="%s" fill="url(#bronze)" stroke="#1c0c03" stroke-width="%s" paint-order="stroke" '
           'stroke-linejoin="round"/>' % (''.join(metal), n(sw * 2)),
           '<clipPath id="wmclip-%s"><path d="%s"/></clipPath><rect x="%s" y="%s" width="%s" height="%s" '
           'fill-opacity="0" filter="url(#grit)" clip-path="url(#wmclip-%s)" opacity=".45"/>'
           % (uid, ''.join(metal), n(x0 - 10), n(baseline - size), n(w + 20), n(size * 1.3), uid)]
    (ax0, ay0, ax1, ay1), (ix0, iy0, ix1, iy1) = boxes
    cid = 'aiclip-' + uid
    out.append('<clipPath id="%s"><path d="%s"/></clipPath>' % (cid, ''.join(ai)))
    out.append('<path d="%s" fill="#ff2a0a" filter="url(#blur14)" opacity=".9"/>' % ''.join(ai))
    out.append('<path d="%s" fill="url(#emberText)" stroke="#1a0302" stroke-width="%s" '
               'paint-order="stroke" stroke-linejoin="round"/>' % (''.join(ai), n(sw * 2)))
    rnd = random.Random(11)  # etched traces inside the letters
    traces = []
    for (bx0, by0, bx1, by1) in boxes:
        y = by0 + size * .08
        while y < by1:
            x = bx0 - 5
            pts = [(x, y)]
            while x < bx1:
                x += rnd.uniform(.08, .18) * size
                y2 = pts[-1][1] + rnd.choice((0, 0, size * .06, -size * .06))
                pts.append((x, pts[-1][1]))
                pts.append((x + abs(y2 - pts[-1][1]), y2))
                x += abs(y2 - pts[-2][1])
            traces.append('M' + ' L'.join('%s,%s' % (n(a), n(b)) for a, b in pts))
            y += size * .13
    out.append('<g clip-path="url(#%s)"><path d="%s" fill="none" stroke="#3a0604" stroke-width="%s" '
               'opacity=".85"/></g>' % (cid, ''.join(traces), n(size * .016)))
    yb = ay1 + size * .09  # traces leaving the feet along the baseline
    leg = ('M%s,%s L%s,%s L%s,%s' % tuple(n(v) for v in (ax0 + (ax1 - ax0) * .15, ay1, ax0 + (ax1 - ax0) * .15, yb,
                                                        ax0 - size * .55, yb))
           + 'M%s,%s L%s,%s L%s,%s L%s,%s' % tuple(n(v) for v in ((ix0 + ix1) / 2, iy1, (ix0 + ix1) / 2,
                                                                 yb + size * .05, ix1 + size * .2, yb + size * .05,
                                                                 ix1 + size * .8, yb + size * .05)))
    out.append('<path d="%s" fill="none" stroke="#ff2a0a" stroke-width="%s" filter="url(#blur6)" opacity=".8"/>'
               % (leg, n(size * .04)))
    out.append('<path d="%s" fill="none" stroke="#ffb070" stroke-width="%s"/>' % (leg, n(size * .012)))
    for ex, ey in ((ax0 - size * .55, yb), (ix1 + size * .8, yb + size * .05)):
        out.append('<circle cx="%s" cy="%s" r="%s" fill="#1a0302" stroke="#ffb070" stroke-width="%s"/>'
                   % (n(ex), n(ey), n(size * .03), n(size * .012)))
    out.append('</g>')
    return '\n'.join(out), size


# --------------------------------------------------------------------------- scenery

def perspective_floor(w, h, y0, vp, tone, line='#0b0806', rows=7, cols=16):
    out = ['<rect x="0" y="%s" width="%d" height="%s" fill="%s"/>' % (n(y0), w, n(h - y0), tone)]
    vx, vy = vp
    d = ''
    for i in range(-cols, cols + 1):
        bx = vx + i * w / cols * 1.1
        t = (y0 - vy) / (h - vy)
        d += 'M%s,%s L%s,%s ' % (n(vx + (bx - vx) * t), n(y0), n(bx), n(h))
    for j in range(1, rows):
        y = y0 + (h - y0) * (j / rows) ** 1.6
        d += 'M0,%s H%d ' % (n(y), w)
    out.append('<path d="%s" stroke="%s" stroke-width="3" fill="none"/>' % (d, line))
    return ''.join(out)


def torch(x, y, s=1.0):
    return '''<g transform="translate(%s,%s) scale(%s)">
<circle r="330" fill="url(#torchGlow)" style="mix-blend-mode:screen"/>
<path d="M-10,20 L10,20 L7,90 L-7,90 Z" fill="#2a1a10" stroke="#0a0604" stroke-width="3"/>
<path d="M-26,14 L26,14 L18,34 L-18,34 Z" fill="#3a2616" stroke="#0a0604" stroke-width="3"/>
<path d="M0,-92 C18,-60 34,-34 26,-6 C22,10 10,18 0,18 C-12,18 -24,10 -27,-6 C-30,-30 -12,-48 0,-92 Z" fill="#ff8a1a"/>
<path d="M2,-62 C12,-40 20,-24 14,-4 C10,8 4,12 -2,12 C-10,12 -16,4 -16,-6 C-16,-24 -6,-36 2,-62 Z" fill="#ffd25a"/>
<path d="M0,-30 C6,-20 8,-10 5,0 C3,6 -3,6 -5,0 C-7,-10 -4,-18 0,-30 Z" fill="#fff6d0"/>
</g>''' % (n(x), n(y), n(s))


# --------------------------------------------------------------------------- heart art

# --------------------------------------------------------------------------- heart art
# Lit like the FMVs: one cold shaft from a ceiling grate, blood-red underglow, murky stone,
# shading rather than outlines. The heart is half flesh, half machine, with one red eye.

ART_DEFS = '''
<linearGradient id="bronze" x1="0" y1="0" x2="0" y2="1">
  <stop offset="0" stop-color="#f4dca4"/><stop offset=".32" stop-color="#bf8e4a"/>
  <stop offset=".62" stop-color="#744822"/><stop offset="1" stop-color="#2a1608"/>
</linearGradient>
<linearGradient id="emberText" x1="0" y1="0" x2="0" y2="1">
  <stop offset="0" stop-color="#ffe8b8"/><stop offset=".3" stop-color="#ff8a34"/>
  <stop offset=".65" stop-color="#cc220e"/><stop offset="1" stop-color="#4a0604"/>
</linearGradient>
<linearGradient id="iron" x1="0" y1="0" x2="1" y2="1">
  <stop offset="0" stop-color="#7a6c5e"/><stop offset=".35" stop-color="#3e352e"/><stop offset="1" stop-color="#0e0b09"/>
</linearGradient>
<linearGradient id="talon" x1="0" y1="0" x2="1" y2="1">
  <stop offset="0" stop-color="#eadcbc"/><stop offset=".55" stop-color="#8a7456"/><stop offset="1" stop-color="#241810"/>
</linearGradient>
<radialGradient id="flesh" cx=".42" cy=".26" r=".85">
  <stop offset="0" stop-color="#e6d8be"/><stop offset=".28" stop-color="#b49e82"/>
  <stop offset=".6" stop-color="#6a5040"/><stop offset=".85" stop-color="#301c16"/><stop offset="1" stop-color="#120807"/>
</radialGradient>
<linearGradient id="lid" x1="0" y1="0" x2="0" y2="1">
  <stop offset="0" stop-color="#a89076"/><stop offset=".6" stop-color="#5a4034"/><stop offset="1" stop-color="#1e100c"/>
</linearGradient>
<radialGradient id="redEye" cx=".5" cy=".5" r=".5">
  <stop offset="0" stop-color="#fff6dc"/><stop offset=".12" stop-color="#ffb468"/><stop offset=".34" stop-color="#ff2c10"/>
  <stop offset=".7" stop-color="#620404"/><stop offset="1" stop-color="#160202"/>
</radialGradient>
<radialGradient id="bloodGlow"><stop offset="0" stop-color="#ff2a10" stop-opacity=".65"/>
  <stop offset=".4" stop-color="#9a0c06" stop-opacity=".22"/><stop offset="1" stop-color="#500404" stop-opacity="0"/></radialGradient>
<linearGradient id="shaft" x1="0" y1="0" x2="0" y2="1">
  <stop offset="0" stop-color="#dfe6ee" stop-opacity=".42"/><stop offset=".65" stop-color="#b8c2cc" stop-opacity=".13"/>
  <stop offset="1" stop-color="#b8c2cc" stop-opacity="0"/></linearGradient>
<linearGradient id="pillar" x1="0" y1="0" x2="1" y2="0">
  <stop offset="0" stop-color="#050403"/><stop offset=".55" stop-color="#2a221c"/><stop offset=".8" stop-color="#3e332a"/>
  <stop offset="1" stop-color="#0c0907"/></linearGradient>
<linearGradient id="coin" x1="0" y1="0" x2="0" y2="1">
  <stop offset="0" stop-color="#fff0a0"/><stop offset=".5" stop-color="#d8a030"/><stop offset="1" stop-color="#6a4410"/></linearGradient>
<filter id="fleshTex" x="0" y="0" width="100%" height="100%">
  <feTurbulence type="fractalNoise" baseFrequency=".028" numOctaves="4" seed="9"/>
  <feColorMatrix values="0 0 0 0 .32  0 0 0 0 .05  0 0 0 0 .04  2 0 0 0 -.78"/>
</filter>
<filter id="blur3" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="3"/></filter>
<filter id="blur22" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="22"/></filter>
'''


def rough_wall(w, h, seed, tone, row_h=70, var=10):
    """Irregular, hand-cut stone blocks lit from above."""
    rnd = random.Random(seed)
    k = row_h / 70
    out = ['<rect width="%s" height="%s" fill="#040302"/>' % (n(w), n(h))]
    y = -rnd.uniform(0, row_h * .5)
    while y < h:
        rh = row_h * rnd.uniform(.8, 1.15)
        x = -rnd.uniform(0, row_h * 1.5)
        while x < w:
            bw = row_h * rnd.uniform(1.1, 2.3)
            g = 3.5 * k

            def j(a=1.0):
                return rnd.uniform(-5, 5) * k * a
            pts = [(x + g + j(), y + g + j()), (x + bw * .5 + j(), y + g + j(.5)), (x + bw - g + j(), y + g + j()),
                   (x + bw - g + j(.6), y + rh * .5 + j()), (x + bw - g + j(), y + rh - g + j()),
                   (x + bw * .5 + j(), y + rh - g + j(.5)), (x + g + j(), y + rh - g + j()),
                   (x + g + j(.6), y + rh * .5 + j())]
            c = tuple(max(0, min(255, int(t + rnd.uniform(-var, var)))) for t in tone)
            hi = tuple(min(255, int(v * 1.5 + 8)) for v in c)
            lo = tuple(int(v * .45) for v in c)
            poly = 'M' + ' L'.join('%s,%s' % (n(a), n(b)) for a, b in pts) + 'Z'
            out.append('<path d="%s" fill="rgb(%d,%d,%d)"/>' % (poly, *c))
            out.append('<path d="M%s,%s L%s,%s L%s,%s" fill="none" stroke="rgb(%d,%d,%d)" stroke-width="%s" '
                       'opacity=".55" stroke-linecap="round"/>' % (*[n(v) for v in (*pts[0], *pts[1], *pts[2])], *hi, n(2 * k)))
            out.append('<path d="M%s,%s L%s,%s L%s,%s" fill="none" stroke="rgb(%d,%d,%d)" stroke-width="%s" '
                       'opacity=".8" stroke-linecap="round"/>' % (*[n(v) for v in (*pts[4], *pts[5], *pts[6])], *lo, n(3 * k)))
            x += bw
        y += rh
    return '<g>%s</g>' % ''.join(out)


def light_shaft(top, bottom, y0, y1, grate=5):
    """Cold light falling from a ceiling grate: polygon from (top x0,x1) at y0 to (bottom x0,x1) at y1."""
    (tx0, tx1), (bx0, bx1) = top, bottom
    poly = 'M%s,%s L%s,%s L%s,%s L%s,%s Z' % tuple(n(v) for v in (tx0, y0, tx1, y0, bx1, y1, bx0, y1))
    bars = ''
    for i in range(1, grate):
        f = i / grate
        bars += 'M%s,%s L%s,%s ' % (n(tx0 + (tx1 - tx0) * f), n(y0), n(bx0 + (bx1 - bx0) * f), n(y1))
    return ('<g style="mix-blend-mode:screen"><path d="%s" fill="url(#shaft)" filter="url(#blur22)"/>'
            '<path d="%s" stroke="#000" stroke-width="%s" opacity=".35" filter="url(#blur6)"/></g>'
            % (poly, bars, n((bx1 - bx0) / grate * .22)))


def motes(rnd, box, count, col='#e8e4d8', rmax=2.4, op=(.15, .6)):
    x0, y0, x1, y1 = box
    return ''.join('<circle cx="%s" cy="%s" r="%s" fill="%s" opacity="%s"/>'
                   % (n(rnd.uniform(x0, x1)), n(rnd.uniform(y0, y1)), n(rnd.uniform(.6, rmax)), col,
                      n(rnd.uniform(*op))) for _ in range(count))


def embers(rnd, box, count):
    x0, y0, x1, y1 = box
    out = []
    for _ in range(count):
        x, y, r = rnd.uniform(x0, x1), rnd.uniform(y0, y1), rnd.uniform(1, 3.2)
        out.append('<circle cx="%s" cy="%s" r="%s" fill="#ff5a1a" opacity=".7" filter="url(#blur3)"/>'
                   '<circle cx="%s" cy="%s" r="%s" fill="#ffd08a"/>' % (n(x), n(y), n(r * 2.4), n(x), n(y), n(r * .6)))
    return ''.join(out)


def coins(rnd, box, count):
    x0, y0, x1, y1 = box
    out = []
    for _ in range(count):
        x, y = rnd.uniform(x0, x1), rnd.uniform(y0, y1)
        s = .6 + (y - y0) / max(y1 - y0, 1) * .6
        out.append('<ellipse cx="%s" cy="%s" rx="%s" ry="%s" fill="url(#coin)" stroke="#2a1a06" stroke-width="1" '
                   'opacity=".85"/>' % (n(x), n(y), n(8 * s), n(3.4 * s)))
    return ''.join(out)


def blood_cracks(w, h, x0, y0, rnd, count=12, spread=1.0):
    """Glowing cracks over the floor that straighten into circuit traces as they run out."""
    d_org, d_circ, ends = '', '', []
    for i in range(count):
        ang = math.pi * (i + .5 + rnd.uniform(-.3, .3)) / count
        dx = math.cos(ang)
        x, y = x0 + dx * 30, y0
        pts = [(x, y)]
        for _ in range(3):  # organic start
            x += dx * rnd.uniform(20, 45) * spread + rnd.uniform(-12, 12)
            y += rnd.uniform(8, 20) * spread
            pts.append((x, y))
        d_org += smooth_open(pts) + ' '
        c = [(x, y)]
        while -20 < x < w + 20 and y < h + 20:
            st = rnd.uniform(30, 70) * spread
            if rnd.random() < .45:
                x += dx * st * 1.7
            else:
                x += dx * st * 1.2
                y += st * .55
            c.append((x, y))
        d_circ += 'M' + ' L'.join('%s,%s' % (n(a), n(b)) for a, b in c) + ' '
        ends.append(c[min(2, len(c) - 1)])
    both = d_org + d_circ
    return ('<path d="%s" fill="none" stroke="#ff2a0a" stroke-width="9" opacity=".45" filter="url(#blur6)"/>'
            '<path d="%s" fill="none" stroke="#1a0302" stroke-width="5"/>'
            '<path d="%s" fill="none" stroke="#ff6a24" stroke-width="1.8" opacity=".9"/>' % (both, both, both)
            + ''.join('<circle cx="%s" cy="%s" r="4" fill="#1a0302" stroke="#ff7a3a" stroke-width="1.6"/>'
                      % (n(a), n(b)) for a, b in ends))


def iron_limb(pts, widths, bend=.18):
    """Blackened iron finger with bone talon; segments bow slightly."""
    segs, hls, joints = [], [], []
    for i in range(len(pts) - 1):
        a, b = pts[i], pts[i + 1]
        dx, dy = b[0] - a[0], b[1] - a[1]
        ln = math.hypot(dx, dy)
        nx, ny = -dy / ln, dx / ln
        if ny > 0:
            nx, ny = -nx, -ny  # normal pointing up, towards the light
        c1 = (a[0] + dx * .33 + nx * ln * bend * .5, a[1] + dy * .33 + ny * ln * bend * .5)
        c2 = (a[0] + dx * .66 + nx * ln * bend * .5, a[1] + dy * .66 + ny * ln * bend * .5)
        segs.append(tapered(a, c1, c2, b, widths[i] * .92, widths[i + 1] * .92))
        o1 = widths[i] * .3
        hls.append('M%s,%s C%s,%s %s,%s %s,%s' % tuple(n(v) for v in (
            a[0] + nx * o1, a[1] + ny * o1, c1[0] + nx * o1, c1[1] + ny * o1, c2[0] + nx * o1, c2[1] + ny * o1,
            b[0] + nx * o1 * .8, b[1] + ny * o1 * .8)))
    a, b = pts[-2], pts[-1]
    dx, dy = b[0] - a[0], b[1] - a[1]
    ln = math.hypot(dx, dy)
    ux, uy = dx / ln, dy / ln
    L = widths[-1] * 3.2
    side = 1 if (ux * 0 - uy * 1) > 0 else -1
    tip = (b[0] + ux * L - uy * L * .45 * side, b[1] + uy * L + ux * L * .45 * side)
    talon = tapered(b, (b[0] + ux * L * .5, b[1] + uy * L * .5), (tip[0] - ux * 6, tip[1] - uy * 6), tip,
                    widths[-1] * .95, .5)
    for i, (x, y) in enumerate(pts[:-1] if len(pts) > 2 else pts):
        r = widths[i] * .56
        joints.append('<ellipse cx="%s" cy="%s" rx="%s" ry="%s" fill="url(#iron)" stroke="#060403" stroke-width="2"/>'
                      '<circle cx="%s" cy="%s" r="%s" fill="#ff3a14" filter="url(#blur3)"/>'
                      '<circle cx="%s" cy="%s" r="%s" fill="#ffb070"/>'
                      % (n(x), n(y), n(r), n(r * .9), n(x + r * .15), n(y + r * .2), n(r * .28),
                         n(x + r * .15), n(y + r * .2), n(r * .1)))
    return ('<g fill="url(#iron)" stroke="#060403" stroke-width="2.5" stroke-linejoin="round">%s</g>'
            '<path d="%s" fill="url(#talon)" stroke="#060403" stroke-width="2"/>'
            '<path d="%s" fill="none" stroke="#b8a690" stroke-width="2" opacity=".4" stroke-linecap="round"/>%s'
            % (''.join('<path d="%s"/>' % d for d in segs), talon, ''.join(hls), ''.join(joints)))


def _ell_pt(a, f, R, ry):
    return (math.cos(a) * R * f, math.sin(a) * ry * f)


def dread_heart(cx, cy, s, uid, legs=True, glow=True, detail=True):
    """Flesh-and-iron dungeon heart centred on the orb at (cx, cy); s=1 -> orb radius 190."""
    rnd = random.Random(21)
    R, RY = 190, 178
    o = ['<g transform="translate(%s,%s) scale(%s)">' % (n(cx), n(cy), n(s))]
    if glow:
        o.append('<circle r="440" fill="url(#bloodGlow)" style="mix-blend-mode:screen"/>')
    if legs:
        # arteries into the floor
        for sx in (-1, 1):
            for (a, b, c, d, w0, w1) in (((120, 60), (300, 120), (380, 300), (520, 340), 30, 16),
                                         ((80, 140), (150, 260), (260, 300), (300, 360), 22, 12)):
                p = tapered((a[0] * sx, a[1]), (b[0] * sx, b[1]), (c[0] * sx, c[1]), (d[0] * sx, d[1]), w0, w1)
                o.append('<path d="%s" fill="#140c0a" stroke="#050302" stroke-width="2"/>' % p)
                o.append('<path d="M%s,%s C%s,%s %s,%s %s,%s" fill="none" stroke="#5a3a30" stroke-width="2" '
                         'opacity=".6" transform="translate(0,-%s)"/>'
                         % (*[n(v) for v in (a[0] * sx, a[1], b[0] * sx, b[1], c[0] * sx, c[1], d[0] * sx, d[1])],
                            n(w1 * .3)))
        for sx in (-1, 1):
            g = '<g transform="scale(%d,1)">%%s</g>' % sx
            o.append(g % iron_limb([(-100, 118), (-196, 196), (-236, 290)], [48, 36, 26]))
            o.append(g % iron_limb([(-34, 156), (-76, 232), (-92, 300)], [40, 30, 22]))
    oid = 'orb-' + uid
    o.append('<clipPath id="%s"><ellipse rx="%d" ry="%d"/></clipPath>' % (oid, R, RY))
    o.append('<ellipse rx="%d" ry="%d" fill="url(#flesh)"/>' % (R, RY))
    g = ['<g clip-path="url(#%s)">' % oid,
         '<rect x="-%d" y="-%d" width="%d" height="%d" fill-opacity="0" filter="url(#fleshTex)" opacity=".55"/>' % (R, RY, 2 * R, 2 * RY)]
    # veins that harden into traces as they near the eye
    veins, circ, nodes = '', '', []
    for i in range(12 if detail else 7):
        a = 2 * math.pi * i / (12 if detail else 7) + rnd.uniform(-.2, .2)
        p = [_ell_pt(a, 1.04, R, RY)]
        for f in (.86, .7):
            p.append(_ell_pt(a + rnd.uniform(-.28, .28), f, R, RY))
        veins += smooth_open(p) + ' '
        x, y = p[-1]
        c = [(x, y)]
        for step in range(7):
            dx, dy = -x, 34 - y
            if math.hypot(dx, dy) < 88:
                break
            if step % 2 == 0 and abs(dx) > 12 and abs(dy) > 12:
                d = min(abs(dx), abs(dy)) * .5
                x += math.copysign(d, dx)
                y += math.copysign(d, dy)
            elif abs(dx) > abs(dy):
                x += math.copysign(min(abs(dx) * .5, 46), dx)
            else:
                y += math.copysign(min(abs(dy) * .5, 46), dy)
            c.append((x, y))
        circ += 'M' + ' L'.join('%s,%s' % (n(a_), n(b_)) for a_, b_ in c) + ' '
        nodes.append(c[0])
    g.append('<path d="%s" fill="none" stroke="#3a0806" stroke-width="7" opacity=".8" stroke-linecap="round"/>'
             '<path d="%s" fill="none" stroke="#8a2416" stroke-width="2.2" opacity=".75"/>' % (veins, veins))
    g.append('<path d="%s" fill="none" stroke="#ff300c" stroke-width="8" opacity=".5" filter="url(#blur6)"/>'
             '<path d="%s" fill="none" stroke="#240403" stroke-width="4.5"/>'
             '<path d="%s" fill="none" stroke="#ff9a5a" stroke-width="1.6"/>' % (circ, circ, circ))
    g.append(''.join('<circle cx="%s" cy="%s" r="4.5" fill="#1a0302" stroke="#ff7a3a" stroke-width="1.8"/>'
                     % (n(a_), n(b_)) for a_, b_ in nodes))
    # iron plates bolted/stapled into the flesh
    for (a0, a1, f0) in ((-138, -42, .8), (158, 202, .84), (-22, 22, .84), (72, 108, .86)):
        pts_out = [_ell_pt(math.radians(a0 + (a1 - a0) * t / 12), 1.06, R, RY) for t in range(13)]
        pts_in = [_ell_pt(math.radians(a1 - (a1 - a0) * t / 12), f0 + rnd.uniform(-.03, .03), R, RY)
                  for t in range(13)]
        d = 'M' + ' L'.join('%s,%s' % (n(x), n(y)) for x, y in pts_out + pts_in) + 'Z'
        g.append('<path d="%s" fill="#000" opacity=".5" filter="url(#blur6)" transform="translate(4,6)"/>'
                 '<path d="%s" fill="url(#iron)" stroke="#060403" stroke-width="2.5"/>' % (d, d))
        inner = pts_in
        g.append('<path d="M%s" fill="none" stroke="#9a8a76" stroke-width="1.6" opacity=".5"/>'
                 % ' L'.join('%s,%s' % (n(x), n(y - 2)) for x, y in inner))
        for t in range(1, 12, 2):
            x, y = inner[t]
            g.append('<circle cx="%s" cy="%s" r="3.6" fill="#8a7a66" stroke="#140e0a" stroke-width="1.5"/>'
                     % (n(x * .97), n(y * .97)))
        for t in (3, 9):  # staples across the seam
            x, y = inner[t]
            ang = math.degrees(math.atan2(y, x))
            g.append('<path d="M-9,-7 V7 M9,-7 V7 M-9,-7 H9" transform="translate(%s,%s) rotate(%s)" fill="none" '
                     'stroke="#b0a08a" stroke-width="2.6" opacity=".8"/>' % (n(x), n(y), n(ang + 90)))
    # lighting: shaft from above, blood glow from below, falloff to the right
    g.append('<ellipse cx="70" cy="120" rx="%d" ry="%d" fill="#000" opacity=".55" filter="url(#blur30)"/>' % (R, RY))
    g.append('<ellipse cx="0" cy="%d" rx="%d" ry="%d" fill="#ff2a10" opacity=".3" filter="url(#blur30)"/>'
             % (RY, R * .9, RY * .4))
    g.append('<ellipse cx="-50" cy="%d" rx="%d" ry="%d" fill="#f4ecdc" opacity=".26" filter="url(#blur14)"/>'
             % (-RY * .72, R * .5, RY * .26))
    g.append('</g>')
    o += g
    o.append('<ellipse rx="%d" ry="%d" fill="none" stroke="#070403" stroke-width="3"/>' % (R, RY))
    # the eye
    o.append('<g transform="translate(0,34)">'
             '<circle r="86" fill="#000" opacity=".45" filter="url(#blur14)"/>'
             '<circle r="66" fill="url(#iron)" stroke="#060403" stroke-width="3"/>'
             '<circle r="54" fill="#0a0303"/>'
             '<circle r="50" fill="#ff2a10" opacity=".6" filter="url(#blur6)"/>'
             '<circle r="46" fill="url(#redEye)"/>'
             '<g stroke="#1c0202" stroke-width="2" opacity=".55" fill="none">'
             '<circle r="30"/><circle r="39"/>%s</g>'
             '<circle r="120" fill="url(#bloodGlow)" style="mix-blend-mode:screen" opacity=".7"/>'
             '<ellipse cx="-17" cy="-19" rx="9" ry="5" fill="#fff" opacity=".7" transform="rotate(-35 -17 -19)"/>'
             '<path d="M-80,-2 C-70,-70 70,-70 80,-2 C60,-14 34,-16 0,-16 C-34,-16 -60,-14 -80,-2 Z" '
             'fill="url(#lid)" stroke="#0a0504" stroke-width="2.5"/>'
             '<path d="M-60,-36 C-36,-54 36,-54 60,-36" fill="none" stroke="#2a1410" stroke-width="3" opacity=".7"/>'
             '<path d="M-54,-15 C-30,-10 30,-10 54,-15" fill="none" stroke="#ff5a2a" stroke-width="2" opacity=".5"/>'
             '<path d="M-74,34 C-60,76 60,76 74,34 C56,56 -56,56 -74,34 Z" fill="url(#lid)" stroke="#0a0504" '
             'stroke-width="2" opacity=".9"/>'
             '</g>' % ''.join('<path d="M%s,%s L%s,%s"/>' % (n(math.cos(a) * 18), n(math.sin(a) * 18),
                                                             n(math.cos(a + .5) * 46), n(math.sin(a + .5) * 46))
                              for a in [i * math.pi / 3 for i in range(6)]))
    # vertebrae above and below the eye
    sp = []
    for i in range(5):
        y = -206 + i * 30
        w = 58 - i * 5
        sp.append((y, w))
    for i in range(2):
        sp.append((116 + i * 30, 34 - i * 6))
    for y, w in sp:
        o.append('<rect x="%s" y="%s" width="%s" height="%s" rx="7" fill="#ff2a10" opacity=".35" filter="url(#blur3)"/>'
                 '<path d="M%s,%s h%s l-6,24 h-%s z" fill="url(#iron)" stroke="#060403" stroke-width="2.5"/>'
                 '<path d="M%s,%s h%s" stroke="#a89680" stroke-width="1.6" opacity=".55"/>'
                 % (n(-w / 2), n(y + 20), n(w), n(8), n(-w / 2), n(y), n(w), n(w - 12), n(-w / 2 + 3), n(y + 2),
                    n(w - 6)))
    # upper claws gripping the flanks
    for sx in (-1, 1):
        o.append('<g transform="scale(%d,1)">%s</g>' % (sx, iron_limb(
            [(-64, -206), (-170, -170), (-224, -66), (-208, 50), (-164, 118)], [52, 46, 38, 30, 24], bend=.12)))
    # spiked collar and a blood crystal
    spikes = ''.join('<path d="M%s,-222 L%s,%s L%s,-222 Z"/>' % (n(x - 9), n(x * 1.25), n(-268 + abs(x) * .35), n(x + 9))
                     for x in (-60, -36, 36, 60))
    o.append('<g fill="url(#talon)" stroke="#060403" stroke-width="2">%s</g>' % spikes)
    o.append('<ellipse cx="0" cy="-212" rx="74" ry="24" fill="url(#iron)" stroke="#060403" stroke-width="3"/>'
             '<ellipse cx="0" cy="-216" rx="58" ry="13" fill="#140606" stroke="#ff3a14" stroke-width="2" '
             'stroke-opacity=".7"/>')
    o.append('<circle cy="-310" r="120" fill="url(#bloodGlow)" style="mix-blend-mode:screen"/>')
    facets = [(((0, -432), (-50, -318), (-6, -330)), '#b0202a'), (((0, -432), (-6, -330), (46, -316)), '#7a0e18'),
              (((-50, -318), (-6, -330), (0, -222)), '#5a0810'), (((-6, -330), (46, -316), (0, -222)), '#2e0408'),
              (((0, -432), (-50, -318), (-30, -380)), '#d8505a')]
    o.append('<path d="M0,-432 L46,-316 L0,-222 L-50,-318 Z" fill="#060202" stroke="#060202" stroke-width="6" '
             'stroke-linejoin="round"/>')
    for (a, b, c), f in facets:
        o.append('<path d="M%s,%s L%s,%s L%s,%s Z" fill="%s"/>' % (*[n(v) for v in (*a, *b, *c)], f))
    o.append('<ellipse cx="-4" cy="-300" rx="18" ry="34" fill="#ff4a2a" opacity=".55" filter="url(#blur6)"/>')
    o.append('<path d="M-30,-352 L-10,-404" stroke="#ffd8d0" stroke-width="3" stroke-linecap="round" opacity=".7"/>')
    o.append('</g>')
    return '\n'.join(o)


# --------------------------------------------------------------------------- pieces

TAGLINE = 'EVIL IS AUGMENTED'
FOOTER = 'An open-source Dungeon Keeper engine, based on KeeperFX  ·  Requires the original Dungeon Keeper'


def tagline(cx, y, size):
    d = CINZEL_BOLD.text(TAGLINE, size, cx, y, 'middle', .22)
    rule_w = CINZEL_BOLD.width(TAGLINE, size, .22) / 2 + size * 1.2
    return ('<path d="M%s,%s H%s M%s,%s H%s" stroke="#a0200e" stroke-width="%s" opacity=".6"/>'
            '<path d="%s" fill="#000" opacity=".8" transform="translate(2,3)"/><path d="%s" fill="#c4a882"/>'
            % (n(cx - rule_w - size * 3), n(y - size * .35), n(cx - rule_w), n(cx + rule_w), n(y - size * .35),
               n(cx + rule_w + size * 3), n(size / 14), d, d))


def footer(cx, y, size, col='#a8927a'):
    d = LATO.text(FOOTER, size, cx, y, 'middle', .02)
    return '<path d="%s" fill="#000" opacity=".9" transform="translate(1.5,2)"/><path d="%s" fill="%s"/>' % (d, d, col)


def icon_large(uid='i'):
    """512 design, used from 48 px up."""
    return svg(512, 512, '<circle cx="256" cy="300" r="250" fill="url(#bloodGlow)" opacity=".7"/>'
               + dread_heart(256, 346, .76, uid, legs=False, glow=False, detail=False))


def icon_small(px):
    """Hand-simplified heart for 16-32 px: orb, eye, gem and two claws on a 32-unit grid."""
    thin = px <= 16
    cw = 3.2 if thin else 2.6
    body = [
        '<radialGradient id="sOrb" cx=".4" cy=".3" r=".8"><stop offset="0" stop-color="#eadcc2"/>'
        '<stop offset=".55" stop-color="#9a826a"/><stop offset="1" stop-color="#3a2218"/></radialGradient>',
        '<radialGradient id="sEye"><stop offset="0" stop-color="#fff2d0"/><stop offset=".3" stop-color="#ff4a1a"/>'
        '<stop offset="1" stop-color="#6a0404"/></radialGradient>',
        # claws around the orb
        '<path d="M11,8.5 C4.5,10 2.5,17 4.2,23 L6.6,29 M21,8.5 C27.5,10 29.5,17 27.8,23 L25.4,29" fill="none" '
        'stroke="#0c0806" stroke-width="%s" stroke-linecap="round"/>' % n(cw + 1.6),
        '<path d="M11,8.5 C4.5,10 2.5,17 4.2,23 L6.6,29 M21,8.5 C27.5,10 29.5,17 27.8,23 L25.4,29" fill="none" '
        'stroke="#5a4c40" stroke-width="%s" stroke-linecap="round"/>' % n(cw),
        # orb
        '<circle cx="16" cy="19.5" r="10.2" fill="url(#sOrb)" stroke="#0c0605" stroke-width="1.3"/>',
    ]
    if not thin:
        body.append('<path d="M8,15 H11 L13,17 M24,14 H21 L19.5,15.5 M9,25 L12,23.5" fill="none" stroke="#c0200e" '
                    'stroke-width=".9" stroke-linecap="round"/>')
        body.append('<circle cx="4.4" cy="16" r="1.1" fill="#ff3a14"/><circle cx="27.6" cy="16" r="1.1" fill="#ff3a14"/>')
    body += [
        # eye: bezel, lens, heavy lid
        '<circle cx="16" cy="21" r="%s" fill="#1a1210" stroke="#0a0605" stroke-width=".8"/>' % ('5.6' if thin else '5.2'),
        '<circle cx="16" cy="21" r="%s" fill="url(#sEye)"/>' % ('4.2' if thin else '3.8'),
        '<path d="M%s,20 C%s,14.4 %s,14.4 %s,20 C19,18.6 13,18.6 %s,20 Z" fill="#6a5040" stroke="#0a0605" '
        'stroke-width=".8"/>' % ((n(9.6), n(11), n(21), n(22.4), n(9.6)) if thin else (n(10.2), n(11.6), n(20.4), n(21.8), n(10.2))),
        '<circle cx="16" cy="21.6" r="1.1" fill="#fff4d8"/>',
        # collar and gem
        '<ellipse cx="16" cy="9.6" rx="4.6" ry="1.6" fill="#3a2e26" stroke="#0a0605" stroke-width=".8"/>',
        '<path d="M16,0.6 L19.6,6 L16,9.8 L12.4,6 Z" fill="#8a0e18" stroke="#1a0204" stroke-width=".8" '
        'stroke-linejoin="round"/>',
        '<path d="M16,0.6 L12.4,6 L16,5.2 Z" fill="#e0505a"/>',
    ]
    inner = ''.join(body)
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 32 32">%s</svg>\n'
            % (px, px, inner))


def splash(W=1280, H=960, uid='sp'):
    """Start-up splash; the scene is laid out for 4:3 and widens by extending the chamber."""
    rnd = random.Random(3)
    k = H / 960
    cx = W / 2
    floor_y = 720 * k
    o = [rough_wall(W, floor_y, 9, (58, 48, 40), row_h=72 * k, var=12),
         '<rect width="%s" height="%s" fill-opacity="0" filter="url(#mottle)"/>' % (n(W), n(floor_y)),
         '<rect width="%s" height="%s" fill="#000" opacity=".35"/>' % (n(W), n(floor_y)),
         perspective_floor(W, H, floor_y, (cx, 340 * k), '#1c150f', line='#060403', cols=int(16 * W / (H * 4 / 3))),
         '<rect width="%s" height="%s" fill-opacity="0" filter="url(#grit)"/>' % (n(W), n(H)),
         '<rect y="%s" width="%s" height="%s" fill="#000" opacity=".7" filter="url(#blur6)"/>'
         % (n(floor_y - 10 * k), n(W), n(30 * k)),
         blood_cracks(W, H, cx, floor_y + 130 * k, rnd, 12 if W / H < 1.5 else 16, k),
         coins(rnd, (cx - 600 * k, 800 * k, cx - 310 * k, 950 * k), 26),
         coins(rnd, (cx + 320 * k, 790 * k, cx + 600 * k, 950 * k), 22)]
    if W / H > 1.5:
        o.append(coins(rnd, (0, 820 * k, cx - 640 * k, 960 * k), 16))
        o.append(coins(rnd, (cx + 640 * k, 820 * k, W, 960 * k), 14))
    # pillars framing the chamber (at the frame edge in 4:3, inside it when wider)
    for px_, flip in ((cx - 575 * k, 1), (cx + 575 * k, -1)):
        o.append('<g transform="translate(%s,0) scale(%s,%s)"><rect x="-65" width="130" height="%s" '
                 'fill="url(#pillar)"/><rect x="-75" y="%s" width="150" height="26" fill="url(#pillar)"/></g>'
                 % (n(px_), n(k * flip), n(k), n(H / k), n(floor_y / k - 30)))
    for tx in (cx - 444 * k, cx + 444 * k):
        o.append(torch(tx, 430 * k, .85 * k))
    if W / H > 1.5:
        for tx in (cx - 760 * k, cx + 760 * k):
            o.append(torch(tx, 430 * k, .75 * k))
    o.append(light_shaft((cx - 120 * k, cx + 120 * k), (cx - 260 * k, cx + 260 * k), -40 * k, floor_y + 160 * k))
    o.append('<ellipse cx="%s" cy="%s" rx="%s" ry="%s" fill="#000" opacity=".7" filter="url(#blur14)"/>'
             % (n(cx), n(floor_y + 140 * k), n(300 * k), n(34 * k)))
    o.append(dread_heart(cx, 600 * k, .8 * k, uid))
    o.append(motes(rnd, (cx - 210 * k, 0, cx + 220 * k, 700 * k), 70))
    o.append(embers(rnd, (cx - 240 * k, 300 * k, cx + 240 * k, 860 * k), 26))
    o.append('<rect width="%s" height="%s" fill="url(#vignette)"/>' % (n(W), n(H)))
    o.append('<rect width="%s" height="%s" fill="#000" opacity=".45" filter="url(#blur30)"/>' % (n(W), n(240 * k)))
    wm, sz = wordmark(cx, 165 * k, 140 * k, uid, max_width=1110 * k)
    o.append(wm)
    o.append(tagline(cx, 248 * k, 28 * k))
    o.append('<rect y="%s" width="%s" height="%s" fill="#000" opacity=".6" filter="url(#blur14)"/>'
             % (n(H - 70 * k), n(W), n(90 * k)))
    o.append(footer(cx, H - 26 * k, 21 * k))
    return svg(W, H, '\n'.join(o), bg='#040302')


LEGAL_LINES = [
    'Free software under the GNU General Public License, version 2 or later. Graphics: GPL version 3.',
    'Based on KeeperFX — github.com/dkfans/keeperfx — © the KeeperFX Team and contributors.',
    None,
    'Dungeon Keeper is a trademark of Electronic Arts. The original game was made by Bullfrog Productions.',
    'This is an unofficial fan project, not affiliated with or endorsed by Electronic Arts',
    'or Bullfrog Productions. It requires the original Dungeon Keeper game data.',
]


def legal(W=1920, H=1080, uid='lg'):
    rnd = random.Random(8)
    k = W / 1920
    o = [rough_wall(W, H, 21, (50, 42, 36), 80 * k, 8),
         '<rect width="%s" height="%s" fill-opacity="0" filter="url(#mottle)"/>' % (n(W), n(H)),
         '<rect width="%s" height="%s" fill="#000" opacity=".55"/>' % (n(W), n(H)),
         light_shaft((W * .4, W * .6), (W * .3, W * .7), -40, H * .8),
         '<circle cx="%s" cy="%s" r="%s" fill="url(#bloodGlow)" style="mix-blend-mode:screen"/>'
         % (n(W / 2), n(H * 1.05), n(W * .45)),
         blood_cracks(W, H, W / 2, H - 70 * k, rnd, 14, k),
         motes(rnd, (W * .3, 0, W * .7, H * .8), 60),
         embers(rnd, (W * .2, H * .7, W * .8, H), 16),
         '<rect width="%s" height="%s" fill="url(#vignette)" opacity=".8"/>' % (n(W), n(H))]
    wm, sz = wordmark(W / 2, H * .36, 150 * k, uid, max_width=W * .8)
    o.append(wm)
    y = H * .36 + 230 * k
    fs = 29 * k
    for line in LEGAL_LINES:
        if line is None:
            y += fs * .9
            continue
        d = LATO.text(line, fs, W / 2, y, 'middle')
        o.append('<path d="%s" fill="#000" opacity=".9" transform="translate(2,2)"/><path d="%s" fill="#dccab0"/>'
                 % (d, d))
        y += fs * 1.65
    return svg(W, H, '\n'.join(o), bg='#060405')


def banner(W=1290, H=270):
    rnd = random.Random(12)
    o = [rough_wall(W, H, 31, (56, 46, 38), 54, 10),
         '<rect width="%d" height="%d" fill-opacity="0" filter="url(#mottle)"/>' % (W, H),
         '<rect width="%d" height="%d" fill="#000" opacity=".45"/>' % (W, H),
         light_shaft((90, 210), (30, 270), -20, H + 40, 3),
         '<circle cx="150" cy="150" r="330" fill="url(#bloodGlow)" style="mix-blend-mode:screen"/>',
         '<radialGradient id="nameShade"><stop offset=".3" stop-color="#000" stop-opacity=".65"/>'
         '<stop offset="1" stop-color="#000" stop-opacity="0"/></radialGradient>'
         '<ellipse cx="%s" cy="135" rx="600" ry="150" fill="url(#nameShade)"/>'
         % n(290 + (W - 290) / 2),
         motes(rnd, (40, 0, 260, H), 30),
         embers(rnd, (20, 60, 300, H), 10)]
    icon = icon_large('bn').split('</defs>', 1)[1].rsplit('</svg>', 1)[0]  # reuse the page's defs
    o.append('<g transform="translate(24,4) scale(.5)">%s</g>' % icon)
    o.append('<rect width="%d" height="%d" fill="url(#vignette)" opacity=".7"/>' % (W, H))
    wm, sz = wordmark(290 + (W - 290) / 2, 140, 120, 'bn', max_width=W - 420)
    o.append(wm)
    d = CINZEL_BOLD.text('OPEN-SOURCE DUNGEON KEEPER ENGINE  ·  BASED ON KEEPERFX', 22, 290 + (W - 290) / 2, 226,
                         'middle', .12)
    o.append('<path d="%s" fill="#000" opacity=".9" transform="translate(2,2)"/><path d="%s" fill="#c4a882"/>'
             % (d, d))
    return svg(W, H, '\n'.join(o), bg='#060405')


# name -> svg text. make_assets.sh knows which output file and pixel size each becomes.
PIECES = {
    'icon.svg': icon_large,
    'icon-small-16.svg': lambda: icon_small(16),
    'icon-small-32.svg': lambda: icon_small(32),
    'splash.svg': lambda: splash(1280, 960),
    'splash-wide.svg': lambda: splash(1920, 1080),
    'legal.svg': lambda: legal(1280, 960),
    'legal-wide.svg': lambda: legal(1920, 1080),
    'banner.svg': banner,
}


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, fn in PIECES.items():
        data = fn()
        with open(os.path.join(OUT, name), 'w') as fh:
            fh.write(data)
        print('%-20s %7d bytes' % (name, len(data)))


if __name__ == '__main__':
    main()
