# Built-in AI (`Computer2`) — architecture investigation

Status: **investigation only, no code written.** Date: 2026-09-23.

This document is the prerequisite groundwork for
[`LLM/01-integration-plan.md`](LLM/01-integration-plan.md): before deciding what a future LLM-driven
player seat needs, this records **what the existing scripted/built-in AI actually is and does** — its
data model, its per-turn behaviour, its config surface, how the level script and Lua already control it
at runtime, its save/load lifecycle, and, most importantly, exactly how it acts on the world. Nothing
here proposes a change; §9 hands off the gaps to the LLM plan.

Context this builds on: `docs/refactor/skirmish/01-scope-expansion-research-winlose-ai.md` §5 already
sketched a per-slot `Controller` model (`Builtin | Roaming | Off | External`) for the skirmish Slots &amp;
AI page, with `External` reserved but unimplemented, favouring a **packet-driven** seat over an in-sim
`Computer2` variant. This document verifies that recommendation against the source rather than assuming
it, and finds it independently confirmed (§5, §8).

## 1. File map

The AI is **not** one file. It's a `kfx_sim` file family (all C, `#pragma pack(1)` structs shared with
`kfx_config`) plus one `kfx_config` loader:

| File | Size | Content |
|---|---|---|
| `src/kfx_sim/src/player_computer.c` | 1694 lines | setup/teardown, the per-turn entry point, the checks/events dispatch loop, hate tracking, trap-placement helpers, gold-vein search |
| `src/kfx_sim/src/player_compprocs.c` | 1396 lines | `ComputerProcess` implementations: `find_best_process`, `set_next_process`, room-build check/setup, dig-to-entrance/gold, attack setup, `get_computer_process`/`suspend_process`/`reset_process`/`shut_down_process` |
| `src/kfx_sim/src/player_comptask.c` | 4228 lines (the largest of the family) | `ComputerTask` implementations: `game_action`/`try_game_action` (§5, the action dispatcher), `tool_dig_to_pos2_f` (the pathfinding/digging state machine), all `create_task_*`, `process_tasks`, `script_computer_dig_to_location` |
| `src/kfx_sim/src/player_compchecks.c` | ~57 KB | `ComputerCheck` implementations: money, room expansion, trap/door availability, neutral places, enemy entrances, slap-imps, sacrifice, etc. |
| `src/kfx_sim/src/player_compevents.c` | ~36 KB | `ComputerEvent` implementations: dungeon breach, room lost/full, payday, fight detection, magic-foe, etc. |
| `src/kfx_sim/src/player_complookup.c` | 269 lines | opponent/hate lookup helpers |
| `src/kfx_sim/src/player_computer_data.cpp` | 70 lines | just `valid_rooms_to_build[]` (room-adjacency preference table) |
| `src/kfx_sim/include/player_computer.h` | 597 lines | every struct/enum declaration and function prototype for the family above |
| `src/kfx_config/src/config_compp.c` / `include/config_compp.h` | 465 / 141 lines | the `keepcompp.cfg` loader: `ComputerProcess`/`ComputerCheck`/`ComputerEvent`/`ComputerType`/`ComputerPlayerConfig` |

Test coverage: `player_computer_test.cpp` (3 cases — only `count_creatures_in_dungeon`/
`count_diggers_in_dungeon`/`count_entrances`), `player_compchecks_test.cpp` (7),
`player_comptask_test.cpp` (6), `player_compprocs_test.cpp` (7), `player_compevents_test.cpp` (2),
`config_compp_test.cpp` (~9, the `keepcompp.cfg` block-parsing). **~35 Catch2 cases against ~6000 lines
of decision logic** — thin, and concentrated on narrow list-walking helpers rather than `find_best_process`'s
priority/backoff selection, the dig state machine, or the dispatch loop itself (see §10).

## 2. Data model

