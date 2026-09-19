# Phase 5, slice 2 — Level Settings expansion (description + map size)

Status: **done, scoped down from the original slice 2 plan.** Covers 2 of the 4 fields originally
grouped into this slice — description and map dimensions. Texture set dropdown and ambient light
level need their own investigation (texture-pack config data, ambient light's storage mechanism)
not done this pass, and are left for a follow-up slice rather than delivered speculatively.

## What this adds

- **Description field** — a genuine find, not new plumbing: `DESCRIPTION` was already a recognized
  `.lof` keyword (`cmpgn_map_commands[]`, `config_campaigns.c:94`) and `LevelInformation` already
  had a `description[LEVEL_DESCRIPTION_LEN]` field (`config_campaigns.h:149`) — neither was wired
  to anything (`level_lof_file_parse()`'s own case 11 was a documented no-op, `// As for now,
  ignore these`). Wired both directions: `level_lof_file_parse()` now actually populates
  `lvinfo->description`; `MapLevelInfo` (`map_content.h`) gained a matching `description_text`
  field; `MapContentWriter::write_level_info()` writes a `DESCRIPTION = ...` line (only when
  non-empty, so a level with none doesn't gain an empty line it never had);
  `KfxNativeMapContentReader::read_level_info()` reads it back. `AUTHOR` (the other no-op'd
  keyword) has no `LevelInformation` field yet and stays deferred — a bigger addition (a new core
  struct field, not wiring an existing one) not attempted this pass.
- **Map dimensions display** — read-only, straight from the live session
  (`kfx_sim_state.map_tiles_x`/`map_tiles_y`), matching the original doc's own "read-only after
  creation for v1" call. No persistence involved — nothing to round-trip, it's always accurate to
  whatever's actually loaded.

## Architecture

- **`src/kfx_sim/src/lvl_filesdk1.c`** — `level_lof_file_parse()`'s case 11 (`DESCRIPTION`) now
  calls `get_conf_parameter_whole()` into `lvinfo->description`, same shape as case 1's
  (`NAME_TEXT`) own handling. Case 10 (`AUTHOR`) stays grouped with the still-ignored DATE/
  MAP_FORMAT_VERSION cases.
- **`src/kfx_sim/include/map_content.h`** — `MapLevelInfo` gained `std::string description_text`.
- **`src/kfx_sim/src/map_content_writer.cpp`+`map_content_reader.cpp`** — `write_level_info()`/
  `KfxNativeMapContentReader::read_level_info()` both extended for the new `DESCRIPTION` key,
  mirroring `NAME_TEXT`/`KIND`/`PLAYERS`'s own already-established `KEY = value` format.
- **Signature growth, same established pattern as every prior slice's own name/players/
  multiplayer growth**: `editor_save_map()` and `editor_save_level_info()`
  (`kfx_editor.h`/`editor_mapsave.cpp`) both gained a `const char *level_description` parameter,
  threaded through `snapshot_level_info()`/`snapshot_map()`. Every call site
  (`editor_dialogs.cpp`'s Save/Save As/Playtest/Level-Settings-Apply, plus
  `ftest_editor_save_reload.c`'s two direct calls) updated to pass
  `editor_current_level_description()` (or `NULL` from the ftest, which only cares about
  name/players/multiplayer).
- **`editor_session.cpp`** — new `s_editor_level_description[LEVEL_DESCRIPTION_LEN]` session
  state, best-effort read from `get_level_info(lvnum)->description` in `editor_open()` (same
  pattern as name/players/multiplayer), `editor_current_level_description()`/
  `editor_set_current_level_description()` accessors.
- **`editor_dialogs.cpp`**: Level Settings dialog gained a `FeTextInput("Description", ...)` field
  and a read-only `FeBodyText` map-size line, both between the existing Name field and the
  Players/Multiplayer controls. Apply now also calls `editor_set_current_level_description()` on
  success, same commit-on-success shape as the other three fields.

## Deferred, explicitly, not silently dropped

- **Author** — `AUTHOR` is a real, already-recognized `.lof` keyword, but `LevelInformation` has
  no field for it (unlike `description_text`, which already existed) — needs a genuinely new core
  struct field, not just wiring an existing one. Small, but a distinct, slightly bigger change than
  this slice's "wire up what's already there" scope — left for a quick follow-up rather than
  bundled in here.
- **Base texture set dropdown** — needs its own investigation (`texture_pack_desc` and how custom
  `tmap?%03d.dat` packs are discovered, per the original doc's own F15 citation) not done this
  pass.
- **Ambient light level** — needs its own investigation into where/how ambient light is actually
  stored (script command vs. a per-map config value) not done this pass.
- **Per-slab texture paint** — already flagged "advanced mode, fast-follow if time-boxed" in the
  original doc; still out of scope.

## Tests

- No new Catch2/ftest coverage this slice specifically for `description_text` — the existing
  `map_content_roundtrip_test.cpp` round-trip test (extended in slice 1 for `script_text`) doesn't
  yet set a non-empty description on its sample content, so this field's write/read path isn't
  independently exercised by an automated test yet. Flagged here rather than silently skipped;
  worth a follow-up addition mirroring the pattern already used for `script_text`.
- No new ftest — this is dialog/session-state plumbing over already-tested `.lof` primitives, same
  "UI glue doesn't need its own ftest" precedent established since phase 3.

## Verification

- `python3 scripts/check_layering.py --strict` — no new violations.
- `keeperfx`/`keeperfx_hvlog` (native Linux) build clean.
- `kfx_editor_utest` — full suite passes, no regressions (53 assertions).
- **`kfx_sim_utest` could not be linked or run this round** — blocked by an unrelated, incomplete
  file from a concurrent session sharing this worktree (`roomspace_extra_test.cpp`, calling
  `roomspace_liquid_path_is_blocked()`, which doesn't exist yet). Confirmed this isn't caused by
  this slice's own changes: the specific new/changed object files
  (`map_content_roundtrip_test.cpp.o`, `map_content_writer.cpp.o`, `map_content_reader.cpp.o`,
  `lvl_filesdk1.c.o`) all compiled successfully in isolation; only the full executable link is
  blocked, by a file this session didn't touch and shouldn't fix. Full `kfx_sim_utest` re-run is a
  pending follow-up once that file is fixed.
- Full ftest sweep (`-ftests -exitonfailedtest -headless`) — clean pass, exit code 0, 22/22 (this
  build path doesn't depend on the Catch2 test directory at all, so it's unaffected by the above).
- Manual live-test — pending; ask the user to open Level Settings, set a description, Apply, close
  and reopen the level, and confirm the description is still there; also confirm the map-size
  display matches the actual map.
