# S11 — Lighting split: registry to kfx_sim, shading stays in kfx_render

**Status:** done 2026-09-28 · **Work items:** W9 · **Depends on:** **S09** · **Risk:**
medium–high (a 2.6 kLOC hot file; resync blob) · **Layout change:** **yes**
(`lish` splits) · **Estimate:** 2 weeks · **Callback entries removed:** ~21 ·
**Timing:** right after an upstream merge

## Goal

- **Lights are simulation state.** kfx_sim creates, moves and deletes them,
  and *reads intensity back* to drive behaviour (`thing_effects.c`,
  `thing_creature.c`, `thing_objects.c`). They are resynced (`lish` in
  `net_resync.cpp`) and saved.
- **The lighting code lives in kfx_render.** So the sim reaches every light
  operation through about 21 callback entries.
- **The fix:** split `light_data.c` into a *registry* (sim state) and
  *shading* (render caches). As a side effect, resync stops shipping about
  4 MB of derived data.

## `struct LightsShadows` today (`kfx_render/include/light_data.h`)

| Field | Kind | After the split |
| --- | --- | --- |
| `lights[LIGHTS_COUNT]` | registry | **kfx_sim** `struct LightRegistry` |
| `light_enabled`, `light_auto_sync`, `global_ambient_light` | registry settings | kfx_sim |
| `shadow_cache[]` | derived cache | kfx_render |
| `stat_light_map[MAX_SUBTILES_X*MAX_SUBTILES_Y]` (≈2 MB) | derived from the static lights | kfx_render; rebuilt after load/resync |
| `subtile_lightness[…]` (≈2 MB) | derived | kfx_render; rebuilt |
| `lighting_tables[1024]`, `lighting_tables_count`, `lighting_tables_initialised` | precomputed tables | kfx_render |
| `shadow_limits[]` | derived | kfx_render |

**Check at the start:** confirm that every field classed as "derived" really
is recomputable from the registry plus the map. `light_stat_refresh`,
`light_initialise_lighting_tables` and `update_light_render_area` are the
rebuild paths. If a field isn't, it moves with the registry.

## Function split (from the 2026-09-27 caller scan of `light_data.c`)

**→ kfx_sim `light_registry.c`**, used by sim, game, script, editor and net:

- `light_allocate_light`, `light_free_light`, `light_is_invalid`;
- `light_add_light_to_list`, `light_remove_light_from_list`;
- `light_create_light`, `light_create_light_adv`, `light_delete_light`;
- `light_set_light_never_cache`, `light_is_light_allocated`;
- `light_set_light_position`, `light_get_light_intensity`,
  `light_set_light_intensity`, `light_get_light_radius`,
  `light_set_light_radius`;
- `light_turn_light_off`, `light_turn_light_on`, `light_set_lights_on`;
- `light_init_dungeon_heart`, `light_set_attached_slab`,
  `delete_lights_attached_to_slab_in_area`;
- `light_count_lights`, `light_initialise` (its registry half),
  `light_export_system_state`/`light_import_system_state` (their registry
  half), `lights_stats_debug_dump`.

**Stays in kfx_render `light_data.c`**, used only by render and apploop:

- the shadow cache (`light_allocate_shadow_cache`,
  `light_shadow_cache_invalid`, `light_shadow_cache_index`,
  `light_shadow_cache_free`);
- `light_initialise_lighting_tables` (708 lines);
- the static light map (`light_stat_light_map_clear_area`,
  `light_stat_refresh`, `clear_stat_light_map`);
- the renderers (`light_render_light*`, `light_render_area`,
  `update_light_render_area`, `update_global_lighting`,
  `calculate_shadow_angle`, `point_is_above_floor`);
- per-pixel (`light_perpixel_*`, `light_has_colour`);
- `get_subtile_lightness`, `clear_subtiles_lightness`,
  `create_shadow_limits`, `clear_shadow_limits`, `clear_light_system`,
  `light_reset_interpolation`, `light_get_lights_enabled`.

**The one real coupling: invalidation.** Registry mutations call
`light_signal_stat_light_update_in_area` and
`light_signal_update_in_area`, which mark shading dirty. After the split:

- the registry records dirty rectangles (and "static light set changed")
  in a small non-serialized queue in kfx_sim;
- `update_light_render_area` in kfx_render drains that queue each frame;
- this is the same "sim signals, view polls" pattern as S07's camera
  counters.

