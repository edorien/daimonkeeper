# Phase 3, slice 4 — native file dialogs + real Open Map addressing

Status: **implementation done, pending a live click-through pass by the user.** Follow-on from
slice 3 ([`02-slice3-dialogs-menubar.md`](02-slice3-dialogs-menubar.md)), driven by live-testing
feedback on that slice: Save As needed a real destination picker, Open needed to reach maps
outside the currently-active campaign, and the Esc-equivalent hub (`editor_menu_frame()`) was
redundant once every item it carried had a home in the new menu bar.

## What prompted this

- **Save As** only ever wrote to the session's own fixed save directory — no way to choose where.
- **Open Map** only listed the *currently active* campaign's own levels
  (`campaign.single_levels`/`freeplay_levels`) — there was no way to reach a level belonging to a
  *different* installed campaign, mappack, or (initially assumed, wrongly) an arbitrary folder.
- **Investigation correction, mid-slice**: the first attempt at this assumed opening a map from
  outside the active campaign would need staging/copying into a scratch level slot, since
  `load_map_file()`'s real loader only knows `(fgroup, lvnum)`, not an arbitrary path. That's
  wrong — KeeperFX already has the real mechanism: `campaign.levels_location` (what
  `FGrp_CmpgLvls` resolves to) changes with whichever campaign/mappack/multiplayer-pack is
  *active*, and `change_campaign(pack, cmpgn_fname)` (`config_campaigns.c:1189`) is the existing,
  full function that switches it — the same one the main menu's own Campaign Select screen uses.
  Every installed campaign (`campgns/*.cfg`), mappack (`levels/*.cfg`), and multiplayer pack
  (`multiplayer/*.cfg`) is already scanned at startup into `campaigns_list`/`mappacks_list`/
  `mp_mappacks_list`, each entry carrying its own `display_name`/`fname` without needing full
  activation. **No copying or scratch slot needed at all** — this was a real, missed part of the
  existing mechanism, not a gap that needed inventing.
- `change_campaign()` also accepts a bare filename (or a `"campgns/"`/`"levels/"`/`"multiplayer/"`
  -prefixed one) and resolves it against those same three lists itself
  (`prepare_campaign_file_name()`/`is_campaign_in_list()`) — so browsing to any already-installed
  `.cfg` and passing just its basename is enough; no separate path-to-pack resolution logic
  needed in this slice's own code either.

## What shipped

- **`deps/tinyfiledialogs`** (new, vendored source, zlib license) — native OS folder/file pickers.
  No build-time GUI-toolkit dependency (shells out to `zenity`/`kdialog`/etc. via `popen` on
  Linux at runtime, native common dialogs on Windows via `comdlg32`/`shell32`). Wired into
  `Dependencies.cmake`/`CMakeLists.txt` the same OBJECT-library-with-explicit-final-link shape as
  `centitoml`.
- **`src/kfx_platform/include/bflib_filedialogs.h`+`.cpp`** (new) — thin wrapper:
  `platform_pick_folder_dialog()`/`platform_pick_open_file_dialog()`.
- **Save As** (`editor_dialogs.cpp`): a "Browse..." button opens a native folder picker; the
  chosen directory is used directly for `editor_save_map()`'s `dir` parameter (which was already
  a plain filesystem path with no campaign/lvnum resolution on the write side — the picker just
  fills it in instead of only ever using the session's fixed save dir). A "Level Number" field
  lets the level number itself be set too — the writer names every file `map%05lu.<ext>` off
  this number, so it *is* the filename in this format (no independent free-text name to set).
  Saving under a different lvnum/dir than the session opened with adopts it as the session's own
  identity from then on (`editor_set_current_lvnum_and_dir()`, `editor_session.cpp`), standard
  "Save As" semantics — a plain File > Save afterward writes to the new location too. `dir` and
  `lvnum` are independent axes here: `get_level_fgroup()` is hardcoded to always return
  `FGrp_CmpgLvls` regardless of lvnum, so within one active campaign every lvnum already resolves
  to the same directory — there's no risk of them drifting out of sync with each other.
