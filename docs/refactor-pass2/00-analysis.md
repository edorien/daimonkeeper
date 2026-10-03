# Refactor pass 2 — opportunities, with a focus on the callback mechanism

**Status:** analysis / proposal (2026-09-27). Nothing here is implemented yet.
**Scope:** `src/` as of `216be51d5` on `refactor-renderer`. Numbers come from
[`tools/callback_inventory.py`](tools/callback_inventory.py) (re-runnable; use
it to track progress) plus hand verification of every individual finding
quoted below.

The first refactor (`docs/refactor/`, summarised in
[`architecture.md`](../Architecture/architecture.md)) produced a strict,
CI-enforced library ladder. That goal was met: `check_layering.py --strict`
and `check_layering_symbols.py` are both clean today. This pass asks a
different question: **is the code on each side of the lines in the right
place, and is the mechanism used to cross the lines paying for itself?**

---

## 0. Summary

1. **The callback surface is large and still growing.** There are 20
   callback tables with **570 function-pointer entries**, plus 5 more
   single-function "provider" setters, plus about 5 "push this value down"
   setters. Adding one entry means touching about 6 places. Around
   **5,000 lines** are pure plumbing: callback headers (~1,800), no-op
   default tables (~1,200), `main.cpp` wrappers/tables (~1,450 of its 2,386
   lines), and tests that only exercise the no-ops (~840).
2. **Most entries are not real inversions of control.** They are symptoms of
   code or state that sits in the wrong library. By my estimate (the
   categories overlap; see §3.3), **about 60–70% of entries** fall into one of:
   - trivial field accessors (62);
   - duplicates (28 implementations exposed through 2–4 entries each);
   - presentation code living in `kfx_sim`;
   - gameplay code living in `kfx_config`;
   - simulation state (lights, the game turn, the local player index) living
     above `kfx_sim`.
3. **The tables are grouped by consumer ("what kfx_sim needs"), not by
   provider.** That's why `SimFeedbackCallbacks` has 134 entries spanning
   nine unrelated concerns and six implementing libraries, and why
   `main.cpp` has to hand-assemble every table.
4. **The mechanism has some sharp edges.**
   - Tables are wired with *positional* initializers. Adjacent entries with
     the same signature can be silently swapped.
   - The defaults are silent no-ops, and wiring is interleaved with
     initialization. So a call made before its table is wired quietly does
     nothing.
   - Several hot paths pay for an indirect call just to read a struct field:
     the pathfinder's `thing->mappos`, and `get_gameturn()` inside every log
     macro.
5. **Recommendation.** Keep the callback pattern, but reserve it for genuine
   inversions. Shrink the surface first by moving code and state to where it
   belongs (§6, work items W1–W12). Then regroup the surviving ~150–200
   entries into **provider-owned ports** whose implementation tables live in
   the implementing library, with designated initializers and loud defaults
   (§5, W13). `main.cpp` would then lose most of its ~1,450 lines of
   wrappers and tables, keeping a short list of `set_*_port(&…)` calls.
6. **Alternatives considered (§7).**
   - *More libraries:* yes, but only where the cut is already one-way. That
     means a data-model header set, the `cfgc_*` content layer, and possibly
     the computer-player AI. Splitting `kfx_sim` by domain would create
     hundreds of new callbacks.
   - *`std::function`/`std::bind`:* no. 95% of callback call sites are in C
     files, nothing needs captured state, and it would fix none of the
     structural problems.
7. **The heavy-log build should become a "Logging" game option (§10)**,
   with the levels Off (crash reports only) / Normal (today's standard log) / Debug (today's
   heavy log, without GPU validation) / Debug max (everything). The two
   executables differ only in `BFDEBUG_LEVEL`. Making it an option:
   - halves library compile work;
   - leaves one binary to ship and test;
   - makes ~740 debug-log sites reachable that are currently dead in *both*
     builds;
   - stops Vulkan validation being switched on as a side effect of heavy
     logging (it becomes a separate, restart-only option).

   The measured cost is ~114 KB of code (0.9%) plus a well-predicted branch
   per debug-log site, which a replay benchmark should confirm.
8. **Camera and `kfx_model` (§8).**
   - The *synced* camera is simulation state whose code lives in kfx_render;
     it moves to kfx_sim, so no camera port is needed (W19).
   - `kfx_model` should start header-only (`struct Thing`/`Map`/`SlabMap`,
     whose include closures are tiny). That removes the hottest pathfinder
     accessors. Moving the world-array storage down is a later step, because
     it changes the save layout (W10).

---

## 1. The mechanism today

### 1.1 Shapes in use

| Shape | Examples | Count |
| --- | --- | --- |
| **A. Struct-of-function-pointers table** — `const struct X *x` global, a no-op default table in `kfx_config/src/x.c`, `set_x_callbacks()` | `SimFeedbackCallbacks`, `ConfigReloadCallbacks`, `NetCallbacks`, … | 20 tables / 570 entries |
| **B. Single function-pointer "provider"** | `set_get_gameturn_provider`, `set_emulate_integer_overflow_provider`, `set_power_grant_revoke_callbacks(add, remove)`, `bf_sprfnt_set_font_role_resolver`, `set_config_network_is_active_check` | 5 |
| **C. Pushed configuration values** — upper layer pushes resolved values into lower-layer globals | `bf_sprfnt_set_language_lwrstr`, `bf_sndlib_set_audio_config`, `bf_sound_set_atmos_config`, … | ~5 |

The three shapes solve the same problem in three different ways. Shape B in
particular hides very hot calls (see §3.6).

### 1.2 Inventory (shape A)

| Table | Entries | Implemented in (entries) | Main callers |
| --- | ---: | --- | --- |
| `SimFeedbackCallbacks` | 134 | render 47, frontend 40, game 13, net 10, main.cpp-only 16 | kfx_sim (~660 call sites), kfx_net, kfx_render |
| `ConfigReloadCallbacks` | 97 | **sim 50**, main.cpp-only 30, frontend 8, render 4, game 3 | kfx_config |
| `PathfindingWorldCallbacks` | 58 | sim 38, main.cpp-only 20 | kfx_pathfinding (~390 call sites) |
| `NetCallbacks` | 56 | frontend 28, game 11, script 5, apploop 3 | kfx_net (`packets*.c`, `net_game.c`, `net_resync.cpp`) |
| `RenderOverlayCallbacks` | 49 | frontend 25, net 7, game 4, render 5 | kfx_render, kfx_sim |
| `GameCallbacks` | 46 | frontend 35, main.cpp-only 9 | kfx_game |
| `ScriptHookCallbacks` | 32 | script 32 | kfx_sim, kfx_game |
| `SoundStateCallbacks` | 25 | config 7, sim 5, game 4, main.cpp-only 7 | kfx_platform |
| `DungeonAvailabilityCallbacks` | 21 | **sim 21** | kfx_config only |
| `RendererImGuiCallbacks` | 11 | frontend | kfx_platform |
| `ReceiveCallbacks` | 10 | (enet-internal) | kfx_platform |
| `SpriteLookupCallbacks` | 8 | render | kfx_config, kfx_sim |
| `InputFocusPredicates` | 8 | config/sim/net | kfx_platform |
| Editor / ContentTools / EditorJournal / Matchmaking / VideoScale / MapZip / RendererDraw | 15 | — | — |

"main.cpp-only" means the implementing wrapper in `main.cpp` just reads or
writes a field of some state struct.

### 1.3 The life of one entry

To add `foo` to an existing table you edit six places:

1. the struct in the `kfx_config/include/*.h` header, usually with a
   5–15 line justification comment;
2. a `noop_foo` in the table's `.c` file;
3. the positional `default_*` table in the same file;
4. often a `static` wrapper in [`main.cpp`](../../src/main.cpp);
5. the positional `*_impl` table in `main.cpp`;
6. the "every default is a safe no-op" test in `kfx_config/tests/`.