**`struct Computer2`** (`player_computer.h:424-450`, `sizeof == 5322`) is the per-player-slot AI
instance: `kfx_sim_state.computer[PLAYERS_COUNT]` (`kfx_sim_state.h:251`). It holds `task_state`
(select/wait/perform), `dungeon_idx` (an **index**, not a pointer, into `kfx_sim_state.dungeon[]` — the
header comment at `player_computer.h:434` says explicitly this is because the struct is saved and
network-synced as a raw blob), `model`, runtime copies of `processes[COMPUTER_PROCESSES_COUNT+1]` /
`checks[COMPUTER_CHECKS_COUNT]` / `events[COMPUTER_EVENTS_COUNT]` (mutable per-instance, seeded from
config — see §4), `opponent_relations[PLAYERS_COUNT]` (hate tracking plus up to
`COMPUTER_SPARK_POSITIONS_COUNT` remembered attack positions per opponent), `trap_locations[]`, and
`task_idx` — the head of a linked list into a **global, shared** task pool,
`kfx_sim_state.computer_task[COMPUTER_TASKS_COUNT]` (100 entries, `kfx_sim_state.h:250`), not a
per-player pool. Every computer player in a game competes for slots in that one fixed-size array
(`get_free_task()`, `player_comptask.c:552`, scans it globally) — a hard, shared ceiling (see §9.4).

**Three config-driven list types**, each with a static template (`config_compp.h`) and a per-`Computer2`
runtime copy:

- **`ComputerProcess`** (`config_compp.h:45-68`) — a long-running goal: build a room, dig to
  entrance/gold, sight-of-evil, attack. Carries `priority` (`int64_t`, can be negative),
  `process_configuration_value_2..5`, and `func_check`/`func_setup`/`func_task`/`func_complete`/
  `func_pause` — small integer `FuncIdx`s resolved at config-load time into
  `computer_process_func_list[]` (`player_computer.h:476`, extern function-pointer table).
- **`ComputerCheck`** (`config_compp.h:70-80`) — a periodic condition test, gated by `turns_interval`
  (e.g. `check_for_money`, `check_for_place_trap`, `check_neutral_places`, `check_enemy_entrances`,
  `check_slap_imps`).
- **`ComputerEvent`** (`config_compp.h:82-95`) — reactive triggers off the shared `kfx_sim_state.event[]`
  queue (`cetype==0`, matched by `mevent_kind`) or a periodic test (`cetype` 1-4) — dungeon breach, room
  lost/full, payday, fight detection.
- **`ComputerTask`** (`player_computer.h:308-415`) — the actual queued unit of work a process creates
  (dig-to-gold, dig-to-attack, move-creatures, sell-traps, sacrifice-diggers, …), stored in the shared
  `computer_task[]` pool and linked via `comp->task_idx` / `ctask->next_task`.

**`ComputerType`** (`config_compp.h:97-111`) is a named personality/model — the `[computerN]` block in
`keepcompp.cfg` — carrying tuning values (§4) plus which named processes/checks/events it enables.
`ComputerPlayerConfig` (`config_compp.h:113-127`, the global `comp_player_conf`) is the whole loaded file:
process/check/event *type tables* (the catalogue every model draws from) plus up to
`COMPUTER_MODELS_COUNT` (64) `computer_types[]`.

## 3. Per-turn tick

Entry point: **`process_computer_players2()`** (`player_computer.c:1555-1581`), called once per game
turn. For each of `PLAYERS_COUNT` slots, it runs the tick if the player is active **and** either
`PlaF_CompCtrl` is set on `player->allocflags` or `dungeon->computer_enabled & 0x01` (the separate
"assist" toggle — see §7). Runs at most one player's `computer_player_demands_gold_check()` follow-up
per call; a `needs_gold_check` accumulator triggers one shared `check_map_for_gold()` afterwards, not one
per player.

**`process_computer_player2(plyr_idx)`** (`player_computer.c:1480-1508`):

