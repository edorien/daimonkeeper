# S10 — Session state down to its readers

**Status:** done 2026-09-28 · **Work items:** W3, plus the view-shake fields found
in S07 · **Depends on:** **S09** (this stage changes save and resync
layouts) · **Risk:** medium · **Layout change:** **yes** (`KfxSimState`,
`KfxGameState`, `KfxNetState`, `struct Dungeon`) · **Estimate:** 1–1.5
weeks · **Callback entries removed:** ~13 plus the `get_gameturn` provider

## Goal

A handful of session values (the game turn, the local player, human-player
count, input lag, replay flags, level numbers) are owned by a library
*above* their main readers. That costs a shape-B provider on one of the
hottest calls in the game (`get_gameturn()`, fan-in 329, inside every log
macro) and 13 `SimFeedbackCallbacks` getters and setters. Put each value
where its readers can reach it directly.

## Field-by-field plan

| Value | Owner today | Readers/writers below the owner | Proposed |
| --- | --- | --- | --- |
| `play_gameturn` | `kfx_game_state` | all of kfx_sim (via `get_gameturn()`), platform logging | **move to `kfx_sim_state`**. `get_gameturn()` becomes a `static inline` in a kfx_sim header. |
| (log prefix turn) | provider → kfx_game | `ERRORLOG`/`WARNLOG`/`SYNCLOG`/`NETLOG` in kfx_platform | **push, don't pull.** kfx_platform owns `lb_log_gameturn`, which the log macros read. The sim sets it once per tick where `play_gameturn++` happens. `set_get_gameturn_provider()` is deleted. |
| `local_plyr_idx` | `kfx_net_state` | `player_computer.c` (sim), which chooses the "player assist" AI model for that seat | move to `kfx_sim_state`, renamed `level_human_player`. Despite the name it is **not** per-machine: `main_game.c` sets it from `campaign.human_player` identically on every machine, so it is level/session data, and serialized correctly. It is distinct from the per-machine `my_player_number` global (`player_data.h`); keep them separate. |
| `human_players_count` | `kfx_net_state` | `player_data.c` writes on player removal; `player_utils.c` increments | move to `kfx_sim_state` (sim is the writer) |
| `input_lag_turns` | `kfx_net_state` | sim reads | keep in kfx_net; the sim reads it once per level start, so **pass it as a parameter** to the sim function that needs it |
| `packet_load_enable` | `kfx_net_state` | sim and render read | move to `kfx_sim_state` as `replay_active` (it is session mode, like `game_kind`) |
| replay-header values: isometric/frontview zoom levels, per-player exists/computer flags (`kfx_net_state.packet_save_head`) | kfx_net | `player_utils.c`'s `init_players()` | **pass as parameters.** kfx_game's level-start code (the caller) reads the header and hands the values to `init_players()`. No state move. |
| `loaded_level_number` | `kfx_sim_state` (already) | kfx_config (`config.c` `is_level_in_current_campaign`) | kfx_config is *below* sim, so the owner is fine. The config function takes the level number as a **parameter** from its (higher) callers. |
| selected / current level number | kfx_game / kfx_frontend helpers | `config.c` (per-level `.cfg` paths), `config_crtrmodel.c` | the same: pass `LevelNumber` into the per-level config loaders from their callers |
| `Dungeon.camera_deviate_quake`, `camera_deviate_jump` | `struct Dungeon` (synced) | written by sim (8 sites), read only by `local_camera.c` | move to a kfx_render per-player view-shake array. The sim's writes become "request shake" through S07's view-signal struct (`KfxSimViewSignals`, not serialized). |

## Callback entries this frees (~13, plus the provider)

`SimFeedbackCallbacks.get_play_gameturn`, `get_local_plyr_idx`,
`set_human_players_count`, `increment_human_players_count`,
`get_input_lag_turns`, `get_packet_load_enable`,
`get_isometric_view_zoom_level`, `get_frontview_zoom_level`,
`get_player_exists_flag`, `get_player_comp_flag`,
`get_loaded_level_number`, `get_selected_level_number`,
`get_level_number`, and `set_get_gameturn_provider`.

## Steps

1. Move `play_gameturn`, add `lb_log_gameturn` and the per-tick push, and
   delete the provider.
2. Move the kfx_net fields (`local_plyr_idx`, `human_players_count`,
   `packet_load_enable`).
3. Parameter-pass `input_lag_turns`, the replay-header values and the level
   numbers.
4. Move the view-shake fields and convert the sim writes to view signals.
5. Bump `KFX_SIM_STATE_VER`, `KFX_GAME_STATE_VER`, `KFX_NET_STATE_VER`
   and `struct Dungeon`'s owner chunk. No migration: saves from before this
   stage are refused (decision 2026-09-27). Add the release-note line to
   the README.
6. Delete the entries and wrappers.

## Verification