Call sites then use `sim_feedback->foo(...)` directly, so the table's
existence leaks into every call site.

---

## 2. What is good, and must be kept

- **The ladder itself.** A library can't see a higher library's types, and
  CI enforces that. Nothing below weakens it.
- **Per-library test binaries.** Each `kfx_*_utest` links only its own
  library. It links because the no-op defaults stand in for the layers above.
  Any replacement has to keep that property.
- **Runtime override in tests is actually used.** For example,
  [`map_events_test.cpp`](../../src/kfx_sim/tests/map_events_test.cpp) copies
  `*sim_feedback`, overrides one entry, installs the copy, and restores it
  afterwards. About 100 `set_*_callbacks()` calls in tests rely on this. This
  is the strongest argument for keeping runtime-swappable tables for genuine
  seams, rather than switching to link-time stubs.
- **`ScriptHookCallbacks` is the model citizen.** It has one implementer
  (`kfx_script`), and every entry is a real upward notification
  (`lua_on_*`, `luafunc_*`, `api_event*`). It is the shape every surviving
  table should have.

---

## 3. Problems

### 3.1 Scale and cost

570 entries and ~5 kLOC of plumbing (§0). The trend is upward: recent work
(the editor, content tools, settings schema, gpu-v2 lighting) added entries
rather than removing them. Every justification comment in the headers
describes a *local* reason ("config needs X from above"). None of them
reconsiders whether the calling code belongs in the lower library at all.

### 3.2 Organised by consumer → grab-bags and duplication

Each table answers "what does library L need from above?". Because of that:

- **`SimFeedbackCallbacks` mixes many unrelated concerns.**
  - Messages and sounds; menus.
  - Cursor tagging.
  - Dynamic lights (16 entries).
  - Camera zoom and move (13).
  - Lens rendering (7).
  - Raw mouse/keyboard (8).
  - Net-state getters (9).
  - Level-number getters.
  - Transfer-creature bookkeeping.
  - The whole 3D `engine()` render call.
- **The same function is exposed several times under different names.**
  28 implementations are reachable through more than one entry. For example:
  - `set_gui_visible` is in Sim, Net, and Game.
  - `turn_on_menu` is in Sim, Net, RenderOverlay, and Game (as
    `turn_on_ingame_menu`).
  - `thing_is_invalid` is in ConfigReload, PathfindingWorld, and SoundState.
  - `output_message_far_from_thing` appears **twice inside
    `SimFeedbackCallbacks` itself** (`play_sound_message_far_from_thing` and
    `output_message_far_from_thing`).
  - `clear_messages` is exposed as both `clear_sound_messages` and
    `clear_all_messages`. At least one of those names describes the function
    wrongly.
- **`kfx_net` has its own copies of entries it could already use.** Because
  `kfx_net` ranks above `kfx_sim`, every `NetCallbacks` entry that duplicates
  a `SimFeedbackCallbacks` entry (`report_error_stat`, `show_onscreen_msg`,
  `is_key_pressed`, `toggle_status_menu`, the four `turn_*_menus`,
  `output_message`) is redundant. `kfx_net` could call `sim_feedback->` today.
- **Every table needs hand-assembly in `main.cpp`,** since no single library
  implements all of one.

### 3.3 Most entries are symptoms of misplaced code or state

I classified every entry by *why* it exists. The categories overlap, so the
counts are indicative:

| Root cause | ≈ Entries | Examples | Fix |
| --- | ---: | --- | --- |
| Dead (no non-test caller) | 9 | `GameCallbacks.{clear_all_messages, process_all_messages, display_objectives}`, `RenderOverlay.{set_packet_control, reinit_all_menus}`, `SimFeedback.is_mouse_pressed_lrbutton`, `DungeonAvailability.try_set_backup_heart_idx`, `RendererImGui.{want_capture_mouse, want_capture_keyboard}` | delete (W1) |
| Unnecessary (every caller ranks ≥ implementer) | 5 | `ConfigReload.thing_create_thing{,_adv}` (called from `kfx_sim`'s own [`lvl_filesdk1.c:808`](../../src/kfx_sim/src/lvl_filesdk1.c)), `RenderOverlay.set_winfont` (implemented in kfx_platform), `RenderOverlay.get_status_panel_width` (implemented in kfx_config), `Net.clear_player_lightning_palette` | direct call (W1) |
| Duplicate exposure | ~32 | see §3.2 | regroup (W13) |
| Trivial field get/set in `main.cpp` | 62 | `return thing->model;`, `return kfx_net_state.local_plyr_idx;`, `kfx_sim_state.block_health[idx] = val;`, `return kfx_game_state.play_gameturn;` | move state or type down (W3, W7, W10) |
| Presentation / input code in `kfx_sim` | ~25 | `draw_power_hand`, `draw_creature_view`, roomspace key handling | move up (W4) |
| Gameplay code in `kfx_config` | ~70 | per-player availability, `Thing`-taking config functions, NamedCommand registries | move down / split (W5, W6, W8) |
| Simulation state owned by `kfx_render` (lights) | ~21 | `light_create_light`, `light_get_light_intensity`, … | split light registry (W9) |
| Packet *application* and input→packet code in `kfx_net` | ~35 | `complete_level`, `turn_off_all_menus`, `is_key_pressed` from `packets*.c` | split kfx_net (W11) |
| Frame composition in `kfx_render` | ~25 | `draw_gui`, `message_draw`, `draw_tooltip`, `draw_whole_status_panel`, `gui_draw_all_boxes` | move composition up (W12) |
| **Genuine inversion** (notifications, lifecycle, platform→app) | **~150–200** | `ScriptHookCallbacks`, `RendererImGuiCallbacks`, `ReceiveCallbacks`, net-yield hooks, config-changed notifications | keep, as ports |

The next five subsections give the clearest examples.

#### 3.3.1 Rendering and input inside the simulation

- **The creature-possession view is drawn from `kfx_sim`.**
  [`draw_creature_view()`](../../src/kfx_sim/src/thing_creature.c) (line 4366)
  calls `sim_feedback->engine(player, cam)` and
  `sim_feedback->draw_lens_effect(...)`. Its only caller is kfx_render's
  [`engine_redraw.c:545`](../../src/kfx_render/src/engine_redraw.c).
- **The keeper's hand is drawn from `kfx_sim`.**
  [`draw_power_hand()`](../../src/kfx_sim/src/power_hand.c) (line 550) makes
  about 40 `sim_feedback->GetMouseX/Y()` / `process_keeper_sprite()` /
  `render_overlay->draw_gui_panel_sprite_*` calls. Its only callers are in
  `engine_redraw.c` (lines 621 and 642).
- **Roomspace key handling lives in `kfx_sim`.**
  [`roomspace.c`](../../src/kfx_sim/src/roomspace.c) (lines 503–539,
  1411–1510) reads `KC_NUMPAD*` and the roomspace keys through
  `sim_feedback`. Its `process_*_roomspace_inputs` functions are called only
  from kfx_frontend's `front_input.c`.
- **Other sim reads of input.** `thing_data.c:344` reads `KC_LALT` inside the
  simulation. [`struct LocalState`](../../src/kfx_sim/include/player_data.h)
  (the local player's UI state: roomspace size, minimap position, …) is
  declared in `kfx_sim`.

This matters for more than tidiness. `kfx_sim` is meant to be the
deterministic, network-synced core. Local-only input reads inside it are a
standing desync hazard. Today they're correct only because each call site
happens to be restricted to the local player's view.

#### 3.3.2 Gameplay rules inside `kfx_config`

- **Per-player availability code lives in `config_*.c`.** This covers
  `set_room_available`, `is_power_available`, `is_trap_buildable`,
  `make_all_rooms_researchable`, `set_creature_available`, special-digger
  get/set, and so on. They mutate per-dungeon state, so each one goes through
  `DungeonAvailabilityCallbacks` (21 entries, all implemented in `kfx_sim`,
  all called only from these config files). Their other callers are all in
  kfx_sim or higher (sim, net, game, frontend, script).
- **`Thing`-taking functions live in `kfx_config`.**
  `creature_stats_get_from_thing` (fan-in 171, ~40 sim files),
  `get_creature_model_flags`, `creature_own_name`, `get_job_for_subtile`,
  `crate_thing_to_workshop_item_{class,model}`, and
  `thing_death_flesh_explosion` all live there. That forces seven
  `ConfigReload` field accessors (`get_thing_model`, `get_thing_class_id`, …)
  onto one of the hottest paths in the game.
- **`config_*.c` holds sim registries by pointer.** It references 15
  `NamedCommand` function-slot registries (`get_process_func_commands`,
  `get_creature_instances_func_type`, …) via callbacks, only because the
  *name* tables sit next to the function-pointer arrays in kfx_sim.

#### 3.3.3 Simulation state owned above `kfx_sim`

- **The game turn counter lives in `kfx_game`.** `play_gameturn` is in
  `kfx_game_state`. `get_gameturn()` (fan-in 329, and inside every
  `ERRORLOG`/`WARNLOG`/`SYNCLOG`/`NETLOG`) is therefore a shape-B provider
  set from `main.cpp`, backed by
  [`game_legacy_get_gameturn`](../../src/kfx_game/src/game_legacy.c). There
  is also a second route to the same value through
  `SimFeedbackCallbacks.get_play_gameturn`.
- **Local-player and session state lives in `kfx_net_state`.** That covers
  `local_plyr_idx`, `human_players_count`, `input_lag_turns`, and
  `packet_load_enable`, but kfx_sim reads or writes all of them.
- **Lights are simulation state owned by `kfx_render`.**
  - `lish` / `light_data.c` lives in kfx_render.
  - kfx_sim creates, moves, and deletes lights for things, and *reads light
    intensity back* to drive behaviour (`thing_effects.c:677`,
    `thing_creature.c:1302`, `thing_objects.c:1214`).
  - [`net_resync.cpp:395`](../../src/kfx_net/src/net_resync.cpp) resyncs
    `lish` as part of the world state.
  - So lights are sim state that happens to be rendered.
- **Settings values are owned above `kfx_config` but written by it.**
  `screenshot_format`, `vid_smooth`, the screen vidmode, `global_hand_scale`,
  `base_mouse_sensitivity`, and the speech queue limit are loaded by
  `kfx_config` but live in render/sim/frontend globals. That costs 10
  get/set entries (the getters were added for the settings screen).
- **Input primitives sit above their data.** `GetMouseX/Y` and
  `is_key_pressed` live in kfx_frontend's
  [`kjm_input.c`](../../src/kfx_frontend/src/kjm_input.c) (lines 330 and
  469), but they only read `lbDisplay`, `lbKeyOn`, and `pixel_size`, which
  all live in kfx_platform
  ([`bflib_video.c:88`](../../src/kfx_platform/src/bflib_video.c)).

#### 3.3.4 `kfx_net` does more than networking

`packets.c`, `packets_input.c`, `packets_cheats.c`, and `packets_misc.c`
(~5 kLOC) are responsible for 50 of the 95 `NetCallbacks` calls. They
**apply** packet actions, with game-flow consequences (`complete_level`,
`lose_level`, `resign_level`, `load_game_chunks`, …) and UI consequences
(`turn_off_all_menus`, `toggle_status_menu`, …). They also **translate local
input into packets**:
[`packets_cheats.c:535`](../../src/kfx_net/src/packets_cheats.c) reads
`KC_RALT` to decide a packet parameter.

Separately, kfx_render's
[`local_camera.c`](../../src/kfx_render/src/local_camera.c) (lines 160–242)
calls *back into kfx_net* through `RenderOverlayCallbacks` to process camera
packets.

#### 3.3.5 `kfx_render` composes the whole frame, including the UI

The top of `engine_redraw.c` builds the full frame, so it needs about 25
`RenderOverlayCallbacks` into kfx_frontend: `draw_gui`, `message_draw`,
`draw_tooltip`, `gui_draw_all_boxes`, `draw_whole_status_panel`, parchment
map, `draw_eastegg`, and so on. The renderer ends up driving the UI rather
than the UI layering itself over the rendered view.

### 3.4 Positional initialization

Both the default tables (for example
[`game_callbacks.c:78`](../../src/kfx_config/src/game_callbacks.c)) and every
`*_impl` table in `main.cpp` are positional. The compiler catches a reorder
only when adjacent entries have different types. Several tables have runs of
same-signature entries, such as `NetCallbacks`' four `void(void)`
`turn_*_menus` or `GameCallbacks`' eight cheat-menu toggles. Inserting or
reordering an entry within such a run miswires the table silently.

Both languages already allow a better form: `CMAKE_C_STANDARD 11` has C
designated initializers, and `CMAKE_CXX_STANDARD 20` has C++20 designated
initializers, which must follow declaration order (and that ordering
requirement is itself a free check).

### 3.5 Silent defaults and wiring interleaved with initialization

`setup_game()` wires tables one by one *between* initialization steps:

- `ConfigReloadCallbacks` is wired at `main.cpp:1293`, then
  `load_configuration()` runs at 1296.
- The renderer is re-initialised in between.
- The `get_gameturn` provider is wired at 1403 and `SimFeedbackCallbacks` at
  1494.

Also, `LbBullfrogMain()` runs `process_command_line` and `RendererInit`
before `setup_game()` starts. Any call made through a table before that
table is wired lands in a no-op and quietly returns `0`, `false`, or `NULL`.
I found no bug caused by this today, but nothing detects one either. The
ordering is maintained by hand across roughly 400 lines.

### 3.6 Indirect calls on hot paths

- **Pathfinding field access.** `PathfindingWorldCallbacks` has about 390
  call sites in the A*/wall-hug code. About 20 entries just read or write a
  field (`thing->mappos`, `thing->move_angle_xy`, `mapblk->flags`,
  `slb->kind`, …). None of these can be inlined.
- **`get_gameturn()`.** Every call and every log macro goes through a
  function pointer to a function that reads one field.
- **`creature_stats_get_from_thing()`.** It has fan-in 171 and costs an
  indirect call to `get_thing_model` on every call.

The real cost is unmeasured; I haven't profiled it. It is worth measuring
before and after W6/W10. In any case the source is much harder to read:
`pfw->thing_set_position(thing, &pos)` where `thing->mappos = pos` was
meant.

### 3.7 Tests that test the plumbing

About 840 lines in `kfx_config/tests/*_callbacks_test.cpp`,
`sim_feedback_test.cpp`, and similar files call every no-op default and
check its return value. They catch nothing a compiler wouldn't, and they add
a sixth place to edit for every entry. If defaults become generated (§5.3),
these tests can be generated too, or dropped.

### 3.8 Header/definition split across libraries

These functions are declared in one library's header but defined in
another. That stays invisible to both layering checks because neither one
looks at *where the prototype lives*:

| Declared in | Defined in | Functions |
| --- | --- | --- |
| `kfx_sim/include/packet_data.h` | `kfx_net/src/packets*.c` | `set_packet_control`, `unset_packet_control`, `set_players_packet_control`, `set_players_packet_position`, `is_packet_empty`, `packet_crtr_control_pressed`, `get_players_packet_action`, `set_packet_pause_toggle`, `restore_users_from_packet_save`, … (10) |
| `kfx_platform/include/bflib_guibtns.h` | `kfx_frontend` | `do_button_click_actions`, `do_button_press_actions`, `do_button_release_actions` |
| `kfx_config/include/config_creature.h` | `kfx_sim` | `thing_death_flesh_explosion` |
| `kfx_pathfinding/include/ariadne_wallhug.h` | `kfx_sim` (and also declared in `slab_data.h`) | `slab_good_for_computer_dig_path` |
| `kfx_platform/include/bflib_main.h` | `app_entry` | `LbBullfrogMain` |

There are also duplicate prototypes in two libraries' headers:
`is_door_buildable` in `config_trapdoor.h` and `room_workshop.h`, and
`emulate_integer_overflow` in `config_rules.h` and `game_merge.h`.

### 3.9 Documentation drift

`architecture.md` has fallen behind the code in several places:

- **§5.1 lists 13 tables; there are 20.** Missing: `PathfindingWorld`,
  `VideoScale`, `RendererDraw`, `Editor`, `EditorJournal`, `ContentTools`,
  `Matchmaking`.
- **§5.2's wiring snippet is incomplete.**
- **§7 and §13 rule 2 still describe "three build definitions".** They also
  describe `linux.mk`, which no longer exists. CLAUDE.md is correct: CMake is
  the only build.
- **The per-library file counts in §2 are stale.** For example, kfx_sim has
  89 sources, not 83.

---

## 4. Decision rubric for any cross-layer need

Apply these in order. Only step 6 should produce a new callback entry.

1. **Dead?** Delete it.
2. **Is the caller's function only ever called from the layer that owns the
   callee?** Move the function *up* to that layer. Examples:
   `draw_power_hand`, `draw_creature_view`, roomspace input handling.
3. **Is it data?** Move the field *down* to the lowest library that reads or
   writes it, and delete the accessor. Examples: `play_gameturn`,
   `local_plyr_idx`, settings values, lights.
4. **Does it only need a type's layout, not its behaviour?** Move the *type
   definition* down (plain struct plus trivial inline accessors) and keep the
   behaviour where it is (W10).
5. **Is it "something changed, whoever cares should react"?** Publish an
   event (§5.4). Don't add one entry per reaction.
6. **Is it a genuine request to a higher layer** (a UI prompt, a Lua hook,
   the network-wait yield, the platform asking the app for a path)? Then add
   it to the *provider's* port (§5.2).

The per-entry justification comments in today's headers show that each
entry was added by stopping at step 6 without trying steps 2–4.

---

## 5. Target design for the callbacks that remain

### 5.1 Principle

A port is **one interface per providing library and concern**, declared low
(still in `kfx_config/include/ports/` or `kfx_platform`, as now) and
**implemented and tabled inside the providing library**. That library is
allowed to include the port header because the header sits below it.

### 5.2 Provider-owned ports

```
kfx_config/include/ports/ui_port.h        ← declared low
kfx_frontend/src/ui_port_impl.c           ← table built here, next to the code it calls
main.cpp:  set_ui_port(&kfx_frontend_ui_port);
```

Suggested ports, after the shrinking in §6 has been done:

| Port | Provider | Covers |
| --- | --- | --- |
| `UiPort` | kfx_frontend | menus, message boxes, on-screen messages, tooltips, cheat menus, objectives, panel/tab refresh |
| `AudioFeedbackPort` | kfx_game (`sounds.c`, `gui_soundmsgs`) | sound messages, speech, thing samples, ambient sounds |
| ~~`CameraPort`~~ | — | not needed: the synced camera moves into kfx_sim and local-camera sync becomes a polled counter (§8.1, W19) |
| `GameFlowPort` | kfx_game | complete/lose/resign level, save/load chunks, intralevel transfer data |
| `ScriptPort` | kfx_script | today's `ScriptHookCallbacks` plus the Lua resync pair |
| `SessionLoopPort` | kfx_apploop | network-yield hooks, host-packet timestamp |
| `PlatformHostPort` | app / config | today's `SoundState`/`MapZip`/`VideoScale`/`InputFocus` merged by concern |

Benefits:

- Every duplicate collapses to one entry.
- A consumer takes the ports it needs, whatever library it is in.
- Adding an entry touches the port header and the provider library, not
  `main.cpp`.
- `main.cpp` stops being the only place each table can be assembled.

### 5.3 A single source of truth per port

**Minimum (do this first, W2):** switch every table, both defaults and
implementations, to designated initializers, and add a completeness check:

```c
// kfx_config/src/game_callbacks.c
static const struct GameCallbacks default_game_callbacks = {
    .toggle_main_cheat_menu = noop_toggle_main_cheat_menu,
    ...
};
// debug/test builds: every slot non-NULL (treat the table as an array of fn pointers)
KFX_ASSERT_TABLE_COMPLETE(struct GameCallbacks, game_callbacks);
```

**Fuller (optional, W13):** describe each port once in an X-macro list, and
generate from it:

- the struct;
- the default stubs, with default return values given in the list;
- a `static inline` call wrapper, `ui_turn_on_menu(idx)`, so call sites stop
  spelling `ptr->`;
- the completeness assert;
- the no-op test.

```c
// kfx_config/include/ports/ui_port.def
PORT_VOID(turn_on_menu,        (MenuID idx),      (idx))
PORT_RET (TbBool, timer_enabled, (void), (), false)
PORT_RET (struct GuiBox *, create_gui_box, (int64_t x, int64_t y, struct GuiBoxOption *o), (x, y, o), NULL)
```

The trade-off is that X-macros are less friendly to IDE navigation than
plain declarations. The generated inline wrappers partly offset that,
because they give grep a stable `ui_*` prefix. Variadic entries such as
`message_add_fmt` need a hand-written `va_list` twin. If the team prefers
plain C, the minimum version alone already removes the miswiring risk and
the need for hand-maintained tests.

### 5.4 Events for "something changed" notifications

Two families of entries are really notifications, and several layers care
about each one:

- **Config changed at runtime.** In `config_terrain.c`,
  `config_trapdoor.c`, and `config_rules.c`, the `assign_*` field hooks call
  `update_room_tab_to_config`, `update_trap_tab_to_config`,
  `update_all_door_stats`, `update_all_trap_draws_of_model`,
  `reinitialise_rooms_of_kind`, `recalculate_effeciency_for_rooms_of_kind`,
  `panel_map_update`, and so on. Replace these with one
  `config_changed(ConfigDomain, field_id, index)` event that kfx_sim and
  kfx_frontend each subscribe to.
- **Sim → UI notices.** For example "the local player entered or left
  possession" (today: `hide_tooltip`, `turn_off_all_panel_menus`,
  `set_gui_visible`, `toggle_status_menu` called directly from
  `thing_creature.c`), and "level ended" (`frontstats_initialise`,
  `set_timer_turns`).

The mechanism can stay simple: a fixed-size listener list per event kind,
dispatched synchronously, living in `kfx_config`. Listeners that mutate sim
state (for example room re-initialisation on a config change) must still
run synchronously and in the same order on every machine. So register them
once at startup, in a fixed order, and never from gameplay code.

### 5.5 Loud defaults and wire-first startup

- **Wire all ports first.** Move all `set_*` calls into one
  `wire_ports()` that runs at the top of `LbBullfrogMain()`, before
  `process_command_line`. Nothing in wiring depends on initialization, since
  the tables are `static const`.
- **Keep silent defaults, but only for test binaries.** In the game
  executables, have `wire_ports()` finish with a `ports_verify_wired()`
  check. In `keeperfx_hvlog`, have each default stub log
  `"unwired port call: %s"` once.

### 5.6 A test helper

Wrap the copy-override-restore idiom that tests already use:

```cpp
ScopedPortOverride<SimFeedbackCallbacks> fb(sim_feedback, set_sim_feedback_callbacks);
fb->get_event_button_info = fake_get_event_button_info;   // restored on scope exit
```

---

## 6. Work items → stages

The work items below are the analysis-level list. Each is planned in detail
in a stage document; [`README.md`](README.md) gives the order and
dependencies, and [`function-moves.md`](function-moves.md) the move ledger
that every stage keeps current.

| Work item | What | Stage |
| --- | --- | --- |
| W1 | Delete dead and unnecessary callback entries | [S01](stage-01-callback-hygiene.md) |
| W2 | Designated initializers, completeness check, wire-first, loud defaults, test helper | [S01](stage-01-callback-hygiene.md) |
| W14 | Header/definition splits across libraries | [S01](stage-01-callback-hygiene.md) |
| W15 | `architecture.md` refresh | [S01](stage-01-callback-hygiene.md), then every stage |
| W18 | "Logging" option instead of the heavy-log build | [S02](stage-02-logging-option.md) |
| W20 | Navigation-context globals → `kfx_pathfinding_state` | [S03](stage-03-ownership-quick-moves.md) |
| W7 | Settings values owned by kfx_config | [S03](stage-03-ownership-quick-moves.md) |
| W8 | NamedCommand name tables → kfx_config | [S03](stage-03-ownership-quick-moves.md) |
| W4 (part) | Input primitives → kfx_platform | [S03](stage-03-ownership-quick-moves.md) |
| W16 | `kfx_content` library | [S04](stage-04-content-library.md) |
| W5, W6 | Gameplay code out of kfx_config into kfx_sim | [S05](stage-05-config-gameplay-to-sim.md) |
| W4 | Presentation code out of kfx_sim | [S06](stage-06-presentation-out-of-sim.md) |
| W19 | Synced camera → kfx_sim | [S07](stage-07-camera-to-sim.md) |
| W10 | Header-only `kfx_model` | [S08](stage-08-kfx-model-headers.md) |
| (§9) | Versioned state chunks | [S09](stage-09-versioned-state-chunks.md) |
| W3 | Session state down to kfx_sim | [S10](stage-10-session-state-down.md) |
| W9 | Lighting split | [S11](stage-11-lighting-split.md) |
| W11 | kfx_net split | [S12](stage-12-net-split.md) |
| W12 | Frame composition up | [S13](stage-13-frame-composition.md) |
| W17 | `kfx_ai` spike | [S14](stage-14-ai-library-spike.md) |
| W13 | Provider-owned ports, events, generated tables | [S15](stage-15-ports-and-events.md) |

**Expected trajectory of callback entries.** These are estimates, and
the stages overlap:

| After | Entries |
| --- | ---: |
| Today | 570 |
| S01 | ~555 |
| S03 + S05 | ~460 |
| S06 + S07 + S08 | ~390 |
| S10–S13 | ~280 |
| S15 (dedup/regroup) | ~150–200 |

Rerun [`tools/callback_inventory.py`](tools/callback_inventory.py) after
each stage.

---

## 7. Evaluated alternatives

### 7.1 Splitting into more libraries

**Rule.** A new library pays for itself only when it cuts along an edge
where calls already go one way (or removes callbacks, as W10 does). Every
upward call across a new cut becomes a new callback entry. Each library also
costs a normal/heavy-log object-library pair (until W18 lands), one more test
binary, and one more `LIBRARY_ORDER` rank. CMake globbing makes that cheap,
so in practice the callbacks a cut creates are the real cost.

| Candidate | Verdict | Evidence |
| --- | --- | --- |
| **Data-model headers** (`struct Thing` / `Map` / `SlabMap` layouts) below kfx_pathfinding | **Yes, header-only** (W10, §8.2) | *Removes* ~20 PathfindingWorld (15 layout + 5 via a map-dimension cache) + ~9 ConfigReload entries, including the hottest field accessors, and restores direct field access in Ariadne's loops. |
| **`kfx_content`**: the `cfgc_*` content layer out of kfx_config | **Yes, cheap** (W16) | Includes only `config_*` and platform headers. No `config_*` loader includes a `cfgc_` header. Its only users are kfx_editor (13 files) and ftests. Zero callbacks needed. |
| **`kfx_ai`**: computer-player AI (~10 kLOC) directly above kfx_sim | **Spike first** (W17) | Only 2 non-AI kfx_sim files include AI headers; the rest of kfx_sim makes 19 calls into AI files from 5 files, several of them to general helpers that merely live there (`get_dungeon_money_less_cost`, `try_game_action`). Every other user (frontend, game, net, `main.cpp`) ranks above. It would make "the AI is a client of the simulation" explicit and testable, but AI state is part of `kfx_sim_state`'s raw blob. |
| **Packet processing** out of kfx_net | **Yes, as a move** (W11) | Moving it into kfx_game (already ranked right) gets the benefit without a new rung. A separate library between net and game would also work but adds little. |
| Splitting **kfx_sim by domain** (things / creatures / rooms / map) | **No** | These call each other in every direction (thing ↔ creature ↔ room ↔ map). Any cut would produce hundreds of callback entries, the opposite of this pass's goal. |
| Splitting **kfx_frontend** (in-game panels vs menus) | **Not now** | No callbacks removed. Revisit if the ImGui migration leaves a clean legacy-GUI remnant to isolate. |

### 7.2 Replacing the tables with C++ `std::function` / `std::bind`

**Recommendation: no.**

1. **The callers are C.** 1,683 of the 1,774 callback call sites (95%) are
   in `.c` files, and C cannot call a `std::function`. The options are:
   - convert kfx_sim and kfx_config to C++, which makes every upstream merge
     painful because dkfans/keeperfx is C;
   - wrap each `std::function` in a C shim, which is today's mechanism plus
     a layer.
2. **Nothing needs captured state.** Captured state is what `std::function`
   is for. Every implementation today is a free function with no context
   pointer. `std::bind` has been superseded by lambdas since C++14 anyway.
3. **It makes the costs worse:**
   - type-erased calls, and possible heap allocation, on the pathfinder's
     ~390 hot call sites;
   - tables can no longer be `static const` and constant-initialized, so they
     need dynamic initialization at startup;
   - larger objects.
4. **It fixes none of the problems in §3:**
   - code in the wrong library;
   - tables grouped by consumer;
   - positional wiring;
   - silent defaults.
5. **Tests already get the lambda convenience.** A captureless lambda
   converts to a function pointer (`map_events_test.cpp` already does this).

**Where C++ does fit:** in areas that are already C++ (kfx_editor, the ImGui
frontend, the `IRenderer` seam), an abstract interface class with one
implementation plus a test double is a good fit for a C++-to-C++ port, and
better than a `std::function` per method. Between C and C++, keep plain C
tables and get type safety from the single-source generation in §5.3.

---

## 8. Design questions (investigated)

### 8.1 Where does the camera belong? — **Two cameras; the synced one is sim state**

The code actually has two different cameras, which the current split mixes
up.

**1. The synced camera.**

- `struct Camera`
  ([`camera_data.h`](../../src/kfx_sim/include/camera_data.h)) is already
  defined in kfx_sim. It is stored as `PlayerInfo.cameras[4]` inside
  `kfx_sim_state.players[]`, so it is saved and network-resynced.
- It is driven by packets: `process_camera_controls`,
  `process_camera_view_controls`, and `process_camera_action`
  ([`packets.c`](../../src/kfx_net/src/packets.c), lines 305–530).
- It is advanced every turn by `update_all_players_cameras()`, called from
  `update()`.
- So it is simulation state. **But every function that mutates it lives in
  kfx_render's
  [`engine_camera.c`](../../src/kfx_render/src/engine_camera.c):**
  zoom in/out, `set_camera_zoom`, velocities, `view_move_camera_to_position`,
  `update_player_camera`, `init_player_cameras`,
  `set_player_cameras_position`, first-person tracking.
- That forces 9 `SimFeedbackCallbacks` entries: `player_instances.c` alone
  makes ~20 camera calls (heart zoom, possession enter/leave, …).
- Those functions use only three kfx_render symbols:
  - `setup_engine_window` (from the engine-window helpers, which stay in
    render);
  - `first_person_horizontal_fov` (a setting, which moves to config with W7);
  - `init_local_cameras` (see the local camera below).

**2. The local camera** ([`local_camera.c`](../../src/kfx_render/src/local_camera.c)).

- A per-machine copy of the synced camera, interpolated between turns and
  predicted from the local player's not-yet-sent packet, plus replay
  free-cam.
- It is not synchronized, so it is genuinely view state and belongs in
  kfx_render.
- kfx_sim reaches it about 18 times: `sync_local_camera`,
  `set_local_camera_destination`, `move_local_camera_to_position`, and
  `get_local_camera`. Those calls say one of two things:
  - "the synced camera just jumped, snap the view" (possession, heart zoom,
    teleport); or
  - "orient the lightning effect toward the viewer" (`draw_god_lightning`
    in `power_process.c`). Despite its name it draws nothing: it spawns
    unsynced effect elements, called from `thing_shots.c` inside the sim
    tick.

**Gameplay reads of the synced camera exist, but only on the unsynced
side.**

- `any_player_close_enough_to_see()` gates effect-element creation and
  effect-generator emission
  ([`thing_effects.c`](../../src/kfx_sim/src/thing_effects.c), lines 87,
  805, 1597).
- `lightning_is_close_to_player()` drives the lightning palette flash.
- Both loop over all players' synced cameras. In HEAD (`216be51d5`) they
  skip every computer-controlled player.
- **Spectator hand-off change (assumed landed before pass 2 starts).** The
  spectator hand-off work (`engine_camera.c`, `ftest_spectator_handoff`),
  uncommitted at the time of writing and to be finalised first, adds an
  `is_my_player_number(i)` exception for a computer-controlled *local* seat
  to both functions and to `update_all_players_cameras()`.
- **What the exception causes.** It is true on one machine only, so after it
  lands, that player's synced camera, and the view-shake counters
  `update_player_camera()` decrements
  (`dungeon->camera_deviate_quake`/`_jump`), advance on the spectator's
  machine alone.
- **It is safe today only because everything downstream is visual-only:**
  - effect elements and effects come from the unsynced thing pool
    (`is_non_synchronized_thing_class`);
  - the randomness they use is `UNSYNC_RANDOM`;
  - the effect generator's `generation_delay` is not checksummed;
  - the `camera_deviate_*` fields are read only by `local_camera.c`.
- This invariant is real, now load-bearing, and written down nowhere.
- **More view state in the wrong place.** The two `camera_deviate_*` fields
  live in the synced `struct Dungeon`, but only the view reads them. They
  belong with the local camera; W19 can move them.

**Decision.** Treat the synced camera as simulation state, like lights (W9):

| What | Goes to |
| --- | --- |
| Camera mutators and per-turn update (from `engine_camera.c`) | kfx_sim, e.g. `player_camera.c` |
| Camera-packet handlers (from `packets.c`) | kfx_sim, so both packet application (W11) and render's local prediction call *down* into them |
| Engine-window helpers, projection, `scale_camera_zoom_to_screen` | stay in kfx_render |
| `local_camera.c` | stays in kfx_render |

Replace the ~18 sim→local-camera calls with **a per-player "camera cut"
counter** in `PlayerInfo`. The sim increments it whenever the synced camera
jumps; `update_local_cameras()` compares it each frame and snaps. That
removes the calls without an event system, and it cannot desync because the
sim never reads the counter. `draw_god_lightning` stays in the sim and
switches from the local camera to the synced camera's rotation. The
effects are unsynced, so the only difference is that their orientation is
no longer interpolated.

Then add a comment and an assert-level check to both proximity functions:
"camera-gated sim code may only touch unsynced things and unsynced random
state".

**Result:**

- **`CameraPort` is not needed** (§5.2).
- Removed:
  - 9 camera entries and 4 local-camera entries from `SimFeedbackCallbacks`;
  - 5 camera-packet entries from `RenderOverlayCallbacks`
    (`process_camera_controls`, `process_camera_view_controls`,
    `process_camera_action`, `process_first_person_look`,
    `can_process_creature_input`), once those move to kfx_sim.
- Work item W19.

### 8.2 Should `kfx_model` be header-only? — **Yes for step one; storage is a separate, later decision**

The 58 `PathfindingWorldCallbacks` entries fall into three groups:

| Kind | ≈ | Examples | Header-only model fixes it? |
| --- | ---: | --- | --- |
| **Layout / pure arithmetic** | 15 | `thing_get_position`/`set_position`, `thing_get_move_angle`/`set_move_angle`, `thing_get_owner`, `thing_get_index`, `thing_get_clipbox_size`, `thing_is_flying`, `map_block_flags`, `slabmap_block_kind`, `door_is_locked`, `stl_slab_center_subtile`, `small_around_index_in_direction`, `cross_x/y_boundary_first`, `get_small_around(_length)`, `get_map_size_z` | **yes**, as plain field access and `static inline` helpers |
| **Map-dimension arithmetic** | 5 | `get_map_size_x/y`, `get_subtile_number`, `stl_num_decode_x/y`. These look pure but read `kfx_sim_state.map_subtiles_x/y`. | **yes, with a cache.** The pathfinder copies the dimensions into `kfx_pathfinding_state` in `init_navigation()`, which runs on every level load and load-game. `kfx_model` provides `static inline` helpers that take the dimensions as a parameter, and both kfx_sim and kfx_pathfinding call them, so the encoding can't drift apart. |
| **Storage** (world arrays and their bounds) | 7 | `get_map_block_at(_pos)`, `map_block_is_invalid`, `get_slabmap_block`, `slabmap_block_is_invalid`, `get_slabmap_for_subtile`, `thing_is_invalid`, `creature_get_navigation`, `creature_get_ariadne_state`. (The 6 navigation-context get/set entries go separately, in W20.) | **no.** A header can declare `extern kfx_sim_state`, but the definition stays in kfx_sim. That is an upward *symbol* reference, which `check_layering_symbols.py` correctly rejects. |
| **Behaviour** (real sim logic) | 20 | `subtile_is_unsafe`, `door_will_open_for_thing`, `get_floor_height_under_thing_at`, `creature_cannot_move_directly_to`, `hug_can_move_on`, `creature_steps_into_toxic_terrain`, … | no. These stay as the pathfinder's genuine port. |

**The type closure is small**, so header-only is cheap:

- `struct Thing` (`thing_data.h`) needs only `globals.h` and
  `bflib_basics.h`. Its include closure contains no other kfx_sim header.
- `struct Map` (`map_data.h`) additionally pulls in only `map_utils.h`.
- `struct SlabMap` (`slab_data.h`) pulls in `config_terrain.h` and
  `config_slabsets.h` (both below, so fine) plus `roomspace.h`; check
  whether that one is actually needed for the type.
- `struct CreatureControl` does **not** need to move. It embeds the
  pathfinder's own `Navigation`/`Ariadne` types, and the pathfinder only
  ever needs a pointer to them.
- The big closures (`creature_control.h`: 8 kfx_sim headers;
  `kfx_sim_state.h`: 30) come from behaviour prototypes and the state
  aggregate, not from the types the pathfinder needs.

**Shape.**

- Create `src/kfx_model/include/` as a CMake `INTERFACE` (header-only)
  target, ranked between kfx_config and kfx_pathfinding in
  `LIBRARY_ORDER`.
- It holds `thing_types.h`, `map_types.h`, `slab_types.h`: the struct
  definitions, their enums, and `static inline` field/arithmetic helpers
  only.
- kfx_sim's existing headers include these and keep their function
  prototypes.
- Rule, checked by `check_layering.py` (by rank) and by a trivial grep:
  **no non-inline function prototypes in `kfx_model/include/`**, so it can
  never create a symbol-level upward reference.

**Storage, later.**

- The 7 storage entries can go only if the storage moves too. Concretely,
  kfx_model becomes a real library owning a `KfxWorldState`
  (`map[]`, `slabmap[]`, `things_data[]`, map dimensions) split out of
  `kfx_sim_state`.
- That is another raw-blob layout change (save and resync), so bundle it
  with the versioned-chunks work (§9) instead of doing it on its own.
- The hottest accessors (thing position and move angle, used throughout the
  wall-hug and A* code) are in the *layout* group, so the header-only step
  already captures most of the performance and readability win.

**Quick win, independent of kfx_model:**

- `owner_player_navigating`, `nav_thing_can_travel_over_lava`, and
  `nav_thing_is_flying` are extern globals in kfx_sim's `thing_navigate.h`.
- They are per-call navigation context: `thing_navigate.c` and
  `creature_states.c` set them just before calling into the pathfinder.
- They aren't in any serialized struct, so moving them into
  `kfx_pathfinding_state` is safe. kfx_sim then writes them directly and 6
  get/set entries disappear.
- Work item W20.

**Upstream-merge cost.** Upstream edits `struct Thing` inside
`thing_data.h`. After the split, such commits land on a moved definition.
The merge workflow's "logic moved into a different file" case
([`upstream-merge-workflow.md`](../Architecture/upstream-merge-workflow.md)
§2) covers it, but it is a recurring manual step. Worth it for `Thing`,
which Ariadne needs; not worth widening to other types without a reason.

### 8.3 Link-time seams instead of runtime tables? — **No**

Since everything links into one executable, a lower header could declare
`ui_turn_on_menu()` and kfx_frontend could simply define it. That removes
tables entirely. The cost: every per-library test binary would need a stub
`.c`, and the tests would lose per-test runtime overrides (§2). Keep runtime
tables; X-macro generation gets most of the boilerplate reduction without
that cost.

---

## 9. Other refactoring opportunities (outside the callback mechanism)

Briefer, lower priority. Weigh every file split against upstream-merge cost
([`upstream-merge-workflow.md`](../Architecture/upstream-merge-workflow.md)):
splitting a file upstream edits often multiplies conflict work. Prefer moving
*whole functions* just after a merge, and prefer files upstream rarely touches.

- **Large files.** The biggest:

  | File | Lines | Note |
  | --- | ---: | --- |
  | `engine_render.c` | 9,831 | |
  | `thing_creature.c` | 8,366 | W4 removes its view-drawing code |
  | `lvl_script_commands.c` | 7,165 | natural split: one file per command family, matching `command_desc[]` groups |
  | `creature_states.c` | 5,810 | |
  | `bflib_render_trig.c` | 4,630 | |
  | `frontend.cpp` | 4,502 | |
  | `thing_list.c` | 4,375 | |
  | `player_comptask.c` | 4,228 | |
  | `room_data.c` | 4,015 | |
  | `front_input.c` | 3,838 | |

- **A determinism boundary for `kfx_sim`.** Once W4 and W11 land, add a CI
  grep: no `lbKeyOn`, `lbDisplay.M*`, `LocalState`, or input-port calls
  under `src/kfx_sim/`. That turns a convention into a check.
- **Split `globals.h` (1,709 lines).** It mixes three things: the shared
  typedef vocabulary, the log macros (with the `get_gameturn` provider), and
  miscellaneous enums. Suggested split: `kfx_types.h`, `kfx_log.h`, and the
  rest. That makes the include cost of "just a typedef" visible.
- **Versioned raw-blob chunks.** W3 and W9 both move fields between
  serialized state structs. A version per state chunk, a clean refusal of
  older saves, and a two-phase load (validate, then copy) turn "moving a
  field silently breaks saves" into a routine version bump. Breaking old
  saves is acceptable (decided 2026-09-27), so no migrations. It's a
  prerequisite worth doing once, before W3 (stage S09).

---

## 10. A "Logging" option instead of the heavy-log build (W18)

### 10.1 What differs today

`keeperfx` and `keeperfx_hvlog` are the same source compiled with
`BFDEBUG_LEVEL=0` and `=10` (`kfx_bfdebug_std` / `kfx_bfdebug_hvlog` in
`CMakeLists.txt`). Every `kfx_*` library is compiled twice.

The flag is used in two ways:

- **The `*DBG(level, …)` macros in
  [`globals.h`](../../src/kfx_platform/include/globals.h).** There are about
  2,130 call sites: `SYNCDBG` 1,929, `NAVIDBG` 63, `SCRIPTDBG` 61,
  `ERRORDBG` 56, `WARNDBG` 21, `NETDBG` 4. In the normal build they compile
  to nothing. In the heavy-log build each becomes
  `if (BFDEBUG_LEVEL > level) log(...)`, and the compiler folds that test
  away.
- **58 direct `#if BFDEBUG_LEVEL` blocks.** I read every one:

| Kind | Where | Runtime-able? |
| --- | --- | --- |
| Extra log lines, sanity checks that only log, counters feeding a log line | `thing_data.c` (bad-index checks), `thing_creature.c:3874`, `config_keeperfx.c`, `config_campaigns.c`, `lvl_script_commands.c`, `main_game.c`, `game_session_loop.cpp` (stats dumps at > 9, play-time accounting), `custom_sprites.c`, `bflib_dernc.c`, `bflib_mouse.cpp`, `cursor_tag.c` | yes, trivially |
| Renderer profiler (`RPROF_*`), compiled only in the heavy-log build | `renderer/RendererProfile.{h,cpp}`, `RendererGpu3D.cpp:783` | yes: always compile it, gate each macro on a flag |
| UI differences: on-screen error-stat messages instead of `WARNLOG`; debug-only Shift+F easter-egg toggle; font test screen (`FeSt_FONT_TEST`) | `gui_topmsg.c:117`, `frontend.cpp` | yes |
| **Vulkan validation.** `SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN` is true only in the heavy-log build | [`RendererGpu3D.cpp:45`](../../src/kfx_platform/src/renderer/RendererGpu3D.cpp) | yes, and it **should be a separate option.** Today, asking for heavy logs also switches on GPU validation (slow, and needs the validation layers installed). |
| Build identity: which `.map` file the crash handler reads; "heavylog"/"standard" in the log header | `bflib_crash.c:156`, `bflib_basics.c:485` | simplifies to one binary and one `.map` |

**None of these blocks writes simulation state**, so a runtime level can't
cause a multiplayer desync. The same holds today, where a normal-build and a
heavy-log player can already share a game. Any new debug-only block should
keep that rule, and W18 should document it next to the macros.

### 10.2 Two findings in the current setup

- **~740 debug-log sites are dead in both builds.** The levels used at call
  sites go up to 19:

  | Level | 0–9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 |
  | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
  | Sites | 1,392 | 12 | 31 | 18 | 10 | 3 | 9 | 34 | 126 | 250 | 249 |

  A site prints only when the build level is *above* its own, and the
  heavy-log build is level 10. So no site at level 10 or higher has ever
  printed, including the per-thing and per-turn traces at 17–19 that were
  presumably written for deep debugging. The proposed **Debug max** level
  makes them reachable without a custom build.
- **Heavy logging is currently tied to Vulkan validation** (the table above).

### 10.3 Proposed design: a "Logging" game option with four levels

The level is a normal game option (a settings-screen row, saved in
`keeperfx.cfg`), not a command-line flag:

| Option value | Writes | Equivalent today |
| --- | --- | --- |
| **Off** | nothing but crash reports. No log lines, no on-screen debug message list, no `keeperfx.log` unless the game crashes. | — (new) |
| **Normal** (default) | every always-on macro: `ERRORLOG`, `WARNLOG`, `SYNCLOG`, `JUSTLOG`, `NETLOG`, `*MSG`, `SCRPT*LOG`, `CONF*LOG`, `MULTIPLAYER_LOG` (~3,300 sites) | the standard `keeperfx` build |
| **Debug** | Normal + `*DBG(level < 10, …)` (~1,390 sites) + the debug-only blocks from §10.1: stats dumps, sanity-check warnings, on-screen error stats, renderer profiler, font test screen, Shift+F toggle | `keeperfx_hvlog`, **without** GPU validation |
| **Debug max** | Debug + `*DBG` levels 10–19 (~740 sites that no build prints today, §10.2) | — (new) |

The default level was first proposed as "Errors". It is named **Normal**
(decided 2026-09-27) because today's standard log also contains warnings and
normal progress lines (`SYNCLOG`/`JUSTLOG`: version, CPU, level loads, …).

**Implementation**

- **The state lives in kfx_platform** (`bflib_basics`), the lowest layer, so
  no callbacks are needed:

  ```c
  enum LogLevel { LogLvl_Off, LogLvl_Normal, LogLvl_Debug, LogLvl_DebugMax };
  extern int64_t kfx_log_level;        // enum LogLevel, default LogLvl_Normal
  extern int64_t kfx_debug_threshold;  // derived: Off/Normal -> 0, Debug -> 10, DebugMax -> 20

  // always-on macros: gate once, inside Lb*Log() itself (no call-site change)
  // debug macros: compile-time ceiling + runtime threshold
  #ifndef KFX_DEBUG_CEILING
  #define KFX_DEBUG_CEILING 20
  #endif
  #define SYNCDBG(dblv, format, ...) do { \
      if ((dblv) < KFX_DEBUG_CEILING && KFX_UNLIKELY(kfx_debug_threshold > (dblv))) \
          LbSyncLog("%s: " format "\n", __func__, ##__VA_ARGS__); } while (0)
  ```

  - Off and Normal are enforced in one place: `LbLog()` returns early when
    `kfx_log_level == LogLvl_Off`. `TbLog` already has a `Suspended` flag
    doing exactly that, so none of the ~3,300 always-on call sites change.
  - Debug and Debug max are the threshold. `(dblv) < KFX_DEBUG_CEILING` is a
    compile-time constant, so a build can still drop levels ≥ 10 entirely if
    the benchmark in §10.4 demands it. In that case Debug max would only
    exist in developer builds.
  - The `do { } while (0)` form also fixes a latent macro hazard: today's
    `{ … }` bodies break `if (x) SYNCDBG(…); else …`.

- **The settings row** follows the schema's existing enum-row shape
  (compare `SCREENSHOT` in `config_settingschema.c`):

  ```c
  {
      .cfg_key = "LOG_LEVEL", .type = SOptT_Enum, .category = SCat_Game, .apply_class = SApply_Live,
      .label_literal = "Logging", .help_literal = "How much the game writes to keeperfx.log. ...",
      .enum_table = log_level_desc,           // OFF / NORMAL / DEBUG / DEBUGMAX
      .get_enum = &get_log_level, .set_enum = &set_log_level,
  },
  {
      .cfg_key = "GPU_DEBUG", .type = SOptT_Bool, .category = SCat_Graphics, .apply_class = SApply_NeedsRestart,
      .frontend_only = true,
      .label_literal = "GPU validation", .help_literal = "Vulkan validation layers (slow; developers only). ...",
      .get_bool = &get_gpu_debug, .set_bool = &set_gpu_debug,
  },
  ```

  - `LOG_LEVEL` is **live**: `set_log_level()` just updates the two
    variables. The only startup-bound piece is the font test screen, which
    is simply unavailable until the next start if Debug is switched on
    mid-session.
  - GPU validation becomes its own restart-only option (read when the
    renderer device is created), no longer tied to logging.

- **Startup ordering.** The log file is set up (`LbErrorLogSetup`,
  `main.cpp:2282`) *before* `keeperfx.cfg` is read, and a few lines (CPU,
  OS, Wine) are written in between. Buffer lines in memory until
  `load_configuration()` has set the level. Then either discard them (Off)
  or write them out (any other level). No second parse of `keeperfx.cfg` is
  needed. If configuration loading fails, write the buffer regardless, since
  that is exactly when it's needed.

- **What "Off" still writes (decided 2026-09-27): crash reports only.**
  - The crash parachute (`bflib_crash.c` signal and exception handlers, the
    stack trace) bypasses the level and opens the log lazily on a crash.
  - `error_dialog_fatal` writes nothing when Off. Its message box drops
    "See keeperfx.log for details" and says that logging is off.
  - `FTest:` lines need no exception, because the ftest harness sets its
    own level in `FUNCTESTING` builds.

  - The HTTP API's `get_log_tail` (`api.c:1504`, used by the LLM/agent
    bridge) reads `keeperfx.log`. When Off, it should return an empty tail
    *plus* the current level, so an agent can see why.
  - When buffering is on (below), `get_log_tail` must flush first.