1. `comp->tasks_did = 1` only if `comp->processes_time != 0` and `comp->turn_begin <= get_gameturn()` —
   otherwise the AI is fully idle that turn (this is how `turn_begin`, a `keepcompp.cfg` tuning value,
   delays AI activation).
2. `computer_check_events(comp)` (`player_computer.c:1352-1402`) — scans `events[]`, dispatching by
   `cetype`.
3. `process_checks(comp)` (`player_computer.c:1404-1426`) — scans `checks[]`, running any whose
   `turns_interval` has elapsed via `computer_check_func_list[]`.
4. `process_processes_and_task(comp)` (`player_computer.c:1428-1478`) — the task-state machine:
   `CTaskSt_Wait` decrements `gameturn_wait`; `CTaskSt_Select` calls `set_next_process()`;
   `CTaskSt_Perform` invokes the ongoing process's `func_task` callback. `process_tasks(comp)`
   (`player_comptask.c:4140-4183`) also runs here, gated to every `comp->click_rate` turns, walking the
   `ComputerTask` linked list and calling `task_function[ctask->ttype].func(comp, ctask)` per active task.
5. **At most one process step and one task-list sweep per player per game turn** — enforced by
   `comp->tasks_did`, with an `ERRORLOG` if violated (`player_computer.c:1505-1507`). This is a
   deliberate low-CPU-budget design ("do a little work every turn"), not "compute a full turn's plan."

**Process selection** — `find_best_process()` (`player_compprocs.c:1282-1351`) ranks eligible processes
in three tiers: (a) previously-"wait"-failed processes (`process_parameter_3 > 0`) whose backoff exceeds
150 turns, (b) processes that have run before (`last_run_turn > 0`) and aged past 100 turns, ranked by
`priority`, (c) fresh/never-run processes, ranked by `priority`. `set_next_process()`
(`player_compprocs.c:1353-1392`) then calls the winner's `func_check`; on `CProcRet_Continue` it calls
`func_setup` and moves to `CTaskSt_Perform`.

**Room-building** (the most-exercised process family): `computer_check_any_room()`
(`player_compprocs.c:351+`) checks availability/capacity/build-task budget
(`comp->max_room_build_tasks`) via `get_room_kind_total_and_used_capacity` /
`computer_get_room_kind_free_capacity`. `computer_setup_any_room()`/`_continue()`
(`player_compprocs.c:123,157`) call `computer_setup_build_room()` (`player_computer.c:194-247`), which
spirals outward from existing rooms of a preferred adjacent kind (`valid_rooms_to_build[]`,
`player_computer_data.cpp:51-65`) at increasing distance/fit tiers.

**Digging**: `tool_dig_to_pos2_f()` (`player_comptask.c:1880+`) is an incremental, budget-limited
(`COMPUTER_TOOL_DIG_LIMIT = 356` per call, `player_computer.h:47`) wall-hugging pathing state machine,
re-entered across many turns by the dig-family tasks (`CTT_DigToGold`, `CTT_DigToEntrance`,
`CTT_DigToAttack`, `CTT_DigToNeutral`) until it returns `TDR_ReachedDestination` or an error.

## 4. Decision signals and personality tuning

**Signals read** — all direct struct access, no abstraction layer, no per-player filtering beyond
ownership: `Dungeon` fields (`total_money_owned`, `creatures_total_pay`, `num_active_diggers`,
`room_list_start[]`, `creatr_list_start`/`digger_list_start`, `mnfct_info.trap_*`, `cta_start_turn`,
`max_creatures_attracted`), the global `GoldLookup[]` array (gold veins, filtered by
`gldlook->player_interested[owner]` — an "already claimed/looked at" bitmask, **not** fog of war), `Room`
list traversal, `SlabMap`/`Map` for dig-target slab kind, `CreatureControl`/`Thing` lists for creature
counts/state/health, and `comp->opponent_relations[]` (per-opponent `hate_amount` plus remembered attack
"spark" positions, sorted by hate — `get_opponent()`, `player_computer.c:476-544`).

