# Phase 4, slice 2 — editor camera profile

Status: **done.** Much smaller than the design doc assumed — most of what "editor camera profile"
asked for already existed before this slice touched anything.

## What prompted this, and what turned out to be already built

`04-views-camera-overlays.md` §4/§5 called this "new, no existing precedent to build on" and
"fully greenfield," based on an earlier investigation that only read `editor_session.cpp`/
`editor_toolbox.cpp` directly and found no camera-mode/rotate/zoom code there. Re-investigating
specifically for this slice found two things that investigation missed entirely, because they live
in files that earlier pass never had reason to open:

- **Pan/rotate/zoom/tilt key bindings for the editor already exist and are already wired**, done by
  a *different*, earlier slice: [`10-definable-keybindings.md`](../10-definable-keybindings.md)
  (Phase 2 companion, landed well before this phase-4 work started). It duplicated
  `Gkey_MoveUp/Down/Left/Right`, `Gkey_RotateCW/CCW`, `Gkey_ZoomIn/Out`, `Gkey_TiltUp/Down/Reset`
  etc. into a parallel `EditorGameKeys` table, and `get_isometric_view_nonaction_inputs()`
  (`front_input.c:2278-2341`) already dispatches every one of them through
  `is_dual_context_key_pressed()`/`get_dual_context_key_axis_value()`, which read from the editor
  table whenever `editor_callbacks->is_active()`. None of this needed touching — it was simply
  never mentioned in `04-views-camera-overlays.md`, which is what led the earlier phase-4
  investigation to conclude (wrongly) that no camera control code existed for the editor at all.
- **"Free rotate" already exists as a normal Options toggle.** `settings.video_rotate_mode`
  (`gui_video_rotate_mode`, `frontmenu_options.c:267`; the toggle button itself,
  `frontmenu_ingame_opts_data.cpp:106`) switches the isometric camera's `view_mode` between
  `PVM_IsoWibbleView` (the classic DK1 "wibble" — buildings snap between 4 fixed facing angles as
  you rotate, a rendering-era trick, not a movement restriction) and `PVM_IsoStraightView` ("fluent
  rotation" per that enum's own doc comment, `player_data.h:67`) — set once in
  `init_player_cameras()` (`engine_camera.c:359-363`) for whichever camera an editor session also
  uses. Same category of finding as slice 1's Low Walls removal: the editor session doesn't hide
  the normal in-game Options menu, so this toggle is already reachable without any editor-specific
  work. Confirmed the rotation *mechanism* itself (`view_set_camera_rotation_inertia_around`,
  `engine_camera.c:291`) is continuous inertia-based rotation regardless of wibble/straight mode —
  "wibble" only affects how buildings are drawn at in-between angles, not whether the camera can
  reach them.

Given both of those, "editor camera profile" reduces to exactly one real gap, found by tracing the
actual zoom clamp: `process_camera_controls()` (`packets.c:440-441`) computes
`zoom_min = max(CAMERA_ZOOM_MIN, kfx_config_state.zoom_distance_setting)` — and
`zoom_distance_setting` (a gameplay "camera zoom distance" video-option slider, `config_keeperfx.c:
830`) is *always* `>= CAMERA_ZOOM_MIN` by construction (`LbLerp(4100, CAMERA_ZOOM_MIN, slider%)`),
so in practice the effective floor a player actually gets in normal gameplay is usually more
restrictive than the engine's own true hard floor, depending on where they've left that slider.
That's the one piece an editor session has a real reason to want loosened beyond what any existing
gameplay control already exposes: the ability to zoom out further than gameplay ever allows, to see
more of the map at once while placing far-apart features.

## What changed

- **`src/kfx_config/include/kfx_config_state.h`** — new `EDITOR_CAMERA_ZOOM_MIN` (130), alongside
  the existing `CAMERA_ZOOM_MIN`/`MAX` constants, with the same "eyeballed, adjusted by feel"
  framing those already carry in their own comments (`CAMERA_ZOOM_MIN`'s own history: originally
  4100, tuned down to 520). This is a starting point for live-test feedback, not derived from any
  actual on-screen-map-coverage calculation — retune if it's not far enough (or is too far) once
  tried against a real map.
- **`src/kfx_net/src/packets.c`** — `process_camera_controls()`'s `zoom_min` now checks
  `editor_callbacks->is_active()` (new `#include "editor_callbacks.h"`, already the established
  downward-callback seam for exactly this "does kfx_net need to know if an editor session is
  live" question — `front_input.c` already uses the same callback for the key-binding dispatch
  above) and uses `EDITOR_CAMERA_ZOOM_MIN` directly when true, bypassing `zoom_distance_setting`
  entirely rather than just lowering the floor by some fixed amount — an editor session has no
  reason to inherit a player's own gameplay comfort-zoom preference. `zoom_max` (the zoom-*in*
  limit, `CAMERA_ZOOM_MAX` = 12000) is left untouched — no concrete evidence surfaced that the
  zoom-in limit is a problem for placement work, so it wasn't loosened speculatively.