- **Debug max needs buffered writes.** `LbLog()` calls `fflush()` after
  every line (`bflib_basics.c`, end of `LbLog`). At levels 17–19 there are
  per-thing, per-turn traces, so that would turn into disk-bound stalls and
  a very large file.
  - For Debug and Debug max, switch to a buffered stream.
  - Flush on: every `ERRORLOG`/`WARNLOG`, once per frame, on level change,
    in the crash handler, and at exit.
  - Consider a size cap or rotation for Debug max.

- **The 58 `#if BFDEBUG_LEVEL` blocks** become
  `if (kfx_debug_threshold > N)`, or plain code where the block only
  declared a counter for a later log line.

- **Automation without editing `keeperfx.cfg`.**
  - CI and ftests: the ftest harness sets the level programmatically in
    `FUNCTESTING` builds.
  - Transition: a `keeperfx_hvlog(.exe)` copy selects Debug for that session
    when its own file name contains `hvlog`, without writing it to
    `keeperfx.cfg`. That keeps existing launchers working (§10.4).

### 10.4 Costs and the gate before merging

- **Size.** The heavy-log code segment is **~114 KB (0.9%)** larger than the
  normal one (measured with `size` on the current `out/linux` binaries). A
  runtime-level binary with ceiling 10 should land near that. Ceiling 20
  adds the ~740 extra sites' strings.
