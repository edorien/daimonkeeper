# Phase 4, slice 4 — core overlays (slab grid, coordinates, ownership tint)

Status: **done.** The three overlays `04-views-camera-overlays.md` §6 grouped as "needing no
per-item data beyond the map itself" — the ones that don't need per-thing/per-light/per-AP
iteration the way slice 5's marker overlays will.

## Architecture

New file, `src/kfx_editor/include/editor_overlay.h` + `src/kfx_editor/src/editor_overlay.cpp` —
kept separate from `editor_dialogs.cpp` (which already carries slice 3's Verify Map markers)
rather than letting either file grow into a catch-all, matching the existing
`editor_toolbox.cpp`/`editor_journal.cpp` split. `extern "C"`-wrapped like `editor_toolbox.h`
(plain get/set accessors, no C++-only types in the signatures — unlike `editor_map_snapshot.h`'s
own exception for `MapContent&`). Called from `editor_session.cpp`'s `editor_frame()` right after
`editor_dialogs_frame()`, so overlays draw on top of every dialog/menu.

All three overlays are independent View-menu toggles (`editor_menubar.cpp`, same `"[x]"`/`"[ ]"`
`FeMenuItem` convention every other checkable View item already uses), each built directly on
slice 3's `project_world_position_to_screen()`:

- **Slab Grid** — one line per slab boundary, spanning the whole map edge to edge (not clipped to
  the visible portion — see "Known limitations" below), heavier/brighter every 5th boundary.
  Skips a boundary line entirely if either endpoint reports as off-screen/behind-camera.
- **Coordinates** — the cursor's current slab (x,y) and subtile position, read via the existing
  `screen_to_map()` at the current mouse position (`GetMouseX()`/`GetMouseY()`, the same
  `kjm_input.h` pair `editor_toolbox.cpp`'s own placement tools already use), drawn as plain text
  in the bottom-left corner over a small dark backing rectangle for legibility. Reimplemented the
  fixed-corner idiom directly rather than reusing `frontgui_ingame_debug.cpp`'s
  `begin_corner_overlay()` helper, which is `kfx_frontend`-internal and not worth a new
  cross-file dependency for one small text draw.
- **Ownership Tint** — a translucent, player-coloured quad over every slab with a real owner
  (`get_slabmap_block()`/`slabmap_owner()`, the same "read world truth directly" precedent the
  eyedropper tool already established for reading live `kfx_sim` slab state from `kfx_editor`).
  Unowned/neutral slabs are left untinted. Colour table duplicated (not exported) from
  `frontmenu_landpreview.c`'s own "approximate classic DK player colours" — that table is
  `static` to a minimap-specific file, and duplicating a small colour table across two
  independent, unrelated consumers is the same "no collision, no forced sharing" precedent
  `10-definable-keybindings.md` already established for key defaults.

## Known limitations, deliberately not engineered around this slice

- **Slab Grid lines aren't clipped to the visible portion of the screen** — a boundary line is
  either drawn in full or skipped in full, based on both endpoints' visibility. On a large map
  where one edge of a grid line is off-screen/behind the camera while the near portion would
  otherwise be visible, that whole line drops out rather than showing its visible segment. Proper
  polyline clipping against the frustum is real extra work with no way to visually verify the
  payoff in this environment (no live display) — left as a documented simplification, worth
  revisiting only if live-testing on a large map actually shows it mattering.
- **Ownership Tint iterates every slab on the map every frame it's on** — for a large map (up to
  85×85 slabs) that's up to 4 projection calls × 7225 slabs ≈ 29,000 calls/frame, each a handful
  of integer multiplies (the same per-call cost `rotpers()` already pays for every visible thing
  every frame in the main renderer). Expected to be cheap enough not to matter, but not measured
  in this environment — flag if live-testing shows a frame-rate impact with the tint on.
- **No cached per-frame map bounds/camera check to skip fully-off-screen slabs early** — every
  slab's corners are individually projected and individually checked; there's no coarse
  broad-phase cull before that. Same "ship the straightforward version, revisit if it's actually a
  problem" reasoning as the point above.

## Tests

- No new Catch2 coverage — every function here (`draw_slab_grid()`/`draw_coordinate_readout()`/
  `draw_ownership_tint()`) depends on live per-frame renderer/session state
  (`project_world_position_to_screen()`'s own file-local statics, the current mouse position, the
  live `kfx_sim_state` map) with no fixture-constructible pure-function shape — same category as
  slice 3's projection primitive itself, which also has no Catch2 coverage.
- Full ftest sweep (`-ftests -exitonfailedtest -headless`) — clean pass, exit code 0, 22/22.
- Manual live-test — pending; ask the user to toggle each of the three View items independently
  and confirm: the slab grid lines up with the actual terrain grid and thickens every 5 slabs; the
  coordinate readout tracks the mouse and matches known landmarks (e.g. the map center); the
  ownership tint's colours match each player's own real colour and only covers owned slabs.

## Verification

- `python3 scripts/check_layering.py --strict` — no new violations (`kfx_editor` calling into
  `kfx_render`'s `project_world_position_to_screen()` and `kfx_sim`'s slab accessors are both
  downward calls, the same direction every other `kfx_editor` file already depends in).
- `keeperfx`/`keeperfx_hvlog` (native Linux) build clean (after fixing a missed `bflib_basics.h`
  include for `TbBool` in the new header, caught immediately by the build).
- `kfx_editor_utest` — full suite passes, no regressions (53 assertions).
- Full ftest sweep — clean pass (22/22), see Tests above.
