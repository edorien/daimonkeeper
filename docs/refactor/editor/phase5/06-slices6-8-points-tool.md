# Phase 5, slices 6–8 — the Points tool (lights, action points, effect generators)

Status: **done** (live-test pending).

Slices 6 (lights), 7 (action points) and 8 (effect generators) were planned as three separate
tools. Investigation showed all three are "a position plus an effect radius" with the same
place / select / resize / delete lifecycle, so they are one tool: **Things > Points**.

## What it does

- **Kind selector** — Light / Action Point / Effect Generator. Each kind has its own defaults for
  *new* points (radius/intensity/height, range, FX kind + range).
- **Place** — click empty ground. The point snaps to the subtile centre (shipped maps' effect
  generators sit at `[n, 128]`).
- **Drag to size** — press, drag outward, release: the distance from the press point is the radius
  (a green preview ring shows it live). A drag shorter than 0.75 subtile is a plain click and uses
  the panel default.
- **Select** — click a marker (picked in *screen* space with a 16 px radius, using the same
  projection the markers are drawn with). The selection is highlighted in red.
- **Edit** — the inspector edits X/Y, radius/range, and per kind: light intensity + height, action
  point number, effect-generator kind. Sliders apply live; X/Y/number apply when the field is left.
- **Delete** — `Del` or the Delete button. (The design doc's RMB-delete was not used: RMB-drag is
  already the Stamp capture gesture and camera input, and a key/button is unambiguous.)
- **Undo/redo** — placement and deletion are journaled (new `EJK_Point` entry kind, History tab
  shows "Place Light" etc.). Property edits are live but not journaled, same as the object position
  edit.
- **Budgets** — the panel shows light / action point / effect generator counts, with a warning once
  static lights fill half the light slots (the threshold `lvl_filesdk1.c` already warns at on load).
  `verify_map()` gained a lights cap check (ERROR at 2048, WARN at 90%), alongside the existing
  thing / creature / action-point caps.
- **Markers** — effect generators previously had no marker at all (invisible things). They now draw
  as `FX` dots with a range ring under View > Thing Markers; while the Points tool is active every
  point is drawn regardless of the View toggles (kinds whose toggle is on are left to the overlay
  so nothing is drawn twice).

## Investigation

