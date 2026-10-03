# S12 — kfx_net split: transport stays, packet application and input translation move

**Status:** done 2026-09-28 · **Work items:** W11 · **Depends on:** S07 (the
camera-packet handlers have already moved to kfx_sim); S01 (the pure packet
helpers are in kfx_sim) · **Risk:** medium–high · **Layout change:** none ·
**Estimate:** 2–3 weeks · **Callback entries removed:** ~35 `NetCallbacks`
+ the remaining key-read entries · **Timing:** right after an upstream merge

## Goal

kfx_net holds three different things:

1. transport, session, lobby, resync and checksums: *networking*;
2. **packet application** (`packets.c`, `packets_input.c`,
   `packets_cheats.c`, part of `packets_misc.c`): turning a player's packet
   into game actions, with game-flow consequences (win, lose, resign,
   load);
3. **input → packet translation** mixed into (2): reading the local
   keyboard to fill in packet parameters.

Items 2 and 3 cause about 50 of the 95 `NetCallbacks` calls. Item 3 is also
the last place where local key state is read during packet application,
alongside `query_thing()` in `thing_data.c`.

## Target

| Part | Home |
| --- | --- |
| Transport, session, lobby, matchmaking, NAT, resync, checksums, input lag, `exchange_packets` | **kfx_net** (unchanged) |
| Packet application: `process_packets`, `process_user_packet`, `process_user_global_packet_action` (409 lines), `process_user_dungeon_control_packet_*`, `process_user_creature_control_packet_*`, `process_user_creature_passenger_packet_action`, `process_user_map_packet_control`, `process_map_packet_clicks`, `process_pause_packet`, `process_dungeon_control_packet_spell_overcharge`, `update_double_click_detection`, and all of `packets_input.c`'s `process_dungeon_control_packet_*`, `process_dungeon_power_hand_state`, `get_thing_under_hand`, `is_mouse_on_map`, `remember_cursor_subtile` | **kfx_game**, new `game_commands.c` / `game_commands_input.c` |
| Cheat application: `packets_process_cheats`, `process_user_global_cheats_packet_action`, `process_players_dungeon_control_cheats_packet_action`, `editor_flood_fill_terrain` | **kfx_game** `game_commands_cheats.c` |
| Replay file I/O (`packets_misc.c`): `open_packet_file_for_load`, `open_new_packet_file_for_save`, `save_packets`, `load_packets_for_turn`, `close_packet_file`, `restore_users_from_packet_save`, `reinit_packets_after_load`, `post_init_packets`, `compute_replay_integrity`, `write_debug_*`, `dump_memory_to_file`, `set_packet_pause_toggle`, `disable_packet_mode` | **kfx_game** `game_replay.c`. It already calls kfx_game's save-chunk functions through `NetCallbacks.load_game_chunks`/`save_packet_chunks`/`fill_game_catalogue_entry`, which become direct calls. |
| Input → packet: every `is_key_pressed`/`process_cheat_heart_health_inputs` read inside the cheat handlers (`packets_cheats.c` lines ~535, 618, 630, 649, 732, 860, 1071), plus `query_thing()`'s `KC_LALT` read (`thing_data.c`) | **kfx_frontend.** Set a bit or parameter in the outgoing packet when the local player issues the command (`additional_packet_values` or `actn_par*`); the application code reads the packet instead of the keyboard |

kfx_game ranks above kfx_net and below kfx_frontend and kfx_apploop.
apploop's `update()` calls `process_packets()` directly, so no new callback
is needed on that path.

## What stays upward after the move

Packet application also drives local UI: `turn_off_all_menus`,
`toggle_status_menu`, message output, `frontend_save_continue_game`, and so
on. kfx_game → kfx_frontend is still upward, so those calls move from
`NetCallbacks` to `GameCallbacks`, which S15 merges into the UI port.

## `NetCallbacks` entries this frees (estimate ~35; confirm with the inventory at the end)

- **Game-flow calls that become kfx_game-internal:** `winning_player_quitting`
  (the `net_game.c` caller stays and keeps it), `reinit_level_after_load`
  (resync keeps it), `complete_level`, `lose_level`, `resign_level`,
  `load_game_chunks`, `fill_game_catalogue_entry`, `save_packet_chunks`,
  `cmd_exec` (chat path; check `net_exchange_gameplay.c`).
- **Key reads, which disappear:** `is_key_pressed`, `clear_key_pressed`
  (already platform after S03), `process_cheat_heart_health_inputs`.