## What didn't need changing (and why)

- **Free rotate** — already available via `settings.video_rotate_mode` (Options > Graphics),
  already reachable during an editor session. No code change.
- **Pan/rotate/zoom/tilt key bindings** — already duplicated into `EditorGameKeys` and already
  dispatched correctly during an editor session, by `10-definable-keybindings.md`'s own slice, well
  before this phase started. No code change.
- **A momentarily out-of-gameplay-range zoom value after leaving the editor** — considered as a
  possible edge case (an editor session zoomed out past `CAMERA_ZOOM_MIN` leaves the camera's
  in-memory `zoom` field there; nothing re-clamps it back on exit) but not fixed: both real editor
  exit paths reset it anyway before it could matter — `editor_close()`'s only current caller sends
  `PckA_QuitToMainMenu` (full session teardown, back to the main menu), and Playtest starts a
  genuinely fresh level session (`FeSt_START_KPRLEVEL`), which re-runs `init_player_cameras()` and
  re-reads the zoom from `player->isometric_view_zoom_level`/settings rather than carrying over the
  camera object's last live value. No reachable path currently returns to *ongoing* normal
  gameplay with the editor's own camera object still live, so there's nothing to restore.

## Corrections needed in `04-views-camera-overlays.md`

Its §4/§5 "fully greenfield, no existing precedent" framing was wrong for two of this slice's three
original asks (key bindings, free rotate) — corrected there to point at this doc.

## Tests

- No new Catch2 coverage — the changed logic (`process_camera_controls()`'s zoom-clamp selection)
  is a live-session camera mutation with no `Catch2`-testable pure-function shape of its own (same
  category as `level_lost_go_first_person()` in slice 1, which also has none), and the constant
  itself has no behavior to unit-test beyond "it's a smaller number."
- Full ftest sweep (`-ftests -exitonfailedtest -headless`) — clean pass, exit code 0, 22/22 tests
  passed (fresh log, no rotation ambiguity this time).
- Manual live-test — pending; ask the user to confirm the editor's Map/Isometric view actually
  zooms out further than normal gameplay, and that leaving the editor doesn't leave the camera
  stuck at an unusually far-out zoom.

## Verification

- `python3 scripts/check_layering.py --strict` — no new violations (`kfx_net` including
  `kfx_config`'s `editor_callbacks.h` is a downward call, same direction `kfx_net` already depends
  on `kfx_config`).
- `keeperfx`/`keeperfx_hvlog` (native Linux) build clean.
- `kfx_net_utest`/`kfx_config_utest`/`kfx_editor_utest` — full suites pass, no regressions
  (83/2176/53 assertions).
- Full ftest sweep — clean pass (22/22), see Tests above.
