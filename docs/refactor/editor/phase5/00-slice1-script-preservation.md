# Phase 5, slice 1 — script text preservation

Status: **done.** The mandatory first slice (`05-script-and-level-settings.md` §0) — fixes an
already-shipped, already-reachable data-loss path before any of this phase's other work builds on
top of it.

## The bug this fixes

`MapContentWriter::write_script()` unconditionally overwrote `map%05lu.txt` with a hardcoded empty
stub on every save. `MapContent` never carried a script-text field at all, so there was nothing to
preserve even in principle. Open Map (phase 3) already lets a mapmaker open any real, shipped
campaign level, and confirmed against real content: **85%+ of shipped levels use this classic
`.txt` format**. Opening one and hitting Save silently replaced its entire script with the stub.
This was a known, documented gap from phase 3 (not a new discovery) — restated here because its
real consequence had never been spelled out this plainly, and phase 5's whole script-editing
design (§4) depends on it being fixed first.

## Architecture

- **`src/kfx_sim/include/map_content.h`** — new `std::string script_text` field on `MapContent`.
  Empty means "nothing known to preserve" (a genuinely new map, or nothing was read) — the signal
  `write_script()` uses to fall back to its own stub rather than writing an empty file outright.
- **`src/kfx_sim/include/map_content_reader.h`+`.cpp`** — new `MapContentReader::read_script()`,
  a non-virtual base-class method (format-independent, same shape as `read_slabs`/`read_ownership`/
  `read_texture` — both KFX-native and classic formats use the identical plain-text `.txt`).
  Reads verbatim via the same `load_whole_file()` helper the other readers already use. A missing
  file is not treated as a read failure — it just leaves `script_text` empty, which is exactly the
  "nothing to preserve" state the writer already needs to handle for a brand new map.
- **`src/kfx_sim/src/map_content_writer.cpp`** — `write_script()` now writes `content.script_text`
  verbatim when non-empty; the empty-stub fallback only fires when it's empty.
- **`src/kfx_editor/src/editor_session.cpp`** — the actual end-to-end fix for the editor's own Save
  path. `editor_open()` now reads the level's `.txt` directly off disk (a small local helper,
  `read_level_script_text()`, using the same `LbFileLength`/`LbFileLoadAt` primitives
  `map_content_reader.cpp` itself uses — deliberately *not* going through `kfx_script`'s own
  live-parsed representation of the script, since that's a transformed/consumed form, not the raw
  source text) into new session state (`s_editor_script_text`), carried unchanged through the rest
  of the session (no script-editing feature exists yet to change it — that's a later slice) and
  exposed via a new `editor_current_level_script_text()` accessor (`kfx_editor.h`).
- **`src/kfx_editor/src/editor_mapsave.cpp`** — `snapshot_map()` now sets
  `content.script_text = editor_current_level_script_text();`, so every path that builds a
  `MapContent` from the live session (Save, Save As, Verify Map's own read-only snapshot) carries
  the real script text through to `write()`.
- **Deliberately not touched**: `editor_save_level_info()` (`.lof`+`.lif` only, the Level Settings
  dialog's own Apply action) — confirmed it never called `write_script()` in the first place, so
  this fix doesn't change that narrower path's behavior at all.

## Tests

- **Catch2** (`src/kfx_sim/tests/map_content_roundtrip_test.cpp`) — the existing full round-trip
  test's sample content now includes a real classic-format script snippet (comment + `IF`/`ENDIF`
  block, not a placeholder), checked for exact round-trip. Three new dedicated cases: writing with
  empty `script_text` still produces the exact stub text; reading when `.txt` is missing leaves
  `script_text` empty without corrupting it into something else; (covered by the round-trip case
  itself) a real, non-trivial script survives write-then-read byte-for-byte.
- No new ftest — this is snapshot/session-state plumbing over already-tested primitives
  (`load_whole_file`/`save_text`), same "UI/session glue doesn't need its own ftest" precedent
  established since phase 3.

## Verification

- `python3 scripts/check_layering.py --strict` — no new violations.
- `keeperfx`/`keeperfx_hvlog` (native Linux) build clean.
- `kfx_sim_utest`/`kfx_editor_utest` — full suites pass, no regressions (2109/53 assertions —
  `kfx_sim_utest` up from 2103 by exactly the 6 new assertions added).
- Full ftest sweep (`-ftests -exitonfailedtest -headless`) — clean pass, exit code 0, 22/22.
- Manual live-test — pending; ask the user to open a real shipped level with a non-trivial script
  (e.g. `keeporig/map00019`), save it (plain Save, not Save As), and confirm the level's `.txt`
  still has its real content afterward rather than the empty stub.
