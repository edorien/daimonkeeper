# Phase 4, slice 5 — marker overlays, and the brush rectangle that turned out to already exist

Status: **done.** The last slice of phase 4's original scope. Grouped exactly as the design doc's
own overlay list did: thing markers, light markers, and one combined action-point/hero-gate
marker toggle (the doc listed those two together as a single bullet, and a hero gate is itself a
kind of thing — the pairing tracks the data shape, not an arbitrary split).

## The brush/mark rectangle: already delivered, no new work needed

Investigated before writing anything, since slice 3 had already found `draw_map_volume_box()`
(the mechanism `04-views-camera-overlays.md` originally pointed at for this overlay) was the wrong
shape for a post-render ImGui overlay — declarative pre-render state, not something `editor_frame()`
could invoke after the fact. That raised the question of whether the brush rectangle needed
building from scratch. It doesn't: `packets_cheats.c`'s `editor_rect_drag_update()` (lines 61-109)
— the shared drag-state machine every "mark a box by dragging" tool already uses (Terrain
Rectangle, Clear-to-Earth, Delete-Things-Inside, Set-Owner, all from phase 2) — already calls
`draw_map_volume_box()` itself, every frame a drag is in progress, from *inside* packet
processing (which runs before the 3D render pass, exactly where that function needs to be called
from). The live yellow preview box a user already sees while dragging a Terrain Rectangle
selection **is** the brush/mark rectangle overlay — built in phase 2, still working, needing
nothing from phase 4. Confirmed this path is reachable during a normal editor session (the tools
using it are already shipped, per `02-editing-toolbox.md`/`09-toolbox-remainder.md`). No code
changed for this item; it's listed here as a scope item resolved by investigation, not a gap.

## What was built

All three toggles live in the same `editor_overlay.h`/`.cpp` slice 4 already created, extended
rather than duplicated:

- **Thing Markers** — a small colored dot + short label (`C<model>`/`O<model>`/`T<model>`/
  `D<model>`) over every creature/object/trap/door, via `thing_get()`/`thing_is_invalid()` scanning
  all `THINGS_COUNT` (12288) slots. Hero gates (`thing_is_object(thing) && object_is_hero_gate(thing)`)
  are explicitly skipped here — drawn by the AP/Hero Gate toggle instead.
- **Light Markers** — a yellow dot + an approximate radius ring over every *static* light
  (`lish.lights[]`, `LgtF_Allocated` set and `LgtF_Dynamic` clear — a dynamic light is a spell
  effect or creature-carried light, not level-designer-placed content, and would churn every frame
  Preview Motion runs). No "selected light" concept exists (no light-editing tool exists yet per
  the phase roadmap) — simplified to always showing every static light rather than gating behind a
  selection UI that doesn't exist; a real scope reduction, not an oversight.
- **AP / Hero Gate Markers** — one toggle covering both: every existing `ActionPoint`
  (`action_point_get()`/`action_point_exists()`, `ACTN_POINTS_COUNT` = 256) gets a marker + its
  trigger-radius ring (`apt->range`, confirmed MapCoord-scale by its own usage in
  `actionpt.c:220-221`'s distance check, not raw subtiles); every hero gate gets a distinct
  magenta marker labelled with its real gate number (`thing->hero_gate.number`).
- **`draw_marker()`/`draw_radius_ring()`** — two small shared helpers added to `editor_overlay.cpp`,
  used by all three new overlays (and reusable by anything added later): a filled-circle-plus-label
  marker (same shape slice 3's Verify Map markers already established) and a radius ring
  approximated by projecting the center and one point offset by the radius along the world X axis,
  then using the screen-space distance between them as the on-screen circle radius. This is a
  deliberate simplification — a true 3D radius projects to an ellipse under the isometric
  perspective, not a circle — documented as "about this big," not exact ground-footprint geometry.

## Tests

- No new Catch2 coverage — same reasoning as slice 4: every new function here depends on live
  per-frame renderer/session state (`project_world_position_to_screen()`'s statics, the live thing/
  light/action-point tables) with no fixture-constructible pure-function shape.
- Full ftest sweep (`-ftests -exitonfailedtest -headless`) — clean pass, exit code 0, 22/22.
- Manual live-test — pending; ask the user to place a few creatures/objects/traps/doors, a light,
  an action point, and a hero gate on a test map, toggle each of the three new View items, and
  confirm markers appear at the right positions with sensible labels, and that the light/AP radius
  rings are roughly the right size.

## Verification

- `python3 scripts/check_layering.py --strict` — no new violations (all new includes —
  `thing_data.h`/`thing_creature.h`/`thing_objects.h`/`thing_traps.h`/`thing_doors.h`/
  `thing_list.h`/`light_data.h`/`actionpt.h` — are `kfx_sim`/`kfx_render` headers, both lower than
  `kfx_editor`).
- `keeperfx`/`keeperfx_hvlog` (native Linux) build clean, first try.
- `kfx_editor_utest` — full suite passes, no regressions (53 assertions).
- Full ftest sweep — clean pass (22/22), see Tests above.

## Phase 4 status after this slice

All five originally-planned slices are done:

1. View menu wiring (Map View, 1st Person, Lights)
2. Camera zoom-clamp loosening
3. World-space overlay projection primitive + Verify Map's visual markers
4. Core overlays (slab grid, coordinates, ownership tint)
5. Marker overlays (things, lights, AP/hero gates) — brush rectangle already existed

See `04-views-camera-overlays.md`'s own status line for the up-to-date summary and the full list
of corrections found along the way.