## Callback entries this frees (~21)

- `SimFeedbackCallbacks.light_*` (16).
- `RenderOverlayCallbacks.light_create_light`, `light_set_attached_slab`,
  `delete_lights_attached_to_slab_in_area`, `get_lights_enabled` (4).
- `ConfigReloadCallbacks.clear_subtiles_lightness` (1). kfx_sim
  (`lvl_filesdk1.c`) calls it at level load to reset shading. It becomes a
  "level reset" entry in the invalidation queue, which render drains.

## Resync and save

- The resync blob sends `LightRegistry` instead of the whole `lish`. After
  import, the client rebuilds the stat light map and lightness, as it
  already does after a load.
- Saves: the S09 version bumps (`KFX_LIGHTS_VER`, and `KFX_SIM_STATE_VER`
  if the registry joins `kfx_sim_state`). No migration: pre-stage saves are
  refused (decision 2026-09-27).

## Steps

1. Create `light_registry.{c,h}` and the `struct LightRegistry` storage;
   move the registry functions (1–2 commits).
2. Add the invalidation queue, and have render drain it.
3. Split `struct LightsShadows`; update the resync blob and save chunks;
   bump the versions; add the release-note line to the README.
4. Delete the ~21 entries.

## Verification

- A replay checksum trace, identical before and after (light intensity
  feeds sim behaviour).
- **Visual:**
  - torches and the dungeon heart light;
  - creature-carried lights moving;
  - spell and effect lights;
  - slab destruction removing attached lights;
  - static lighting after level load, after save/load, and after a forced
    resync;
  - the gpu-v2 per-pixel lighting path (`light_perpixel_*`) with coloured
    lights.
- **Resync payload size** before and after: expect about 4 MB less.
- Catch2 `light_data_test.cpp`, split into registry (kfx_sim) and shading
  (kfx_render) tests.

## Risks

- **A hidden ordering dependency:** some shading update might have to
  happen synchronously within the sim tick, for example if sim code reads
  `get_subtile_lightness` for creature vision. The 2026-09-27 scan shows
  `get_subtile_lightness` used only by kfx_render (9 sites). Re-check at the
  start. If the sim reads shading, that value is sim state and must move
  too.
- **Size and hotness of `light_data.c`:** move whole functions only, with
  no rewrites in the same commit.

## As built (2026-09-28)

Code commit `4c07367cd`.

- **The start-of-stage check** held: `get_subtile_lightness` has only
  kfx_render callers, and everything classed "derived" is rebuilt from the
  registry and the map. Two findings changed the plan:
  - **Shadow-cache allocation is registry state.** Creating a dynamic light
    fails when no shadow cache is free, and the sim sees that (a thing
    without its light). So `LightRegistry.shadow_cache_used[]` holds which
    caches are handed out (`light_allocate_shadow_cache()`/
    `light_free_shadow_cache()` return and take an index), and kfx_render's
    `struct ShadowCache` lost its flags byte. The allocation order is
    unchanged: first free slot from 1.
  - **`lish` was never saved.** Pass 1 moved it out of `struct Game`
    without adding it to a save chunk, so a loaded game kept whatever
    lights the previous level left. The registry is now inside
    `kfx_sim_state`, so saves carry it; that fixes the regression.
