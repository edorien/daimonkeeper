# Phase 6, slice 2 — palette thumbnails, owner symbols, classic-HUD override

Status: **built, live-test pending.**

## 1. Thumbnails for items the game has no icon for

`editor_thumbs.h/.cpp` + a new `render_keepsprite_indexed()` in `engine_render.c`.

- **Things** (specials, decor): the object's in-world sprite. Config `sprite_anim_idx` is a
  first-person animation number; `get_td_animation_sprite()` converts it to the top-down animation
  the game view draws (the "front view" sprite), and frame 0 is decoded. The renderer's existing
  keepsprite decoder only writes a `0xFF` silhouette mask (it exists for creature shadows), so
  `sprite_to_sbuff()` gained a switch that copies the sprite's palette bytes instead, turned on
  only for the duration of one thumbnail decode. Result cropped to the drawn pixels, converted
  through the level palette. 132 of 138 non-icon objects have a sprite (missing: CHICKEN_GRW,
  CTA_ENSIGN, ROOM_FLAG, TORTURE_SPIKE, POWER_SIGHT, POWER_LIGHTNG — they stay text tiles).
- **Slabs**: the slab kind's centre column (slabset style 3, subtile 4 — stored as a *negated*
  `columns_data[]` index, which cost a debugging round) is a stack of cubes; the highest cube is
  what shows. **Solid slabs** (rock, earth, gold, gems, walls) show the cube's **front face**
  (texture slot 2); **floor slabs** (paths, water, lava, claimed floor) show the **top face**
  (the column's floor texture). The face is a 32×32 block in `block_ptrs[]`, offset by the level's
  most common texture variation, like the renderer's `slab_ext_data` offset. 31 of 33 non-room,
  non-door slabs work (missing: SLAB50 — unused — and PURPLE_PATH).
- **Door slabs** have empty columns (the door is a thing), so the Other tab shows the door's
  workshop icon for them, as the Traps & Doors tab does.
- Text tiles with a thumbnail draw the picture on the left and the name beside it; thumbnails are
  cached (texture handles are reused across sessions) and rebuilt on `editor_open()`.
- Verified against the real game data: the ftest builds every thumbnail, and a contact sheet of
  the decoded pixels was inspected — real textures (dirt, gems, gold, lava, water) and coloured
  sprites (torches, barrels, statues, flags, potions, special boxes).

## 2. Classic HUD is ignored in the editor

`GUI_ICON_PACK = CLASSIC` selects the legacy sprite HUD, which the editor's windows and full-screen
view don't work with. `ingame_gui_force_imgui_hud()` (kfx_config) makes
`ingame_gui_use_classic_hud()` report false while an editor session is open; `editor_open()` sets it
(and re-establishes the full-width engine window, since the classic HUD insets the view) and
`editor_deactivate()` clears it. The saved setting is untouched and applies again outside the
editor. (Not testable headlessly; live-test with the option set to CLASSIC.)

## 3. Owner selector uses the player symbols

The bottom bar's owner row is now six symbol tiles, the same ones the in-game query panel uses:
red / blue / green / yellow keeper symbols, the white symbol for Hero, and **Neutral flashing
through the four keeper colours** (the same trick the game uses for the neutral player's chat
icon), driven by wall-clock time so it also flashes in the paused editor. Selecting still sends
`PckA_CheatSwitchPlayer`. Icon packs override them automatically (they resolve through the static
sprite override table).

## Known limits

- Wall thumbnails use slot 2 (the renderer's "front") of the topmost cube; some wall kinds look
  alike (dirt-like) because their upper cube is. Owned-colour cubes show their base texture.
- Thumbnails use the level's dominant texture variation, not per-slab overrides.
- Rotating (creature-style) sprites aren't used; objects are non-rotating.

## Verification

Layering clean; `kfx_editor_utest` 277/56, `kfx_sim_utest` 3233/687, `kfx_config_utest` 2178/360;
production builds; ftest sweep 26/26 including the new `editor_thumbs` test. The visual layout (tile
sizes with thumbnails, owner row spacing) and the CLASSIC override need a live look.