- **UI calls that move from `NetCallbacks` to `GameCallbacks`,** a net
  change of 0 until S15 merges the tables: `turn_off_all_menus`,
  `turn_off_query_menus`, `turn_on_main_panel_menu`,
  `turn_off_all_panel_menus`, `turn_on_menu`, `toggle_status_menu`,
  `set_gui_visible`, `output_message`, `panel_map_update`,
  `update_trap_tab_to_config`, `instant_instance_selected`,
  `frontend_save_continue_game`, `get_default_tag_mode`,
  `is_frontend_at_initial_state`, `get_frontend_alliances`,
  `report_error_stat`, `show_onscreen_msg`, `is_onscreen_msg_visible`.
- **Remaining in `NetCallbacks`** (genuine net → above): the join/session
  UI (`display_attempting_to_join_message`, …), resync payload
  export/import, network-yield hooks, `set_host_packet_received`, chat
  display.

## Steps

1. **Input → packet first**, while everything is still in kfx_net, so each
   change can be verified on its own. Add the packet bits, set them in
   `front_input.c`, and read them in the cheat handlers and `query_thing`.
   A replay recorded *before* this step can't reproduce the key-dependent
   paths. That is fine: they are cheat and debug paths. Note it in the
   commit.
2. Move replay file I/O to `game_replay.c`.
3. Move packet application in groups (global actions, dungeon control,
   creature control, cheats), one commit each.
4. Re-point the UI calls from `net_callbacks->` to `game_callbacks->`, and
   delete the freed `NetCallbacks` entries.
5. Move the Catch2 and ftest harness pieces that include `packets.h` for
   application code.

## Verification

- **Replays:** a fixed single-player replay and a fixed multiplayer replay
  produce identical checksum traces before and after steps 2–5.
- Ftests: fake multiplayer, enet loopback host/join, `ai_bridge_*`
  (external seat uses `packets.c`), `ftest_packet_inject`.
- **Manual:**
  - the cheat menu: every cheat that read a modifier key (steal room with
    Right-Alt, Right-Shift variants, Ctrl drag);
  - query mode with and without Left-Alt;
  - a two-player LAN game: pause, chat command, resign, win.

## Risks

- **`external_seat.c`** (the AI seat, kfx_net) is *not* affected. It only
  builds packets (`get_packet`, `set_packet_action`,
  `set_players_packet_position` from kfx_sim's `packet_data.h`, checked
  2026-09-27) and never calls application code. It is still a candidate to
  move to kfx_game later, because it is an agent-facing *player* rather
  than transport, but that is not needed for this stage.
- **Size:** packet application is about 5 kLOC. Keep commits to pure moves.

## As built (2026-09-28)

Commits `1b4c8f8c7` (step 1), `0b4101066` (steps 2 and 3) and `f967916fa`
(step 4).

