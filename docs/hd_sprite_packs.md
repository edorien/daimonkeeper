# HD sprite packs

dAImon Keeper can draw the things in the world (creatures, objects, effects, traps, what the power hand
holds) from higher-resolution, true-colour art: 2, 3 or 4 texels for every original texel. This document is
for people making such packs. The game ships none; you add them.

- Select an asset pack in **Options > Graphics > Asset Pack**, listed by the `name` in its `pack.toml` (its
  folder name if it has none), or **Original** for the game's own art. The pack's `sprites/` is its HD sprite pack
  (its `textures/`, if any, its HD terrain textures: docs/hd_texture_packs.md). The change applies at the next frame.
  `daimonkeeper.cfg` records the folder name (`ASSET_PACK=<folder>`, or `NONE`).
- **Mod Sprites in True Colour** (`SPRITE_TRUE_COLOUR=ON`) draws the sprites that mods add (the druid,
  maiden, time mage, traps and objects from `fxdata/*.zip`, a campaign's or map's zips) in their PNGs' own
  colours instead of the game's 256-colour palette. It needs no pack.
- **Texture Filtering** (Vulkan renderer: Nearest, Sharp, Bilinear; Trilinear is Bilinear for sprites)
  applies to HD sprites too. The Software renderer always draws them nearest.
- **Generate Missing HD Art** (`ASSET_PACK_FILL=ON`) draws every frame no pack covers -- all of them with no
  pack, mods' sprites included -- from its original enlarged x4 in the game: Lanczos smoothing, an unsharp mask
  and rounded outlines (the routine of `spritepack.py upscale --filter lanczos`, below). A frame is made the first
  time it is drawn, in the background (the original shows for a frame or two), and counts against **Asset Pack
  Memory** like a pack's. A pack's own art always comes first.

Nothing about the game changes with a pack: the simulation never sees the art, so frame counts, sizes,
timing, saves and multiplayer are as without it, and two players with different packs play together.

## How a pack attaches to the game's sprites

The original sprites are the frames in `data/creature.tab` and `creature.jty` (and a mod's or campaign's
zips). An HD frame replaces an original frame **by its content**: each frame line in a pack carries a *key*,
the hash of the exact original frame it was made from. So:

- HD art never lands on a frame it wasn't made for, and the 579 frames the game stores twice share one.
- Frames that a pack doesn't cover stay as they are.
- Each frame of each rotation and of the first-person versions is its own frame. Directions 6-8 are the
  engine's mirror images of rotations 2-4 (as in the original), so an asymmetric creature shows mirrored
  there: don't draw them.

## Layout

An HD sprite pack is one part of an *asset pack*, a folder in `fxdata/asset_packs/`:

```
fxdata/asset_packs/<pack>/
  pack.toml        # name = "My HD pack": what the options list it as
  sprites/         # an HD sprite pack (docs/hd_sprite_packs.md), if it has one
  textures/        # an HD terrain texture pack (docs/hd_texture_packs.md), if it has one
```

Its `sprites/` folder is in the layout of FXGraphics' `gfx/enginefx/sprites-*` folders, plus the keys:

```
fxdata/asset_packs/<pack>/sprites/
  pack.toml                                             # scale (below); a name here only labels its log lines
  animlist.txt                                          # the filelists the pack has (any subset), one a line
  creatures/creature_archer/walk_td/filelist_rot1.txt   # a header line, then one PNG a line
  creatures/creature_archer/walk_td/r1frame01.png       # each frame's canvas
  ...
  keys/animlist.idx                                     # generated: 16 bytes a frame line, in animlist order
```

```toml
name = "My HD creatures"
scale = 4                  # 2, 3 or 4: texels per original texel, the same for the whole pack
use_original_at_1x = true  # far away (a texel a pixel or less), draw the original frame
```

- **Canvases**: a frame's PNG is its whole canvas, `scale` times the original's `FrameWidth` x
  `FrameHeight`, with the creature where the original has it. `spritepack.py template` writes
  `FRAMES.txt` with every canvas size, the original's crop on it and the anchor (the pixel the creature's
  ground point is on).
- **Alpha** below 128 is transparent; the rest is opaque (sprites are cut out, not blended). Keep the
  silhouette close to the original's: picking, shadows and the water line follow the original frame.
- **Art past the original's crop** (a few pixels a 3D render adds) is drawn, up to 8 original texels a side,
  without moving any pixel the original draws. `spritepack.py check` reports it.
- **`use_original_at_1x = false`** for repainted art where switching to the original at far zooms would show:
  the pack's own art, reduced, is drawn all the way down instead.
- A pack without `keys/animlist.idx`, or with keys that don't match its frame lines, is ignored (with a
  warning in `daimonkeeper.log`).

## Content folders: HD art for a campaign, a map or a mod

A `sprites-hd/` folder in the same layout, next to the sprite zips the game loads (a map's folder, a
campaign's config folder, a mod's fxdata, config or levels folder), applies to the frames of that place. It
is used before the user's pack, and whether or not a pack is selected in the options; when several have the
same frame, the one loaded last wins (a mod loaded after the map over the map, as their zips do).

## Mods' sprites

A mod's zip sprites have keys too, from the frame the game makes of each PNG, so a pack or content folder can
upgrade them like originals. `spritepack.py import-zip` lays zips' sprites out as a pack, their PNGs upscaled
as the starting art, keyed the way the game keys them. None of the zip sprites shipped with the game or its
campaigns is the same as an original frame, so a pack for the originals never reaches them: give them their
own pack or content folder.

## Glows, flames and sparkles: additive art

