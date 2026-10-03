# S05 — Gameplay code out of kfx_config, into kfx_sim

**Status:** done 2026-09-28 · **Work items:** W5, W6 · **Depends on:** S01 ·
**Risk:** low (mechanical moves; a hot-path win) · **Layout change:** none ·
**Estimate:** 1 week · **Callback
entries removed:** 45 (planned 43) · **Timing:**
right after an upstream merge (busy files)

## Goal

Functions that mutate per-player dungeon state, or that take a
`struct Thing *`, live in `config_*.c` for historical reasons. That forced
the whole of `DungeonAvailabilityCallbacks` and about half of
`ConfigReloadCallbacks` into existence. This stage moves them into kfx_sim,
where their types and state already are, and where every one of their
callers already sits or above.

## Moves

### A. Per-player availability → new `kfx_sim/src/player_availability.c` (+ `.h`)

| From | Functions |
| --- | --- |
| `config_terrain.c` | `make_all_rooms_researchable`, `set_room_available`, `is_room_available`, `is_room_obtainable`, `find_first_available_roomkind_with_role`, `make_available_all_researchable_rooms` |
| `config_magic.c` | `make_all_powers_researchable`, `set_power_available`, `is_power_available`, `is_power_obtainable`, `make_available_all_researchable_powers` |
| `config_trapdoor.c` | `is_trap_placeable`, `is_trap_buildable`, `is_trap_built`, `is_door_placeable`, `is_door_buildable`, `is_door_built`, `make_available_all_doors`, `make_available_all_traps` |
| `config_creature.c` | `set_creature_available`, `update_players_special_digger_model`, `get_players_special_digger_model`, `get_players_spectator_model` |

**Callers** (verified 2026-09-27): no other `kfx_config` file calls any of
them. The external callers are kfx_sim, net, game, frontend, script and
editor, all ranked at or above kfx_sim.

### B. `Thing`-taking functions → the kfx_sim file that owns the concept

| Function | From | To |
| --- | --- | --- |
| `creature_stats_get_from_thing` (fan-in 171) | `config_creature.c` | `kfx_sim/src/thing_stats.c` |
| `get_creature_model_flags` | `config_creature.c` | `thing_stats.c` |
| `creature_own_name` | `config_creature.c` | `creature_control.c` |
| `get_job_for_subtile` | `config_creature.c` | `creature_jobs.c` |
| `crate_thing_to_workshop_item_class`, `_model` | `config_objects.c` | `room_workshop.c` |
| `get_required_room_capacity_for_object` | `config_objects.c` | `room_data.c` |

Model-index lookups (`creature_stats_get(model)` and similar) **stay** in
kfx_config. Only the `Thing`-to-model step moves.

## Callback entries this frees (43, measured by use inside the moved code)

- **`DungeonAvailabilityCallbacks`:** all remaining 20 entries. The 21st,
  `try_set_backup_heart_idx`, goes in S01. **Delete the table**, its header,
  `.c`, test, and `main.cpp` wiring.
- **`ConfigReloadCallbacks` (23):**
  - `get_thing_model`, `get_thing_class_id`, `get_thing_owner`,
    `get_thing_creation_turn`, `get_thing_index`, `get_creature_blood_type`;
  - `thing_is_invalid`, `thing_is_workshop_crate`,
    `get_wealth_size_of_gold_hoard_model`, `thing_is_creature_digger`,
    `creature_is_for_dungeon_diggers_list`, `get_room_kind_thing_is_on`;
  - `get_slabmap_for_subtile`, `slabmap_owner`, `slab_is_area_inner_fill`;
  - `player_has_heart`, `player_is_roaming`, `get_my_player_number`,
    `get/set_player_special_digger`;
  - `set_door_buildable_and_add_to_amount`,
    `set_trap_buildable_and_add_to_amount`, `reactivate_build_process`.
- **Still used elsewhere in kfx_config after the move, so kept:**
  `get_creature_name_buffer`, `thing_class_and_model_name`,
  `update_creatr_model_activities_list`.

## Steps

1. Create `player_availability.{c,h}`. Move group A one source file at a
   time (4 commits):
   - replace each `dungeon_availability->x()` with the direct kfx_sim call
     it wrapped (the `main.cpp` wrapper shows which);
   - keep the prototypes reachable: include `player_availability.h` from
     the old config headers for one stage, then drop that include in the
     last commit, fixing any includes that break.
2. Delete `DungeonAvailabilityCallbacks`.
3. Move group B (1 commit per destination file). Replace each
   `config_reload_callbacks->get_thing_*()` with the plain field read.
