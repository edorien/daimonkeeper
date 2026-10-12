# Upstream merge 2026-10-12 (225d19d44..fb0ab7f4d, 32 commits)

Branch `merge-upstream-2026-10-12`, worktree `../dk-fx-v2-merge`. Every upstream commit is an ancestor of the branch.
Twelve merge commits: seven real merges, five ancestry-only (`-s ours`) records. **Open: the camera refactor and the
commits built on it** -- not ported, see "Flagged" below.

| Upstream | Handling |
|---|---|
| 3bfba53ec creature Inertia/FallAcceleration (#5394) | merged: `INERTIA` (2 values) and `FALLACCELERATION` rows in `config_crtrmodel.c`, `CreatureModelConfig` fields, defaults 32/32/8; `SET_CREATURE_CONFIGURATION` takes them from the table. Config state v3 |
| 9cfa45c80 russian speech (#5397) | merged (lang only; its landview .po files are unused here) |
| 28ebbbb8c abyss message (#5396), 535651745 (#5416) | merged: `Thing.last_turn_pushed`/`pushed_by_player` (sim state v15), a push into the abyss kills as a battle death with the pusher as killer, message only for the owner (via `audio_output_message_far_from_thing`) |
| 0fae1daf8 resume campaign progress after a failed load (#5399) | **not taken**: the fork's Continue goes to the campaign select screen (no land view), nothing to resume. If a failed load leaves the previously selected campaign changed, that is worth a look (not seen) |
| ce07bd884 packetsave fixes (#5400) | merged: replay crash log (`packetfile:{…}` in the crash log; globals in kfx_platform's `bflib_crash.c`, set by `game_replay.c`), `packetsave_max_kb` default 32768, replays `.pck` -> `.fxpkt` (both accepted when pruning), `OS_ABSPATH_SIZE` paths |
| 56725dbe8 / 0a529fcd5 boulder FallAcceleration | cancel each other; #5404's wallhug change (`subtile_has_lava/water/abyss_on_top`) taken |
| abdb9b7dd camera refactor (#5377) | **ancestry only, port pending** -- see Flagged |
| e54ca6030 Lua console refused in MP (#5401) | merged (`cmd_lua`: not in a network game; the cheat check is the table's `needs_cheats`) |
| c9c74b986 compass letters (#5403) | **not taken**: the ImGui compass centres letters already; only the legacy HUD's compass changed (its lang tweaks came) |
| 4fd4150a1 object Inertia (#5402) | merged: `INERTIA` rows, `update_all_objects_of_model()` refreshes sizes/inertia/immobile (kfx_sim `thing_objects.c`). Config state v4 |
| 9b556f5f7, 469f48b35 (#5405), 7198627df (#5408) | merged: multiplayer sync, see below |
| 9e5b7ba29 map fade (#5407), f44ee0aab menu fade (#5406), 224dd27ba skip drawing (#5409), 2e682e99b (#5411), 9ffb227f2 (#5414), fb0ab7f4d (#5427) | **not taken** (ancestry only): the map fade is the legacy `PVT_MapFadeIn/Out` path the fork removed (`gui_parchment.c`), the menu fade is the legacy button screens, the rest is the OpenGL render thread. `PARCHMENT_MAP_FADE` stays a settings key the fork doesn't read (`kfx_parity`: "Settings", not content) |
| b260fcab2 Lua date/time without desync (#5383) | merged: `GetGameTimestamp()`, `kfx_sim_state.timestamp*` (sim state v16), `PckA_SetTimestamp` (appended to the packet enum), replay header v6, initial sync carries it |
| 9f8b18056 possession and the detached view (#5412) | ancestry only: local-camera code of the camera refactor |
| 38ce66bbd Lua sandboxing (#5389) | **not taken**, evaluated: [`lua-sandbox-evaluation-2026-10-12.md`](lua-sandbox-evaluation-2026-10-12.md). Its `get_rid` RANDOM draw from the synchronised seed is taken |
| 40689f2a4 roaming heroes (#5418), 4b4f67df9 chickens (#5420), 6293f9720 hatchery capacity (#5419) | merged (the fork's `RoomStorageKind` garden code got the chicken change) |
| 70c85d902 user preferences out of saves/packets (#5417) | ancestry only, port pending (built on the camera refactor's `UserState`) |
| 674527e1a possessed corpse (#5423) | merged |
| dfc114bbb camera movement at high frameskip (#5421) | ancestry only, port pending (local camera time). Its texture-animation change isn't needed: the fork steps `update_animating_texture_maps()` per game turn |
| 8875ee050 multiplayer spectators (#5413) | ancestry only, port pending |
| 77ae73535 MOVE_CREATURE (#5425) | merged: effect or effect element, default = the script player's puff colour (bounds-checked, `BALL_PUFF_EFFECTS_COUNT`) |

## Multiplayer sync (#5405, #5408), what was and wasn't taken
Taken: a gameplay generation counter in gameplay/repair/resync messages (stale ones ignored), the client's
`netstate.resync_pending` driving `process_packets`, `NetSP.peekmsg`, resync data in a wide ENet window with a retry
and a progress bar from the received fragments, repair-history resend every 50 ms while catching up, bad peer ids
ignored instead of `abort()`, two uninitialised cheat positions, buffered key presses (`lbKeyPressed`) while the game
waits for packets. **Not taken**: rebuilding the navigation mesh after a resync/load (`rebuild_navigation()` exists
and `init_navigation()` uses it, but the fork saves and resyncs the mesh itself -- a rebuilt mesh would be a different
one, `ariadne_saved_state.h`); the 24-bit `Coord` bitfields (the fork's are int64 bitfields).

## Flagged: needs a plan, not a quick port
**The camera refactor (#5377) and what stands on it: #5412, #5417, #5421, #5413.** Upstream removed the per-player
synced `cameras[4]`, `view_mode`, `view_type` and the zoom levels from `PlayerInfo`; the canonical camera is now a
small `DungeonCamera` in `UserState` and each machine's camera state is `local_state.camera`. The fork did the opposite
in refactor pass 2 (S07): the synced cameras moved into kfx_sim's `player_camera.c`, driven by `process_camera_action`
and signalled to kfx_render. A trial merge of #5377 alone conflicted in 40 files (about 100 hunks, 15 files with no
fork counterpart, `PlayerInfo`/`UserState` layout, packet fields `pos_x/pos_y` with a new `PCtr_CameraContext`,
`PCtr_ViewRotate*` removed) and ~100 references in fork-only code (ImGui HUD, editor, LLM bridge, ftests, tests) use
what it removes. #5417 (preferences as `UserState.prefs[]` sent by `PckA_SetUserPref`) and #5413 (observer.c,
net_spectator.c, ~100 files incl. the legacy lobby/menu screens) build directly on it. What they give: spectators in
multiplayer, a camera that is no longer synced game state (a source of desyncs), the replay detached view, camera
movement at high frameskip. Why not now: it re-designs a saved/synced state layout, the packet format and the replay
format at once, and the check that matters (two real machines, a recorded and replayed multiplayer game) can't be run
unattended. Suggested plan: (1) decide whether the fork wants multiplayer spectators (if not, only #5377's de-syncing
of the camera and #5421 are worth having, and #5413/#5417 can stay out); (2) port #5377 as its own change on top of
`player_camera.c` (keep the fork's sim->render signals, move `cameras[]`/`view_type` out of `PlayerInfo`, add
`DungeonCamera` to `UserState`), with `KFX_SIM_STATE_VER`, the packet golden, the replay header and
`net_mp_game_host/_join` + replay ftests as the gates; (3) #5417 and #5421 on it; (4) #5413 last, ImGui lobby first
(the legacy lobby is obsolete). The upstream diffs are in `git diff <sha>~1 <sha>`, all already ancestors.

**Lua sandboxing**: see the evaluation document; needs a two-tier design (trusted engine Lua / untrusted content).

## Other things to know
- `PckA_SetTimestamp` is the last entry of the fork's packet action enum, so its number differs from upstream's.
- The fork's `kfx_net` keeps NetSP in kfx_platform (`bflib_netsp.h`); `NETSP_RESYNC_DATA_TAG` there is asserted equal
  to `NETMSG_RESYNC_DATA`.
- State versions: sim 14 -> 16, config 2 -> 4, packet file header 5 -> 6 (architecture.md §6.2). Older saves and
  replays are refused.

## Coverage
New or flipped tests: `game_commands_input_test` (the host's timestamp packet round trip, non-host ignored),
`bflib_datetm_test` (`decode_posix_time`, `get_local_timezone`), `bflib_basics_utils_test` (`get_rid` RANDOM draws
from the wired seed), `room_data_test` (reposition entries keep duplicates / unique variant), `game_replay_test`
(`.fxpkt`), `wire_layout_test` (`PacketSaveHead` 343), `save_layout_test` (`Thing` 8658). Goldens regenerated with the
cause checked: creature config (struct grew: every hash changed, 140 new cases for the two new keys), base config
(`PACKETSAVE_MAX_SIZE` default). `packet_actions_golden`: verified layout-only with `KFX_GAME_GOLDEN_CUT` (the Thing and timestamp bytes cut reproduce the old hashes), then regenerated. ftest goldens (cheats, console_cmd, lua_api, script_*, spell_effects, computer_*): every hash changed with the state layouts, regenerated from a print run (script_creature_config gains the 8 INERTIA/FALLACCELERATION cases); `ftest_golden.c` now always masks the timestamp. Not diffed against an old binary. Full short ftest list: 114 runs, all pass (net_mp_game pair with the client-side desync included). Not run: Windows cross-build.
Manual QA only: abyss push kills, roaming-hero targets, chickens after rebuilding a hatchery, deleting a possessed
corpse, MOVE_CREATURE effects, `GetGameTimestamp()` in a LAN game and a replay, a real resync (`intentional_desync`),
keys pressed while a network game waits for packets.
