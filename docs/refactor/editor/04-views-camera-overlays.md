# Phase 4 — views, camera, and editor overlays

Status: **all 5 slices done, plus a post-ship live-test fix round.** View menu wiring; camera
zoom-clamp loosening; world-space overlay projection spike + Verify Map's visual markers; slab
grid/coordinates/ownership tint; thing/light/AP/hero-gate markers (the brush rectangle turned out
to already exist, built in phase 2). Live-testing then found the zoom-out loosening from slice 2
could exhaust the renderer's polygon pool (`POLY_POOL_SIZE`) on large/open maps, dropping part of
the 3D view to black — root-caused via a live diagnostic (not a guess) and fixed by raising that
pool 4×; see [`phase4/05-live-test-fixes.md`](phase4/05-live-test-fixes.md), confirmed working.
Depends on phase 1 (done). Independent of 2/3 (both now done). See
[`phase4/00-slice1-view-menu.md`](phase4/00-slice1-view-menu.md),
[`phase4/01-slice2-camera-profile.md`](phase4/01-slice2-camera-profile.md),
[`phase4/02-slice3-overlay-projection.md`](phase4/02-slice3-overlay-projection.md),
[`phase4/03-slice4-core-overlays.md`](phase4/03-slice4-core-overlays.md), and
[`phase4/04-slice5-marker-overlays.md`](phase4/04-slice5-marker-overlays.md) for what actually
landed, including several corrections to this doc's own pre-implementation claims found only once
each slice was attempted: collapsing "Plan"/"Full Map" into one item;
`level_lost_go_first_person()` requiring an existing creature rather than merely preferring one;
Low Walls dropped from the View menu after landing (duplicate of the existing Options > Graphics
toggle); slice 2's "camera profile" turning out to be almost entirely already-existing (free
rotate is an existing Options toggle, camera key bindings were already duplicated for the editor
by an earlier, unrelated slice) — only the zoom-out floor genuinely needed loosening; slice 3
finding the world-space projection math already exists internally (`rotpers()`/`EngineCoord`,
exercised every frame for every visible thing) just not exposed — the real gap was one small
public wrapper, not new math, and `draw_map_volume_box()`'s pre-render declarative state (which
this doc originally pointed at for reuse) turned out to be the wrong shape for a post-render ImGui
overlay; and slice 5 finding the brush/mark rectangle overlay item was already fully delivered by
phase 2's own Terrain Rectangle tool (`draw_map_volume_box()` called from packet processing, the
one place that mechanism is actually usable) — nothing to build there at all. Trust those tracking
docs over this one on any conflict, same "plan vs. actual" precedent phase 3's slice docs already
established.

Goal, unchanged from the original scoping: the View menu from the original editor (Plan /
Isometric / 1st-person / Lights / Map / Low Walls) plus the editing overlays a modern editor
needs (grid, coordinates, thing/light/AP markers, verification flags).