**Personality/model tuning** — `[computerN]` blocks in `config/fxdata/keepcompp.cfg` (17 shipped models,
lines 810-956), each a `struct ComputerType`:

- `Values`: `dig_stack_size` (% of dungeon area to dig at once), `processes_time` (turn interval between
  process steps), `click_rate` (turn interval between micro-actions — room-tile placement, dig
  highlighting, creature drops), `max_room_build_tasks`, `turn_begin` (activation delay),
  `sim_before_dig`, `drop_delay`.
- `Processes` / `Checks` / `Events`: space-separated mnemonic lists selecting which global
  `[processN]`/`[checkN]`/`[eventN]` definitions this personality uses, in priority order — e.g.
  `computer0` ("General build, defend and attack") enables `Attck1`/`AttckSafe`; `computer8` ("Build
  Only") has almost no attack/move checks; `computer12` ("Rapid Gold Digging… Imp Army") has just 4
  gold-dig processes.
- `[common]`: `SkirmishFirst`/`SkirmishLast` (the random-model range for skirmish, consumed in
  `setup_computer_players2()`, `player_computer.c:1608-1611`), `DefaultComputerAssist`,
  `ComputerAssists[4]`.

Concrete check tunables (from `keepcompp.cfg:242-390`): `Money1` (`Params = 500 -1000 0 0` — low-gold
threshold / critical threshold), `RoomExp1..6` (room-expansion turn thresholds, 10–101010), `TrapAvl1/2`
(trap-placement aggressiveness), `NeutPlc1..7` (neutral-tile claiming, turn-gated up to 25000),
`ImpSlap1..3` (`Params = 75 0 0 -250` — % of imps to slap, minimum turn). Backing fields:
`ComputerProcess.priority`/`process_configuration_value_2..5`/`process_parameter_1..3,5`/`flags`,
`ComputerCheck.turns_interval`/`primary/secondary/tertiary_parameter`,
`ComputerEvent.test_interval`/`primary/secondary/tertiary_parameter`.

All of this is also mutable at script/Lua runtime without reloading the file (§6).

## 5. Action surface — direct in-process calls, confirmed not packets

This is the load-bearing finding for the LLM plan. Every AI mutation funnels through
**`game_action()`** (`player_comptask.c:289-433`), a switch on `enum GameActionTypes`
(`player_computer.h:89-126`) that calls simulation functions **directly, in-process**:
`magic_use_available_power_on_subtile`/`_on_thing`/`_on_level` (keeper powers), `player_build_room_at()`
(`GA_PlaceRoom`), `player_place_trap_at()`/`player_sell_trap_at_subtile()` (traps),
`tag_blocks_for_digging_in_area()`/`place_slab_type_on_map()` (`GA_MarkDig`),
`dump_first_held_thing_on_map()` (hand-drop). `try_game_action()` (`player_comptask.c:435-445`) wraps it
and decrements `comp->tasks_did` on success — the per-turn action-budget enforcement matching §3's
"at most one" rule.

**Verified, not just claimed**: `game_action` is defined once, in `player_comptask.c`, and is never
called from `kfx_game`, `kfx_net`, or `kfx_frontend` — grepping those trees for the symbol returns
nothing. The one call that superficially looks packet-shaped — `GA_PlaceDoor` → `packet_place_door()`
(`player_comptask.c:380`) — is not an enqueue into the `PckA_*` queue; `packet_place_door()`
(`player_instances.c:1376`) is a plain shared mutator that the *human* packet-processing path also calls
after decoding a real `PckA_PlaceDoor` packet (`packets_input.c:880`). The AI calls the same mutator
directly, bypassing packet creation and serialization entirely. No other packet-layer touchpoint exists
anywhere in the `player_comp*.c` family.

**Confirmed from the consuming side too**: `process_packets()` (`src/kfx_net/src/packets.c:1588-1598`)
iterates every connected `NetUserId` and calls `process_user_packet(user)` only when
`(packet_player->allocflags & PlaF_CompCtrl) == 0` — a `PlaF_CompCtrl` seat's packet, if it even has one,
is never dispatched. The built-in AI and the packet system are two disjoint code paths by construction,
not just by convention.

**Determinism**: the AI's only randomness source is `AI_RANDOM(range)` (`kfx_sim_state.h:180`), backed
by its own synced seed `kfx_sim_state.ai_random_seed` (`kfx_sim_state.h:271`) — grouped with
`GAME_RANDOM`/`THING_RANDOM`/`PLAYER_RANDOM` (synced), distinct from `UNSYNC_RANDOM`/`SOUND_RANDOM`.
Because `Computer2`, `computer_task[]` and `ai_random_seed` all live inside the single `kfx_sim_state`
blob that gets `memcpy`'d wholesale for save/resync/level-reset (architecture.md §6.2), every peer in a
multiplayer game runs its own identical copy of every `Computer2` and they stay in lockstep **without any
packets at all** for the AI's own decisions — nothing non-deterministic is ever read. This determinism
guarantee is exactly what a real external decision-maker (network round trip, model inference latency)
cannot offer; see the LLM plan's §2.

## 6. Script and Lua runtime control

These mutate the **live runtime copy** in `kfx_sim_state.computer[i]`, not the static `keepcompp.cfg`
table — changes are lost on the next `setup_a_computer_player()`/level reload unless reapplied.

| Command | Handler | Effect |
|---|---|---|
| `COMPUTER_PLAYER(p, model\|ROAMING\|OFF)` | `computer_player_check`/`_process` (`lvl_script_commands.c:6103-6203`) | `OFF` → `script_support_setup_player_as_zombie_keeper()`; `ROAMING` → sets `PT_Roaming` + `PlaF_CompCtrl` directly; model → full re-init via `script_support_setup_player_as_computer_keeper(i, model)` (§7) + `recalculate_player_creature_digger_lists(i)` |
| `SET_COMPUTER_GLOBALS` | `lvl_script_commands.c:6348-6378` | overwrites `dig_stack_size`, `processes_time`, `click_rate`, `max_room_build_tasks`, `turn_begin`, `sim_before_dig`, and conditionally `task_delay` |
| `SET_COMPUTER_PROCESS` | `lvl_script_commands.c:6408-6450` | name-matches `comp->processes[]` by mnemonic, overwrites `priority`/`process_configuration_value_2..5` |
| `SET_COMPUTER_CHECKS` | `lvl_script_commands.c:6480-6525` | name-matches `comp->checks[]`, overwrites `turns_interval`/parameters |
| `SET_COMPUTER_EVENT` | `lvl_script_commands.c:6559-6605` | name-matches `comp->events[]`; branches on `level_file_version` (v0 sets fewer params) |
| `COMPUTER_DIG_TO_LOCATION` | `lvl_script_value.c:697-700` → `script_computer_dig_to_location()` (`player_comptask.c:4192`) | resolves two `TbMapLocation`s and creates a dig task directly |

**Lua equivalents** (`src/kfx_script/src/lua_api.c`): `lua_Computer_player` (:83),
`lua_Computer_dig_to_location` (:1567), `lua_Set_computer_process` (:1576),
`lua_Set_computer_checks` (:1609), `lua_Set_computer_globals` (:1643),
`lua_Set_computer_event` (:1675) — near-duplicate implementations of the same field-by-name mutation
logic, not shared with the script-command versions.

## 7. Lifecycle

**Creation** — `script_support_setup_player_as_computer_keeper(plyr_idx, comp_model)`
(`player_computer.c:1320-1350`) sets `PlaF_Allocated`, `is_active = 1`, `PlaF_CompCtrl`
(`player_data.h:46`, bit `0x40`), calls `init_player_start()`, then `setup_a_computer_player()`
(`player_computer.c:1219-1317`), which **`memset`s the whole `Computer2` to zero** and repopulates
`processes[]`/`checks[]`/`events[]` by copying from `comp_player_conf.{process,check,event}_types[...]`
per the model's `[computerN]` block — every re-setup fully discards runtime AI state (the task list is
not cleared here; only `setup_computer_players2()`, below, clears it globally).

