# Upstream merge 2026-09-29 (bef3ea7a6..3ca592163, 11 commits)

Branch `merge-upstream-2026-09-29`, worktree `../dk-fx-v2-merge`. Coverage-first tests landed first
(28c150514), then one merge commit per batch. Verified: `check_layering.py --strict` and
`check_layering_symbols.py --strict` clean, ctest 2083 cases green (the usual kfx_editor cases flake only
under `-j`, pass serially), Linux release build and Windows (mingw) cross build — see the fixups commit.
Not run: ftests, real MP/replay sessions.

| Upstream | Handling |
|---|---|
| 96e492c6d `is_active` refactor (#5362) | merged with ac23b44b3; `PlayerInfo::is_active` -> `unused` (layout unchanged), `is_active_keeper()` everywhere incl. fork-only sites (External seat, editor start, `process_pause_packet`, test fixtures) |
| ac23b44b3 scripted-only victory (#5365) | merged; `check_players_won` gone, WIN_GAME/LOSE_GAME decide every undecided human + placeholders; `resolve_standins` homed in kfx_sim `player_utils.c` (its caller `process_dungeons` is there) instead of kfx_net — no port; `winning_player_quitting` + its game_port entry removed |
| 780581941 GL atlas UVs (#5364) | `-s ours` (GL only) |
| bdd73f153 COMMAND_CHAR removed (#5367) | merged |
| dc3420dcd StandIn -> Placeholder (#5366) | merged; rename applied to the fork's kfx_sim home and tests too |
| 62826be5c Lua AddShotToLevel parent (#5363) | merged (lua_params.c stack pops, `lua_isnoneornil`) |
| 6f9d8cd17 autosaved/compressed replays (#5359) | hand-ported into kfx_game `game_replay.c` (upstream's new `replay.c/.h` not added); `PacketSaveHead.flags`, `PACKET_SAVE_HEAD_VER` 2; REPLAY_MAX_SIZE on fork id 58 (+ PACKETSAVE_MAX_SIZE alias), MAX_REPLAYS on id 66; `FGrp_Replays` in kfx_platform `globals.h` |
| 788c94f97 MP maps 621/623 | merged |
| d19666f9a no interpolation on Lua move (#5369) | merged |
| a7aff5e4f all played campaigns (#5360) | **`-s ours`**: legacy-menu Continue/progress rework, CONT chunk, progress files, lang/.po edits not taken (fork has `save/progress.cfg`; upstream's guitext:1124/1125 are the fork's MnuEnterLand/MnuPlayLevel). Ported 3 fixes: Free Play campaign-flag overrun in `reset_script_timers_and_flags`, ensign-override getter bound, `intralvl` cleared on MP start |
| 3ca592163 fly over lava (#5372) | merged |

## Coverage
- `game_replay_test.cpp`: 300-turn / 2-user save->load round trip and a chat long turn, written on the pre-merge
  (uncompressed) format and passing unchanged on the compressed one; plus compressed-flag and size check.
- `lvl_script_win_lose_test.cpp`: pre-merge "given player only", flipped to "every undecided human, placeholder
  included, never plain computers".
- `net_game_drop_test.cpp` `[victory]`: rewritten for `human_victory_kernel_exists` / `player_defeat_settled` /
  `resolve_placeholders`.
- `main_game_reset_flags_test.cpp`: failed before the ported fix (Free Play wiped `intralvl.next_level` and the
  first ensign override), passes after.
- `thing_navigate_test.cpp`: the "flying must also be natural" lava case flipped.
- `config_campaigns_test.cpp`: `get_campaign_sanitized_id`.
- No automated check (manual QA): autosave naming/eviction under `replays/`, playing back a real autosaved
  game, MP co-op scripted win, a dropped player's placeholder being defeated/freed, Lua AddShotToLevel parent and
  Player.type "Placeholder", Lua-teleported thing not sliding, spell-flying creature crossing lava, campaign then
  MP game (no carried-over creatures).

## Flagged
- Editor playtests go through `post_init_level`, so they are autosaved as replays too (upstream has no editor).
- Functional-test runs will also autosave replays into the data dir's `replays/` (capped by MAX_REPLAYS).
- Old `.pck` files (head version 1) are refused, as upstream.
