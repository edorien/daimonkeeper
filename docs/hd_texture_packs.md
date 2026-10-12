# HD terrain texture packs

dAImon Keeper can draw the terrain textures (walls, floors, rock, liquids) from higher-resolution,
true-colour art: 64, 128 or 256 texels a block instead of the original 32. This document is for
people making such packs. The game ships none; you add them.

- Select an asset pack in **Options > Graphics > Asset Pack**, listed by the `name` in its `pack.toml`
  (its folder name if it has none), or **Original** for the game's own art. The pack's `textures/` is its HD
  terrain texture pack (its `sprites/`, if any, its HD sprites: docs/hd_sprite_packs.md). The change applies
  immediately. `daimonkeeper.cfg` records the folder name (`ASSET_PACK=<folder>`, or `NONE`).
- **Texture Filtering** (Vulkan renderer: Nearest, Sharp, Bilinear, Trilinear) and **Texture Mipmaps**
  (smaller versions for far zooms, both renderers) work with or without a pack.
- **Generate Missing HD Art** (`ASSET_PACK_FILL=ON`) gives every block no pack covers -- all of them with no
  pack, a mod's or campaign's own tmap files included -- 128-texel art made in the game from the 32x32 block:
  Lanczos smoothing and an unsharp mask, each block filtered with its neighbour in the tmap file where the picture
  runs on into it, else with its own opposite edge (the routine of `texpack.py upscale --filter lanczos`). It is
  made when a level loads (under a second for the base game's blocks) and counts against **Asset Pack Memory**;
  a pack's own art always comes first.

## How a pack attaches to the game's textures

The game's terrain textures are the 32x32 blocks in `data/tmapaNNN.dat` and `tmapbNNN.dat` (and a
campaign's, map's or mod's own copies). An HD block replaces a 32x32 block **by its content**: each
filelist block in a pack carries a *key*, the hash of the exact 32x32 block it was made from. So:

- HD art never lands on a block it wasn't made for. A campaign that ships its own tmap files gets HD
  only on the blocks it copied unchanged from the art your pack was made against.
- Blocks that a pack doesn't cover, or whose art doesn't match a key, stay at 32x32.
- Single-colour blocks (fillers) never get HD.

## Layout

An HD terrain texture pack is one part of an *asset pack*, a folder in `fxdata/asset_packs/`:

```
fxdata/asset_packs/<pack>/
  pack.toml        # name = "My HD pack": what the options list it as
  sprites/         # an HD sprite pack (docs/hd_sprite_packs.md), if it has one
  textures/        # an HD terrain texture pack (docs/hd_texture_packs.md), if it has one
```

Its `textures/` folder is in the layout of FXGraphics' `gfx/enginefx/textures-*` folders, plus the keys:

```
fxdata/asset_packs/<pack>/textures/
  pack.toml                      # size, flags (below); a name here only labels its log lines
  filelist_tmapa005.txt          # header "textures_pack_005 8 68 128 128", then one
                                 #   "pack005/<sheet>.png x y w h" line per block, in block order
  pack005/earth_standard.png     # the sheets, RGB or RGBA PNG, at the pack's size
  pack005/...
  keys/tmapa005.idx              # generated: one 8-byte key per filelist block (0: none)
```

```toml
name = "My HD textures"
size = 128                 # 64, 128 or 256: the block size, the same for the whole pack
use_original_at_32 = true  # draw the original block at 32x32 and below (art derived from the originals)
```

- **Filelists**: line *i* (after the header) is block *i* of that tmap file. Tabs or spaces, with or
  without a byte-order mark. A pack may have any subset of the filelists, and a filelist may name sheets
  that don't exist yet: their blocks stay 32x32. Every crop rectangle must be `size` x `size`.
- **Sheets**: named as in FXGraphics' `textures-32` set; `scripts/texpack.py template` writes the list.
- **Alpha** below 128 draws black, as texture index 0 does in the originals. Terrain is opaque.
- **`use_original_at_32 = false`** for repainted art where switching to the original at far zooms
  would show: the pack's own art, reduced, is drawn all the way down instead.
- A filelist without its `keys/*.idx` is ignored (with a warning in `daimonkeeper.log`).

## Content folders: HD art for a campaign or a map

A `textures-hd/` folder in the same layout, next to a tmap file, applies to the blocks of that file:
in a map's folder for its map-specific `mapNNNNN.tmapaNNN.dat`, in a campaign's config folder for its
own tmap files, in a mod's folder. It is used before the user's pack, and whether or not a pack is
selected in the options.

## The tool: `scripts/texpack.py`

Python 3, stdlib, plus Pillow for `sample`, `upscale` and `check`'s art comparison. In the source
repository.

```
texpack.py template <out> --ref gfx/enginefx/textures-32 --data <folder with tmap*.dat> [--size 128]
```
An empty pack to paint: `pack.toml`, every filelist at your size, the keys, and `SHEETS.txt` listing
each sheet's path, pixel size and how many blocks it holds. No images (a blank one would draw black).

```
texpack.py keys <pack> --data <folder with tmap*.dat>
```
Writes `keys/*.idx` from the 32x32 files the art was made against. Rerun it if those files change.
Use a campaign's own tmap files for a campaign's content folder.

```
texpack.py check <pack> --data <dat folder> --ref gfx/enginefx/textures-32 --palette data/palette.dat [--review-png out.png]
```
Reports what the game would skip or mis-attach (missing `pack.toml`, wrong sizes, crops off a sheet,
filelists without keys, stale keys, sheet names unlike the 32 set's) and lists blocks whose art doesn't
look like their 32x32 block, a likely wrong sheet or position (`--review-png` draws them side by side).

```
texpack.py sample <out> --big gfx/enginefx/textures-128 --ref gfx/enginefx/textures-32 --data <dat folder>
```
A pack from FXGraphics' 128 set, laid out and named as the 32 set (its own filelists name older sheets
and point packs 1-7 at pack000's art). Blocks whose 128 art isn't the 32 art's are left out. A local aid
for looking at the result; mind FXGraphics' licence before passing it on.

```
texpack.py upscale <out> --data <dat folder> --ref gfx/enginefx/textures-32 --palette data/palette.dat [--factor 4]
```
A test pack made from the 32x32 data itself (nearest-neighbour). It looks like the originals; with
`KFX_TEXTURE_HD_PARITY=1` in the environment the game must draw exactly what it draws without HD.

## Checking in the game

`daimonkeeper.log` gets a line per tmap file that got HD (`HD textures: tmapa005 (...): 412/543
blocks`) and a total with memory and load time. Terrain textures may take a quarter of **Options > Graphics >
Asset Pack Memory** (`ASSET_PACK_MEMORY`; with Auto at least 768 MiB: a whole 128 pack fits, a whole 256 pack
(2.3 GB) needs 12 GiB set); over it, packs keep HD on the texture variations the level uses.