- **Speed.** Many level 5–9 sites sit inside per-thing and per-turn loops.
  The branch is cheap, but it's unmeasured. **Gate:** time the same replay
  (`-packetload`, fixed seed, `-frameskip`) with the current `keeperfx` and
  with the new binary at **Normal** (the default). Accept a sim-turn time within
  noise (~1%); if it isn't, lower the ceiling or the level of the hottest
  sites. Also run the ftests at Normal and at Debug, and measure Debug max
  once, with buffered writes, to size the stall and log-growth problem.
- **Compatibility.** Several things know the name `keeperfx_hvlog`:
  - release packaging (`Packaging.cmake` installs `keeperfx_hvlog` and its
    `.map`);
  - the three GitHub workflows;
  - `build-cmake.sh` / `build-package.sh`;
  - README, CLAUDE.md, architecture.md §12.3;
  - probably the external KeeperFX launcher's "heavy log" option (not in
    this repo, so check before removing the file name).

  Transition option: for one or two releases, ship `keeperfx_hvlog(.exe)`
  as a copy of the same binary that runs at **Debug** for that session when
  its own file name contains `hvlog`. Existing shortcuts and launchers keep
  working, and dropping it later is a packaging change only.
- **Upstream merges.** Upstream still uses `BFDEBUG_LEVEL`. Keep the macro
  names and `(level, format, …)` signatures unchanged so merged code
  compiles as-is. Merged `#if (BFDEBUG_LEVEL > N)` blocks keep compiling if
  `BFDEBUG_LEVEL` stays defined as the ceiling. Then convert them to runtime
  checks during the merge's review step.

### 10.5 What it buys

- **Faster builds.** Every `kfx_*` library is compiled once instead of twice:
  roughly half the library compile time, locally and in CI.
- **Simpler CMake.** `kfx_bfdebug_std`/`_hvlog`, the `*_hvlog` object
  libraries, `KFX_STATIC_LIBS_HVLOG`, and the second executable and link
  step all go away (architecture.md §7.1 / §12.3).
- **One binary to test and ship.** Ftests and bug reports run the same
  executable players use.
- **A simpler support workflow.** "Options → Logging → Debug" instead of
  "run the other exe". It takes effect immediately, so a player can switch it
  on just before reproducing a bug. Players who want no log file at all get
  **Off**.
- **Separate switches.** GPU validation is no longer a side effect of wanting
  more logs.
