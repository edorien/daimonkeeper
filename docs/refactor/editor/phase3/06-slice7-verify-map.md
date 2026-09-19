# Phase 3, slice 7 — `verify_map()`

Status: **done.** The last major unbuilt piece of phase 3.

## What prompted this, and what turned out to be stale

The original design (`docs/refactor/editor/03-map-serialization.md` §5) says to "port ADiKtEd's
`level_verify_struct`". Confirmed stale before writing anything: `3rdparty/Keeper/ADiKtEd-master`
does not exist anywhere in this repo — nothing to port. Investigated before designing anything
fresh:

- **`PckA_ZoomToPosition`** — real and fully wired (`src/kfx_net/src/packets.c:797-805`), not just
  declared. Usable today for a report row's "zoom to" action.
- **Phase 4's tile-highlight overlay** — does not exist (`04-views-camera-overlays.md` itself:
  "Status: not started"). A report row can *zoom to* a location (real) but can't visually flag the
  tile itself this slice.
- **Engine caps** (F18) — confirmed exact and current: `THINGS_COUNT` 12288, `CREATURES_COUNT`
  1024, `ACTN_POINTS_COUNT` 256.
- **Dungeon Heart identification** — `get_player_soul_container()`'s own lookup is live-session-
  only, but the underlying model check (`thing_is_dungeon_heart()`) is just
  `get_object_model_stats(model)->model_flags & OMF_Heart` — config-only, reachable from a
  `MapContent` snapshot.
- **Solid/impenetrable slab check** — `get_slab_kind_stats(kind)->block_flags &
  SlbAtFlg_Blocking`, **not** `category == SlbAtCtg_FortifiedWall` as an earlier read of the
  design doc might suggest (rock's own category is `SlbAtCtg_Unclaimed`, same as claimed dirt;
  `block_flags` is what actually marks it solid — confirmed against `config/fxdata/terrain.cfg`).

## Scope confirmed with the user

- **No room checks at all** (min size, split, adjacent access) and **no portal-presence check** —
  skirmish maps with pre-placed creatures and no portal, or maps with imperfect/broken rooms, are
  legitimately valid; enforcing either would be actively wrong, not just a gap. Also would have
  needed a flood-fill/connected-component analysis over the slab grid with no existing precedent
  anywhere in this codebase.
- **Dungeon Heart check is presence-only** — does at least one `OMF_Heart`-flagged thing exist for
  each player, no 3×3-pedestal-intact requirement (confirmed against real shipped content,
  `core_files/campgns/keeporig/map00019`, that a heart doesn't need its full surrounding structure
  intact to be a valid, playable map).
- **No new "target mode" concept.** `map_is_legacy_compatible()` already computes classic-
  compatibility on demand (already wired into Auto-format Save) — `verify_map_content()` reuses it
  as one INFO line instead of inventing a parallel KeeperFX-vs-classic mode selector, a new
  `MapLevelInfo` field, and Level Settings UI for the same thing.
- **No script checks** — no script-authoring feature exists in the editor yet.
- **No classic vanilla ID-range enforcement** — already a separately-documented, deliberate gap in
  `map_is_legacy_compatible()` itself (`01-slice2-classic-save.md`'s "Known gap carried forward");
  not this slice's job to redo.
- **Non-blocking, on-demand report** — matches this codebase's own "never silently block, always
  let Force/override through" pattern already established for classic-format Save. "Verify Map"
  is a File-menu item, not a Save gate.

## Checks implemented

All computed from one `MapContent` snapshot (`map_content_verify.h`/`.cpp`, `kfx_sim`, mirrors
`map_content_compat.h`/`.cpp`'s own shape exactly):

1. Missing Dungeon Heart per player (ERROR).
2. Thing embedded in a solid/impenetrable slab (ERROR).
3. Thing/creature/action-point counts vs the real engine caps — WARN at 90% of cap, ERROR at/over
   (the 90% threshold is a judgment call, stated as such in the code).
4. Duplicate action point numbers (WARN).
5. Duplicate hero gate numbers (WARN).
6. Map border ring not fully impenetrable (WARN, one summary issue not one per breached slab).
7. Classic-compatibility (INFO) — surfaces `map_is_legacy_compatible()`'s own result and reasons.

## Architecture

- **`src/kfx_sim/include/map_content_verify.h`+`.cpp`** (new) — `MapVerifyIssue` (severity/
  message/optional position) + `verify_map_content(const MapContent&)`. Calls
  `map_is_legacy_compatible()` directly (same library, no new dependency).
- **`src/kfx_editor/include/editor_map_snapshot.h`** (new, plain C++, not `extern "C"`-wrapped —
  same-library internal glue between `editor_mapsave.cpp` and `editor_dialogs.cpp`, the role
  `editor_toolbox.h`/`editor_journal.h` already play) — `editor_snapshot_current_map()` exposes
  `editor_mapsave.cpp`'s own private `snapshot_map()` using the session's current lvnum/name/
  players/multiplayer, exactly what a plain File > Save would write, without writing anything.
- **`editor_dialogs.cpp`**: "Verify Map" dialog, same gated-modal pattern as every other dialog
  here. Snapshots + runs the checks once when opened (not re-run every frame), shows a
  `FeBeginListBox` of `[SEVERITY] message` rows, each with a "Zoom" button for issues tied to a
  location (`set_players_packet_action(get_my_player(), PckA_ZoomToPosition, pos_x, pos_y, 0, 0)`)
  — the real, already-wired packet, no tile-highlight overlay (doesn't exist). Purely
  informational, closeable any time.
- **`editor_menubar.cpp`**: "Verify Map" item in the File menu.

## Tests

- **Catch2** (`src/kfx_sim/tests/map_content_verify_test.cpp`, new) — one case per check, mirroring
  `map_content_compat_test.cpp`'s own SECTION-per-variant style. Found and fixed two real fixture
  bugs while writing these (not implementation bugs — both were the test's own synthetic-config
  setup): `get_object_model_stats()` silently falls back to model 0's stats for any model at or
  past `object_types_count`, which the fixture hadn't set; and `MapThingRecord`'s default `(0,0)`
  position lands on the test map's own blocking border ring, spuriously tripping the embedded-
  thing check for any thing whose position was left at its default.
- No new ftest — UI/report glue over already-tested primitives, same precedent the last several
  slices established.

## Verification

- `python3 scripts/check_layering.py --strict` — no new violations.
- `kfx_sim_utest`/`kfx_editor_utest`/`kfx_frontend_utest` — full suites pass with no regressions
  (2103/53/134 assertions — `kfx_sim_utest` up from 2087 by exactly the 16 new assertions added).
- `keeperfx`/`keeperfx_hvlog` (native Linux) build clean.
- Full ftest sweep — 22/22, no regressions.
- Manual live-test pass of the new "Verify Map" dialog and its Zoom buttons — pending, same as
  every prior slice's own pending item (no live display in this environment).
