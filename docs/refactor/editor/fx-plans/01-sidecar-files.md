# FX plan 01 — sidecar files (Save As warning, Playtest, copy)

Status: **Save As warning built** (2026-09-19); Playtest and copy planned.

## Problem

KeeperFX loads more per-level files than `MapContentWriter` writes. The writer covers
`.slb .own .inf .slx .txt .tng/.tngfx .lgt/.lgtfx .apt/.aptfx .lof .lif`; the loader also picks up,
all keyed by `map%05d.`:

| File | Loaded by | Shipped |
|---|---|---|
| `map%05d.lua` | `lua_base.c` (runs in addition to the `.txt`) | 8 |
| `map%05d.<config>` — any of the 19 config files (`rules.cfg`, `creature.cfg`, `trapdoor.cfg`, `magic.cfg`, `objects.cfg`, `terrain.cfg`, `sounds.cfg`, `slabset.toml`, `effects.toml`, ...) | `load_config()` (`config.c`), applied after the campaign layer | 16 `rules.cfg` (others unverified) |
| `map%05d.tmap[ab]NNN.dat` | `engine_textures.c` | few |
| `map%05d.zip` | `custom_sprites.c` | 3 |
| a level-folder `cfg/` and `lua/` (mod-style overrides) | mod loader | 1 pack |

A save in place leaves these files untouched, so nothing is lost there. **Save As** to another level
number or folder writes the new level without them. **Playtest** does the same (it saves to a
scratch level number in the same folder), so a Lua- or rules-driven level plays differently in
Playtest than for real.

## 1. Save As warning — done

- `editor_sidecars.{h,cpp}`: `editor_find_sidecars(dir, lvnum)` lists `map<lvnum>.*` files whose
  extension is neither written by the editor nor derived by the loader (`dat clm wib wlb une` etc.);
  `editor_save_is_relocation()` says whether the target differs from the session's level or folder.
- `editor_dialogs.cpp`: pressing Save in Save As now runs sidecar check → (confirm) → overwrite check
  → save. The confirm lists up to eight files, says they will not be copied, and offers
  **Save anyway** / **Cancel**. No prompt for saves in place.
- Tests: `editor_sidecars_test.cpp` (classification, listing, relocation).

## 2. Copy option — planned

Add a third button, **Copy them too**. Copy `map<old>.<ext>` to `map<new>.<ext>` for each listed file
after the save succeeds, refusing to overwrite an existing target without a second confirm. Two
caveats to show in the dialog: a Lua or config file can name its own level number (open questions:
rewrite `map<old>` strings inside the file, or leave and warn); a per-level file the target already
has is kept.

## 3. Playtest — planned (bug, do first)

Playtest saves to `EDITOR_PLAYTEST_LEVEL_NUMBER`. Fix: before launching, copy the session level's
sidecars to the scratch number (overwriting, they are scratch), and delete them when the playtest
ends or the scratch level is next overwritten. Depends on nothing else. If Lua or config editing
lands (plans 02, 03), the editor's *unsaved* text for those files must be written to the scratch
copies too — Playtest already auto-commits unsaved map edits (decision D1).

## 4. Open questions

- Should Save (in place) warn when a sidecar was edited outside the editor since load? Probably not.
- Directory-style sidecars (`cfg/`, `lua/`) are per folder, not per level; Save As to a new folder
  should mention them. Not covered by the file-name scan.
