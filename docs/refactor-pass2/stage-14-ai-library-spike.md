# S14 — Spike: computer-player AI as `kfx_ai`

**Status:** done 2026-09-28 (spike: go; full move done) · **Work items:**
W17 · **Depends on:** S03 (the AI's NamedCommand name tables are already in
kfx_config) · **Risk:** medium · **Layout change:** none (state stays in
`kfx_sim_state`) · **Estimate:** spike 2–3 days, full move 1–1.5 weeks

## Goal

The computer-player AI (`player_computer*.c`, `player_comp*.c`: 7 files,
about 11.6 kLOC, 229 exported functions) is a *client* of the simulation.
It reads the world and issues actions. Giving it its own library, ranked
directly above kfx_sim, makes that explicit. It also gives the AI its own
test binary, and shrinks kfx_sim (122 kLOC) by about 10%.

## What the spike establishes

1. **Calls from the rest of kfx_sim into AI files** (measured 2026-09-27:
   19 call sites in 5 files):

   | Caller | Calls | Likely resolution |
   | --- | --- | --- |
   | `player_utils.c` | `setup_a_computer_player`, `toggle_computer_player`, `script_support_setup_player_as_computer_keeper`, `computer_player_invalid` ×2, `add_to_trap_locations` ×2 | a small `AiPort` (setup/toggle/notify) implemented by kfx_ai; `computer_player_invalid` is a type check, so it stays in kfx_sim |
   | `thing_list.c` | `script_support_setup_player_as_computer_keeper` | `AiPort` |
   | `power_hand.c` | `computer_force_dump_specific_held_thing`, `thing_is_in_computer_power_hand_list` ×2 | `AiPort`, or move the power-hand list query to kfx_sim if it only reads state |
   | `room_jobs.c` | `get_dungeon_money_less_cost` ×3 | a general dungeon helper that lives in an AI file: **move it to kfx_sim**, no port |
   | `thing_creature.c` | `creature_could_be_placed_in_better_room`, `get_job_to_place_creature_in_room` (both in `player_comptask.c`), `try_game_action` (`player_comptask.c`, the AI's action executor taking a `Computer2 *`), `computer_able_to_use_power` (`player_computer.c`), `computer_dungeon` | the two placement queries are general creature/room helpers: **move them to kfx_sim**. The `try_game_action` + `computer_able_to_use_power` call (≈ line 5854, an AI healing decision embedded in creature code) is AI logic: move that block into kfx_ai behind one `AiPort` entry (for example `ai_consider_heal_creature`). `computer_dungeon` is a trivial accessor; it stays with the types. |

   **Target:** at most ~6 `AiPort` entries. If the spike finds many more,
   stop: the split isn't worth it.
2. **Types versus behaviour.** `kfx_sim_state.h` includes `player_computer.h`
   and embeds `struct Computer2 computer[PLAYERS_COUNT]` and
   `struct ComputerTask computer_task[]`. Split the header:
   - `player_computer_types.h` (types, enums, constants) stays in kfx_sim;
   - the behaviour prototypes go to `kfx_ai/include/player_computer.h`.

   The state stays in `kfx_sim_state`. kfx_ai, ranked above, accesses it
   downward, so there is **no layout change**.
3. **Config.**
   - `config_compp.c` (kfx_config) parses `keepcompp.cfg` into
     `comp_player_conf`; that stays.
   - `ConfigReloadCallbacks.get_computer_player_f` (used by
     `config_terrain.c`) stays or goes depending on whether the rebuild
     trigger can become a flag the AI polls.
   - `reactivate_build_process` is gone after S05.
4. **Callers above kfx_sim** (frontend, game, net, script, `main.cpp`,
   apploop's `process_computer_players2`) all rank above kfx_ai and need
   nothing.

## Spike deliverable

A branch that:
- splits `player_computer.h` into types and behaviour;
- moves the 7 files to `src/kfx_ai/`;
- moves the general helpers down;
- adds the `AiPort`;
- builds, and passes both layering checks.

Report: the final `AiPort` size, anything surprising, and a go or no-go.

## Full stage (if go)

1. The header split and the general helpers moved to kfx_sim (useful on
   their own, even if no-go).
2. `AiPort`, wired in `wire_ports()`.
3. The file move, a `kfx_ai` CMake target, and `LIBRARY_ORDER`.
4. An AI Catch2 test binary (move the relevant kfx_sim tests).

## Verification

- `bug_ai_bridge` (repeat 100×, seed 1) with an identical outcome
  distribution.
- The AI-seat ftests (`ai_bridge_*`, `spectator_handoff`).
- A replay checksum trace of an AI-vs-AI skirmish, identical before and
  after.

## As built (2026-09-28)

Code commit `0ed2452ff`. The spike came out well under its threshold, so
the full stage was done in the same pass.

- **The spike's numbers.** kfx_sim called into the AI files at 15 call
  sites in 6 files (the plan measured 19 in 5; S05 had since added
  `reactivate_build_process` in player_availability.c). They resolved as:
  - **moved down to kfx_sim** (they only read or record state):
    `get_computer_player_f`, `computer_player_invalid`, `computer_dungeon`,
    `thing_is_in_computer_power_hand_list`, `find_trap_location_index`,
    `add_to_trap_locations` into a new `player_computer_state.c`, and
    `get_dungeon_money_less_cost` into dungeon_data.c;
  - **moved up to kfx_ai instead:** the plan wanted
    `creature_could_be_placed_in_better_room`/
    `get_job_to_place_creature_in_room` moved down and the healing block in
    thing_creature.c behind a port entry. Both sit in
    `player_list_creature_filter_needs_to_be_placed_in_room_for_job()`, a
    computer-player filter (its parameter carries the `Computer2`) used only
    by player_comptask.c, so the filter moved to player_comptask.c (now
    static) and thing_creature.c no longer touches the AI at all;
  - **`AiPort` (5 entries):** `setup_a_computer_player`,
    `toggle_computer_player`, `script_support_setup_player_as_computer_keeper`,
    `computer_force_dump_specific_held_thing`, `reactivate_build_process`
    (it compares the process's check function with an AI function, so it
    can't move down).
  - The target was "at most ~6": **go**.
- **The header split.** kfx_sim's `player_computer_types.h` holds the
  constants, enums and structs (`struct Computer2`, `struct ComputerTask`,
  and `struct GoldLookup` from player_complookup.h, all embedded in
  `kfx_sim_state`), plus the accessors above. kfx_ai's `player_computer.h`
  and `player_complookup.h` hold the behaviour. The state stays in
  `kfx_sim_state`: **no layout change**, `KFX_SIM_STATE_SIZE` unchanged.
- **`src/kfx_ai/`:** the 7 AI sources (`git mv`), the two behaviour headers,
  a `kfx_ai` STATIC target ranked between `kfx_sim` and `kfx_render`
  (`LIBRARY_ORDER`, the root include path, the link order), and
  `kfx_ai_utest` with the 6 AI test files (31 cases; kfx_sim_utest 749 →
  718). `make_computer_player()` moved from kfx_sim's test fixtures to
  `kfx_ai_test_fixtures.h`. Every higher library's test binary links
  `kfx_ai`; CI and the build scripts build `kfx_ai_utest`.
- **Config:** nothing to do. `ConfigReloadCallbacks.get_computer_player_f`
  no longer exists, and the AI's function-name tables that config_compp.c
  resolves are already kfx_config's (S03, config_funcnames.h).
- **Callback inventory:** +5 (the `AiPort`); callback_inventory.py now
  counts `*Port` tables too.
- **Checks:** Linux and Windows builds, both layering checks (nothing
  in kfx_sim or below reaches kfx_ai), all 13 Catch2 binaries. Scratch
  builds of S13's end and of `0ed2452ff` logged the per-turn
  `compute_replay_integrity()` checksum and the light-registry hash: across
  the 70-test sweep (which has the `ai_seat_*`, external-seat and
  spectator tests, and computer players in most levels) every test passes
  on both, and the checksum traces (11,646 turns), light traces and log
  prefixes (23,652 lines) are identical. With their Python clients,
  `ai_bridge_reference` and `ai_bridge_vs_agent` pass on the new build.
  Not run: the plan's 100-run `bug_ai_bridge` distribution (S05 found it
  is a statistical investigation, not a gate; the per-turn traces cover
  the AI's decisions directly) and a separate AI-vs-AI skirmish replay.

## Upstream-merge notes

AI files are busy upstream. Only the *directory* changes (file names stay),
so `find src -iname` in the merge workflow still works. The accessors that
moved to kfx_sim (player_computer_state.c, dungeon_data.c) and the filter
that moved from thing_creature.c to player_comptask.c have ledger rows.
Upstream changes to `player_computer.h`'s structs go to kfx_sim's
`player_computer_types.h`; a new call from sim code into AI behaviour needs
an `AiPort` entry.
