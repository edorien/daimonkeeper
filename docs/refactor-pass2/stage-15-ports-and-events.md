# S15 — Provider-owned ports and events

**Status:** done 2026-09-28 · **Work items:** W13 · **Depends on:** every
earlier stage that is going to happen (regrouping entries that are about
to be deleted is wasted work) · **Risk:** medium (wide, but mechanical) ·
**Layout change:** none · **Estimate:** 2–3 weeks · **Target:** ~150–200
entries, and `main.cpp` wiring under 300 lines

The design is in [`00-analysis.md` §5](00-analysis.md#5-target-design-for-the-callbacks-that-remain).

## Starting point

Run `tools/callback_inventory.py`. After S01–S13 the expected picture is
about 280 entries. They are still grouped by *consumer*:
`SimFeedbackCallbacks`, `GameCallbacks`, `NetCallbacks`,
`RenderOverlayCallbacks`, `ConfigReloadCallbacks`, … Duplicate exposures
remain (for example `set_gui_visible`, `turn_on_menu`, message output,
error stats).

## Target ports (each implemented and tabled in its provider library)

| Port | Provider | Absorbs |
| --- | --- | --- |
| `UiPort` | kfx_frontend | the UI halves of Sim, Game, Net and RenderOverlay: menus, message boxes, on-screen messages, tooltips, cheat menus, objectives, tab/panel refresh, `create_*_box`, high score, frontend-state save/load/size, net join/session UI |
| `AudioFeedbackPort` | kfx_game (`sounds.c`, `gui_soundmsgs`) | sound messages, speech, thing samples, ambient sounds |
| `GameFlowPort` | kfx_game | level end, intralevel transfer data, bonus-level activation, `process_dungeon_destroy` |
| `ScriptPort` | kfx_script | today's `ScriptHookCallbacks`, plus the Lua resync export/import |
| `SessionLoopPort` | kfx_apploop | network-yield hooks (including S13's redraw), host-packet timestamp |
| `RenderPort` | kfx_render | what remains of sim → render after S06, S07, S11 and S13 (for example `setup_eye_lens`, `PaletteApplyPainToPlayer`, sprite lookups) |
| `AiPort` | kfx_ai | S14, if done |
| `PathfindingWorld` | kfx_sim | its behaviour group (~20) plus storage (7, until the kfx_model storage step) |
| `PlatformHost` | app / config | `SoundState`, `MapZip`, `VideoScale`, `InputFocus`, merged by concern |

## Mechanics

1. **Port headers.** Declare each port in `kfx_config/include/ports/` (or
   `kfx_platform` for the platform ones). A `.def` X-macro list per port
   generates the struct, the no-op defaults (with default return values),
   `static inline` call wrappers (`ui_turn_on_menu(idx)`), the completeness
   check, and the no-op test.
   - The X-macro part is optional. The minimum is plain structs with
     designated initializers, which S01 already delivers.
   - Variadic entries (`message_add_fmt`) get a hand-written `va_list`
     twin.
2. **Provider-owned tables.** Each provider library defines its table:
   `kfx_frontend/src/ui_port_impl.c`: `const struct UiPort
   kfx_frontend_ui_port = { … }`. `main.cpp`'s `wire_ports()` becomes a
   list of `set_ui_port(&kfx_frontend_ui_port);` lines, and the wrappers in
   `main.cpp` disappear. Trivial field accessors should be gone by now; any
   left over move into the provider's impl file.
3. **Call sites.** Use a scripted rename, `sim_feedback->turn_on_menu(x)` →
   `ui_turn_on_menu(x)`, one old table at a time. The old struct stays as a
   thin alias until its last caller is converted, then is deleted.
4. **Events.** A small synchronous dispatcher in kfx_config: a fixed-size
   listener list per event kind, registered once in `wire_ports()` in a
   fixed order.
   - **`config_changed(domain, field_id, index)`** replaces the
     `ConfigReloadCallbacks` "push the reloaded value out" entries
     (`update_room_tab_to_config`, `update_trap_tab_to_config`,
     `update_all_door_stats`, `update_all_trap_draws_of_model`,
     `reinitialise_rooms_of_kind`, `recalculate_effeciency_for_rooms_of_kind`,
     `panel_map_update`, …). kfx_sim and kfx_frontend each subscribe.
   - **`player_view_transition(player, kind)`** (enter or leave map, enter
     or leave possession, heart zoom) replaces the ~30 remaining kfx_sim
     uses of `LocalState` (see S06) and the menu/tooltip/palette UI calls
     from `player_instances.c`. The frontend and render subscribe.
   - **Determinism rule:** a listener that mutates sim state must be
     registered at startup and run identically on every machine. UI-only
     listeners must not mutate sim state.
5. **`LocalState` leaves kfx_sim.** Once step 4 removes its last sim uses,
   move `struct LocalState` to kfx_render, its lowest remaining user.
6. **Tests.** Delete the hand-written "every default is a no-op" tests; the
   generated ones replace them. `ScopedPortOverride<T>` (from S01) works
   unchanged.

## Verification

- Both builds and both layering checks pass (the symbol check matters).
- All Catch2 suites and all ftests.
- A fixed replay checksum trace, identical.
- Inventory: no duplicate exposures, and every port has exactly one
  provider library. `main.cpp` is under ~900 lines in total.

## Risks

- **Breadth:** hundreds of call-site renames. Do them by script, one old
  table per commit, with a compile check.
- **Event listener order,** where two listeners touch sim state (for example
  room re-initialisation and the computer player's rebuild after a config
  change). Registration order is explicit in `wire_ports()`. Keep the same
  order the direct calls had.

## As built (2026-09-28)

Nine code commits: `d19e1973f` (step 1, machinery and ScriptPort),
`e613ebc3d` (2, UiPort), `38051aaa4` (3, six more ports), `1ec46da0b` (4,
PathfindingWorldPort), `8dd3926ac` (5, AiPort and EditorPort), `89c71db77`
(6, kfx_platform ports and main.cpp), `85905e2b1` (7, LocalState),
`da2563e5a` (8, comments and the inventory tool), `18ddb9a5c` (9, a new
ftest).

### Ports

Every callback table is now a port: listed once in a `.def` file, tabled
in its provider library, installed by one line in `wire_ports()`.

| Port | Prefix | Provider | Entries | Absorbs |
| --- | --- | --- | ---: | --- |
| `UiPort` | `ui_` | kfx_frontend | 113 | the UI halves of SimFeedback, Game, Net and RenderOverlay; plus `local_view_transition` (step 7) |
| `ScriptPort` | `script_` | kfx_script | 38 | ScriptHookCallbacks, the Lua resync pair |
| `SimPort` | `simport_` | kfx_sim | 30 | ConfigReloadCallbacks (kfx_config's reach into kfx_sim) |
| `PathfindingWorldPort` | `world_` | kfx_sim | 29 | PathfindingWorldCallbacks |
| `GamePort` | `game_` | kfx_game | 19 | the game-flow entries of SimFeedback, Game, Net and ConfigReload |
| `SoundHostPort` | `soundhost_` | kfx_game | 19 | SoundStateCallbacks without its file paths |
| `RenderPort` | `render_` | kfx_render | 18 | RenderOverlay's render half, SpriteLookupCallbacks, and `local_view_type_settle` (step 7) |
| `AudioFeedbackPort` | `audio_` | kfx_frontend | 12 | sound messages, speech, samples |
| `DisplayHostPort` | `display_` | kfx_frontend | 11 | RendererImGuiCallbacks (entries now `imgui_*`), VideoScaleCallbacks, RendererDrawCallbacks |
| `EditorPort` | `editorport_` | kfx_editor | 10 | EditorCallbacks, ContentToolsCallbacks, EditorJournalCallbacks, and a `frame` entry |
| `InputFocusPort` | `focus_` | kfx_sim | 8 | InputFocusPredicates |
| `AiPort` | `ai_` | kfx_ai | 5 | S14's hand-written AiPort |
| `SessionLoopPort` | `loop_` | kfx_apploop | 5 | network-yield hooks, host-packet timestamp, interpolate time |
| `NetPort` | `netport_` | kfx_net | 4 | packet history, MatchmakingConfigCallbacks |
| `FilePathPort` | `filepath_` | kfx_config | 4 | SoundState's file paths, MapZipCallbacks |

The plan named `GameFlowPort`; it is `GamePort`. `PlatformHost` became four
ports split by concern (FilePath, SoundHost, InputFocus, DisplayHost), each
with one provider: a merged table would have needed a provider above all of
config, sim, game and frontend.

`AudioFeedbackPort` is tabled in kfx_frontend, not kfx_game as planned:
half its entries (`output_message*`, `play_speech_ref`) are kfx_frontend's
`gui_soundmsgs.cpp`, and kfx_frontend can reach kfx_game's half.

### Mechanics

- **`.def` X-macros.** `src/<lib>/include/ports/<stem>.def` lists each
  entry once, in one of five forms: `KFX_PORT_VOID`, `KFX_PORT_RET` (with
  its unwired return value), `KFX_PORT_VOIDX`/`KFX_PORT_RETX` (the unwired
  default runs a statement first, such as zeroing an out-parameter), and
  `KFX_PORT_RETS` (the unwired default is a body that returns a
  non-constant: a pointer to fallback storage, or an argument). From it:
  `ports/<stem>.h` (the struct, `KFX_ASSERT_PORT_TABLE`, the installed
  pointer, `set_<stem>()`, and a `static inline` wrapper per entry,
  `PREFIX##name`), `src/<stem>.c` (the `KFX_UNWIRED` defaults), and one
  `TEST_CASE` per port in `tests/ports_defaults_test.cpp` (kfx_config's and
  kfx_platform's). The hand-written "every default is a no-op" tests are
  gone.
- **Field comments** travel into the `.def` above their entry.
- **Variadic:** `message_add_vfmt` is the port entry; `ui_message_add_fmt`
  is a hand-written variadic wrapper in `ui_port.h`.
  `SoundState.prepare_file_fmtpath` was dropped instead: its three callers
  format the name themselves.
- **Provider tables:** `src/<provider>/src/<stem>_impl.c(pp)` defines the
  table, with the small wrappers that used to live in main.cpp.
- **Conversion** was scripted, one old table (or group) per commit, each
  with a compile check: fields, defaults and main.cpp implementations were
  read from the old table, the old entry removed, and every
  `old_var->entry(` call site rewritten to `PREFIX##entry(`.

### Duplicates and dead entries

- `slabmap_block_invalid` (SimPort and PathfindingWorld): `get_slab_stats()`
  moved from config_terrain.c to kfx_sim's slab_data.c, since every caller
  is kfx_sim or higher. SimPort's `slabmap_block_invalid`/`slabmap_kind` went
  with it. In unit tests `get_slab_stats()` now reads the real slab kind
  instead of always `slab_cfgstats[0]`; seven test comments that described
  that limitation are corrected.
- `thing_is_invalid` (SoundState and PathfindingWorld): SoundState's copy,
  and `play_creature_sound`, only served `SoundManager::playCreatureSound()`,
  which had no callers. All three are deleted.
- `UiPort.turn_off_all_window_menus`: dead after step 7.
- `RenderPort.hide_map_volume_box`, `reset_box_lag_compensation`,
  `tag_cursor_blocks_dig`: their only caller, roomspace_prediction.c, moved
  to kfx_render (step 7).
- `UiPort.message_add_vfmt` has no caller outside `ui_port.h`: it backs the
  variadic wrapper.

### Events

- **`config_changed`: not built.** The "push the reloaded value out" calls
  are made from the per-field assign hooks of kfx_config's `NamedField`
  tables (`assign_update_door_stats`, `assign_refresh_trap_anim`, …, in
  config_trapdoor.c, config_terrain.c, config_rules.c and
  config_crtrmodel.c). Those hooks already map a field to its reaction. A
  generic `config_changed(domain, field_id, index)` would make each
  listener switch on field ids again, add an ordering rule, and remove no
  edge. The calls are named SimPort/UiPort/GamePort entries instead (11
  SimPort, 4 UiPort).
- **`player_view_transition`: built as a port entry, not a dispatcher.**
  kfx_sim's player instances, `init_player()`,
  `prepare_to_controlled_creature_death()` and `set_player_mode()` touched
  `LocalState` and the UI inside `is_my_player()` blocks. Each block moved
  verbatim, same statement order, into kfx_frontend's `local_view.c`, as one
  case of `local_view_transition(player, enum LocalViewTransition)` (9
  kinds, `ports/ui_port.h`). kfx_sim calls `ui_local_view_transition()`.
  There is one subscriber (kfx_frontend, which calls kfx_render itself), so
  a listener list would add registration and ordering with nothing to
  order. `set_player_mode()`'s view-type prediction check became
  `RenderPort.local_view_type_settle()`, in local_camera.c, which owns the
  prediction. `set_map_ui_hidden()` moved to local_view.c with its test (now
  in kfx_frontend_utest, with a toggle-function seam).

### `LocalState` leaves kfx_sim

`struct LocalState` and `local_state` moved to kfx_render's
`local_state.h/.c`. After step 7 kfx_sim has no use of it:

- roomspace_prediction.c/.h (the local dig prediction; its callers are all
  kfx_render or above) moved to kfx_render. `roomspace.c` included its
  header without using it.
- `clear_players()`'s `memset(&local_state, …)` moved to its one caller,
  kfx_game's `clear_game_for_summary()`.
- In `init_player()` the multiplayer `minimap_zoom = 256` workaround now
  runs in the `LVTr_LevelStart` case, a few statements earlier than
  before. Nothing in between reads or writes `minimap_zoom`.

### `main.cpp`

1502 lines at S15's start, 649 now (the plan asked for under ~900).
`wire_ports()` is 15 `set_*_port()` lines in library order, plus the five
value/provider setters. `ports_verify_wired()` checks the 15 tables. The
wrappers moved to their providers' `_impl` files. `process_command_line()`
and `set_default_startup_parameters()` moved to kfx_apploop's
`command_line.cpp`. main.cpp's 179 quoted includes outside `#if` blocks
went to 40. Each include was dropped only if the object still built, and the pruned object's
disassembly is identical to the unpruned one; the same was done for
`command_line.cpp`. The ImGui submit now goes straight to
`FrontendImGuiFrame()`, which calls `editorport_frame()` last, so main.cpp's
`app_imgui_frame` wrapper is gone.

### Inventory

`tools/callback_inventory.py` now reads `.def` files next to their header,
knows `KFX_PORT_RETS`, finds provider tables outside main.cpp, and skips
the generated defaults. It leaves out `ReceiveCallbacks`: kfx_platform's
ServiceProvider takes it per instance, and its only implementation and all
its callers are in kfx_platform, so it is not a cross-layer port.

- 20 tables and 352 entries (without ReceiveCallbacks) at S15's start;
  **15 ports and 325 entries** now.
- Every port has exactly one provider library.
- No implementation is exposed through two entries.
- The plan's target was ~150–200. The gap is almost all in `UiPort`'s 113
  entries. Those are real sim/game/net → UI calls (upstream calls them
  directly), not duplicates or accessors. Shrinking them means moving
  simulation code, not regrouping entries.

### Checks

- Linux, FUNCTESTING and Windows (mingw) builds; both layering checks; all
  13 Catch2 binaries (2062 cases).
- The move ledger (below) checks clean: 680 rows, 0 problems.
- `save_versions_test` uses a per-process save file. Parallel `ctest` runs
  raced on the shared name (a problem since S09, found here).
- **New ftest `local_view_transitions`.** No existing ftest possessed a
  creature or opened the map. This one possesses an imp and leaves, rides
  it as a passenger and leaves, possesses it again and kills it, then
  opens and closes the map on both the live path and the fade path. It logs
  the local presentation state every turn and checks that the map hold
  puts the UI back.
- **Trace sweep.** Scratch builds of S14's end (`077ba4a3f`) and of step 8
  (`da2563e5a`), both carrying the new test, logged each turn's
  `compute_replay_integrity()` checksum, the light-registry hash, and a hash
  of the first frame drawn after each turn. The frame was drawn at the
  turn's own state, with the unsynced seed pinned per turn.
  Across the 70-test sweep plus `local_view_transitions`, every test passes
  on both builds. Identical on both:
  - the checksum and light traces (11,907 turns);
  - the new test's per-turn local-state lines;
  - the log prefixes, apart from the per-draw lines (24,419 lines).

  Frame hashes are identical on every turn both builds drew in 64 tests
  (7,547 frames).
- **The other 7 tests' frames.** In those 7, two runs of the *same* build
  also differed: drawing more than one frame in a turn advances per-frame
  render state. With the trace build limited to one draw per turn, each run
  of one build matches a run of the other on every common turn (2,487
  frames). The remaining run-to-run mismatches all follow a turn the frame
  limiter skipped in that run.
- An S13-style frame hash taken outside `RendererLockFramebuffer()` hashed
  nothing: the framebuffer pointer is NULL there. The trace hashes inside
  the lock, next to the screenshot code.
- **Other checks.** The real ENet loopback pair passes. With their Python
  clients, `ai_bridge_reference` and `ai_bridge_vs_agent` pass;
  `ai_bridge_smoke` fails the same 7 seat checks on S14's build as on
  S15's (S12 recorded it failing since S11 at least). **Resolved after
  S15:** the client, not the game, was wrong. M10 (0955ff802) made the
  local human's own slot claimable on purpose, but
  `scripts/ai_bridge_smoke.py` still expected `claim_seat` on player 0 to
  be refused; the extra seat then took user 1 from the rival. The client
  now claims and releases player 0 first, and the game side checks the
  slot went back to the human rather than the built-in AI.
- **Not run:** the manual checks (a LAN game, a playtest of possession and
  the map with the GUI). A GUI playtest is still worth doing:
  `local_view_transitions` covers them headless.

## Upstream-merge notes

- Upstream calls UI functions directly from sim code (for example
  `turn_off_all_menus()`). After this stage the merge rule becomes "map to
  the `ui_*` wrapper", which is simpler than today's "find which of four
  structs exposes it".
- The ledger records each old table entry → port as a `callback-entry` row
  (387 S15 rows in all). `to` is the port's `.def`; an entry renamed on the
  way is written `path/to/port.def#new_name`, and the note names the
  wrapper to call (`call ui_turn_off_menu()`). `move_ledger.py check` treats
  an earlier row as superseded when a later row picks the symbol up from
  where it left it.
- As built: upstream's direct `foo()` in a lower library becomes the
  wrapper of whichever port lists `foo` — `grep -n '\bfoo,' src/*/include/ports/*.def`.
  A genuinely new upstream call is one `.def` line plus one line in the
  provider's `*_port_impl.c(pp)`.
- Upstream's `if (is_my_player(player)) { … }` presentation blocks in
  `player_instances.c` (palette fades, menus, the map UI hold) now live in
  kfx_frontend's `local_view.c`, one `case` per transition. A merged change
  to such a block goes there, not back into kfx_sim.
- `roomspace_prediction.c` is in kfx_render now; `get_slab_stats()` in
  kfx_sim's `slab_data.c`; `process_command_line()` in kfx_apploop's
  `command_line.cpp`.