- **`light_registry.{c,h}`** (kfx_sim) holds `struct Light`, the flags,
  `struct LightRegistry` (`kfx_sim_state.light_registry`: lights, shadow
  cache slots, ambient/enabled/auto-sync, the light counters) and every
  function on the plan's registry list, moved whole. On top of that:
  - `light_get_lights_enabled()` and `light_reset_interpolation()` were on
    the render list but only touch registry fields, so they moved too.
    `light_export/import_system_state()` and `struct LightSystemState` are
    gone. Their counters are registry fields; the bitmask is a constant
    table kfx_render builds; the per-frame render counters stay static in
    light_data.c.
  - The registry never touches kfx_render. Where it cleared the static
    light map, it records the area in `light_shading_signals` (not saved;
    64 areas, repeats of the last one skipped, overflow means "rebuild the
    whole map"). It still flags the affected lights `LgtF_NeedUpdate` at
    the same moment, so the clear is all that's deferred.
  - `light_request_stat_refresh()`, `light_request_lightness_reset()`
    (was `ConfigReloadCallbacks.clear_subtiles_lightness`),
    `light_registry_invalidate_shading()` (after a load or resync) and
    `light_registry_clear()` (the frontend's land view and torture screen,
    which wipe `lish`) are the requests.
- **kfx_render's `light_data.c`** keeps the shading.
  `light_drain_shading_signals()` applies the requests. It runs first
  thing in `update_light_render_area()`, and straight after kfx_render
  changes the registry itself (`light_stat_refresh()`, and the radius-0
  delete inside `light_render_light()`), so an area is always cleared
  before a light flagged with it is shaded again. The lighting tables are
  built the first time they're needed (`light_initialise_shading()`), not
  by `light_initialise()`, which is registry-only now.
  `clear_stat_light_map()` drops pending areas, since it zeroes the whole
  map anyway.
- **Deferred clears read the map when they run.** An area cleared at drain
  time decides between ambient and 0 from the columns as they are then,
  not as they were at the signal. That only differs if a column became
  or stopped being column 0 in between, and the shading reads the map at
  render time everywhere else anyway.
- **After a load or a resync** the registry is left exactly as it arrived
  (the fake-multiplayer ftest compares the received sim state byte for
  byte). kfx_render flags every light and rebuilds the static map and the
  lightness on its next drain (`registry_replaced`).
- **Fields kfx_render keeps up to date stay in `struct Light`:** `range`
  (which the registry's invalidation reads), the interpolation and
  flicker fields, and the `NeedUpdate`/`OutOfDate` flags. The sim never
  decides anything from them, and nothing checksums them. Two render-side
  registry writes predate this stage and are unchanged:
  `update_local_mouse_light()` moves the local cursor light every frame,
  and `light_render_light()` deletes a light whose radius is 0.
- **Resync** no longer sends `lish` (4,612,643 bytes); the registry
  (354,850 bytes) travels inside `kfx_sim_state`, so the blob is about
  4.26 MB smaller. `ResyncVersions` lost its `lights` field.
- **Versions:** `KFX_SIM_STATE_VER` 3 (140,005,011 bytes) and
  `KFX_GAME_STATE_VER` 3 (439,768, without `lightst`), identical on both
  targets (measured with both toolchains). `KFX_LIGHTS_VER`/`_SIZE` are
  gone: `lish` is neither saved nor sent. README decisions table has the
  release note.
- **Callback entries:** the planned 21 removed (16 `SimFeedbackCallbacks`,
  4 `RenderOverlayCallbacks`, 1 `ConfigReloadCallbacks`), 404 → 383.
  lvl_filesdk1.c's duplicated `LIGHTS_COUNT` and its
  `light_create_light_adv` wrapper went with them.
- **Tests:** `light_data_test.cpp` became kfx_sim's
  `light_registry_test.cpp` (20 cases: the old accessor/allocator cases,
  shadow-cache exhaustion, the signals, the requests) plus a new kfx_render
  `light_data_test.cpp` (7 cases: the drain).
- **Checks:** Linux and Windows builds, both layering checks, all 12
  Catch2 binaries. Scratch builds of HEAD and of this stage logged, per
  turn, the `compute_replay_integrity()` checksum and a hash of the light
  registry's sim-visible fields (allocation, flags the sim sets, position,
  intensity, radius, shadow cache and attached slab, the ambient and
  enabled settings; not the fields kfx_render maintains, and not the local
  cursor light, which `update_local_mouse_light()` moves every frame, so
  it differs between two runs of the same binary). Across the 70-test
  ftest sweep every test passes on both, and three things are
  byte-identical: the checksum traces (11,646 turns), the light-registry
  traces (11,646 turns) and every log line's turn prefix and function name
  (23,652 lines). The sweep includes save/load (`ai_seat_identity`), the
  fake-multiplayer resync (whose byte-for-byte comparison of the received
  state now covers the lights) and the editor tests that place and save
  lights. A hash of the static light map was tried as well and dropped: it
  depends on the camera and frame timing, so two runs of one binary
  already differ. The visual checks in the plan weren't run.

## Upstream-merge notes

- **High churn:** `light_data.c` changes upstream with lighting features.
  After the split, a hunk belongs to one half by function name, which the
  ledger and `move_ledger.py upstream src/light_data.c` show.
- **New upstream fields** in `struct LightsShadows` need classifying
  (registry or cache), and a registry field means an S09 version bump.