4. Delete the 23 freed `ConfigReloadCallbacks` entries (the inventory
   confirms they're unused).
5. Move the Catch2 tests that exercise the moved functions from
   `kfx_config/tests/` (`config_magic_test.cpp`, `config_creature_test.cpp`,
   `config_trapdoor_test.cpp`, `dungeon_availability_test.cpp`) to
   `kfx_sim/tests/player_availability_test.cpp` and friends.

## Verification

- Both builds and both layering checks pass. Catch2 for kfx_config and
  kfx_sim.
- ftests: every level-script ftest (they exercise `set_*_available` through
  script commands), `bug_ai_bridge` (the computer player uses
  `is_room_available` and friends), and the workshop ftests.
- Manual: a campaign level whose script grants rooms and powers mid-level;
  the cheat menu's "make everything available".
- Inventory: −43. `move_ledger.py detect` is clean.

## Risks

- **Include fallout:** many files reach these prototypes through
  `config_*.h`. The transitional include in step 1 keeps each commit
  building.
- **Behaviour:** none intended. The functions move verbatim, except that
  callback calls become direct calls to the same functions.

## As built (2026-09-28)

Two code commits instead of the planned eight or so; each builds and passes
its tests on its own.

- **S05-A (`f32b8f3d6`)** — the 24 group-A functions plus
  `is_room_of_role_available` (it calls
  `find_first_available_roomkind_with_role`, so it had to come along) are in
  `kfx_sim/src/player_availability.c` with a new `player_availability.h`.
  Each `dungeon_availability->x()` became a direct call to the same
  `dungeon_data.c` accessor, and each `config_reload_callbacks->x()` a
  direct call or a field read (`my_player_number`,
  `get_player(p)->special_digger`). The 42 callers include the new header
  directly; the transitional include from the old config headers wasn't
  needed (and would have been a layering violation anyway).
  `DungeonAvailabilityCallbacks` is gone (table, `.c`, `.h`, test, wiring).
  So is `set_power_grant_revoke_callbacks`, the function-pointer injection
  `set_power_available` used for `add/remove_power_from_player`.
- **S05-B (`a86884654`)** — group B moved as planned.
  `creature_own_name`'s name-part tables came with it, now `static`.
  `get_job_for_subtile`'s room-kind lookup, a `main.cpp` wrapper before, is
  inlined as `get_room_thing_is_on()` + `room->kind`. Then **25**
  `ConfigReloadCallbacks` entries had no callers and were removed (70 → 45):
  the 23 listed above, plus `get_creature_name_buffer` (the plan expected it
  to stay, but `creature_own_name` was its only user) and
  `get_computer_player_f` (`set_room_available`'s). `main.cpp` lost the
  wrappers behind them.
- **Tests.** The kfx_config cases that ran these functions against fakes of
  the callback tables were replaced by kfx_sim tests on real state:
  `player_availability_test.cpp` (16 cases), plus new cases in
  `thing_stats_test`, `creature_control_test` (including the generated-name
  path, untested before), `room_workshop_test` and `room_data_test`. Three
  sim tests that faked `get_thing_model` now set `thing->model`, and
  `thing_creature_test`'s digger predicates, previously pinned as "always
  false" by that fake's default, get real assertions. Catch2: 2041 cases.
- **Found, not changed:** `set_creature_available` clamps `force_avail` to
  `CREATURES_COUNT-1` (1023), but `dungeon->creature_force_enabled[]` is
  `unsigned char`, so anything above 255 wraps. Upstream has the same code.
  `player_availability_test.cpp` pins the current behaviour.
- **Inventory:** 519 → 474 entries (−45: 20 + 25). `move_ledger.py check`
  and `detect` are clean.
- **Checks:** Linux and Windows builds, both layering checks, all Catch2
  suites, and the 70-test ftest sweep (all pass), whose 17 `ai_seat_*`
  tests drive the computer player through the availability checks.
  `bug_ai_bridge` wasn't run to completion: it's in the long-running list
  and is a statistical investigation, not a pass/fail gate (100 seeds of up
  to 35,000 turns each, reporting how often the AI builds bridges; hours of
  run time). Nor were the two-process `ai_bridge_*`/`net_enet_*` tests.

## Upstream-merge notes

- **High churn.** Upstream edits `config_creature.c`, `config_magic.c`,
  `config_trapdoor.c` and `config_terrain.c` regularly, and these functions
  are among the edited ones.
- **Schedule the stage right after an upstream merge.** Afterwards,
  `move_ledger.py upstream src/config_magic.c` shows each moved function's
  new home.
- **Watch for new availability functions.** When upstream adds a new
  `set_*_available`-style function to a config file, place it in
  `player_availability.c`, not back in config.