- **Open Map** (`editor_dialogs.cpp`): a "Browse for Campaign/Mappack..." button opens a native
  file picker filtered to `*.cfg`; the picked file's basename is passed to `change_campaign()`.
  On success, the *same* level list the dialog already shows just re-renders against the newly
  active campaign next frame — no separate "step" or extra state needed, since it always reads
  the live `campaign` global. On failure (an unrecognized `.cfg` — not yet scanned into any of
  the three lists), a small error modal reports it rather than failing silently.
- **Format badge per level row** — `level_is_kfx_native_format()` checks for `map%05u.lgtfx`
  (KFX-native) vs its absence (classic) and shows "(KFX)"/"(classic)" next to each row. Purely
  informational: `load_level_file()` (`lvl_filesdk1.c`) already auto-detects this itself per
  level when actually loading (tries `.lgtfx` first, falls back to `.lgt`) — this doesn't feed
  back into that decision, it just tells the user what they're about to open.
- **Save As: level number and level name, set independently** (live-testing follow-up) —
  `editor_save_map()` gained a `level_name` parameter, written into `.lof`'s `NAME_TEXT`
  (`MapContentWriter::write_level_info()`, already fully implemented for KFX-native saves —
  `ClassicMapContentWriter`'s own override was a no-op; since `.lof` naming is genuinely
  format-independent, that method moved up to be a concrete base-class method instead of two
  near-duplicate overrides, fixing classic saves too as a side effect). Save As shows a "Level
  Number" field (the writer's own `map%05lu.<ext>` naming — there's no independent filename in
  this format, the number *is* it) and a separate "Level Name" text field. Saving under a
  different lvnum/dir/name than the session opened with adopts all three as the session's own
  identity (`editor_set_current_lvnum_and_dir()`/`editor_set_current_level_name()`), so a later
  plain File > Save reuses them rather than blanking the name back out. `editor_open()` does a
  best-effort initial read of the name via `get_level_info(lvnum)` (empty for a level with no
  `.lof` yet).
- **Esc-equivalent hub retired** (`editor_menu_frame()`/`editor_open_menu()`/
  `s_show_editor_menu`, `editor_session.cpp`) — every item it carried now has a real home: Save/
  Save As/Exit in the File menu, Preview Motion in the View menu, and Playtest (still a disabled
  stub) moved into the File menu too. The redundant "File > Editor Menu..." item that briefly
  existed to reach it is gone.

## Known, pre-existing architectural wart (not fixed here)

The requirement that every loadable level ultimately live under a *registered* campaign/mappack/
multiplayer-pack `.cfg` — because that `.cfg` is also where the newer per-campaign creature/
resource config lives — means a genuinely unregistered directory (one with map files but no
`.cfg` anywhere recognized, or a `.cfg` not yet scanned into the in-memory lists this session)
still can't be opened through this dialog. Browsing to such a `.cfg` reports a clear error rather
than pretending to work. Making arbitrary, unregistered directories fully openable would mean
wiring the already-existing but currently test-only `MapContentReader` (phase 3 slices 1/2) into
a new "populate a live session directly from a `MapContent`" path, bypassing `load_map_file()`'s
`(fgroup, lvnum)` addressing entirely — a materially bigger capability, not attempted this slice.

## Tests

The dialog/native-picker surface itself has no new automated coverage (`platform_pick_*` wrappers
around a vendored library, `change_campaign()`'s own already-tested resolution,
`level_is_kfx_native_format()`'s trivial file-existence check) — a live click-through is still the
real verification for that part. The level-name plumbing did get real coverage:
`ftest_editor_save_reload.c` now saves under `TEST_LEVEL_NAME` (both formats) and asserts
`get_level_info(lvnum)->name` matches after each reload, proving `.lof` naming round-trips
through the real engine for KFX-native *and* classic saves.

## Verification

- `python3 scripts/check_layering.py --strict` — no new violations.
- `keeperfx`/`keeperfx_hvlog` (native Linux) build clean with the new `tinyfiledialogs` dependency
  linked.
- Full ftest sweep — no regressions (nothing in this slice touches the production load path
  itself, only how the editor's own dialogs choose what to call it with).
- Manual live-test pass — pending, same as slice 3.