- Catch2, including S09's refusal test: a pre-stage save is refused
  cleanly and live state is untouched.
- Ftests; save and load; `-packetload` start and continue replays; fake
  multiplayer, including a forced resync.
- **Log prefixes:** the turn numbers in `keeperfx.log` match before and
  after, on a fixed replay.
- Inventory: −13, and the provider is gone.

## Risks

- **`lb_log_gameturn` staleness:** logs written *between* the increment and
  the push. Push in the same statement block as the increment, and before
  anything else in `update()`.
- **`local_plyr_idx`:** it is the level's designated human seat, not the
  local machine's player. Renaming it (`level_human_player`) avoids the
  confusion that almost led this plan to merge it with `my_player_number`.
  Don't merge them.

## As built (2026-09-28)

Code commit `12b820686`.

- **The game turn.** `play_gameturn` moved to `kfx_sim_state`.
  - The plan made `get_gameturn()` a `static inline` in a kfx_sim header,
    and pushed a copy into kfx_platform every tick for the log macros. But
    kfx_platform, kfx_config and kfx_pathfinding call `get_gameturn()` too,
    and a pushed copy goes stale whenever a save load or resync replaces the
    sim state without passing through `update()`.
  - So instead, `get_gameturn()` is a `static inline` in kfx_platform's
    `globals.h` that dereferences a `const GameTurn *` which main.cpp's
    `wire_ports()` points at `kfx_sim_state.play_gameturn`
    (`set_gameturn_source()`). That's one load, no call, never stale, and
    no call site changed. It reads 0 until wired (unit tests wire their
    own).
  - `set_get_gameturn_provider()` and `game_legacy_get_gameturn()` are
    gone.
- **kfx_net fields** moved to `kfx_sim_state`, which is cleared and
  restored at the same points as before:
  - `local_plyr_idx` became `level_human_player` (still separate from
    `my_player_number`);
  - `human_players_count`;
  - `packet_load_enable` became `replay_active`.
- **Parameters.**
  - `init_player()`, `init_players()` and `init_players_local_game()` take
    the replay header; main_game.c and net_game.c pass
    `&kfx_net_state.packet_save_head`.
  - The input lag is passed into `update_local_dig_tag_prediction()` and
    `_cursor_preview()` from kfx_net's packet processing. The prediction
    module keeps the last value for its render-side query.
- **Level numbers in kfx_config** use read-only pointers too
  (`set_config_level_sources()`, `config_selected_level_number()`,
  `config_loaded_level_number()`, `config_level_number()`), not parameters.
  - `load_config()` has 29 callers, so parameters would have been wide
    churn.
  - Parameters would also have changed behaviour: on a save load, the info
    chunk reloads configs *before* the sim state chunk arrives, so today
    those loaders see the pre-load level numbers. The pointers keep exactly
    that.
  - kfx_sim and kfx_net read the fields directly.
- **Camera shake.** `camera_deviate_quake/_jump` left `struct Dungeon` for
  `kfx_sim_view_signals` (S07's never-saved struct), as per-player arrays
  indexed by `dungeon->owner`. The extra slot takes writes to a cleared
  dungeon, as `bad_dungeon` did.
  - The plan put them in kfx_render with the sim's writes turned into
    requests. But one write adds to the current value, and the decay runs
    in the sim's per-turn camera update. So the sim keeps writing and
    decaying them, and local_camera.c reads them. Behaviour is identical,
    and the values are out of the saved, synced struct.
- **Versions:** `KFX_SIM_STATE_VER`, `KFX_NET_STATE_VER` and
  `KFX_GAME_STATE_VER` are now 2, with new sizes, identical on both targets
  (measured with both toolchains). `save_layout_test.cpp`'s pinned
  `sizeof(struct Dungeon)` went 163512 → 163496. The README decisions
  table has the release note. S09's refusal tests pass unchanged.
- **Callback entries:** the planned 13 `SimFeedbackCallbacks` entries were
  removed, 417 → 404, and the gameturn provider is gone too.
- **Checks:** Linux and Windows builds, both layering checks, all Catch2
  suites (2046 cases). Scratch builds of HEAD and of this stage logged the
  per-turn `compute_replay_integrity()` checksum. Across the 70-test ftest
  sweep, every test passes on both, and two things are byte-identical:
  the traces (11,646 turns) and every log line's turn prefix and function
  name (12,006 lines, the plan's "log prefixes match" check). The sweep
  includes save/load (`ai_seat_identity`), the fake-multiplayer resync and
  the spectator hand-off (which checks the camera-shake decay). Real
  `-packetload` replays and a two-machine game weren't run.

## Upstream-merge notes

- Upstream uses `game.play_gameturn` and `get_gameturn()`. The ledger maps
  `play_gameturn` to `kfx_sim_state`, as pass 1 already remapped it once.
- Upstream code reading `dungeon->camera_deviate_*` must be redirected to
  the view-shake request. The ledger row says so.
