# dAImon Keeper branding

Source of the icon, start-up screens and README banner. Everything is generated:
edit `gen_branding.py`, never the SVGs or PNGs by hand.

```bash
python3 res/branding/make_assets.py
```

regenerates `svg/*.svg` and installs the rasterised files:

| Output | Size | Used by |
|---|---|---|
| `res/daimonkeeper_icon.ico` | 16-256, 32-bit | Windows executable (`res/daimonkeeper_stdres.rc`) |
| `res/daimonkeeper_icon{016..256}.png` | | `256` is the Linux window icon (`src/kfx_platform/CMakeLists.txt`) |
| `config/fxdata/daimonkeeper/splash.png`, `splash-wide.png` | 1920x1440, 2560x1440 | start-up splash, 4:3 and wider screens (`front_simple.c`) |
| `config/fxdata/daimonkeeper/legal.png`, `legal-wide.png` | 1920x1440, 2560x1440 | legal screen |
| `docs/assets/readme-banner.png` | 2580x540 | `README.md` |

The start-up screens are loaded as 32-bit PNGs and scaled to the screen; they ship in
`fxdata/daimonkeeper/` (`build/make/package.mk`), so installing over a KeeperFX folder
doesn't replace its own `data/*.raw` screens. The legal screen falls back to those if the
PNG is missing; the splash doesn't (KeeperFX's is KeeperFX-branded).

16-32 px icons are a separate hand-simplified drawing (`icon_small()`); 48 px and up
downscale the full one.

Needs Python with fontTools and Pillow, the Lato font (`fonts-lato`) to regenerate
text, and a headless Chromium/Chrome to rasterise (`CHROME=/path/to/chrome` if it isn't
on `PATH`). The wordmark uses Cinzel from `config/fxdata/font/`.

Licence: GPLv3, like the game's other graphics (dkfans/FXGraphics, cloned into `gfx/`). Fonts: SIL OFL
(Cinzel, Lato) -- the text is outlined into the artwork, no font files are embedded.
