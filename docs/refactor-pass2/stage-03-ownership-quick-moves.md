# S03 — Ownership quick moves

**Status:** done 2026-09-28 (`2343d3dc1`, `6181efba9`, docs in the following commit) · **Work items:** W20, W7, W8, input primitives (the
first part of W4) · **Depends on:** S01 · **Risk:** low · **Layout
change:** none · **Estimate:** 1 week · **Callback entries removed:** ~36

## Goal

Four independent, low-risk moves of *state or data* to the lowest library
that uses it. Each removes a family of trivial get/set callback entries.
None of them touches a serialized struct.

## A. Navigation context → `kfx_pathfinding_state` (W20)

| Symbol | Now | New home |
| --- | --- | --- |
| `owner_player_navigating` | extern global, `kfx_sim/src/thing_navigate.c` (declared in `thing_navigate.h`) | field of `struct KfxPathfindingState` |
| `nav_thing_can_travel_over_lava` | same | same |
| `nav_thing_is_flying` | same | same |

- **Writers:** `thing_navigate.c` and `creature_states.c` (kfx_sim), which
  set them just before calling into Ariadne. **Readers:** Ariadne, through
  6 `PathfindingWorldCallbacks` get/set entries.
- **Serialization:** the globals are in no serialized struct, and
  `kfx_pathfinding_state` is deliberately excluded from the save and resync
  blobs (architecture.md §2.2a). Moving them there changes no layout.
- **Better still:** give Ariadne's entry points a small `struct NavContext`
  argument instead of globals. Do that only if the entry-point count stays
  small; otherwise the state field is enough.
- **Removes:** `PathfindingWorldCallbacks.{get,set}_owner_player_navigating`,
  `{get,set}_nav_thing_can_travel_over_lava`, `{get,set}_nav_thing_is_flying`
  (6).

## B. Settings values owned by kfx_config (W7)

