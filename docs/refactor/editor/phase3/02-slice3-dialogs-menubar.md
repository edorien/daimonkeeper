# Phase 3, slice 3 — editor menu bar + New/Open/Save dialogs

Status: **implementation done, pending a live click-through pass by the user.** Builds on slices
1/2 (`00-slice1-native-save.md`, `01-slice2-classic-save.md`, both done) — `editor_save_map()`
exists for both formats but had zero UI callers until this slice.

## Scope for this slice

Confirmed with the user: `docs/refactor/editor/08-gui-layout.md`'s full redesign proposal (icon
tool rail, reused sidebar/minimap column, splitter inspector) is **stale** — the tab-strip toolbox
actually built in phase 2/9 (`09-toolbox-remainder.md`) is the accepted direction instead, and
only `08`'s menu-bar/dialog ideas carry forward here.

**In scope:**
- `FeMenuBar`/`FeMenu`/`FeMenuItem` wrappers (`frontgui_widgets.h`/`.cpp`).
- A real File/Edit/View/Script menu bar in `kfx_editor`, replacing the toolbox header's Menu/
  Undo/Redo buttons.
- New Map / Open Map / Save As dialogs, File > Save, drawn from `kfx_editor`.
- Session state: `editor_session.cpp` starts tracking the current `lvnum` and save directory, and
  the dirty flag gets a real set/clear lifecycle.
- The relaunch mechanism (`kfx_frontend`) letting an in-session File > New/Open tear down and
  re-bootstrap the local game session on a different level, reusing the existing
  `FeSt_START_EDITOR` bootstrap path.
- Retiring `frontgui_editorbrowser_frame()` and `FeSt_EDITOR`; Tools → Editor now jumps straight
  into a blank New Map.
- Exporting `land_preview_build_minimap()` for Open-dialog thumbnails.

**Explicitly out of scope, deferred:**
- Full `08-gui-layout.md` redesign (icon tool rail, sidebar/minimap reuse, splitter inspector).
- New Map name/author/keeper-count fields (needs `MapLevelInfo` round-tripping — phase 5).
- Save As free-form destination picker (no file-picker primitive exists).
- Script menu real content (no script-editing feature exists yet — present but disabled).
- Verification report strip / `verify_map()`.
- Playtest button/ribbon.

## Architecture

See the approved plan for full detail; summary:

- **Widgets**: `FeBeginMenuBar/FeEndMenuBar`, `FeBeginMenu/FeEndMenu`, `FeMenuItem` added to
  `frontgui_widgets.h/.cpp`, mirroring `FeBeginTabBar`'s "only End if Begin returned true"
  contract.
- **Session state** (`editor_session.cpp`): `s_editor_lvnum` + `s_editor_save_dir[512]` set in
  `editor_open()` via a new `editor_level_save_dir()` helper (reproduces the
  `prepare_file_fmtpath(get_level_fgroup(lvnum), ...)` + `strrchr('/')` pattern
  `ftest_editor_save_reload.c` had duplicated inline). New accessors `editor_current_lvnum()`/
  `editor_is_dirty()`. Dirty flag now set from `editor_journal_record_placement()`/
  `editor_journal_record_rect_terrain()`, cleared after a successful save.
- **Menu bar** (`editor_menubar.h/.cpp`, new): File (New/Open/Save/Save As/Exit) · Edit (Undo/
  Redo) · View (Preview Motion checkbox) · Script (disabled placeholder). Map name + dirty `*`.
- **Dialogs** (`editor_dialogs.h/.cpp`, new): New Map (w/h/texture), Open Map (list + thumbnail),
  Save As (format selector), unsaved-changes confirm.
- **Relaunch mechanism** (`kfx_frontend`): `editor_pending_relaunch` flag +
  `frontend_request_editor_relaunch()` (new, exported) + a new branch in
  `get_startup_menu_state()` before its `PlaF6_PlyrHasQuit` check, returning `FeSt_START_EDITOR`
  instead of `FeSt_MAIN_MENU` when set. Reuses the existing quit packet
  (`PckA_QuitToMainMenu`) and `FeSt_START_EDITOR` bootstrap unchanged.
- **Retirement**: `frontgui_editorbrowser_frame()` and `FeSt_EDITOR` deleted; Tools → Editor
  stashes blank-New-Map defaults and requests `FeSt_START_EDITOR` directly.

## Tests

- Catch2 (`kfx_editor_utest`): `editor_level_save_dir()` (pure path logic, no live session) —
  `editor_session_test.cpp`.
- Catch2 (`kfx_frontend_utest`): **deviation from the original plan** — a live ftest driving a
  real quit-through-the-frontend-loop-and-back-into-`FeSt_START_EDITOR` round trip turned out to
  need much more scaffolding/risk than the mechanism itself warranted (`ftest_update()` only runs
  from inside an active gameplay session's per-turn loop, so surviving a real quit-to-menu-and-back
  cycle inside one ftest is a materially bigger undertaking than proving the new logic is correct).
  Instead: `get_startup_menu_state()`'s new `editor_pending_relaunch` branch was moved to be the
  very *first* check in that function — ahead of `game_flags2`/`kfx_sim_state`/`get_my_player()`
  reads the rest of the function needs — specifically so it stays cheaply unit-testable on its own
  with no player/session fixture. `editor_relaunch_test.cpp` covers
  `frontend_request_editor_relaunch()`'s stashing and this branch's priority/one-shot-consume
  behavior directly.
- Manual live-test pass of the menu bar + dialogs (ImGui click-paths aren't ftest-covered) — see
  below.

## Progress

- [x] `FeMenuBar`/`FeMenu`/`FeMenuItem` widgets (`frontgui_widgets.h/.cpp`)
- [x] Session state (`lvnum`/save dir/dirty lifecycle) (`editor_session.cpp`)
- [x] Menu bar (`editor_menubar.h/.cpp`)
- [x] New Map / Open Map / Save As dialogs + File > Save (`editor_dialogs.h/.cpp`)
- [x] Relaunch mechanism (`frontend.h/.cpp`'s `editor_pending_relaunch` +
      `frontend_request_editor_relaunch()` + `get_startup_menu_state()`'s new branch)
- [x] Retire `frontgui_editorbrowser_frame()`/`FeSt_EDITOR`
- [x] Export `land_preview_build_minimap()` + a lightweight RGB-per-slab accessor for the Open
      dialog's own thumbnail drawing (`frontmenu_landpreview.h/.c`)
- [x] Tests (Catch2 — see the deviation note above): `editor_relaunch_test.cpp`
      (`kfx_frontend_utest`), `editor_session_test.cpp` (`kfx_editor_utest`); full
      `kfx_sim_utest`/`kfx_editor_utest`/`kfx_frontend_utest` suites re-run clean, no regressions.
- [ ] Manual live-test pass — not done from this session (no live display here); needs a
      human click-through: Tools -> Editor -> blank map, File > New/Open/Save/Save As, the
      unsaved-changes confirm, and the View > Preview Motion toggle.
- [x] `check_layering.py --strict` clean
- [x] `keeperfx`/`keeperfx_hvlog` build clean; full ftest sweep (22/22, exit 0) passes with no
      regressions.

Status: **implementation done, pending a live click-through pass by the user** (see the unchecked
item above).