- **Step 1, keys into the packet.** The handlers read the *local* keyboard
  while applying *any* player's packet, so in multiplayer one player's
  modifier keys changed another player's cheat or query, and a replay
  couldn't reproduce them. The frontend now packs them into the local
  packet's `control_flags` in `input()` (`set_packet_modifier_keys()`):
  - held keys, every frame: `PCtr_ModRAlt` (steal room/slab effect),
    `PCtr_ModRShift` (pretty walls), `PCtr_ModCtrl` (editor door lock
    toggle), `PCtr_ModLAlt` (`query_thing()` on a key queries the key rather
    than its door; it takes this as a parameter now);
  - keys that are consumed when pressed, only while the player is in that
    cheat: `PCtr_ToggleDetails` (query-all's Right Shift) and a 3-bit heart
    health step (`PCtr_HeartHealthMask`: +1, −1, +100, −100). The step
    replaces `NetCallbacks.process_cheat_heart_health_inputs`, which read
    the keys for the handler. As before, +1 at the heart's maximum sends
    nothing and leaves the key pressed.
  - `control_flags` had bits 23–31 free (this uses 23–30), so `struct Packet` keeps its
    layout. `is_packet_empty()` sees the new bits, as it already saw the
    modifier bits in `additional_packet_values`.
  - Replays recorded before step 1 don't carry the bits, so they can't
    reproduce those cheat and debug paths.
- **Steps 2 and 3 in one commit.** kfx_net's `process_packets()` calls
  `save_packets()`, so moving the replay I/O on its own would have left an
  upward call for a commit. Pure moves:
  - `packets_input.c` → `game_commands_input.c` and `packets_cheats.c` →
    `game_commands_cheats.c` (`git mv`);
  - `packets.c`'s application half → `game_commands.c`;
  - `packets_misc.c`'s replay file I/O → `game_replay.c`, calling
    game_saves.c directly (the three save-chunk `NetCallbacks` entries are
    gone);
  - new headers `game_commands.h` and `game_replay.h`; `packets.h` keeps
    the transport.
- **What stays in kfx_net, unlike the plan:** `process_pause_packet()` and
  `set_packet_pause_toggle()`. kfx_net's own unpause message handling
  (`net_exchange_gameplay.c`) calls `process_pause_packet()`, so moving it
  would need a new callback. `clear_packets()` stays too (packet storage).
  `resync_game_allowed()` became public for `process_packets()`.
- **Step 4.** `complete_level`, `lose_level` and `resign_level` are direct
  calls. 14 UI entries moved from `NetCallbacks` to `GameCallbacks`, and
  `set_gui_visible`/`turn_on_menu` use `GameCallbacks`' existing
  `set_gui_visible`/`turn_on_ingame_menu`. The one remaining UI call from
  kfx_game into `NetCallbacks`, `create_frontend_error_box` in
  main_game.c, predates this stage and is shared with net_game.c.
- **Callback entries.** `NetCallbacks` 52 → 29: 23 entries out, 14 of them
  into `GameCallbacks` (44 → 58). The inventory is 383 → 374. The plan's
  "~35" counted the key reads, which S03 had already removed, and entries
  that stay with genuine kfx_net callers (`winning_player_quitting`,
  `reinit_level_after_load`, `cmd_exec`, `is_frontend_at_initial_state`).
- **Left for later:** the moved code still calls `SimFeedbackCallbacks`
  (`clear_messages_from_player`, `targeted_message_add`,
  `show_onscreen_msg`, `play_sound_message`, all kfx_frontend, plus
  `thing_play_sample`, which kfx_game implements and could call directly);
  that is S15's port merge.
- **Tests:** `is_mouse_on_map`/`remember_cursor_subtile` (8 cases) and
  `restore_users_from_packet_save` (2 cases) moved from kfx_net's Catch2
  binary to kfx_game's (`game_commands_input_test.cpp`,
  `game_replay_test.cpp`). The `GameCallbacks` defaults test covers the 14
  new entries. The ftests include `game_commands.h` where they drive
  packet application.
- **Checks:** Linux and Windows builds, both layering checks, all 12
  Catch2 binaries. Scratch builds of S11's end (`970731c51`) and of
  `f967916fa` logged, per turn, the `compute_replay_integrity()` checksum
  and S11's light-registry hash. Across the 70-test ftest sweep every test
  passes on both, and the checksum traces (11,646 turns), light traces and
  every log line's turn prefix and function name are byte-identical. The
  sweep includes the fake-multiplayer resync, `gui_packet_parity`, the
  external-seat and `ai_seat_*` tests (which drive packets), the cheat and
  editor tests, and save/load. The real ENet loopback pair
  (`scripts/run_ftest_net_enet_loopback.sh`) passes on the new build.
  With their Python clients (`scripts/run_ftest_ai_bridge_*.sh`),
  `ai_bridge_reference` and `ai_bridge_vs_agent` pass on the new build;
  `ai_bridge_smoke` fails at the same step (`bridge_a02_wait_for_client`)
  on S11's build too, so that failure predates this stage. (Resolved after
  S15, in 19cf396be: M10 made the local human's slot claimable on purpose,
  and the smoke client still expected that claim to be refused. See
  stage 15's record.) (Their game-side
  traces can't be compared: the pace is the client's, in real time.) Not run: a `-packetload` replay recorded before and played after,
  and the manual checks (cheats with modifier keys, query with Left Alt, a
  two-player LAN game).

## Upstream-merge notes

- **Highest churn in the plan:** `packets.c`, `packets_input.c` and
  `packets_cheats.c` change in almost every upstream release. Schedule the
  stage immediately after a merge, and expect the next merge to lean on
  `move_ledger.py upstream src/packets.c`.
- **Upstream key reads in cheat handlers** (for example a new modifier on a
  cheat) must be converted to packet bits when merged: add a `PCtr_Mod*`
  bit (bits 23–30 are taken, so a new one gets bit 31, or a field
  elsewhere), set it in front_input.c's
  `set_packet_modifier_keys()`, and read it from the packet.
- **File names.** Upstream `src/packets.c`, `packets_input.c`,
  `packets_cheats.c` and `packets_misc.c` hunks go to `game_commands.c`,
  `game_commands_input.c`, `game_commands_cheats.c` and `game_replay.c`,
  except the exchange/pause/resync functions, which stay in kfx_net's
  `packets.c`/`packets_misc.c`. `move_ledger.py upstream src/packets.c`
  lists each function's home.