**Re-verified before expanding this doc** — this repo is on a `refactor-renderer` branch, and this
session's own experience with phase 3's docs (an "ADiKtEd port" claim citing a library that isn't
vendored; a "phase 4 overlay" a later phase assumed existed but didn't) is a direct warning that
nothing here should be trusted without checking. Most of the doc's original claims held up; two
did not, and they change how phase 4 should be scoped — see §1 and §3 below for what changed and
why.

---

## 1. What the engine already gives us

| Original View item | Engine equivalent today | Confirmed |
|---|---|---|
| Isometric | the normal 3D game view (`ViewMode` default) | — |
| Plan / top-down | parchment map **zoom box** magnifier — `draw_zoom_box` (`src/kfx_frontend/src/gui_parchment.c:876`), calling `draw_zoom_box_terrain()`/`draw_zoom_box_things()` (`:817`/`:849`) | Renders terrain **and** things, confirmed by reading both helpers. `local_state.minimap_zoom` (`player_data.h:299`) ranges 128–2048 live (`frontmenu_ingame_tabs.c:140-151`); the *persisted* `settings.minimap_zoom` config field only allows 256–2048 (`config_settings.c:693`) — 128 is session-only, not save-restorable as-is if the editor wants to persist a default zoom. |
| 1st Person | the **spectator ("ghost") camera** — `level_lost_go_first_person()` (`player_instances.c:1429-1461`) / `PckA_GoSpectator` (`packets.c:877-879`) / `crtr_conf.spectator_breed` (`config_creature.c:2140-2149`) / `CMF_IsSpectator` (`config_creature.h:74`) | All four confirmed real. Traced the full call chain: `level_lost_go_first_person()` only *picks a position* (a random owned creature's `mappos`) before calling `create_and_control_creature_as_controller(player, spectator_breed, &mappos)` (`creature_control.c:134`) — that function already takes an arbitrary `Coord3d*`. An editor variant just needs to supply the cursor's world position instead of a found creature's — genuinely small, confirmed not just assumed. Exit via `PckA_DirectCtrlExit` (`packets.c:1398-1408`) is real too. |
| Lights toggle | `cmd_toggle_lights` (`console_cmd.c:2630`) / `PckA_ToggleLights` (`packets.c:669-673`) | Both call `light_set_lights_on(lish.light_enabled == 0)` — a straight flip of one global flag. |
| Map (full screen) | parchment full-screen map | Same underlying screen as the zoom-box magnifier's parent. |
| Wall Height toggle | `settings.video_cluedo_mode` / `PckA_SetCluedo` (`packets.c:678-683`, a straight settings write + `save_settings()`) | Real; wall-height-5-vs-2 logic lives in `floor_height_for_volume_box()` (`engine_render.c:2366` — **not** `:2491` as an earlier draft of this doc cited; that line is unrelated box-outline code today). Two other sites echo the same 5-vs-2 convention (`engine_render.c:8400-8401`, `:9228`); `fill_in_points_cluedo()`/`do_a_plane_of_engine_columns_cluedo()` (`:1096`/`:4553`) drive the actual geometry. **Forward-looking note**: this repo also has a real, merged-but-disabled-by-default OpenGL/GPU renderer backend (`src/kfx/renderer/RendererOpenGL.cpp` et al.) with **zero cluedo-mode support** (`grep -i cluedo` over that tree is empty) — not a blocker for phase 4 (the software renderer is what actually runs today), but whoever eventually enables that backend will need to port this logic too. |

**View-menu wiring is still, confirmed, just wiring** — every item above is an existing engine
mechanism with a real, already-working call path. This part of the original "no new renderer
work" framing holds up.

## 2. View menu (editor menu bar → View)

The editor's menu bar **already has a View menu** — built in phase 3 slice 3
(`editor_menubar.cpp:69-84`), currently holding exactly one item ("Preview Motion (unpause)", a
sim-tick toggle unrelated to camera/view-mode). Phase 4 extends this existing menu, not a
from-scratch one.

- **Isometric** (default): normal camera. Wants further zoom-out than gameplay allows — done in
  slice 2 (§4; turned out to need only a zoom-floor change, not a new camera mode).
- **Plan / Top-down**: open the parchment map with the zoom box magnifier (`draw_zoom_box`) — a
  true top-down projection rendering terrain + things already. The editor's Plan toggle drives
  that; no new ortho camera. (In-place editing under a top-down 3D camera, if wanted later, is a
  separate renderer ask — not v1.)
- **1st Person**: spawn + possess a spectator creature at the cursor. A small
  `PckA_EditorGoSpectator` variant taking a position (confirmed buildable as a thin variant of
  `level_lost_go_first_person`'s own call chain, see §1's table). Exit via the existing
  `PckA_DirectCtrlExit`. WASD + mouse look come free with the possessed-creature control scheme.
  Needs `crtr_conf.spectator_breed` set (standard KFX config, already has a fallback + warning if
  unset).
- **Full Map**: existing parchment full-screen map, with editor annotations (AP numbers, hero
  gates, heart locations) — these annotations are new overlay content, see §3.
- **Lights**: `PckA_ToggleLights` — preview dynamic lighting on/off.
- **Wall Height (Low Walls)**: real and wired (`PckA_SetCluedo` / `settings.video_cluedo_mode`,
  confirmed in the table above), but **deliberately not in the View menu** — slice 1 shipped it,
  then removed it on feedback: the editor session doesn't hide the normal in-game Options menu,
  and Options > Graphics already exposes this exact setting, so a second control for it in View
  would just be a duplicate, not a missing feature the editor needed to surface.

## 3. Overlays — the real technical risk of this phase

**Corrected from the original doc.** The original plan said overlays would go "through the same
overlay path the in-game GUI / debug overlays use (`RenderOverlayCallbacks`,
`frontgui_ingame_debug.cpp`)". Investigated directly — this is wrong on what the mechanism *is*,
but the actual situation turns out better in one way and needs real new work in another:

- **`RenderOverlayCallbacks`** (`render_overlay.h:34-133`) is **not** a pluggable overlay-content
  system. It's a one-directional layering-inversion callback struct that lets `kfx_render` (a
  lower layer) call *up* into pre-existing `kfx_frontend`/`kfx_net` GUI functions it can't
  `#include` directly (parchment redraw, status-panel drawing, tooltips, camera-control
  processing, and — among many others — `draw_debug_overlays`). It's plumbing for *existing* code,
  not a toggle-content-on/off seam a new feature would hook into.
- **The debug overlays this callback drives are mostly dead today anyway.**
  `render_overlay_draw_debug_overlays()` (`main.cpp:967-975`) only runs for the **classic** HUD;
  under the modern ImGui HUD (the normal case), `ingame_debug_overlays_frame()`
  (`frontgui_ingame_debug.cpp:273-282`) draws the same content instead, called directly from
  `FrontendImGuiFrame()` → `app_imgui_frame()` (`main.cpp:1126-1130`) — **the exact same wrapper
  that already calls `editor_frame()` on the very next line.** So `kfx_editor`'s own overlay code
  doesn't need the callback struct at all — it's already a direct peer of the debug overlays in
  the same per-frame hook, wired once in `main.cpp` (the one file allowed to see every layer).
  This is genuinely simpler than the original plan assumed: no new callback plumbing needed.
- **Every *existing* overlay in this codebase was screen-space, not world-space — resolved in
  slice 3.** `frontgui_ingame_debug.cpp`'s content (`:31-282`) is corner-anchored ImGui windows
  (`begin_corner_overlay()`, `:42-50`) showing text/tables — gameturn counter, frametime, network
  stats — nothing that projects a world/map coordinate onto the screen. Slice 3
  (`phase4/02-slice3-overlay-projection.md`) closed this gap: a new public
  `project_world_position_to_screen()` (`kfx_render`'s `engine_render.h`/`.c`) wraps the exact
  camera-relative-offset-plus-`rotpers()` transform every thing sprite already goes through every
  frame (`do_map_who_for_thing()`), which turned out to already exist internally — the actual gap
  was that `map_x_pos`/`map_y_pos`/`map_z_pos`/`camera_matrix` were `static` to `engine_render.c`,
  not that the math itself was missing. First consumer: Verify Map's issue markers, live now. The
  rest of this list can build on the same function directly.

The rest of the overlay list:

- **Slab grid — done (slice 4).** One line per slab boundary, heavier every 5th, via
  `project_world_position_to_screen()`. Not clipped to the visible screen portion — a documented
  simplification, see `phase4/03-slice4-core-overlays.md`.
- **Coordinate readout — done (slice 4).** Cursor slab (x,y) and subtile, drawn bottom-left.
  `cmd_toggle_tooltip_land_coord` (`console_cmd.c:2623-2627`, confirmed real, flips
  `tool_tip_dbg.land_coord`) was the closest *existing* precedent before this; the editor's own
  version is new ImGui-drawn content built on `screen_to_map()`, not a reuse of that debug toggle.
- **Ownership tint — done (slice 4).** Translucent player-coloured quad per owned slab, colour
  table duplicated from `frontmenu_landpreview.c`'s own minimap colours (that table is file-local,
  not exported).
- **Thing markers — done (slice 5).** A colored dot + short label
  (`C<model>`/`O<model>`/`T<model>`/`D<model>`) over every creature/object/trap/door (hero gates
  excluded — see below).
- **Light markers — done (slice 5).** A dot + approximate radius ring over every *static* light
  (`LgtF_Allocated` set, `LgtF_Dynamic` clear). No "selected light shows radius" distinction —
  no light-editing tool exists yet to select one, so every static light's radius shows always; a
  real scope reduction, not an oversight.
- **Action point / hero gate markers — done (slice 5), one combined toggle.** AP number + trigger
  radius ring for every action point; a distinct-coloured marker with the real gate number for
  every hero gate.
- **Verification flags — done.** Phase 3 slice 7's Verify Map dialog already had the non-visual
  half (a "Zoom" button sending `PckA_ZoomToPosition`); slice 3 of this phase added the visual
  half — a colored marker (red/yellow/blue by severity) drawn at each positioned issue's location,
  every frame the dialog is open, via `project_world_position_to_screen()`.
- **Brush/mark rectangle — already delivered, no phase 4 work needed (found in slice 5).**
  `draw_map_volume_box()` (which slice 3 found was the wrong shape for a post-render ImGui
  overlay) turned out to already be called exactly where it needs to be: `packets_cheats.c`'s
  `editor_rect_drag_update()`, the shared drag-state machine every "mark a box by dragging" tool
  (Terrain Rectangle, Clear-to-Earth, and friends) already uses, calls it every frame a drag is in
  progress, from inside packet processing — before the 3D pass, exactly the right place. The live
  preview box a user already sees while dragging a Terrain Rectangle selection *is* this overlay
  item, built in phase 2.

## 4. Camera controls in the editor

**Corrected — not the greenfield an earlier pass concluded.** That earlier read covered
`editor_session.cpp`/`editor_toolbox.cpp` only (phases 1-3 added exactly one camera-touching block
there, `editor_open()`'s heart-less-map centering fix, and four read-only `screen_to_map()` calls
for click-to-place targeting) and missed that pan/rotate/zoom/tilt key bindings for the editor were
already built by a separate, earlier slice — [`10-definable-keybindings.md`](../10-definable-keybindings.md)
— duplicating them into `EditorGameKeys` and wiring `front_input.c`'s
`get_isometric_view_nonaction_inputs()` to read from that table whenever an editor session is
active. Free pan and free rotate (via the existing `settings.video_rotate_mode` Options toggle,
see [`phase4/01-slice2-camera-profile.md`](phase4/01-slice2-camera-profile.md)) were both already
in place before phase 4 started; slice 2's only real gap was the zoom-out floor, now fixed
(`EDITOR_CAMERA_ZOOM_MIN`).

- **Zoom-to**: `PckA_ZoomToPosition` — already exists, already used by the editor (Verify Map's
  own "Zoom" button, `editor_dialogs.cpp`).
- **Bookmarks**: **corrected from the original doc** — `PckA_BookmarkLoad` is not a
  save/recall-numbered-slot bookmark system today. Every real call site
  (`front_input.c:696`, `frontmenu_ingame_map.c:868,895`) sends it purely as "jump the camera to
  this world position" on a minimap click/drag — it's shaped exactly like `PckA_ZoomToPosition`,
  just driven from minimap coordinates specifically. A real numbered-bookmark feature would be new
  state (an array of saved camera positions) on top of this same jump-primitive, not something
  that already exists to build on. Still not in the original 1998 editor either — stays deferred,
  now for the right reason (nothing to reuse, not "exists but out of scope").

## 5. What must exist after phase 4

1. Editor View menu (extending the one already there): Isometric / Map View (parchment zoom box +
   full map, one item — see §2) / 1st Person (spectator camera) / Lights — all wiring to existing
   engine features, confirmed real end-to-end and shipped in slice 1. Low Walls deliberately
   excluded (§2) — already reachable via Options > Graphics.
2. Editor camera profile: done in slice 2 — turned out free rotate and the pan/rotate/zoom key
   bindings already existed (Options toggle; an earlier, separate D6 slice); only the zoom-out
   floor needed a real change (`EDITOR_CAMERA_ZOOM_MIN`, `phase4/01-slice2-camera-profile.md`).
3. A world-space overlay projection primitive — done in slice 3
   (`project_world_position_to_screen()`, `phase4/02-slice3-overlay-projection.md`), plus its
   first consumer (Verify Map's visual markers).
4. Toggleable overlays built on that primitive: slab grid, coordinates, ownership tint (all three
   done in slice 4, `phase4/03-slice4-core-overlays.md`), thing markers, light markers,
   AP/hero-gate markers (all three done in slice 5, `phase4/04-slice5-marker-overlays.md`;
   verification-flag markers done in slice 3), mark/brush rectangle (already existed — phase 2's
   `draw_map_volume_box()` call from `editor_rect_drag_update()`).
5. Overlays render only when `editor_is_active()`, drawn directly from `editor_frame()`'s own
   per-frame hook in `main.cpp` — no callback-struct plumbing needed (corrected from the original
   plan).

## 6. Suggested slicing — all done

Mirrors the phase-3 approach (small, test-covered, one clear deliverable each) rather than one
large pass:

1. **View menu wiring** — done (`phase4/00-slice1-view-menu.md`): Map View, 1st Person, Lights,
   all confirmed-real engine mechanisms. Low Walls shipped then removed on feedback (§2). Lowest
   risk, done first, builds confidence before the harder overlay work.
2. **Editor camera profile** — done (`phase4/01-slice2-camera-profile.md`): only the zoom-out
   floor needed changing; pan/rotate/zoom bindings and free rotate were already in place.
3. **Overlay projection spike** — done (`phase4/02-slice3-overlay-projection.md`): the projection
   math already existed internally (`rotpers()`/`EngineCoord`), just needed one small public
   wrapper (`project_world_position_to_screen()`); shipped with a real first consumer (Verify
   Map's visual markers) rather than as a discarded prototype.
4. **Core overlays** — done (`phase4/03-slice4-core-overlays.md`): slab grid, coordinate readout,
   ownership tint, each an independent View-menu toggle built on
   `project_world_position_to_screen()`.
5. **Marker overlays** — done (`phase4/04-slice5-marker-overlays.md`): thing/light/AP/hero-gate
   markers (verification flags already done in slice 3); the brush/mark rectangle turned out to
   already be delivered by phase 2's own Terrain Rectangle tool, no new code needed.

## 7. Tests

- ftest `editor_views`: cycle Isometric → Plan → 1st Person (spectator) → Full Map → back; assert
  no crash, spectator spawned/despawned, camera mode changes, `editor_is_active()` preserved.
- ftest `editor_overlay_toggles`: toggle each overlay on/off over N frames, assert stable.
- Manual/visual: the user reviews grid alignment, wall-height cut, and marker legibility on a real
  map (live-desktop review — ask first per the repo's interaction rules; also the "no live display
  in this environment" limitation every phase-3 slice hit).