- **Frontend/skirmish path** — `setup_computer_players()` (`thing_list.c:1290-1300`, called from
  `main_game.c:690`, **before** `load_script` — `main_game.c:687` vs `:540`) calls
  `setup_computer_player(plr_idx)` (`thing_list.c:1277-1288`) per slot: if
  `find_players_dungeon_heart(plr_idx)` finds a heart, model 0 via
  `script_support_setup_player_as_computer_keeper`; if not, `script_support_setup_player_as_zombie_keeper`
  (non-active, no AI) — conditional on heart presence, not unconditional on every unoccupied slot.
- **Skirmish random-model path** — `setup_computer_players2()` (`player_computer.c:1583-1627`, a
  *different* function despite the near-identical name) zeroes the shared `computer_task[]` pool,
  re-seeds `srand()`, and for each active player picks
  `skirmish_AI_type = GAME_RANDOM(maxSkirmishAI+1-minSkirmishAI) + minSkirmishAI` from
  `comp_player_conf.skirmish_first/skirmish_last` (the local player gets `player_assist_default`
  instead), then calls `setup_a_computer_player()`.
- Ordering consequence: a level's `COMPUTER_PLAYER` script command re-initialises (fully `memset`s) a
  slot the frontend already set up, because the script runs after the frontend path.