Some effects (room and heart flames, torch glows, sparkles, the ghost) aren't drawn in colours: each texel of the
original names a step of a light ramp that is *added* to what is behind it (or, for the black ramp, subtracted).
Their HD art is additive too:

- Put `additive` as a sixth word on the filelist's header line (`animation_1234 0 8 -20 -40 additive`).
- Each frame's PNG is what it adds: RGB, 0 adds nothing, 255 the most; alpha below 128 marks texels that add
  nothing at all. An optional `<frame>_sub.png` beside it (same canvas) is what it subtracts.
- Such art is only used where the game draws the frame as a light effect; a filelist without `additive` is only
  used where it draws the frame in colours.

`spritepack.py upscale --additive-log <file>` and `sample --additive-log <file>` write additive art for the
filelists of the frames listed in `<file>`, which the game writes with `KFX_SPRITE_ALPHA_LOG=<file>` in its
environment (every frame it draws as a light effect while it runs).

## Player colours

The flags, call-to-arms and heart flames come in a variant for each player colour (`fxdata/spritecolors.toml`),
each its own frames. Paint the base (red) variant and a `<frame>_mask.png` beside each frame, white where the
player's colour is; `spritepack.py colourise` makes the other colours' frames, mapping the masked colours the
way the originals' variants differ.

## Rendering from 3D models

Art can be rendered from Blender meshes (or voxel models, or generated meshes imported into Blender) through
cameras matched to the game's:

- Model axes: **Z up, facing -Y** (Blender's front view), the ground point under the creature at the origin.
- `spritepack.py fit model.obj --anim creatures/creature_archer/walk ...` measures the model's scale (pixels a
  Blender unit), the cameras' pitch and a sub-pixel anchor nudge, by how well its silhouettes match the
  original frames (IoU, printed per view; above 0.95 on the overhead views is a good fit). The model should
  stand in the pose of the frames named (`--frames`).
- `spritepack.py blender render.py --anim ... --fit fit.json --scale 4 --renders out/` writes a Blender script:
  `blender model.blend -b -P render.py` renders the animation's overhead and first-person views, rotations 1-5,
  at the original's frame count spread over the scene's frame range (or one OBJ a frame with `--mesh`).
- `spritepack.py from-renders out/ <pack> --anim ...` puts the renders in a pack, named and keyed.

## The tool: `scripts/spritepack.py`

Python 3, plus Pillow (and numpy for `sample`, `fit` and `colourise`). In the source repository. `--data` is the
game's `data` folder, `--ref` FXGraphics' `gfx/enginefx/sprites-32` (its names and order are the game's).

| Command | Does |
|---|---|
| `template <out> --data --ref --scale N [--only <folder>]` | An empty pack: `pack.toml`, animlist, filelists, keys and `FRAMES.txt` (canvas, crop and anchor of every frame). No images |
| `keys <pack> --data --ref` | Writes `keys/animlist.idx` from the frames the art was made against. Rerun it if they change |
| `check <pack> --data --ref` | What the game would skip or mis-attach (no `pack.toml`, wrong scale, canvases not `scale` x the original's, missing or stale keys, missing PNGs), and art to look at: a silhouette unlike the original's, art past the crop |
| `dump <out> --data [--ref]` | The original frames as canvas PNGs, with `frames.csv` (sizes, crops, keys, duplicates) |
| `upscale <out> --data --ref [--factor 4] [--additive-log <file>]` | A test pack from the originals themselves (nearest neighbour; light effects as additive art) |
| `sample <out> --big gfx/enginefx/sprites-128 --data --ref` | A x4 pack from FXGraphics' 128 set, frames whose 128 art isn't the original's left out. A local aid for looking at the result; mind FXGraphics' licence before passing it on |
| `import-zip <out> <zip>... --data [--scale 4]` | Mods' zip sprites as a pack |
| `colourise <pack> --data --ref --spritecolors fxdata/spritecolors.toml --objects fxdata/objects.cfg --zips fxdata/colored_sprites.zip` | Player-colour variants from the base variant's art and masks |
| `fit`, `blender`, `from-renders` | Rendering from 3D models (above) |

## Checking in the game

`daimonkeeper.log` gets a line per pack or content folder opened (`HD sprites: pack "..." (name): x4, 9165
frame lines, 8564 entries indexed`) and warns about art it can't use. A whole x4 pack takes about 1.9 GiB
and loads in about a second in the background; a frame not loaded yet is drawn as the original meanwhile.
Over the memory **Options > Graphics > Asset Pack Memory** allows (`ASSET_PACK_MEMORY`: Auto, a quarter of the
computer's memory and at least 3 GiB, or 1 to 32 GiB; the sprites get what the terrain textures don't use, at least
half), the frames drawn least recently are dropped and loaded again when needed. With the Vulkan renderer the HD
sprite atlas in video memory grows up to the same amount.

For testing, in the environment: `KFX_SPRITE_HD_PARITY=1` draws each HD frame from the middle texel of each
original texel (an `upscale` pack then draws exactly the originals); `KFX_SPRITE_HD_SYNC=1` loads the whole
pack before drawing.

## Shadows

With **Shadow Style** set to Soft (`SHADOW_STYLE=SOFT`, the default; Hard draws the classic sharp silhouettes), creature
shadows are soft: with a pack loaded their outline comes from your art's alpha (at the pack's scale), blurred by about an
original texel, and the floor under them darkens gradually rather than in hard steps. A frame your pack doesn't cover
(and every frame, with no pack) casts its original silhouette, softened the same way. The shadow keeps the original's place,
size and darkness, so give your art the same silhouette and the shadow will match it.
