# Upstream merge 2026-10-03 (3ca592163..225d19d44, 14 commits)

Branch `merge-upstream-2026-10-03`, worktree `../dk-fx-v2-merge`. Coverage-first tests landed first
(2a0b7b7ac), a preparatory refactor before #5376 (5aa2d1b46), then one merge commit per batch.
Verified: `check_layering.py --strict` and `check_layering_symbols.py --strict` clean, `move_ledger.py
check` clean, ctest 2241 cases green, `kfx_parity.py` nothing missing (4 config keys were before),
`gen_kfx_compat_reference.py --check` current; Linux release, Windows (mingw) and `KFX_FUNCTESTING`
builds -- see the end of this file. Not run: ftests, real MP/LAN/matchmaking sessions, recording and
playing back real replays.

| Upstream | Handling |
|---|---|
| 5552b2594 zoom/pause replays (#5370) | merged into kfx_game `game_replay.c` (upstream `replay.c` renamed to the fork's state, 3-way merged); chat queued with its cursor and recorded as a long-turn record; paused view actions recorded; playback pauses under a menu; `cmd_exec()` takes the cursor (game port, API uses `console_cmd_default_cursor()`). `replay_playback_is_paused()` in kfx_render `local_camera.c`; `initial_replay_seed` in `kfx_net_local`; new GamePort entries for kfx_net -> replay. `PckA_PlyrMsgEnd` still carries External seats' chat |
| 92d60c42c replays off (#5374) | upstream's `AUTOMATIC_REPLAYS` is now the key's name (id 67, options row writes it), `AUTOSAVE_REPLAYS` still read; default stays OFF |
| c9e234a9c arachnid egg needs -alex (#5378) | merged (cheat check before the spawning temple check) |
| bbb84a5e6 landview text boxes (#5352) | **`-s ours`** (the full-screen land view is retired here). Taken: keeporig's level introductions as `[mapN] DESCRIPTION` in `campgns/keeporig.cfg` (shown by the ImGui campaign screen), and `SHOW_DESCRIPTION`/`DESCRIPTION_GEO`/`INTRO_DESC_ID` recognised and ignored |
| c5cc35d3b replays handle resyncs (#5376) | merged; see below |
| 28ddadc82 effects next to elevations (#5381) | merged |
| 64eb3fd85 lobbies listed while playing (#5373) | network side merged (metadata, join refusals, advertising thread, sorted list, async port mapping); `net_matchmaking.c` taken whole with the fork's rules re-applied (off by default, KeeperFX server refused, slug versions); lobbies advertise `NET_SESSION_VERSION`; KeeperFX stable (1.4.0.5136) not accepted at login; 16 strings appended at guitext:1204-1219 with translations; legacy lobby screens not taken |
| cad05fc9e lobby display fix (#5384) | legacy lobby screen + GL only: nothing taken |
| 9e616602d replay map checksum (#5385) | merged; header version 5 |
| a2d1b56cc ATTACK_ROOMS on disconnected dungeons (#5388) | merged |
| b93906834 hide IPs, move logging (#5386) | logging stays in `bflib_basics.c`; address sanitising ported (own parser), on every log line |
| 2c85bce79 dialog transparency (#5390) | **`-s ours`**: text box, legacy lobby and GL only |
| 27d0becb3 tileset on a new map (#5393) | **`-s ours`**: GL atlas only; the fork's GPU block cache re-checks block contents every frame |
| 225d19d44 landview tooltips (#5395) | merged (tooltip palette from the level's own land view) |

## #5376 in this fork
- `struct ReplayState replay` (kfx_sim `packet_data.h`, upstream's names) holds the packet file state and
  replay mode, in no state blob: before, `kfx_net_state` (resynced and saved) and `kfx_sim_state.replay_active`
  held them, so a resync or a load could overwrite a recording or a playback. Net state version 3.
- `system_flags` split: `kfx_sim_state.run_after_victory` (synced) and `local_system_flags` (kfx_sim global).
  `network_is_active()` is false in a replay; what depends on the game being multiplayer goes by
  `game_kind` (score, objective timers, finish game, complete_level, mods check via main.cpp).
- `PlayerInfo` gains each player's start settings (zoom distances, cheats_allowed, skip_heart_zoom,
  highlight_mode) from the startup sync or the replay header (per-user `UserStartSettings`, header version 4).
  Sim state version 12.
- Resyncs are recorded and replayed (`apply_recorded_resync()`); `reinit_packets_after_load()` runs after a load only.
- Cheats: `player_cheats_allowed()` -- the recorded permission in a replay, live the existing cheat mode;
  console commands use it. The fork's cheat-packet gate (none in multiplayer) is kept; upstream's
  per-packet `cheats_allowed` checks are not taken.

## Coverage
- Coverage-first, flipped by the merge: `thing_effects_test.cpp` (an effect element no longer flies into a
  wall), `player_utils_test.cpp` (a replayed multiplayer game doubles the score).
- New: replay chat record and playback, header game kind and start settings, map checksums
  (`game_replay_test.cpp`); `player_cheats_allowed` (`cheat_mode_test.cpp`); `apply_user_start_settings`
  (`net_game_drop_test.cpp`); lobby metadata and join refusals (`bflib_netsession_test.cpp`); log address
  sanitising (`log_sanitize_test.cpp`); base config golden (`AUTOMATIC_REPLAYS`); level info golden (keeporig
  descriptions); `PlayerInfo`/`PacketSaveHead` sizes.
- Goldens: `packet_actions_golden` regenerated twice, both layout-only, checked with `KFX_GAME_GOLDEN_CUT`
  (replay_active removed: the old binary with the byte cut prints the new hashes; PlayerInfo fields added:
  the new binary with them cut prints the old hashes).
- Manual QA only: recording and replaying a real game (pause under a menu, zoom, chat commands at the cursor,
  a multiplayer recording with a resync and a host drop), hosting/joining a LAN and an online lobby (listed
  while in game, refusal messages), effects near walls, a hero party with ATTACK_ROOMS reaching rooms but not
  the heart, the land view tooltips, IP-free `daimonkeeper.log` in a network game, ftest goldens (sim state
  and PlayerInfo layout changed: `sim_state_continuity` and the console golden need regenerating in a
  data-bearing tree).

## Flagged
- A `keeperfx.cfg` imported from KeeperFX has `AUTOMATIC_REPLAYS = ON` (upstream's default), so a player
  migrating from KeeperFX gets autosaved replays until they turn it off.
- Old `.pck` files (head versions 2-4) and saves (sim state 10/11, net state 2) are refused.
- The ImGui session screen doesn't show the new lobby state/players/version columns yet (the data is there:
  `TbNetworkSessionNameEntry.phase/players/max_players/version`).

## Builds
Linux release (`./build-cmake-linux.sh`), Windows mingw (`KFX_OS=windows`) and a `KFX_FUNCTESTING=ON` build all
succeed; the functest build needed `ftest_net_fake.c`'s `drop_user` and the ENet loopback ftest's call updated
for the join-refusal reason.