**Save/load** — `restore_computer_player_after_load()` (`player_computer.c:1644-1668`): if the player
doesn't exist or `is_active != 1`, the `Computer2` is fully zeroed and the dungeon link invalidated;
**otherwise the struct is left untouched** (it was already restored as raw save-file bytes) and only
`computer_set_dungeon()` re-resolves `dungeon_idx` from the freshly-loaded `dungeon[]` array. So
everything — process/check/event runtime state, the entire task queue, hate history, trap-location
memory — survives a save/load for an active computer player; nothing is reset except the dungeon-index
recomputation. Matches architecture.md §6.2's "raw blob" model, and matches
`docs/refactor/skirmish/01-scope-expansion-research-winlose-ai.md` §14's independent confirmation for the
skirmish-override feature.

**Disabling AI for a slot** — `PlaF_CompCtrl` (`player_data.h:46`) is the flag `process_computer_players2()`
checks to decide whether to run a slot's tick at all, and the flag `process_packets()` checks to decide
whether *not* to dispatch a slot's packet (§5). It is also used pervasively elsewhere (33 call sites) for
camera control, network join/leave, packet validation, checksum/desync detection (`net_checksums.c`
explicitly excludes AI players from checksum comparison), and save/load. A **separate** per-dungeon
"assist" flag, `dungeon->computer_enabled & 0x01` (toggled by `toggle_computer_player()`,
`player_computer.c:1670-1689`), lets the AI tick run *alongside* a human seat (auto-pilot) without
setting `PlaF_CompCtrl` — additive assistance, not a substitute for AI ownership.

**There is no flag that keeps a slot counted as AI (for sync/checksum/UI purposes) while suppressing its
decision logic.** The closest existing primitive runs *extra* AI on top of a human seat; it does not turn
decision-making off for an AI-flagged seat. See §9.3.

## 8. Multiplayer/sync model, restated

