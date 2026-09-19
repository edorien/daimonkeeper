# Phase 3, slice 5 — Playtest, Level Settings, Save As overwrite confirm

Status: **implementation done, pending a live click-through pass by the user.** Follow-on from
slice 4 ([`03-slice4-file-dialogs.md`](03-slice4-file-dialogs.md)), after the user confirmed live
testing of slices 3/4 worked.

## Scope for this slice

Three independent, user-requested items, each kept genuinely separate rather than folded into
New Map/Save As:

- **Playtest**: reuses the exact mechanism the `-level` command-line launch argument already
  uses (`set_selected_level_number()` + a normal single-player game start), not a new capability.
- **Level Settings**: name/players/is_multiplayer (`.lof`'s `NAME_TEXT`/`PLAYERS`/`KIND`) on their
  own modal, independent of New Map (size/texture only) and Save As (number/destination/format
  only) — per the user's own ask.
- **Save As overwrite confirm**: warn before silently replacing an existing map at the target
  lvnum/dir.

## Architecture

### Playtest

- `frontend.h`/`.cpp`: `EDITOR_PLAYTEST_LEVEL_NUMBER` (900002, distinct from New Map's own
  `EDITOR_SCRATCH_LEVEL_NUMBER` 900001) + `editor_pending_playtest` flag +
  `frontend_request_editor_playtest(lvnum)` (calls `set_selected_level_number(lvnum)` — the same
  primitive `-level` uses in `main.cpp` — then stashes the flag). `get_startup_menu_state()`
  checks `editor_pending_playtest` right alongside `editor_pending_relaunch` (same "checked first,
  no player/session fixture needed" placement), returning `FeSt_START_KPRLEVEL` — a normal
  single-player game start, reusing that state's existing handler
  (`game_session_loop.cpp`) completely unchanged.
- `editor_dialogs.cpp`: "Playtest" (File menu, now enabled) opens a confirm modal stating the
  real limitation up front — the map's own script is always `MapContentWriter::write_script()`'s
  minimal stub ("REM Empty script"; no script-authoring feature exists in the editor), so
  win/lose conditions and custom triggers won't run. Confirming saves the current map to
  `EDITOR_PLAYTEST_LEVEL_NUMBER` (never the session's own lvnum — playtesting never touches what
  you're actually editing) via `editor_save_map()`, then `frontend_request_editor_playtest()` +
  `editor_close()` (the same quit-and-relaunch mechanism New/Open already use, targeting
  `FeSt_START_KPRLEVEL` instead of `FeSt_START_EDITOR`).
- **Not built this slice**: returning to the editor when the playtest session ends
  (win/lose/quit lands wherever a normal single-player game would — main menu, level stats, ...).
  That needs a "return to editor" ribbon/session hand-off (`editor_notify_playtest_end()`, still a
  no-op) — a separate, bigger UI piece, not attempted here.

### Level Settings

- `MapContentWriter::write_level_info()` gained a public wrapper, `write_level_info_only()`, so a
  caller can write *just* `.lof` without a full `write()` (which would also rewrite slabs/things/
  etc.) — `.lof` genuinely has nothing to do with the rest of the map's own content.
- `kfx_editor.h`: new `editor_save_level_info(lvnum, dir, name, players, is_multiplayer)` (uses
  `write_level_info_only()` via a throwaway `KfxNativeMapContentWriter` — format-independent, any
  concrete writer works) + re-runs `find_and_load_lof_files()`/`find_and_load_lif_files()`
  afterward, same as `editor_save_map()` does (`KIND` (re-)registers the level into
  `campaign.single_levels`/`multi_levels`).
- `editor_save_map()` itself gained `level_players`/`level_is_multiplayer` parameters (alongside
  the existing `level_name` from slice 4) — every caller now passes the session's own current
  values (`editor_current_level_players()`/`editor_current_level_is_multiplayer()`), the same
  "don't blank out what's already set" discipline `level_name` already established.
- `editor_session.cpp` tracks `players`/`is_multiplayer` the same way it tracks `name`: best-effort
  read from `get_level_info(lvnum)` in `editor_open()` (`level_type & LvKind_IsMulti` for the
  multiplayer bit), kept current by the Level Settings dialog's own Apply action.
- `editor_dialogs.cpp`: "Level Settings..." (File menu) opens a modal with Name/Players/
  Multiplayer fields, defaulting to the session's current values. Apply writes only `.lof`
  (`editor_save_level_info()`) — no map-content save, so nothing for the dirty flag to track here.

### Save As overwrite confirm

- Save As's own Save button now checks `LbFileExists()` on `map%05lu.slb` at the target lvnum/dir
  before writing. If found, it stashes the pending save parameters and opens a new confirm modal
  ("A map already exists at level N in that folder.") instead of writing immediately — same
  sequential "one modal hands off to the next, never nested" pattern the unsaved-changes confirm
  already established (Save As's own modal closes first; the overwrite confirm opens on the next
  frame). Confirming performs the save; cancelling discards the pending save silently (the user
  would need to reopen Save As to try again — same simplification precedent as the
  unsaved-changes confirm's own Cancel).

## Tests

No new automated coverage this slice — every addition is either UI/dialog glue (Playtest confirm,
Level Settings modal, overwrite check) already covered by the same kind of manual-verification-only
precedent slice 4's native pickers set, or a thin, obviously-correct wrapper
(`write_level_info_only()` just forwards to the already-tested `write_level_info()`). Verified by
build + layering + full ftest sweep, same as every prior slice; a live click-through is still the
real verification for the new dialogs themselves.

## Verification

- `python3 scripts/check_layering.py --strict` — no new violations.
- `keeperfx`/`keeperfx_hvlog` (native Linux) build clean.
- `kfx_sim_utest`/`kfx_frontend_utest`/`kfx_editor_utest` all pass with no regressions (2080/134/53
  assertions — same counts as before this slice, confirming nothing existing broke).
- Full ftest sweep — 22/22, no regressions.
- Manual live-test pass — pending, same as slices 3/4.