| Value | Owner today | `keeperfx.cfg` key / source |
| --- | --- | --- |
| `screenshot_format` | kfx_render `scrcapt.c` | `SCREENSHOT` |
| `smooth_on` | kfx_render `engine_redraw.c` | `VID_SMOOTH` |
| `screen_vidmode` (pending mode) | kfx_render `vidmode.c` | `INGAME_RES` |
| `base_mouse_sensitivity` | kfx_render `vidmode.c` | `POINTER_SENSITIVITY` |
| `first_person_horizontal_fov` | kfx_render `vidmode.c` | (engine setting; S07's camera code needs it in kfx_sim) |
| `global_hand_scale` | kfx_sim `power_hand.c` | `HAND_SIZE` |
| `g_speech_queue_limit` | kfx_frontend `gui_soundmsgs.cpp` | `sounds.cfg` `[system]` |

- **New `struct KfxRuntimeSettings kfx_runtime_settings`** in
  `kfx_config/include/config_settings.h`.
  - **Not** in the existing `struct GameSettings settings`, which is written
    to the settings file, so adding fields there would be a layout change.
  - Its fields are **not** part of any serialized state struct.
- The loaders (`config_keeperfx.c`, `config_sounds.c`) and the settings
  schema (`config_settingschema.c`) write it directly. The owning libraries
  read it.
- `screen_vidmode` needs care. `vidmode.c` also *changes* the active mode
  at runtime, so only the *pending* value (what `INGAME_RES` holds) moves;
  the active mode stays with kfx_render.
- **Removes:** `ConfigReloadCallbacks.{set,get}_screenshot_format`,
  `{set,get}_screen_vidmode`, `{set,get}_base_mouse_sensitivity`,
  `set_vid_smooth`, `{set,get}_hand_scale`, `set_speech_queue_limit` (10).
  The corresponding `main.cpp` wrappers go too.

## C. NamedCommand name tables → kfx_config (W8)

| Name table (moves) | Function-pointer table (stays in kfx_sim) |
| --- | --- |
| `computer_process_func_type[]` (`player_compprocs.c`) | `computer_process_func_list[]` |
| `computer_check_func_type[]` (`player_compchecks.c`) | `computer_check_func_list[]` |
| `computer_event_func_type[]`, `computer_event_test_func_type[]` (`player_compevents.c`) | event/test function lists |
| `creature_instances_func_type[]`, `_validate_func_type[]`, `_search_targets_func_type[]` (`creature_instances.c`) | instance function lists |
| `creature_job_player_assign/check_func_type[]`, `creature_job_coords_check/assign_func_type[]` (`creature_jobs.c`) | job function lists |
| `process_func_commands[]`, `cleanup_func_commands[]`, `move_from_slab_func_commands[]`, `move_check_func_commands[]` (`creature_states.c`) | state function lists |

- The name tables are pure `{"name", index}` data, parsed by kfx_config's
  `.cfg` field tables. They move to a new `kfx_config/src/config_funcnames.c`
  and `include/config_funcnames.h`.
- Each pair gets a count macro (`COMPUTER_PROCESS_FUNC_COUNT`, …) in the
  config header. The kfx_sim `.c` that owns the function array adds
  `_Static_assert(sizeof(list)/sizeof(list[0]) == COUNT, …)`, so the two
  can't drift apart.
- **Removes:** the 15 `ConfigReloadCallbacks.get_*_func_type` /
  `get_*_func_commands` entries, and their `main.cpp` wrappers.

## D. Input primitives → kfx_platform (the first part of W4)

| Symbol | Now | New home | Reads |
| --- | --- | --- | --- |
| `GetMouseX`, `GetMouseY` | `kfx_frontend/src/kjm_input.c` | `kfx_platform/src/bflib_mouse.cpp` | `lbDisplay.MMouseX/Y`, `pixel_size` (platform) |
| `is_mouse_pressed_lrbutton` | same | same | `lbDisplay` |
| `is_key_pressed`, `clear_key_pressed` | same | `kfx_platform/src/bflib_keybrd.c` | `lbKeyOn[]`, `key_modifiers` |
| `key_modifiers` + `update_key_modifiers()` | same | same | `lbKeyOn[]` |

- The prototypes stay reachable from `kjm_input.h`, which includes the
  platform headers, so frontend callers don't change.
- The button-tracking state (`left_button_held`, etc.) stays in kfx_frontend.
- **Removes:** `SimFeedbackCallbacks.{GetMouseX, GetMouseY, is_key_pressed}`
  and `NetCallbacks.{is_key_pressed, clear_key_pressed}` (5).
  - The 45 call sites in kfx_sim, kfx_render and kfx_net become direct
    calls.
  - The sim's key reads remain *reads*, now visible as direct calls; S06 and
    S12 remove them.

## Steps

One commit per section, in the order A, D, B, C. D goes before S06
starts.

## As built (2026-09-28)

- **Commits:** sections A, B and D share files (`main.cpp`, `power_hand.c`,
  `engine_redraw.c`), so they landed as one commit (`2343d3dc1`); C is
  `6181efba9`. Inventory: 555 → 534 → 519, −36 as planned.
- **A:** the three fields are in `struct KfxPathfindingState`; kfx_sim writes
  `kfx_pathfinding_state.*` directly. The `NavContext` argument alternative
  wasn't needed. The test fake world resets the fields per fixture.
- **B:** `struct KfxRuntimeSettings kfx_runtime_settings`
  (`config_settings.h`). Three fields were renamed on the way: `smooth_on` →
  `vid_smooth`, `global_hand_scale` → `hand_scale`, `g_speech_queue_limit` →
  `speech_queue_limit`. `screen_vidmode` turned out to be the configured
  mode only (nothing in kfx_render writes it), so it moved whole, with
  `get/set_screen_vidmode()` moving to kfx_config as accessors
  (`vidmode.h` includes `config_settings.h`, so no caller changed).
  **`first_person_horizontal_fov` did not move:** it is computed by
  kfx_render from the aspect ratio, not configured, and no callback entry
  exposed it. It moves with the synced camera in S07 (ledger row `dropped`).
- **C:** the tables use two conventions (the computer-player, instance and
  job tables are 1-based with a trailing `"none"`; the creature-state tables
  are 0-based), so each gets `_COUNT` (named entries) and `_SLOTS` (highest
  index + 1). kfx_sim asserts its function table has at least `_SLOTS`
  entries; config_funcnames.c asserts `_COUNT`; `config_funcnames_test.cpp`
  checks every index is below `_SLOTS` and used once.
- **D:** also moved `update_key_modifiers()`. This finished the planned S12
  entry `NetCallbacks.is_key_pressed` early (ledger row updated).

- **Checks:** both layering checks, Linux and Windows builds, 2033 Catch2
  tests (the new one: `config_funcnames_test.cpp`), and all 70 short ftests
  at Normal from a clean build of `6181efba9`. The settings round-trip in
  the options screen is left to the maintainer.

## Verification

- Both builds and both layering checks pass.
- Catch2:
  - kfx_pathfinding tests, whose fake world in
    `kfx_pathfinding/tests/pathfinding_fake_world.h` loses the 6 entries;
  - kfx_config settings-schema and loader tests;
  - kfx_sim computer-player config tests.
- ftests: `bug_pathing_stair_treasury`, `bug_ai_bridge`, the creature-state
  ftests, and a settings round-trip (change each B-value in the options
  screen, restart, check).
- Inventory: −36 entries. `move_ledger.py detect` is clean.

## Risks

- **B, `INGAME_RES`:** the pending mode versus the active mode (see B).
  There is an existing settings-schema test to extend.
- **C, a missed name/function pair:** the static asserts catch count drift.
  Name-order drift can't be asserted. Mitigate by keeping each pair
  documented side by side, with a comment in the kfx_sim file pointing at
  the name table.

## Upstream-merge notes

- **C touches files upstream edits often** (`creature_states.c`,
  `player_comp*.c`). When upstream adds a new creature state or AI process,
  it adds a row to *both* tables in the same file. After this stage, the
  name row belongs in `config_funcnames.c`. The ledger rows say so, and
  `move_ledger.py upstream` flags the file.
- A–B–D touch low-churn code.
