# S08 — Header-only `kfx_model`

**Status:** done 2026-09-28 · **Work items:** W10 (step one) · **Depends on:** S03
(navigation context in `kfx_pathfinding_state`); **the Ariadne playtest**
recommended in architecture.md §12.1 · **Risk:** medium · **Layout
change:** none · **Estimate:** 1–1.5 weeks · **Callback entries removed:**
23 (`PathfindingWorldCallbacks` 52 → 29; planned ~20)

The analysis is in
[`00-analysis.md` §8.2](00-analysis.md#82-should-kfx_model-be-header-only--yes-for-step-one-storage-is-a-separate-later-decision).

## Goal

Ariadne (kfx_pathfinding, ranked below kfx_sim) reads and writes
`struct Thing` fields through about 390 indirect calls, because the type is
defined in kfx_sim. Moving the *layouts* of `struct Thing`, `struct Map` and
`struct SlabMap` into a header-only library ranked below kfx_pathfinding
lets Ariadne use plain field access again. This is the biggest readability
and performance win in the pathfinder, with no save-layout change.

## Pre-conditions

- **The Ariadne playtest.** Ariadne was converted to callbacks in pass 1
  and "is build-verified but not playtested" (architecture.md §12.1). Do
  that playtest first: the pathing ftests plus a campaign level with heavy
  digging and doors. Then any movement difference after this stage can be
  attributed correctly.
- **S03 A** has moved the three navigation-context globals.

## What goes into `src/kfx_model/include/`

It is a CMake `INTERFACE` target with no sources.

| Header | Contents | Taken from |
| --- | --- | --- |
| `thing_types.h` | `enum ThingAllocFlags`, `ThingAllocationPool`, `ThingFlags1/2`, `ThingRenderingFlags`, `ThingSizeChange`, `ThingMovementFlags`, `ThingAddFlags`; `struct Thing` (L121–313 today); `static inline` field helpers used by the pathfinder (`thing_is_flying`-style flag tests); `cross_x_boundary_first` / `cross_y_boundary_first` once confirmed pure | `kfx_sim/include/thing_data.h`, `thing_physics.c` |
| `map_types.h` | `struct Map`; `stl_slab_center_subtile`; `small_around[]` as a `static const` table plus `small_around_index_in_direction`; **dimension-parameterised** subtile helpers: `kfx_subtile_number(dims, x, y)`, `kfx_stl_num_decode_x/y(dims, n)` | `map_data.h`, `map_data.c`, `map_utils.c` |
| `slab_types.h` | `struct SlabMap` (platform typedefs only; `slab_data.h`'s `roomspace.h` include is for prototypes, not the type) | `slab_data.h` |

**Rules**, checked in CI:

- kfx_model headers include only kfx_platform and kfx_config headers
  (`check_layering.py`, once `kfx_model` is added to `LIBRARY_ORDER` between
  kfx_config and kfx_pathfinding).
- **No non-inline function prototypes, and no `extern` data.** A grep in
  `check_layering.py`, or a tiny new script, enforces this. It guarantees
  kfx_model can never create an upward symbol reference.
- **What stays in kfx_sim:** `INVALID_THING`, `thing_get()`,
  `thing_is_invalid()`, and anything else that indexes
  `kfx_sim_state.things_data[]` or other world arrays (the storage group).

kfx_sim's existing headers (`thing_data.h`, `map_data.h`, `slab_data.h`)
include the new ones, so no kfx_sim or higher code changes.

## Map-dimension cache (the 5 "looks pure but isn't" entries)

- `init_navigation()` already runs on every level load and every loaded
  game. It copies `map_subtiles_x/y` into `kfx_pathfinding_state` there.
- Ariadne calls the `kfx_model` helpers with its cached dimensions.
- kfx_sim's `get_subtile_number()` and friends become thin wrappers over the
  same helpers, passing `kfx_sim_state`'s dimensions, so the encoding can't
  drift apart.
- **Removes:** `get_map_size_x/y`, `get_subtile_number`,
  `stl_num_decode_x/y`.

## Callback entries this frees (~20)

- **Layout (15):**
  - `thing_get_position`/`thing_set_position`,
    `thing_get_move_angle`/`thing_set_move_angle`, `thing_get_owner`,
    `thing_get_index`, `thing_get_clipbox_size`, `thing_is_flying`;
  - `map_block_flags`, `slabmap_block_kind`, `door_is_locked`;
  - `stl_slab_center_subtile`, `small_around_index_in_direction`,
    `get_small_around`, `get_small_around_length`,
    `cross_x/y_boundary_first`, `get_map_size_z`.
- **Map dimensions (5):** as above.

**Remain** until the storage decision:

- storage (7): `get_map_block_at(_pos)`, `map_block_is_invalid`,
  `get_slabmap_block`, `slabmap_block_is_invalid`, `get_slabmap_for_subtile`,
  `thing_is_invalid`, `creature_get_navigation/ariadne_state`;
- behaviour (~20): the pathfinder's genuine port.

The `ConfigReloadCallbacks` `Thing` accessors that the analysis counted
under W10 are removed earlier, by S05.

## Steps

1. Create `src/kfx_model/` (an `INTERFACE` target), add it to
   `LIBRARY_ORDER`, and add the no-prototypes check.
2. `slab_types.h` and `map_types.h`: move the types, update includes.
3. `thing_types.h`: move `struct Thing` and its enums (the largest step).
4. Add the map-dimension cache in `init_navigation()`, plus the
   parameterised helpers.
5. Convert Ariadne one file at a time (`ariadne.c`, `ariadne_wallhug.c`,
   `ariadne_update.c`, …): `pfw->thing_get_position(t)` becomes
   `t->mappos`, and so on.
6. Delete the ~20 entries, the `main.cpp` wrappers, and the fake-world
   entries in `kfx_pathfinding/tests/pathfinding_fake_world.h`. The tests
   build real `struct Thing` values instead.

## Verification

- Both builds and both layering checks pass (the symbol check matters here).
- `kfx_pathfinding_utest` and the sim pathing tests.
- Ftests: `bug_pathing_stair_treasury`, `bug_imp_goldseam_dig`, the
  `bug_imp_tp_attack_door__*` family, `bug_ai_bridge`.
- **A replay checksum trace** identical before and after (movement is
  synced).
- A timing comparison on a pathing-heavy replay. Expect an improvement;
  record it.

## Risks

- `struct Thing`'s field types might pull in a typedef defined in a kfx_sim
  header. The 2026-09-27 include closure shows none (`thing_data.h`
  includes only `globals.h` and `bflib_basics.h`). Re-check at the start.
- Upstream-merge cost, see below.

## Upstream-merge notes

- Upstream adds fields to `struct Thing` in `thing_data.h`, which now lands
  in `kfx_model/include/thing_types.h`. The same applies to `struct Map`
  and `struct SlabMap`. The ledger `type` rows point there.
- This is a recurring manual step, accepted because Ariadne needs the
  layout. Don't widen `kfx_model` to other types without a similar reason.

## As built (2026-09-28)

One code commit, `61969ca49`.

- **Pre-condition.** The Ariadne playtest wasn't done before this stage. It
  started on request. The before/after turn trace below is what attributes
  any movement difference.
- **`src/kfx_model/`** is a CMake `INTERFACE` target with three headers.
  - `thing_types.h`: `struct Thing`, its flag enums, `ThingAddFlags`,
    `CREATURES_COUNT` (a `struct Thing` array size) and
    `cross_x/y_boundary_first`.
  - `map_types.h`: `struct Map`, `small_around[]`/`SMALL_AROUND_LENGTH`,
    `small_around_index_in_direction`, `stl_slab_center_subtile`, and the
    parameterised `kfx_subtile_number`/`kfx_stl_num_decode_x/y`.
  - `slab_types.h`: `struct SlabMap`.
  - Each struct keeps its `#pragma pack(1)` region, and every field type is
    a kfx_platform typedef, as the 2026-09-27 check said.
  - kfx_sim's `thing_data.h`/`map_data.h`/`slab_data.h` (and
    `map_utils.h`) include them, so no kfx_sim-or-above code changed.
  - `get_subtile_number` and `stl_num_decode_x/y` are now wrappers over the
    kfx_model helpers.
- **Ranking:** `kfx_model` sits after `kfx_content` in `LIBRARY_ORDER`, so
  the ladder reads config → content → model → pathfinding. It also went
  into the callback inventory's ranks.
- **The "headers only" rule** is in `check_layering.py`
  (`check_model_headers()`), so CI's `--strict` run enforces it. It flags
  any non-inline prototype or `extern` data outside the
  `#ifdef __cplusplus` wrappers. A deliberately bad prototype and
  `extern` both failed it.
- **Map size.** The plan had `init_navigation()` copy the map size. It
  can't: `init_navigation()` is kfx_pathfinding code and can't read
  `kfx_sim_state`.
  - Instead a new `ariadne_set_map_dimensions(x, y, z)` fills
    `kfx_pathfinding_state.map_subtiles_x/y/z`.
  - `set_map_size()` calls it (level start, the editor's new map), and so
    does `reinit_level_after_load()` (save load and network resync, which
    replace `kfx_sim_state` wholesale).
  - `map_subtiles_z` is cached too, which frees `get_map_size_z`.
- **Ariadne conversion.** 254 call sites in `ariadne.c`, `ariadne_update.c`
  and `ariadne_wallhug.c` were rewritten by a script to plain field access
  or the kfx_model helpers. For example `pathfinding_world->thing_get_position(t)`
  became `t->mappos`, and `thing_set_position(t, &p)` became `t->mappos = p`.
  The get/copy/set shape of the old value accessors is kept as is.
- **Callback entries:** 23 removed, 440 → 417 in all: the 18 layout
  entries listed above, which the plan counted as 15, and the 5 map-size
  ones. `slabmap_owner` stays: it's a kfx_sim function.
- **Tests.** `pathfinding_fake_world.h` now hands out real structs.
  - A real `struct Map`/`struct SlabMap` per grid cell (kept in step with
    the cell on lookup), plus off-map sentinels like kfx_sim's
    `INVALID_MAP_BLOCK`. Returning `NULL` there crashed
    `init_navigation_map()`, which reads one subtile past the map, as the
    real game does.
  - `FakeThing` wraps a real `struct Thing` as its first member.
  - The fixtures set the map-size cache.
  - `ariadne_test.cpp`'s clipbox cases use a real thing instead of a
    one-field fake.
  - The fake's `cross_*_boundary_first` always returned false; the real
    ones are used now, and all 82 pathfinding tests pass.
- **Regression check.** Scratch builds of HEAD and of this stage logged
  `compute_replay_integrity()` every turn: the positions, angles and owners
  of every synced thing, plus the slabs and dig tasks. Both builds then ran
  the 70-test ftest sweep. All 70 pass on both builds, and the traces
  (11,646 turns) are byte-identical.
- **Timing: no measurable change.** The same builds also summed the
  thread CPU time of `update()` per test.
  - Over the 70-test sweep: 6.35 s (HEAD) vs 6.29 s (S08), −0.8%.
  - Five alternating runs each, medians:
    - `bug_imp_goldseam_dig`: 156.5 → 151.2 ms (0.966);
    - `bug_imp_tp_attack_door__claim`: 371.4 → 374.0 ms (1.007);
    - `bug_pathing_stair_treasury`: 299.2 → 298.7 ms (0.998).
  - All within run-to-run noise. Pathfinding is only a small part of a turn
    in these tests, and each removed call was a cheap indirect jump. So the
    stage's gain is readability and a smaller interface, not speed. The
    storage step's go/no-go (below) shouldn't count on a performance win.
- **Checks:** Linux and Windows builds, both layering checks, all Catch2
  suites (2041 cases).

## Later: the storage step (not part of this stage)

Removing the 7 storage entries means a `KfxWorldState` (`map[]`,
`slabmap[]`, `things_data[]`, dimensions) owned by a real `kfx_model`
library, split out of `kfx_sim_state`. That is a save and resync layout
change, so it depends on S09. Decide after S08 lands, using its timing
numbers.
