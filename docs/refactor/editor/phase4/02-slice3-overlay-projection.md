# Phase 4, slice 3 — world-space overlay projection spike

Status: **done.** The one item `04-views-camera-overlays.md` §6 flagged as needing its own
investigation/spike before committing to the rest of the overlay list. Landed as a small, real
primitive plus one genuine consumer, not a throwaway prototype.

## The question this slice had to answer

Can an arbitrary world position be projected to a screen position and drawn as a marker from
inside `editor_frame()` (an ImGui overlay running *after* the frame's own 3D scene has already
rendered), tracking correctly as the camera moves? Nothing in `kfx_editor` or the debug-overlay
precedent (`frontgui_ingame_debug.cpp`, all screen-space corner-anchored text/tables) does anything
like this today.

## What was found

- **The primitive already exists internally, just not exposed.** Every thing sprite drawn in the
  isometric view already goes through exactly this transform: `do_map_who_for_thing()`
  (`engine_render.c:8723-8780`) computes `ecor.x/y/z` by offsetting a world position against three
  file-local statics (`map_x_pos`, `map_y_pos`, `map_z_pos` — the camera's own position in the same
  coordinate space, refreshed every frame by `setup_rotate_stuff()`), then calls
  `rotpers(&ecor, &camera_matrix)` (`engine_lenses.c`, a public function pointer already selecting
  the active lens mode) to fill in `ecor.view_width`/`view_height` — screen pixel coordinates — and
  `ecor.clip_flags` (nonzero when the point is behind the camera or off the visible frustum/screen
  edges, confirmed via `flicker_fix()`/`pers_set_view_width()` in `engine_lenses.c`).
- **`map_x_pos`/`map_y_pos`/`map_z_pos`/`camera_matrix` are `static` to `engine_render.c`** — not
  reachable from any other file, let alone another library. This is the actual gap: not "no
  projection math exists" (it does, and is exercised every frame for every visible thing), but "no
  public entry point wraps it."
- **Frame ordering already guarantees freshness.** `RendererSoftware.cpp`'s present step
  (`RendererSoftware.cpp:153-192`) blits the just-rendered 3D scene to the screen, *then* calls
  `RendererRunImGuiFrameCallback()` (which reaches `editor_frame()` via `main.cpp`'s
  `app_imgui_frame()`) — both within the same frame, before `SDL_RenderPresent()`. So
  `map_x_pos`/`map_y_pos`/`map_z_pos`/`camera_matrix` are guaranteed current for the frame an ImGui
  overlay draws into; no one-frame lag to account for.
- **`draw_map_volume_box()`/`map_volume_box`** (the existing generic world-space box mechanism the
  original design doc pointed at for "reuse roomspace highlight rendering") turned out to be the
  wrong shape for this: it's *declarative pre-render state* — set before the 3D pass runs, consumed
  *during* that pass by the software rasterizer — not something an ImGui overlay (which only runs
  *after* that pass finishes) can invoke to draw something new post-hoc. Real for slice 5's brush
  rectangle if that overlay is driven from game-logic code that runs early enough in the frame, but
  not usable from `editor_frame()` itself the way a point marker needs to be.

## What was built

- **`src/kfx_render/include/engine_render.h` + `.c`** — new public
  `TbBool project_world_position_to_screen(MapCoord x, MapCoord y, MapCoord z, long *screen_x, long *screen_y)`,
  placed next to `draw_map_volume_box()`. Implements the exact `do_map_who_for_thing()` recipe
  (camera-relative offset + `rotpers()`), returns `false` (leaving the output params untouched)
  when `clip_flags != 0` — the same "off-screen or behind the camera" signal the renderer's own
  internal code already relies on. `kfx_editor` can call this freely (it's ranked above
  `kfx_render`); no callback-struct plumbing needed, matching `04-views-camera-overlays.md`'s
  earlier finding that `editor_frame()` is already a direct, same-frame peer of every other
  per-frame draw call.
- **First real consumer, not a discarded prototype**: `src/kfx_editor/src/editor_dialogs.cpp`'s
  Verify Map dialog now draws an on-screen marker (a filled circle, colored red/yellow/blue by
  severity, `verify_severity_marker_color()`) for every issue with `has_pos`, every frame the
  dialog is open (`draw_verify_map_markers()`, called from `editor_dialogs_frame()` right after
  `draw_verify_map_dialog()`) — using `ImGui::GetForegroundDrawList()`, which composites above the
  3D scene the same way the rest of the editor's ImGui UI already does. This is exactly the visual
  half of the "verification flags" overlay item `04-views-camera-overlays.md` §3 already noted as
  the one place the non-visual half (Verify Map's own "Zoom" button) had already shipped in phase
  3 slice 7 — so this slice both proves the projection mechanism *and* closes that specific gap,
  rather than leaving an inert unused function behind. Markers use `z=0` (ground level) for every
  issue — `MapVerifyIssue` carries no height, and a flag sitting on the ground is a reasonable
  default for "something is wrong around here."

## What's still open for later overlay slices

- **Slab grid, coordinate readout, ownership tint, thing/light/AP/hero-gate markers, brush
  rectangle** — all still unbuilt (slices 4-5 per `04-views-camera-overlays.md` §6). Each is now a
  straightforward consumer of `project_world_position_to_screen()` (points/markers) or needs its
  own investigation for the brush rectangle specifically (declarative pre-render state, not a
  postprocess overlay — see the `draw_map_volume_box()` finding above).
- **No toggle exists yet to hide/show markers independently** — Verify Map's markers are tied to
  the dialog's own open/closed state, matching how the dialog's list itself already works; a
  separate persistent "show verification flags" toggle (independent of the dialog) is not part of
  this slice's scope, and wasn't asked for.

## Tests

- No new Catch2 coverage — `project_world_position_to_screen()`'s correctness depends entirely on
  live per-frame renderer state (`map_x_pos`/`map_y_pos`/`map_z_pos`/`camera_matrix`, all
  file-local statics with no fixture-constructible value outside a running render pass), the same
  category of "no pure-function shape to unit-test" as `rotpers()`/`do_map_who_for_thing()`
  themselves, neither of which has Catch2 coverage today either.
- Full ftest sweep (`-ftests -exitonfailedtest -headless`) — clean pass, exit code 0, 22/22.
- Manual live-test — pending; ask the user to open Verify Map on a map with at least one flagged
  issue (an embedded thing or a missing Heart are the easiest to trigger deliberately) and confirm
  a colored marker appears at the right on-screen spot and tracks correctly as the camera
  pans/zooms/rotates.

## Verification

- `python3 scripts/check_layering.py --strict` — no new violations (`kfx_editor` calling
  `kfx_render`'s new public function is a downward call, same direction every other `kfx_editor`
  call into `kfx_render`/`kfx_sim`/etc. already goes).
- `keeperfx`/`keeperfx_hvlog` (native Linux) build clean.
- `kfx_render_utest`/`kfx_sim_utest`/`kfx_editor_utest` — full suites pass, no regressions
  (322/2103/53 assertions).
- Full ftest sweep — clean pass (22/22), see Tests above.
