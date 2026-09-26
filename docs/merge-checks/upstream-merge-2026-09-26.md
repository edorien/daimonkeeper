# Upstream merge 2026-09-26 (d9550da6b..bef3ea7a6, 18 commits)

Branch `merge-upstream-2026-09-26`, worktree `../dk-fx-v2-merge`. Verified: Linux `keeperfx` + `keeperfx_hvlog` build,
`check_layering.py --strict` and `check_layering_symbols.py --strict` clean, ctest 2005/2005 (two kfx_editor cases are
flaky only under `-j16`, pass alone). Not run: ftests, real MP/replay sessions, Windows build.

| Upstream | Handling |
|---|---|
| 9e1d54254 objective text (GL text renderer) | `-s ours` |
| e28562e72 text clipping in OpenGL | `-s ours` |
| 289e76476 Abyss tooltip | merged (`string_idx_is_empty`) |
| c78df151d MP connectivity | merged; net_holepunch.c taken from upstream (fixes fork's widened packed StunHeader); enet callbacks gained holepunch_handle_packet / holepunch_stun_keepalive, matchmaking_punch takes udp_ipv4 |
| 747181133, 5badbb87f, 1b1893464, 4ace2947b, 43d17531b, 653f2c52e | merged |
| a92bfce05 Lua room capacity | merged; `script_hooks->luafunc_room_capacity_func`; `struct Room` union flattened (save layout change) |
| e45e88720, 7ca23d191 | merged (translucent slab fill drawn as upstream, colour via resolve_indexed_pixel) |
| 211438fa1, 981deeeb4 | merged; active_players_count -> human_players_count; player_has_enemies_to_defeat removed |
| bc51c4ef7 packetload replay | hand-ported (see merge commit); engine_redraw keeps fork's switch |
| 334818833, bef3ea7a6 | merged; packets_misc.c long-turn records ported by hand |

## Coverage
- New Catch2 `[victory]` cases (net_game_drop_test.cpp) replace the player_has_enemies_to_defeat cases; written post-merge.
- local_camera_test / render_overlay_test updated for the new callback signatures.
- No automated check: replay Tab-cycling/freecam, chat-in-replay (.pck long turns), MP connectivity/holepunch, translucent
  slab look, sell-button reset, Lua room capacity funcs, StartMoney. Manual QA.

## Flagged
- `get_local_user()` upstream change not ported (fork derives it from player->user_id); check Tab cycling in a replay.
- `Room` layout change: old saves incompatible.