DK's multiplayer is lockstep: every client simulates the whole world identically from the same per-turn
input packets (`packets_input.c`). Human/network seats are driven exclusively through
`struct Packet` (`packet_data.h:414-427`), exchanged via `exchange_packets()`/`process_packets()`
(`src/kfx_net/src/packets.c`) — the same path a local (non-networked) skirmish game uses, just without
the actual socket send/receive. AI seats never touch this path (§5) and stay in sync purely because every
peer runs the identical deterministic `Computer2` logic off the same synced RNG stream. These are the two
existing, working models this codebase has for "who controls a seat" — nothing today sits between them.

## 9. Gaps for a future external (LLM) controller

Handed off in full to `LLM/01-integration-plan.md`; summarised here as the concrete list this
investigation surfaced:

1. **No fog-of-war-respecting observation API.** The AI reads `Dungeon`/`Room`/`GoldLookup[]`/
   `Thing`/`CreatureControl` globally; the only per-player gating in the AI itself is
   `player_interested[owner]` ("already noticed/claimed"), not visibility. A real fog-of-war-aware,
   per-player read-only snapshot is new work — though the underlying primitive it would be built on
   (`map_block_revealed(mapblk, plyr_idx)`, `src/kfx_sim/src/map_data.c:299-330`) already exists and is
   used engine-wide; see the LLM plan's observation-API section.
2. **The action surface bypasses packets entirely** (§5) — confirmed, not inferred. Any seat that needs
   packet-level recording, replay, rate-limiting, or human-equivalent validation needs a genuinely new
   code path; none of `game_action()`'s ~30 cases can be reused as-is for that.
3. **No mid-flight decision-suppression flag** (§7, last paragraph) — `PlaF_CompCtrl` conflates "is this
   seat AI for sync/UI/checksum purposes" with "run the decision loop"; there is no independent kill
   switch for just the latter.
4. **The task pool is global, not per-player**, and fixed at 100 entries
   (`COMPUTER_TASKS_COUNT`, `config_compp.h:33`) — any coexisting external-agent bookkeeping that wanted
   to reuse `ComputerTask` machinery would compete with every built-in AI seat in the same game for the
   same fixed pool.
5. **Deep coupling to global function-pointer tables.** `computer_process_func_list[]`/
   `computer_check_func_list[]`/`computer_event_func_list[]`/`computer_event_test_func_list[]`
   (`player_computer.h:475-485`) are indexed by small integers resolved from `keepcompp.cfg` mnemonics at
   load time (`config_compp.c`'s `resolve_compp_func_type_pointers`, itself already flagged in-repo as a
   layering workaround, `docs/refactor/todo/check-layering-symbol-level-blind-spot.md`). New behaviour
   must extend these tables and the config schema; it cannot be composed from outside.
6. **The whole design assumes determinism** (§5, §8). An LLM agent — nondeterministic timing, no synced
   RNG, living outside the `kfx_sim_state` blob — cannot be represented as a `Computer2` variant without
   breaking the model that makes AI seats sync for free. This is the strongest evidence for the
   already-recorded design direction (skirmish/01 §5.3): a packet-driven pseudo-human seat, not an
   in-sim AI variant.
7. **Thin test coverage** (§1, §10) — a refactor that carves out hooks for an external seat has little
   regression net over `find_best_process`'s selection logic, the dig state machine, or the dispatch loop
   itself.
8. **Two setup functions share almost the same name** — `setup_computer_players()` (`thing_list.c:1290`,
   frontend/free-play, heart-conditional, model 0) and `setup_computer_players2()`
   (`player_computer.c:1583`, skirmish, random-model) are genuinely different call paths; any future
   per-slot controller UI/config work must be explicit about which one it's extending.

## 10. Test coverage detail

See §1's table. The existing suites test isolated helpers (creature/digger/entrance counting, config
block parsing) rather than the stateful parts of the system (process selection order, task state
transitions, the dig pathing state machine). A change that adds an "external" hook to
`process_computer_player2()`, `process_packets()`, or the `PlaF_CompCtrl` semantics should budget for new
Catch2 coverage of the paths it touches, not assume the existing suite will catch a regression.
