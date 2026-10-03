#!/usr/bin/env python3
"""Regenerate the branding SVGs and install the rasterised assets into the tree.

    python3 res/branding/make_assets.py

Needs fontTools + Pillow, and a Chromium/Chrome for rasterising (headless; set CHROME to
its path if it isn't on PATH). Writes:

    res/daimonkeeper_icon{016..256}.png, res/daimonkeeper_icon.ico   window/exe icon
    config/fxdata/daimonkeeper/{splash,splash-wide,legal,legal-wide}.png   start-up screens
    docs/assets/readme-banner.png                                         README banner

The .ico holds every size as 32-bit PNG (Windows Vista and later), which png2ico can't
write -- it reduces to 256 colours with a 1-bit mask.
"""
import io
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
SVG = os.path.join(HERE, 'svg')

# svg -> (output path, device scale factor); the svg's own width/height times the scale is the pixel size
SCREENS = {
    'splash.svg': ('config/fxdata/daimonkeeper/splash.png', 1.5),            # 1920x1440
    'splash-wide.svg': ('config/fxdata/daimonkeeper/splash-wide.png', 4 / 3),  # 2560x1440
    'legal.svg': ('config/fxdata/daimonkeeper/legal.png', 1.5),
    'legal-wide.svg': ('config/fxdata/daimonkeeper/legal-wide.png', 4 / 3),
    'banner.svg': ('docs/assets/readme-banner.png', 2),                       # 2580x540
}
ICON_SIZES = (16, 24, 32, 48, 64, 128, 256)


def find_chrome():
    c = os.environ.get('CHROME')
    if c:
        return c
    for name in ('chromium', 'chromium-browser', 'google-chrome', 'google-chrome-stable', 'chrome'):
        p = shutil.which(name)
        if p:
            return p
    npm = '/usr/local/lib/node_modules/chromium/lib/chromium/chrome-linux/chrome'
    if os.path.exists(npm):
        return npm
    sys.exit('no Chromium found; set CHROME=/path/to/chrome')


def render(chrome, svg_name, scale, tmp):
    path = os.path.join(SVG, svg_name)
    head = open(path).read(400)
    w, h = (int(float(v)) for v in re.search(r'width="([\d.]+)" height="([\d.]+)"', head).groups())
    out = os.path.join(tmp, svg_name + '.png')
    subprocess.run([chrome, '--headless=new', '--no-sandbox', '--disable-gpu', '--hide-scrollbars',
                    '--default-background-color=00000000', '--force-device-scale-factor=%g' % scale,
                    '--window-size=%d,%d' % (w, h), '--screenshot=' + out, 'file://' + path],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    img = Image.open(out)
    img.load()
    want = (round(w * scale), round(h * scale))
    if img.size != want:
        sys.exit('%s rendered at %s, expected %s' % (svg_name, img.size, want))
    return img


def write_ico(path, images):
    """ICO with one PNG-compressed 32-bit entry per image."""
    blobs = []
    for img in images:
        b = io.BytesIO()
        img.save(b, 'PNG', optimize=True)
        blobs.append(b.getvalue())
    offset = 6 + 16 * len(images)
    hdr = struct.pack('<HHH', 0, 1, len(images))
    entries = b''
    for img, blob in zip(images, blobs):
        w, h = img.size
        entries += struct.pack('<BBBBHHII', w % 256, h % 256, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    with open(path, 'wb') as fh:
        fh.write(hdr + entries + b''.join(blobs))


def main():
    subprocess.run([sys.executable, os.path.join(HERE, 'gen_branding.py')], check=True)
    chrome = find_chrome()
    with tempfile.TemporaryDirectory() as tmp:
        for name, (dst, scale) in SCREENS.items():
            img = render(chrome, name, scale, tmp).convert('RGB')
            full = os.path.join(ROOT, dst)
            os.makedirs(os.path.dirname(full), exist_ok=True)
            img.save(full, 'PNG', optimize=True)
            print('%-45s %dx%d  %d KiB' % (dst, img.width, img.height, os.path.getsize(full) // 1024))
        large = render(chrome, 'icon.svg', 1, tmp).convert('RGBA')
        small = {16: render(chrome, 'icon-small-16.svg', 8, tmp).convert('RGBA'),
                 32: render(chrome, 'icon-small-32.svg', 4, tmp).convert('RGBA')}
    icons = []
    for px in ICON_SIZES:
        src = small[16] if px == 16 else small[32] if px <= 32 else large
        img = src.resize((px, px), Image.LANCZOS)
        icons.append(img)
        dst = 'res/daimonkeeper_icon%03d.png' % px
        img.save(os.path.join(ROOT, dst), 'PNG', optimize=True)
        print(dst)
    write_ico(os.path.join(ROOT, 'res/daimonkeeper_icon.ico'), icons[::-1])
    print('res/daimonkeeper_icon.ico')


if __name__ == '__main__':
    main()