| Question | Finding |
|---|---|
| How is a static light created? | `light_create_light(InitLight*)`; `radius` is a `short` in raw units (so 128 subtiles max), `intensity` a byte. `light_delete_light()` already flags the old footprint for a static-light refresh, `light_create_light()` flags the new one — so editing a light = delete + create, the only path known to keep the static light map right. |
| Action point creation | `actnpoint_create_actnpoint()`; **no "next free number" helper exists** (confirmed) — added locally (lowest unused ≥ 1; number 0 would collide with the sentinel slot 0, whose `num` is also 0). Range is raw units, tested against distance in `process_action_points()`. |
| Effect generators | `create_effect_generator(pos, model, range, owner, parent)`, a `TCls_EffectGen` thing. `range` is raw units (`UNSYNC_RANDOM(range + 1)` deviation). Kinds come from `effectgen_desc` / `effectgen_cfgstats_count` via `effectgenerator_code_name()`. Shipped maps use owner 5 (neutral) and `EffectRange = [5, 0]` (5 subtiles). |
| Hero gates | **No new code needed** (as the plan predicted): `HERO_GATE` is an ordinary entry in the existing Object picker, and `thing_objects.c` auto-assigns `hero_gate.number` via `get_free_hero_gate_number()` for any route that creates one. |
| Packets vs direct? | Direct, per the D2 single-player-local exception the rect-terrain undo/Stamp/Paint Texture already use. Lights are render state that was never synced, and each verb would otherwise need a `PckA_*` plus a Redo twin (the journal's ambient-position history in `editor_journal.cpp`). |

### Two latent save bugs found and fixed on the way

Both were pre-existing in `editor_mapsave.cpp`, both would have made the new tool's output not
survive a save:

1. **Effect generators saved with range 0 and no parent.** `snapshot_thing()` had no
   `TCls_EffectGen` case, so `effect_range` and `parent_tile` were never captured. Every effect
   generator in a saved map came back with range 0.
2. **Thing-owned lights were saved as level lights.** `snapshot_lights()` wrote every allocated
   light. A torch, a creature glow, a spell effect, and each player's cursor light all own a light
   (`thing->light_id`, `UserState::cursor_light_idx`) that its owner recreates on load — so each
   save/reload cycle added a second, level-owned copy (and made the cursor light permanent). It now
   skips lights referenced by a thing or a player cursor
   (`editor_points_mark_thing_owned_lights()`). Slab-attached static lights (wall torches with
   `attached_slb`) are *not* thing-owned and are still saved, as before. The same predicate keeps
   the Points tool from selecting/deleting a thing's own light out from under it.

## Files

- `editor_points.h/.cpp` (new) — the tool; `editor_toolbox.cpp` adds the `Points` button (Things
  tab) and forwards panel/frame; new work state `PSt_EditorPlacePoint` (appended to
  `config_players.h`, click-routing only).
- `editor_journal.h/.cpp` — `EJK_Point` entries. Undo of a placement deletes, of a deletion
  recreates; the delete re-checks position (and model for effect generators) so a reused light /
  thing slot is never removed by a stale entry.
- `editor_overlay.h/.cpp` — `FX` markers; `editor_overlay_draw_marker()` / `_draw_radius_ring()`
  exported for the tool.
- `editor_mapsave.cpp` — the two save fixes above.
- `map_content_verify.cpp` — lights cap.

## Live-test finding: only the lava generator was placeable

The first live-test showed the effect-generator picker offering nothing beyond the default (lava).
Cause: the picker counted kinds with `effects_conf.effectgen_cfgstats_count`, which the config
loader never writes, so the list was empty and only the initial `s_def_fx_model = 1`
(`EFFECTGENERATOR_LAVA`) could be placed. It now walks `effectgen_desc` (the NULL-terminated table
the loader fills), giving all five real kinds: LAVA, DRIPPING_WATER, ROCK_FALL, ENTRANCE_ICE,
DRY_ICE. Guarded by a check in the `editor_points` ftest (`editor_points_effectgen_kind_count() >= 2`).

**Other light types:** none exist to offer. The `.lgtfx` format persists only position, range,
intensity, parent slab and a `Dynamic` flag; the flicker/oscillation/animation bits live in
`Light::flags` but the loader has a literal "TODO: not implemented yet" for them, so nothing could
round-trip. Dynamic lights are per-frame, shadow-cache-limited and belong to things/spells, not
level design. Everything else that glows (torches, the heart, magic lights) is a *thing's own*
light, placed via the Object tool.

## Known limits

- Static lights only (`LgtF_Dynamic` clear). Light flags beyond the file format (flicker etc.) are
  not editable: the `.lgtfx` loader itself has a literal "TODO: not implemented yet" for them, so
  there is nothing to round-trip. Intensity is offered 1–255 (the byte's range); whether values
  past the ~63 the engine's own lights use visibly saturate was not verified.
- Renumbering an action point does not update script references to it (the inspector says so).
- Effect generator owner is fixed to neutral (what shipped maps use); no picker.
- Property edits aren't undoable; placement/deletion are.
- A click within 16 px of an existing marker selects it rather than placing — to put a point that
  close to another, place it first and move it with X/Y.
- Ambient light is still a scope finding, not part of this slice (per-campaign
  `rules.cfg`, see phase5/03).

## Tests

- **Catch2** — `map_content_verify_test.cpp`: lights cap (none / ERROR at cap / WARN near it).
- **ftest `editor_points`** (new, `ftest_editor_points.c`): creates a light, action point 7 and an
  effect generator through the same create/journal entry points the tool uses; undoes ×3 (all gone)
  and redoes ×3 (all back); saves KFX-native to scratch level 90003; reloads via `load_map_file()`;
  asserts each point's position/size/model came back **and** that the count of level-owned lights is
  unchanged across the round trip (the duplicate-torch-light regression).
- The click/drag/inspector UI is glue over that path and is covered by the live-test.

## Verification

- `check_layering.py --strict` clean; `kfx_editor_utest` 104/24, `kfx_sim_utest` 2181/678;
  `keeperfx` + `keeperfx_hvlog` build clean; full ftest sweep 23/23, exit 0.
- Mutation check: with the thing-owned-light filter disabled the new ftest fails
  ("2 before, 10 after"), so the round-trip guard is real, not vacuous.
- Test-harness note: `load_map_file()` alone does not reset things, lights or action points (a real
  level start runs `clear_game()`/`init_level()` first), so the existing `editor_save_reload`
  ftest's reload sits on top of the un-cleared original. The new ftest does the relevant part of
  that reset (`clear_things_and_persons_data()`, `light_initialise()`,
  `delete_all_action_point_structures()`) so "found after reload" really means loaded from the file.

Manual live-test pending: Things > Points; place a
light (drag to size), an action point and an FX; select each and edit; Del; Ctrl+Z / Ctrl+Y; save,
reopen and confirm all three are back; toggle View > Light Markers to confirm nothing draws twice.
