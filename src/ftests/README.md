Target Audience: Software Engineers

This is proof of concept; the goal is primarily for setting up scenarios to replicate bugs or test implementation of new features.

Added benefit is that if tests are setup in a continuous integration environment we can automatically know when we break behaviours when adding new features.

The functional tests should run fast, hence you will see `frame_skip = 8` is used by default for some tests. It can be specified per-test.
They also skip the trademark/cutscene by default for super fast launch.

# Quickstart

## Available Test Names

- `example_template_test`
- `bug_imp_tp_attack_door__claim`
- `bug_imp_tp_attack_door__prisoner`
- `bug_imp_tp_attack_door__deadbody`
- `bug_imp_goldseam_dig`
- `bug_pathing_stair_treasury`
- `creature_combat_power_hand` -- not a bug repro: a coverage-driven test (docs/Architecture/testing-harness.md §7.7) exercising real creature-vs-creature combat and a POWER_HAND pickup/drop, on map00011 ("Hearth")
- `creature_temple_prayer` -- coverage-driven: a creature assigned TEMPLE_PRAY reaching CrSt_AtTemple/CrSt_PrayingInTemple, on map00011
- `creature_lair_healing` -- coverage-driven: a hurt creature reaching CrSt_CreatureSleep/CrSt_AtLairToSleep in its lair, on map00011
- `creature_garden_eating` -- coverage-driven: a hungry creature reaching a garden-eating state, on map00011
- `creature_training` -- coverage-driven: a creature assigned TRAIN reaching CrSt_AtTrainingRoom/CrSt_Training, on map00011
- `creature_guard_post` -- coverage-driven: a creature assigned GUARD reaching CrSt_AtGuardPostRoom/CrSt_Guarding, on map00011
- `creature_barracks` -- coverage-driven: a creature assigned BARRACK reaching CrSt_AtBarrackRoom/CrSt_Barracking, on map00011
- `creature_prison_capture` -- coverage-driven: an enemy creature captured via controlled_creature_drop_thing() reaching CrSt_CreatureArrivedAtPrison/CrSt_CreatureInPrison while keeping its own owner, on map00011
- `creature_torture_ownership` -- coverage-driven: contrasts process_torture_function()'s room-owner-vs-creature-owner branches (own creature: harmless; enemy creature: torture points actually accumulate), on map00011
- `net_resync_fake_multiplayer` -- coverage-driven, first user of the fake-multiplayer harness (docs/refactor/todo/ftest-fake-multiplayer.md, `ftest_net_fake.h`): plays both sides of a host/client resync round trip in one process against a fake in-process `NetSP`, proving `send_resync_game()`/`receive_resync_game()` restore state byte-for-byte after a forced desync
- `gui_seam_ingame` -- Phase 0/1 of the in-game GUI → ImGui migration (docs/refactor/ingame-gui/): proves the per-`GMnu_*` opt-in seam with the migrated quit-confirm menu (`GMnu_QUIT`) and the pause-menu launcher (`GMnu_OPTIONS`). Asserts `ingame_imgui_menu_active()` / `ingame_imgui_modal_active()` (frontgui_ingame.h) report both correctly, that `turn_on_menu`/`menu_is_active` are untouched by the migration, and that the migrated quit "Yes" action sends the same `PckA_QuitToMainMenu` packet the legacy `gui_quit_game()` does. Quits the level as its last act, so it is registered separately from `gui_packet_parity`
- `gui_packet_parity` -- the lockstep-determinism regression net for the in-game GUI → ImGui migration (docs/refactor/ingame-gui/). Uses the `ftest_packet_capture.h` harness (per-turn snapshot of `sim_packets[my_player_number]`, taken in `gameplay_loop_logic()` right after `exchange_packets()`) to pin the exact command-packet trace a handful of legacy sidebar menu clicks produce -- tend-to-imprison/flee (`PckA_ToggleTendency`), computer-assist (`PckA_ToggleComputer`), and that minimap zoom stays packet-free. When a menu is migrated to ImGui the same golden trace must still match, driven against the ImGui path. Clicks go through `ftest_util_gui_click()` (`fake_button_click()` under the hood -- no cursor/hit-test, so headless-safe)
- `net_enet_loopback_host` / `net_enet_loopback_join` -- coverage-driven, targets `bflib_enet.cpp` (the real ENet `NetSP`, not the fake above -- two instances can't coexist in one process, see docs/refactor/todo/ftest-fake-multiplayer.md's Phase 2). Two halves of one real host/join/send/receive session over real `127.0.0.1` UDP, each a *separate* `keeperfx` process; neither passes run alone. Beyond the original ping/pong round trip, also exercises `sendmsg_all()`/`sendmsg_single_unsequenced()`, the connection-quality query functions (`GetPing()` et al.), and `drop_user()` (the manual-disconnect path), synchronized by an explicit ack so the host doesn't tear the connection down before the join side has actually read everything. Listed in `long_running_tests_list` (needs `-includelongtests`) purely to keep a bare `-ftests` sweep from hanging on one half waiting for the other -- run both together via `scripts/run_ftest_net_enet_loopback.sh`, not by hand
- `editor_place_creature` -- first ftest coverage for the in-game level editor (docs/refactor/editor/02-editing-toolbox.md §6): calls `editor_open()` directly on an already-loaded level (safe -- its `lvnum` param is only used for a log line, so this doesn't need to replicate the full menu-driven Tools→Editor flow), places a creature via `PckA_EditorRedoCreature` (not the toolbox's normal `PckA_CheatMakeCreature` -- that verb reads position from the packet's ambient `pos_x`/`pos_y`, which `input()` unconditionally overwrites from the headless cursor position every real turn, the same bug class this session's own Redo work found; `PckA_EditorRedoCreature` carries position explicitly instead, immune to that, while still exercising the same creation/journaling logic), asserts it exists with the expected model/owner/experience, then undoes it (`PckA_EditorUndo`) and asserts it's gone. Notably: `editor_open()` sets `simulation_suspended = true`, which freezes `get_gameturn()` — every action after the editor opens uses `turn_delay=0` and polls via `FTRs_Repeat_Current_Action` instead of waiting on turn advancement, since the test framework's own action scheduler stalls forever waiting for a game turn that will never arrive otherwise (same failure shape as `bug_invisible_units_cant_select` above, different cause)
- `editor_paint_terrain` -- docs/refactor/editor/09-toolbox-remainder.md's rect-terrain-op coverage: sends `PckA_EditorPlaceTerrainRect` and asserts the drag-start corner (fully explicit, carried in `actn_par1`/`actn_par2`) ends up the requested slab kind/owner. Deliberately never reads or reasons about the packet's *other* corner (the release point, carried in the ambient `pos_x`/`pos_y`) -- found live while building this test that an ftest action structurally can't observe that field at all: `gameplay_loop_logic()` (`game_session_loop.cpp`) calls `ftest_update()` *before* `input()` each tick, and `clear_packets()` wipes every packet back to zero right after `process_packets()` consumes it, so a test's own read always sees the just-wiped 0 from the *previous* tick -- only `process_packets()` itself, running between those two points, ever sees the real value. Since `editor_apply_slab_rect()` always includes both corners in the resulting box regardless of where the untestable one lands, asserting only the explicit corner is correct no matter what
- `editor_undo` -- extends undo/redo coverage beyond `editor_place_creature`'s creature-only case to a trap: places one via `PckA_EditorRedoTrap` (the same explicit-position trick as creature placement -- the toolbox's own `PckA_EditorPlaceTrap` reads position from the packet's ambient `pos_x`/`pos_y`), asserts it exists, undoes it (`PckA_EditorUndo`), asserts it's gone, then redoes it (resending the same `PckA_EditorRedoTrap` packet, exactly what `editor_journal.cpp`'s own Ctrl+Y handler sends) and asserts it's back with the same position/model/owner. Looks up the placed trap at the slab's *center* subtile (`slab_subtile(slb, 1)`, not `0`) -- `player_place_trap_without_check_at()` re-centers a non-"place on subtile" trap kind onto its slab's center regardless of the subtile it was asked for, so asserting at subtile 0 looks at the wrong cell and never finds it
- `skirmish_setup_override` -- the Skirmish Setup tab's script override in a real running game (docs/refactor/skirmish/). Uses the new optional `pre_start_func` hook in `FTestConfig` (runs after the level is selected, before it loads): the tab's state module (`skirmish_setup.h`) edits the setup of original-pack multiplayer map 50 exactly as the UI would -- gold, creature limit, generation speed, creature pool, creature/room/spell availability, a pinned skirmish AI model, replaced win rules -- and installs the one-shot override (`skirmish_setup_install_for_play`). The action then checks the *live* sim: every edited value, and every untouched one still the level's own. Map 50 has no `LEVEL_VERSION` (v0), so it also proves the v0 -> v1 `CREATURE_AVAILABLE` translation (untouched creatures must not become "forced") and that the prelude's forced version 1 does not leak into the file's own. A second action saves, deliberately breaks the live state, loads, and re-checks everything (plus that the mappack and level number come back) -- the override is long gone by then, so this proves a saved skirmish is self-contained
- `skirmish_setup_locks` -- the same override on a `LEVEL_VERSION(1)` map with runtime-controlled rows (dk2maps map 220, the Reaper spell toggle): an edit to the locked row is ignored (the level's own value stays), edits elsewhere apply, and the level's own runtime conditions and win rules are still loaded (Keep mode)
- `config_content_anchor` / `config_content_readback` / `config_content_reset` -- docs/refactor/editor/fx-plans/10-config-content-model-and-writers.md W5: the config content layer (`kfx_config`'s `cfgc_*`) against the real loaders. *Anchor*: with keeporig level 1 loaded, `ConfigStack` (base + campaign + level files read from disk), the schema's ranges and the name registry must equal the live tables for trap, door, object, slab and room -- block counts, every name, and every plain numeric field of every block (about 6 500 values); drift between the two implementations of the format fails it. *Readback* (uses `pre_start_func`): before the level loads, writes a campaign-scope and a level-scope `trapdoor.cfg` change with `TrapDoorConfigWriter`, then checks the live values after the real loader ran (campaign-only key, key set in both layers reads the level value, untouched key) and that the stack reports the same source layers; restores the files afterwards. *Reset*: writes a level-scope override, resets it through the writer (the generated file must be deleted), and checks the live value is the lower layers' again. Needs the keeporig campaign config directory (`levels/classic_cfgs`) in the data tree
- `config_content_tool_smoke` -- docs/refactor/editor/fx-plans/03-content-editors-foundation.md F3: opens the Config Files window (raw config editor) from the map editor's host inside a running editor session, then from the standalone host, and draws it for 80 ImGui frames. Fails on any ImGui misuse (unbalanced Begin/End, assert) and if fewer than 40 real ImGui frames ran while the window was open
- `config_content_rules_editor` -- docs/refactor/editor/fx-plans/03-content-editors-foundation.md F4 acceptance: the Rules editor's session (`content_struct.h`) edits `[game] PayDaySpeed` at level scope in `pre_start_func` (before the level loads); the running game must use the edited value (read from the live rules struct). The level file is removed afterwards
- `config_content_scratch_level` -- docs/refactor/editor/fx-plans/03-content-editors-foundation.md spike S6: the editor's Playtest runs a scratch copy of the map (number 900002) saved into a campaign's levels folder. In `pre_start_func` this copies keeporig level 1 to `map900002.*`, gives it a level-scope rules file (through the Rules editor's session) and a campaign-scope trapdoor value, then starts level 900002; the real loader must apply both layers. The scratch files are removed afterwards
- `config_content_trapdoor_editor` / `config_content_spell_editor` -- plan 05 T2 / plan 06 S2 acceptance: the editors' sessions edit values at level scope in `pre_start_func` (a trap's and a door's Health; a power's per-level Cost cell and a shot's Damage) and the running game must use them (read from the live tables). The level files are removed afterwards
- `config_content_creature_editor` -- plan 04 C2 / plan 06 S4 acceptance: the Creature editor's sessions edit the first creature's Health (its model file), creature.cfg `[experience] HealthIncreaseOnExp` and ability 1's Time at level scope in `pre_start_func`; the running game must use all three, and the editor's experience-preview formula must equal the engine's `compute_creature_max_health()` at level 4. Files are restored afterwards. (`config_content_anchor` additionally compares 36 creatures' numeric attributes, skipping attributes the level's script sets.)
- `config_content_text_editor` -- plan 09 X2 acceptance: the Text editor's session writes the level string file `map00001.eng.dat` in `pre_start_func` (a new string with an accented letter and a line break); after the level loads the real `get_string()` must return it as UTF-8 (CR LF kept) and an untouched base string must be unchanged. The file is removed afterwards
- `config_content_room_editor` -- plan 07 R2/R3 acceptance: the Room editor's session edits room2's Cost, slab1's GoldHeld and the `[block_health]` DIRT value at level scope in `pre_start_func`; the running game must use all three (the last from the sim's live `block_health[]`). The level file is removed afterwards

## Run Existing Test

1. Enable Functional Testing
    - <b>CTRL+SHIFT+B</b> (VSCode shortcut for switching debug option)
    - select one of the FTEST_DEBUG options
        - FTEST_DEBUG=1    (debug symbols ON, functional tests ON)
		- FTEST_DEBUG=0    (debug symbols OFF, functional tests ON)
2. Update [launch.json](../../.vscode/launch.json)
    - add `-ftests` argument to run all tests *(optionally provide the name of an existing test to run)*

        | without test name | with test name |
        |----------|-------------|
        | `"args": [`<br/>`"-ftests"`<br/>`],` | `"args": [`<br/>`"-ftests", "bug_imp_teleport_attack_door"`<br/>`],` |

3. Run game - Watch test execute
4. View keeperfx.log for test results (lines prefixed with `FTest:`)
5. (optional) If using the `-exitonfailedtest` flag, keeperfx.exe will exit on first failure, exit code represents test results (used for automation)
    - exit code 0 == `test success`
    - exit code -1 == `test failure`
    - optionally view the keeperfx.log to view details on why the test failed
        - example failure message: `FTest: [20] ftest_template_action001__spawn_imp: Failed to level up imp`
        - the above message tells us that at game turn 20, the test failed at function `ftest_template_action001__spawn_imp` because `Failed to level up imp`
6. (optional) `-headless`: for running in CI/sandboxed environments with no real display or audio device (e.g. under coverage instrumentation). Forces SDL's "dummy" video driver (a real window/surface still gets created, just never actually shown) and disables audio the same way `-nosound` does. Check `keeperfx.log` for `SDL video driver: dummy` to confirm it took effect.

## Building with CMake

The CMake build (see the repo root `CLAUDE.md`) needs `-DKFX_FUNCTESTING=ON` to compile this directory in at all -- without it, every `#ifdef FUNCTESTING` block here (and in kfx_platform/kfx_config/kfx_sim/kfx_game/kfx_frontend/kfx_apploop) compiles to nothing and `-ftests` is silently ignored. `KFX_FUNCTESTING` cannot be combined with `-DKFX_BUILD_TESTS=ON` in the same build tree (see the option's own comment in the root `CMakeLists.txt` for why); use separate build trees for the Catch2 unit-test suite and for this harness. Combine with `-DKFX_TEST_COVERAGE=ON` and `-DKFX_FTEST_DATA_DIR=<path to a real KeeperFX install>` for a gcov/lcov coverage report driven by real game execution -- `cmake --build <dir> --target coverage` stages the needed game-data subset, builds keeperfx, and runs every registered (non-long-running) test via `-headless -exitonfailedtest` before capturing. Full details -- headless mode internals, what data gets staged and why, the known GUI-dependent-test gap, and merging this with the unit-test suite's own coverage report into one combined one (`scripts/merge_coverage.sh`) -- are in [`docs/Architecture/testing-harness.md`](../../docs/Architecture/testing-harness.md) §7.

## Create New Test

1. Copy example test files and rename them to reflect your test
    - [ftest_template.h](./tests/ftest_template.h) example: `ftest_bug_warlock_cooks_chicken.h`
    - [ftest_template.c](./tests/ftest_template.c) example: `ftest_bug_warlock_cooks_chicken.c`
2. Update the name of the init function inside your new files to also reflect the name of your test.
    - [ftest_template_init.h](./tests/ftest_template.h#L17) example: `ftest_bug_warlock_cooks_chicken_init`
    - [ftest_template_init.c](./tests/ftest_template.c#L44) example: `ftest_bug_warlock_cooks_chicken_init`
3. Implement test actions in your .c file.
    - follow the naming convention provided `ftest_<test_name>__action###__<action_name>`
        - this is not required, but doing so prevents naming collisions from occuring
    - declare any test variables inside a struct like `ftest_template__variables` see [ftest_template_init.c](./tests/ftest_template.c#L21), eg: `ftest_bug_warlock_cooks_chicken__variables`
        - define instances of the variables struct and set their values, like `ftest_template__vars` see [ftest_template_init.c](./tests/ftest_template.c#L30), eg: `ftest_bug_warlock_cooks_chicken__vars`
        - again, not required, but highly recommended for preventing many naming collisions; tests usually have a lot of variables!
    - use the variables inside your test actions
        1. when appending test actions using `ftest_append_action`, pass a reference of your vars, see [ftest_template_init.c](./tests/ftest_template.c#L49)
        2. access the vars inside your actions by casting `args->data` to your variables type, see [ftest_template_init.c](./tests/ftest_template.c#L60)
            - doing this allows for more flexibility, the test actions can be re-used multiple times with different data in your test, or other tests can use your actions.
            - NOTE: you may access your glolbal variables `__vars` directly instead of using `args->data`, but this makes the actions not as flexible or re-usable.
4. Update your tests init function
    - for example, inside `ftest_bug_warlock_cooks_chicken.c` make sure all your actions are appended
        - `ftest_append_action( ftest_template_action001__spawn_warlock,            100, NULL );`
        - `ftest_append_action( ftest_template_action002__drop_chicken_on_warlock,  100, NULL );`
    - actions are executed:
        - **sequentially**: `action002` will not execute until the previous action returns `FTRs_Go_To_Next_Action`
        - **after the `game turn delay`**: provided to `ftest_append_action(<test_action>, <game_turn_delay>)`
        
         this means if an action always returns `FTRs_Repeat_Current_Action` it will never finish and the `test will run forever`
     
5. Add your test to the test list [ftest_list.c](./ftest_list.c)
    - add the include for your tests header file
        - example: `#include "tests/ftest_bug_warlock_cooks_chicken.h"`
    - update [tests_list](./ftest_list.c#L30)
        - add the `test_name` of your test, your tests `init_func`, the `level_file` and specify the `frame_skip` *(there are other advanced optional values not listed here)*
        - example: `{ .test_name="bug_warlock_cooks_chicken",  .init_func=ftest_bug_warlock_cooks_chicken_init,  .level_file="keeporig",  .level=1,  .frame_skip=0 },`
        - this `test_name` is what you pass to the `ftests` arg in [launch.json](../../.vscode/launch.json) if you only want to run that test
6. [Run Your Test](#run-existing-test)

###  See Example Tests:

| .h | .c |
|----------------------------------------|----------------------------------------|
| [ftest_template.h](./tests/ftest_template.h) | [ftest_template.c](./tests/ftest_template.c) |
| [ftest_bug_imp_tp_job_attack_door.h](./tests/ftest_bug_imp_tp_job_attack_door.h) | [ftest_bug_imp_tp_job_attack_door.c](./tests/ftest_bug_imp_tp_job_attack_door.c) |
| [ftest_bug_imp_goldseam_dig.h](./tests/ftest_bug_imp_goldseam_dig.h) | [ftest_bug_imp_goldseam_dig.c](./tests/ftest_bug_imp_goldseam_dig.c) |

### See Advanced Example Tests:

| .h | .c |
|----------------------------------------|----------------------------------------|
| [ftest_bug_pathing_pillar_circling.h](./tests/ftest_bug_pathing_pillar_circling.h) | [ftest_bug_pathing_pillar_circling.c](./tests/ftest_bug_pathing_pillar_circling.c) |

- This advanced test showcases using a single action to perform multiple stages, using custom data via void*
- It allows for re-usable test actions that can be shared between tests. (Think of a setup function that creates a base with different rooms and doors, then fills them with different creatures for each test)
- In the example itself `ftest_bug_pathing_pillar_circling` you can see that it creates a tunneler that digs towards the dungeon heart.
- The second test action does the same thing, but from different map coordinates and with a different base block type, showcasing the differences of data passed to the action.

| .h | .c |
|----------------------------------------|----------------------------------------|
| [ftest_bug_imp_tp_job_attack_door.h](./tests/ftest_bug_imp_tp_job_attack_door.h) | [ftest_bug_imp_tp_job_attack_door.c](./tests/ftest_bug_imp_tp_job_attack_door.c) |

- This advanced test showcases defining multiple tests inside the same file, while re-using shared functionality between the tests
- Each test has an init func that is slightly different from the other. One also modifies the data slightly to change logic inside actions

### Explanation / Flow

1. The desired campaign/mappack is atuomatically loaded for the current active test
    - see [ftest_list.c](./ftest_list.c) for examples
2. Init function is called for the current active test, which creates a list of actions to perform accordingly.
    - NOTE: You can also call one-time logic here, like altering the map before the test actions are executed.
3. The game loop will call each action in the list after the desired GameTurn delay.
    - actions are counted as completed when they return `FTRs_Go_To_Next_Action`, if they return `FTRs_Repeat_Current_Action`, the action will be executed again next game turn.
    - by default, if a test fails, the next test will be ran, unless you use the `-exitonfailedtest` flag.
    - (optional) when using the `-exitonfailedtest` flag if there is a failure at any stage (you decide this with your test, using the [FTEST_FAIL_TEST](./ftest.h#L24) macro) the test exits immediately, closing the program.
        - exit code 0 == `test success`
        - exit code -1 == `test failure`
        - optionally view the keeperfx.log to view details on why the test failed
            - example failure message: `FTest: [20] ftest_template_action001__spawn_imp: Failed to level up imp`
            - the above message tells us that at game turn 20, the test failed at function `ftest_template_action001__spawn_imp` because `Failed to level up imp`
    - (optional) when using the `-includelongtests` flag, extra tests from `long_running_tests_list` will be included in the test search.


Seeds are overriden when using the functional tests. This means tests should perform the same every time for random events.

- (NOTE: There may still be some unaccounted for RNG, the bug_imp_tp_attack_door__deadbody test is an example of this. Sometimes the imps do not fly into the door after teleport and the door isn't attacked/broken...)
- (The only srand not wrapped yet is inside [crypt.h](../../deps/zlib/contrib/minizip/crypt.h#L113) but requires further investigation)
