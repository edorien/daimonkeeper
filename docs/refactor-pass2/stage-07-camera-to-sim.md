# S07 — Synced camera → kfx_sim

**Status:** done 2026-09-28 · **Work items:** W19 · **Depends on:** S03
(`first_person_horizontal_fov` in kfx_config) and S06 (the possession view
has left thing_creature.c) · **Baseline:** includes the spectator hand-off
change · **Risk:** medium · **Layout change:** none (the view-shake fields
move in S10) · **Estimate:** 1–1.5 weeks · **Callback entries removed:**
18 · **Timing:** right after an upstream merge

The analysis and evidence are in
[`00-analysis.md` §8.1](00-analysis.md#81-where-does-the-camera-belong--two-cameras-the-synced-one-is-sim-state).

## Goal

The synced camera (`PlayerInfo.cameras[]`) is simulation state: it is
packet-driven, advanced every turn, saved and resynced. Its code moves into
kfx_sim. The local camera (`local_camera.c`: interpolation, prediction,
replay free-cam) stays in kfx_render, and the sim stops calling into it.

## Moves → new `kfx_sim/src/player_camera.c` (+ `player_camera.h`)

| From | Functions |
| --- | --- |
| `kfx_render/src/engine_camera.c` | `view_set_camera_position`, `view_zoom_camera_in`, `view_zoom_camera_out`, `view_move_camera_on_zoom` (static), `view_zoom_camera_in_to`, `view_zoom_camera_out_from`, `set_camera_zoom`, `get_camera_zoom`, `update_camera_zoom_bounds`, `view_set_camera_x_velocity`, `view_set_camera_y_velocity`, `view_set_camera_rotation_velocity`, `view_set_camera_rotation_velocity_around`, `view_set_camera_tilt`, `get_walking_bob_direction` (static), `first_person_camera_near_wall` (static), `update_first_person_position`, `update_first_person_camera`, `view_move_camera_x/y` (static), `camera_move_rate`, `view_process_camera_velocity`, `view_set_camera_move_to_position`, `view_move_camera_to_position`, `update_player_camera`, `update_all_players_cameras`, `set_player_cameras_position`, `init_player_cameras`, `any_player_close_enough_to_see`, `lightning_is_close_to_player` |
| `kfx_net/src/packets.c` | `process_camera_position` (static), `process_camera_controls`, `process_camera_view_controls`, `set_all_cameras_position` (static), `set_all_cameras_rotation` (static), `process_camera_action`, `process_first_person_look`, `can_process_creature_input` |

**Stays in kfx_render `engine_camera.c`:** `scale_camera_zoom_to_screen`,
`change_engine_window_relative_size`, `centre_engine_window` (the engine
window, `local_state`, the status panel).

**Dependencies checked** (2026-09-27): the moving `engine_camera.c`
functions use only three kfx_render symbols:

- `first_person_horizontal_fov` (in kfx_config after S03);
- `init_local_cameras` (replaced by the signal below);
- `setup_engine_window`, used only by the functions that stay.

The packet camera handlers read only `kfx_config_state.zoom_distance_setting`.

## Sim → local camera: signals instead of calls

Today kfx_sim calls `sync_local_camera` (×6), `set_local_camera_destination`
(×8), `move_local_camera_to_position` (×1) and `get_local_camera` (×3)
through `SimFeedbackCallbacks`. Replace these with:

- **A new non-serialized `struct KfxSimViewSignals kfx_sim_view_signals`**
  in kfx_sim, holding `uint64_t camera_sync_seq[PLAYERS_COUNT]` and
  `camera_retarget_seq[PLAYERS_COUNT]`.
  - It sits deliberately *outside* `kfx_sim_state`, so it is not saved or
    resynced, and there is no layout change.
  - It is reset at level start.
- **The sim increments a counter** where it called `sync_local_camera()`
  (snap) or `set_local_camera_destination()` (retarget).
- **`update_local_cameras()`** (kfx_render, every frame) compares against
  its last-seen values for the local player and performs the snap or
  retarget itself.
  - `init_player_cameras()` bumps the sync counter, and the render side
    calls `init_local_cameras()` on the first frame of a level, or when the
    counter jumps from 0.
  - This can't desync: the sim never reads the counters.
- **`move_local_camera_to_position(x, y)`** (`thing_creature.c`, one call)
  becomes "set the synced camera position, bump sync". Confirm during the
  stage that this call is only reached for the local player's own view
  action.
- **`get_local_camera()`:**
  - `power_process.c`: `lightning_modify_palette` (a local visual) and
    `draw_god_lightning` (spawns unsynced effects) switch to the synced
    camera. Visual difference: the orientation is no longer interpolated.
  - `thing_creature.c`: gone with S06.

## Invariant to write down

`any_player_close_enough_to_see()` and `lightning_is_close_to_player()` may
gate only unsynced effects. Their consumers must use unsynced things,
`UNSYNC_RANDOM`, and state that is neither checksummed nor read by synced
code.

Since the spectator hand-off change, the local computer-controlled seat's
camera advances on one machine only, which makes this rule load-bearing.
State it in a comment on both functions and on `update_all_players_cameras`.
Add a debug-level assertion in `process_effect_generator` that the
generator's synced fields (other than `generation_delay`) aren't modified
on the camera-gated path.

## Callback entries this frees (18)

- `SimFeedbackCallbacks`:
  - `get_camera_zoom`, `set_camera_zoom`, `view_zoom_camera_in`,
    `view_zoom_camera_out`, `view_set_camera_move_to_position`,
    `view_move_camera_to_position`, `init_player_cameras`,
    `any_player_close_enough_to_see`, `lightning_is_close_to_player` (9);
  - `sync_local_camera`, `set_local_camera_destination`, `get_local_camera`,
    `move_local_camera_to_position` (4).
- `RenderOverlayCallbacks`: `process_camera_controls`,
  `process_camera_view_controls`, `process_camera_action`,
  `process_first_person_look`, `can_process_creature_input` (5). kfx_render's
  `local_camera.c` now calls *down* into kfx_sim for prediction.

## Steps

1. Create `player_camera.{c,h}` and move the `engine_camera.c` functions.
   `engine_camera.h` keeps only what stays, and includes `player_camera.h`
   for one stage.
2. Move the packet camera handlers from `packets.c`.
3. Add `KfxSimViewSignals`, convert the 18 sim call sites, and teach
   `update_local_cameras()` to consume the counters.
4. Switch `power_process.c` to the synced camera.
5. Delete the 18 entries.
6. Add the invariant comments and assertion.

## Verification

- **Replays are the key regression test.** Camera state is resynced and
  saved, so a fixed multiplayer replay (`-packetload`) must produce an
  identical checksum trace (`net_checksums`) before and after the stage.
- Ftests: `spectator_handoff`, the fake-multiplayer and enet-loopback
  ftests, and any possession ftests.
- **Manual:**
  - heart-zoom at level start;
  - entering and leaving possession (the camera snaps back correctly);
  - map-screen fade in and out;
  - zoom limits and rotation in isometric and front view;
  - replay free-cam (detach, move, reattach);
  - god lightning orientation;
  - a spectator hand-off in a two-player LAN game.
- Inventory: −18.

## Risks

- **Ordering of snap versus retarget within a frame.** Today they are
  immediate calls, made from different player-instance functions: retarget
  at `player_instances.c` lines 409, 515, 698, 716 and 890; sync at 661,
  977, 999, 1032 and 1047. With counters, both can be seen in the same
  frame. Apply snap first, since it sets a consistent base, then retarget.
  During the stage, check that no instance sequence within a single turn
  relies on the reverse order.
- **`init_local_cameras` timing at level start.** Cover it with the
  heart-zoom check.

## As built (2026-09-28)

One code commit, `e3a52fbfb`.

- **Moves as planned** into `kfx_sim/src/player_camera.c` and
  `player_camera.h`. Those are the synced-camera functions from
  `engine_camera.c` and the packet camera handlers from `packets.c`.
  `CAMERA_TILT_*` came along too. `engine_camera.c` keeps
  `scale_camera_zoom_to_screen`, the engine-window helpers and the renderer's
  `camera_zoom`. The zoom tests moved with the zoom code, from
  `engine_camera_test.cpp` to kfx_sim's `player_camera_test.cpp`.
  `engine_camera.h` doesn't re-include `player_camera.h`: the few callers
  include the new header directly.
- **`first_person_horizontal_fov` went to kfx_platform's `bflib_video.c`, not
  kfx_config.** S03 had left it in kfx_render's `vidmode.c`, because it is
  derived from the screen's aspect ratio. It now sits next to
  `first_person_vertical_fov` and `FOV_based_on_aspect_ratio()`.
  `vidmode.c` still sets it, and `init_player_cameras()` reads it.
- **Signals.** `struct KfxSimViewSignals kfx_sim_view_signals` in
  `player_camera.h` holds three per-player counters: init, sync and
  retarget.
  - The sim bumps them through `signal_local_camera_sync()` and
    `signal_local_camera_retarget()`. `init_player_cameras()` bumps the
    init counter.
  - `local_camera.c`'s `apply_sim_camera_signals()` applies anything new
    for the local player, in the order init, snap, retarget.
  - The plan had only `update_local_cameras()` consume them. But `update()`
    runs `update_local_cameras()` *before* the sim processes player
    instances, so a snap would have shown one turn late. So every
    local-camera reader calls it first: `camera_packet_set_state`,
    `move_local_camera_to_position`, `update_local_cameras`,
    `interpolate_local_cameras`, `get_local_active_camera(_index)`. A
    change is therefore seen by the next reader, as the direct call was.
  - One real difference: a snap or retarget now copies the synced cameras
    as they are when first read, not when the sim asked. Usually that's the
    same turn; the turn traces below show the synced cameras themselves are
    unchanged.
  - The counters sit outside `kfx_sim_state`, so there's no layout change.
- **`move_local_camera_to_position` stays a local call.** The plan made
  `thing_creature.c`'s one call "set the synced camera position, bump sync".
  But that call is in `go_to_next_creature_of_model_and_gui_job()`, and every
  caller of that function is local UI: the creature panel, the query tabs
  and the console. Changing synced state there would do it on one machine
  only. So the function moved to `local_camera.c`, beside
  `move_local_camera_to_position()`.
- **`power_process.c`**: `lightning_modify_palette` and `draw_god_lightning`
  use the synced camera. The lightning orientation is no longer
  interpolated.
- **Invariant.** A rule comment sits above `update_all_players_cameras()`,
  referenced from `any_player_close_enough_to_see()` and
  `lightning_is_close_to_player()`. At Debug log levels,
  `process_effect_generator()` snapshots the generator after the camera
  gate. It logs an error if anything but `generation_delay` changed.
- **Callback entries:** the planned 18 removed, 458 → 440.
- **Tests.** `local_camera_test.cpp` used to spy on the three removed
  render_overlay entries. It now checks where the camera ends up: a
  parchment jump lands exactly on the clicked subtile despite a scroll, and
  a normal packet scrolls. Disabling the jump guard makes it fail.
- **Regression check (in place of a fixed replay).** A replay recorded from
  an ftest wouldn't reproduce it: ftests also change state directly, not
  only through packets. So scratch builds of HEAD and of this stage logged
  two things every turn: every player's four synced cameras (position,
  rotation, zoom, view mode, velocities) and the replay integrity checksum
  (`compute_replay_integrity()`). Both builds then ran the 70-test ftest
  sweep. All 70 tests pass on both builds, and the traces (141,250 lines)
  are byte-identical.
- **Checks:** Linux and Windows builds, both layering checks, and all Catch2
  suites (2041 cases).
- **Still to do by hand:** the manual list below. The two-process
  fake-multiplayer resync is part of the sweep. The enet-loopback tests and
  a real LAN spectator hand-off were not run.

## Upstream-merge notes

- Upstream edits `engine_camera.c` (camera feel, zoom limits) and
  `packets.c` (camera controls). Those hunks now go to `player_camera.c`.
  `move_ledger.py upstream src/engine_camera.c` tags every moved function.
- Upstream's direct `sync_local_camera(player)` or
  `set_local_camera_destination(player)` calls inside sim code must become
  counter bumps. The ledger rows' notes say so.
