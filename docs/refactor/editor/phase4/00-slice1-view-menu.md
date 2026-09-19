# Phase 4, slice 1 — View menu wiring

Status: **done.** First slice of phase 4 (views/camera/overlays); wires the View menu to existing
engine features. No new renderer code, no overlay content yet (that's slices 3-5).

## What prompted this, and what turned out to differ from the design doc

`04-views-camera-overlays.md` was itself just refined/expanded against current code before this
slice started (see that doc's own investigation notes). Implementing slice 1 surfaced two more
corrections, found only once the actual wiring was attempted:

- **"Plan" and "Map (full screen)" are the same engine screen, not two.** The design doc's table
  listed them as separate View items (Plan → the zoom-box magnifier; Map → full-screen parchment).
  Traced `redraw_parchment_view()` (`gui_parchment.c:962-986`): `draw_zoom_box()` runs
  unconditionally whenever `PVT_MapScreen` is the active view type — it's not a separately
  toggleable overlay, it's just part of what the parchment screen always draws. There's also a
  `redraw_minimal_overhead_view()` (`gui_parchment.c:988-994`) that draws the map *without* the
  zoom box, but it has zero callers anywhere in the codebase — dead code, not a real second mode.
  **Collapsed to one View menu item, "Map View"**, toggling `zoom_to_parchment_map()` /
  `zoom_from_parchment_map()` (both already public, `kfx_frontend/include/gui_parchment.h`).
- **1st Person needs more than a position-taking variant of `level_lost_go_first_person()`.**
  Traced that function in full (`player_instances.c:1429-1461`): it doesn't just prefer an owned
  creature's position, it *requires* one — `get_random_thing_of_class_with_filter(...,
  TCls_Creature, ...)` returning nothing makes it return early without creating anything. An
  editor session's map frequently has no creatures at all (a fresh New Map, or a skirmish map
  before creatures are placed), where this would silently no-op. Added
  `level_editor_go_spectator_at(PlayerNumber, MapCoord pos_x, MapCoord pos_y)`
  (`kfx_sim/player_instances.c`, right after `level_lost_go_first_person`) — identical body, minus
  the creature search, taking an explicit position instead (the editor's active camera center).
- **Simulation freeze interaction, not previously considered.** `editor_frame()`
  (`editor_session.cpp:331`) re-asserts `kfx_sim_state.simulation_suspended = !s_preview_motion`
  every single frame — a possessed spectator creature can't move at all unless Preview Motion is
  already on, since creature movement needs per-turn updates the freeze blocks. Selecting 1st
  Person now turns Preview Motion on automatically if it wasn't already (one call to the existing
  `editor_set_preview_motion(true)`) rather than shipping a spectator view that looks broken.

## What's real and confirmed unchanged from the design doc

- **Lights**: `PckA_ToggleLights` (`packets.c:667-673`) — pure flip of `lish.light_enabled`, no
  session state needed. Checkbox state reads `lish.light_enabled` directly (`kfx_render`).
- **1st Person exit**: the existing `PckA_DirectCtrlExit` (`packets.c:1398-1409`), same call shape
  `front_input.c` already uses for the normal Escape-while-possessed handler
  (`player->controlled_thing_idx, 0, 0, 0`).

## Removed after initial landing: Low Walls

Shipped initially as a fourth View item (`PckA_SetCluedo` toggling `settings.video_cluedo_mode`,
same shape as Lights) but pulled right after, on user feedback: the editor session never hides the
normal in-game Options menu, and Options > Graphics already exposes this exact setting — a second
control for it in View is a pure duplicate, not a missing engine feature the editor needed to
surface. Left out entirely rather than kept as a redundant shortcut.

## Architecture

- **`src/kfx_sim/include/player_instances.h` + `.cpp`** — new
  `level_editor_go_spectator_at(PlayerNumber, MapCoord pos_x, MapCoord pos_y)`, mirroring
  `level_lost_go_first_person()`'s body (spectator breed lookup, camera zoom stash, spawn via
  `create_and_control_creature_as_controller()`, `move_creature_to_nearest_valid_position()` to
  snap onto valid ground, `CCFlg_NoCompControl`) with an explicit position instead of a
  found-creature search. Floor height computed via `get_floor_height_at()` (`map_columns.h`, newly
  included in `player_instances.c`).
- **`src/kfx_sim/include/packet_data.h`** — new `PckA_EditorGoSpectator`, appended at the end of
  the enum (never insert — same "these numeric values are saved" rule every other `PckA_Editor*`
  entry already follows). Same "explicit position, full int32 range" shape as
  `PckA_EditorPlaceObject` (`actn_par1`/`actn_par2` = x/y).
- **`src/kfx_net/src/packets_cheats.c`** — new `case PckA_EditorGoSpectator:` in
  `process_players_dungeon_control_cheats_packet_action()` (the same switch every other
  `PckA_Editor*` action is handled in), one line calling
  `level_editor_go_spectator_at(plyr_idx, pckt->actn_par1, pckt->actn_par2)`.
- **`src/kfx_editor/src/editor_menubar.cpp`** — View menu grew three items above the existing
  "Preview Motion", each an `FeMenuItem` with a leading `"[x]"`/`"[ ]"` marker (the same
  no-`FeCheckbox`-precedent convention Preview Motion already established in this pull-down):
  - **Map View** — toggles `player->view_type == PVT_MapScreen` via
    `zoom_to_parchment_map()`/`zoom_from_parchment_map()`.
  - **1st Person** — toggles `player->view_type == PVT_CreatureContrl`; entering sends
    `PckA_EditorGoSpectator` with the active camera's own `mappos.x.val`/`mappos.y.val`
    (`get_player_active_camera(player)`, works for whichever camera mode — isometric or front-view
    — is actually active) and force-enables Preview Motion first if it was off; exiting sends
    `PckA_DirectCtrlExit`.
  - **Lights** — toggles `lish.light_enabled` via `PckA_ToggleLights`.
  - No Low Walls item (see below) and no separate "Isometric" item — each of Map View/1st Person
    is a toggle; clicking the active one again returns to the normal isometric view, so a third
    "return to normal" entry would be redundant.

## Deferred, deliberately, to later phase-4 slices

- **Cursor-based 1st-person spawn position** — v1 spawns at the current camera center, not a
  tracked mouse-cursor world position; `kfx_editor` has no cursor-to-world tracking outside of
  per-click `screen_to_map()` calls in the toolbox's placement tools, and building that just for
  this would be new scope beyond "wire existing engine features." Camera-center is a reasonable
  default and matches "look around from roughly where you're already looking."
- **Full Map / Plan as textually distinct labels** — the original design's two rows are now one
  ("Map View"); if a future need surfaces for a *zoom-box-only, no full map* mode, that would be
  new engine work (the dead `redraw_minimal_overhead_view()` isn't wired to anything), not
  something this slice's wiring could reach.
- **World-space overlays** (grid, markers, verification flags) — slice 3+, per
  `04-views-camera-overlays.md` §6's slicing plan; this slice touches nothing about content drawn
  *inside* any of these views, only which view is active.
- **Editor camera profile** (looser zoom, free rotate) — slice 2.

## Tests

- No new Catch2 coverage this slice — every new/changed function here (`level_editor_go_spectator_
  at()`, the packet handler, the menu items) either mirrors an already-tested code path exactly
  (`level_lost_go_first_person()` has no Catch2 coverage of its own either — it's live-session-only
  logic, not a `MapContent`-shaped predicate like phase 3's checks) or is UI glue over primitives
  the engine's own gameplay code already exercises every time a player uses Options/possession/the
  parchment map. Matches the "UI-only glue doesn't need its own ftest" precedent phase 3 already
  established for similarly-shaped dialog/menu wiring.
- Full ftest sweep (`-ftests -exitonfailedtest -headless`) — clean pass, exit code 0, final state
  `FTSt_TestsCompletedSuccessfully`; nothing here touches the production load/save path.
- Manual live-test pass of all four new View items — **done by the user**: Map View, 1st Person,
  Lights, and Low Walls all confirmed working.

## Verification

- `python3 scripts/check_layering.py --strict` — no new violations (`kfx_editor` calling
  `kfx_frontend`'s `zoom_to_parchment_map()`/`zoom_from_parchment_map()` is a downward call, same
  direction `kfx_editor` already calls into every other lower layer; the new packet flows the
  normal way, `kfx_editor` → packet → `kfx_net` → `kfx_sim`).
- `keeperfx`/`keeperfx_hvlog` (native Linux) build clean.
- `kfx_sim_utest`/`kfx_net_utest`/`kfx_editor_utest` — full suites pass, no regressions
  (2103/83/53 assertions).
- Full ftest sweep — clean pass, see Tests above.
- Manual live-test — done by the user, all four View items confirmed working.
