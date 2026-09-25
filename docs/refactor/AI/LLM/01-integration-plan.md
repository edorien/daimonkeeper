# Live LLM player-seat — integration plan (index)

Status: **investigation and plan, no code written.** Date: 2026-09-23. Originally one document; split
into per-stage docs (§0) once the investigation grew deep enough that each stage needed its own room —
first two follow-up investigations (§2.7, §2.8) found a load-bearing engine blocker and a full gesture
catalog, then, after checking explicitly for what else might have been missed, two more (§2.9, §2.10)
found that the same blocker's fix doesn't survive a save/load, and that the seat has no defined behaviour
if its controlling process disconnects or if it wins/loses mid-game. None of these four findings were
guessed at in the original single-document plan; each is a concrete fact checked against the source.

## 0. Where this sits

Seven documents now cover this area; read them in this order:

1. [`../00-overview.md`](../00-overview.md) — what the *existing* built-in AI (`Computer2`) is and does.
   This plan assumes that document's findings rather than re-deriving them.
2. [`00-config-json-spec.md`](00-config-json-spec.md) — a JSON surface for an **offline** tool to read and
   edit KeeperFX **configuration files** (`.cfg`) through `kfx_config`, with no game running. Config
   content only; explicitly out of scope there: "Live game control (the in-game TCP JSON API … is a
   separate channel for a running game)" (§1).
3. **This document** — the index and shared landscape reference for the "separate channel" §00's own text
   pointed at: a **live, in-game** player seat that an external LLM agent process drives while a game is
   running. §1 (goal/non-goals), §2 (what exists), §4–§6 (cross-cutting design decisions, relationship to
   the config spec) live here; the concrete build plan is in the five sub-documents below.
4. [`02-transport-and-protocol.md`](02-transport-and-protocol.md) — how an external process talks to a
   running game: extend `api.c` or build a dedicated channel, the response-size constraint that decision
   runs into, and the request/response envelope.
5. [`03-observation-api.md`](03-observation-api.md) — the fog-of-war-correct per-player read API.
6. [`04-seat-and-action-api.md`](04-seat-and-action-api.md) — the `External` seat itself (including a
   confirmed engine blocker in `get_net_user_player_number()`, §2.7) and the packet-gesture layer that
   turns one agent request into the multi-turn click/hold/release sequence a human's drag produces.
7. [`06-lifecycle-and-robustness.md`](06-lifecycle-and-robustness.md) — what happens to the seat across a
   save/load (§2.9: the identity fix from doc 04 doesn't survive one without a second fixup), when its
   controlling bridge disconnects (§2.10), and when it wins or loses mid-game (§2.10).
8. [`05-testing-and-rollout.md`](05-testing-and-rollout.md) — test strategy, phasing across all of the
   above, and the open questions for the owner.

The skirmish design note,
`docs/refactor/skirmish/01-scope-expansion-research-winlose-ai.md` §5, already reached a design
conclusion worth restating up front because everything below builds on it:

> Prefer a **packet-driven** seat (agent acts like a human through the normal input path — recorded/
> replayable, rate-limited to sim turns, validated like human input) over an in-sim `Computer2`-replacement
> seat.

`../00-overview.md` §5, §8 and §9.6 independently confirm this from the source: the built-in AI's
determinism guarantee comes from being part of the synced `kfx_sim_state` blob and never touching packets;
an LLM agent cannot offer that guarantee, so it must not be modelled as a `Computer2` variant. The seat
must be **packet-driven and human-shaped**: a slot without `PlaF_CompCtrl`, whose `struct Packet` for each
turn is filled by a bridge process instead of `front_input.c`. §2.7 below found that this is necessary but
not sufficient — a second, previously-unexamined layer (identity mapping) also needs a real code change.

## 1. Goal and non-goals

**Goal**: define what remains to be built so an external process (an LLM agent, initially in a skirmish
game against the player or other AI seats) can occupy a Dungeon Keeper player seat: read a
fog-of-war-correct view of the game state, decide, and submit actions, once per turn, indistinguishably
(from the sim's perspective) from a human player.

**Non-goals for v1** (carried over from skirmish/01 §5.3/§9, restated for this document):

- Multiplayer/networked games. Skirmish is single-machine (`fe_network_active = 0`); an agent seat adds
  nondeterministic real-world latency, which interacts badly with the existing net turn-sync tolerance
  (§2.5) — out of scope until proven out locally.
- Replacing the built-in `Computer2` AI's code. It stays exactly as documented in `../00-overview.md`;
  the new seat type is additive.
- A specific model/provider integration, prompt design, or agent framework choice. This document is
  about the game-side surface an agent (any agent) would be bridged through, not the agent itself.
- Editing config files at runtime through this channel — that remains `00-config-json-spec.md`'s job,
  even though both surfaces may end up sharing transport code (§6).

## 2. What already exists to build on

Investigated directly against the source across three research passes (the original plan, plus two
follow-up investigations, §2.7–§2.8, run specifically to check what the original plan had missed).

### 2.1 The in-game TCP JSON API (`src/kfx_script/src/api.c`)

Already running, already a live channel into a running game:

- Raw TCP, localhost-only (`INADDR_LOOPBACK`), one client at a time, non-blocking, polled once per game
  turn from `api_update_server()`. Wire format is line/brace-delimited JSON
  (`api_process_multipart_json()` scans for balanced `{...}` objects); every response echoes an optional
  client-supplied `"ack"`. An unsolicited push-event channel exists (`subscribe_event`/`subscribe_var`),
  firing at most once per turn per subscription.
- Command dispatch (`api_process_buffer()`) is gated: metadata commands (`get_kfx_info`, subscribe/
  unsubscribe) always work; gameplay commands (`map_command`, `console_command`, `read_var`/`set_var`,
  `get_level_info`, `get_current_game_info`, `get_all_player_flags`) require
  `kfx_sim_state.game_kind == GKind_LocalGame` — **single-player/skirmish only, never multiplayer**
  (`enum GameKinds`, `src/kfx_sim/include/kfx_sim_state.h:105-111`), and both `map_command` and
  `console_command` refuse to run while `GOF_Paused` is set.
- A convenience already built in: `api_process_buffer()` resolves a `"player"` field (int player number,
  or string name via `player_desc`) once per request, falling back to `my_player_number`
  (`api.c:1083-1094`) — the exact shape a per-seat `get_player_view`/`submit_action` pair would reuse.
- **Read surface today**: script "flag" variables and condition values, level/campaign metadata, current
  turn. No direct map/creature/dungeon-state query command exists in `api.c` itself.
- **Write surface today**: one-line DK-script commands (`map_command`, arbitrarily powerful — most
  authoring verbs), the ~100-entry console-command table (`console_command`), and a few settable script
  flags.
- **The big one — `console_command` → `lua`**: `console_cmd.c:2878` registers a `"lua"` console command
  that (gated only on `kfx_sim_state.easter_eggs_enabled`, i.e. cheat mode) executes arbitrary Lua via
  `script_hooks->execute_lua_code_from_console(args)`. That gives a JSON API client the *entire* Lua API
  surface (§2.2) over the existing TCP channel **today, with no new plumbing**, modulo: cheat-mode gate,
  `GKind_LocalGame`-only, pause-gated, and no return-value channel (Lua `print` output isn't piped back —
  only success/failure is).
- Other reachable console commands of direct interest: `reveal`/`conceal` (fog of war), `comp.procs`/
  `comp.events`/`comp.checks`/`comp.kill`/`comp.me` (inspect/toggle the built-in AI — useful for a
  side-by-side agent-vs-builtin-AI test harness), `pause`/`step`/`turn`, `thing.*`/`room.*`/`slab.*`/
  `player.*` families.

### 2.2 The Lua API surface (`src/kfx_script/src/lua_api*.c`)

The richest existing read+write surface for player/creature/room/map state, already reachable externally
today via §2.1's `console_command`/`lua` bridge:

- **Global functions** (`lua_api.c` `global_methods[]`, ~110 entries): setup/config, flow control,
  spawning, display, map manipulation (`RevealMapLocation`/`RevealMapRect`/`ConcealMapRect`, `PlaceDoor`,
  `PlaceTrap`, `ChangeSlabOwner`/`ChangeSlabType`), computer-AI tuning (the same
  `SetComputerProcess/Checks/Globals/Event` family `../00-overview.md` §6 documents), power/spell actions,
  and query functions (`GetCreatureNear`, `GetCreatureByCriterion`, `GetThingByIdx`, `GetThingsOfClass`,
  `GetThingsOnSubtile`/`OnSlab`, `GetSlab`, `GetRoomsOfPlayerAndType`).
- **Player object** (`lua_api_player.c`): `add_gold`, `set_texture`; field access to `camera`, `heart`,
  `controls` (per-creature-type counts), `available` (creature/room/power/trap/door availability), `type`
  (Human/Computer/Roaming/Neutral/Inactive), `max_creatures`, plus any DK-script variable/flag/condition
  by name (the same underlying bridge `read_var`/`set_var` use).
- **Thing object** (`lua_api_things.c`, ~900 lines): actions (`kill`, `stun`, `transform`, `transfer`,
  `level_up`, `teleport`, `change_owner`, `walk_to`, `set_start_state`); fields (`pos`, `orientation`,
  `health`, `owner`, `state`, `instance`, `party_objective`, `hunger_level`, `gold_held`, `exp_points`,
  `moveto_pos`, `shots`, plus kind-specific sub-fields).
- **Slab / Room / Map / Camera / Lens / Sound objects**: per-tile owner/kind/room/`revealed`; per-room
  type/owner/workers/health/capacity/efficiency; map metadata; camera/lens/sound control.

### 2.3 The packet system — two distinct mechanisms

`../00-overview.md` §5, §8 already establishes that the AI never touches this path; here is what the path
itself looks like for a *human* seat, since an agent seat must reproduce it. Full per-verb detail (this
summary corrects two names the original pass got wrong) is in
[`04-seat-and-action-api.md`](04-seat-and-action-api.md).

- **(a) Discrete one-shot verbs** — `enum TbPacketAction` (`packet_data.h:53-357`, ~190 entries). Mostly
  meta/UI/cheat/editor: pause, camera/view switches, message-box handling, power-box triggers, the whole
  cheat menu, the editor's own action family. Dispatched by `switch(pckt->action)` in
  `src/kfx_net/src/packets.c`.
- **(b) Continuous mouse-tagging verbs** — dig, build room, sell, slap, place trap/door,
  cast-on-subtile/target, drop/pick creature are **not** discrete action packets. They are driven every
  turn by the packet's ambient `pos_x`/`pos_y` and `control_flags` (`PCtr_LBtnClick/Held/Release`,
  `PCtr_MapCoordsValid`, …), interpreted according to the player's persistent `work_state`
  (`enum PlayerStates`, `config_players.h:44-177`). **Correction to the original pass**: the everyday
  dig-marking verb is *not* `PSt_MkDigger` (that's a cheat state — "spawn a digger creature") — it's the
  default `PSt_CtrlDungeon` state with cursor context `CSt_PickAxe`, sharing its drag/roomspace machinery
  with room building. Dispatch is `process_dungeon_control_packet_clicks()` (`packets_input.c:709-930`),
  `switch (player->work_state)`. `work_state` itself is set by the one-shot `PckA_SetPlyrState` action.
  **"Build a room at X,Y" is not one packet** — it needs `work_state` set once, then a click→hold→release
  control-flag sequence over successive turns, exactly mirroring a human drag gesture (multi-turn drags
  are interpolated by `roomspace.c`/`packets_input.c`'s cursor-interpolation).
- `struct Packet` (`packet_data.h:414-427`): `turn`, `checksum`, `input_lag_turns`, `action`,
  `actn_par1..4`, `pos_x/pos_y`, `control_flags`, `additional_packet_values`. One per connected slot,
  `extern struct Packet sim_packets[PACKETS_COUNT]` (`PACKETS_COUNT == 9`).
- **Creation (human)**: `front_input.c` populates the local packet every frame via
  `set_players_packet_action()`, `set_players_packet_control()`/`set_packet_control()`,
  `set_players_packet_position()` — pure C-struct mutation, no abstraction layer.
- **Consumption**: `exchange_packets()` (`packets.c:1528-1574`) stamps/checksums/sends the local packet,
  then `process_packets()` (`packets.c:1579-1618`) dispatches every connected slot **except** one flagged
  `PlaF_CompCtrl` (verified directly: `packets.c:1595-1599`). A slot with `PlaF_CompCtrl` clear, whose
  `sim_packets[N]` a bridge fills before that turn's `exchange_packets()` call, would be indistinguishable
  from a human's *dispatch-wise* — **but §2.7 found that "dispatch-wise" is not the whole story**: the
  packet has to actually be reachable through `NetUserId` indexing first, which today it structurally
  isn't for any slot but one.
- **No existing injection hook for an external process** — but the technique is already proven
  in-process: `src/ftests/ftest.c` and its `FTestActionArgs` mechanism call `set_players_packet_action()`
  directly to synthesise input for a running (headless) game.

### 2.4 Fog of war — a real per-player primitive already exists

`struct Map` has a per-tile `PlayerBitFlags revealed` (`map_data.h:46`), queried via
`subtile_revealed(stl_x, stl_y, plyr_idx)` / `map_block_revealed(mapblk, plyr_idx)`
(`map_data.c:299-330`) — which **correctly** ORs in allied players' bits when
`allies_share_vision` is set, and has a `_directly()` variant that doesn't. Mutated via
`reveal_map_block`/`conceal_map_block`/`reveal_map_subtile`. Used pervasively for real gameplay gating
(room placement, creature pathing/targeting, spell-cast legality, minimap rendering). Traps and doors
carry their **own** separate per-player `revealed` bit (`thing->trap.revealed`, `doortng->door.revealed`),
independent of tile reveal.

**Gap found**: the only existing *scripting* exposure of this, `lua_api_slabs.c`'s `slab_get_field`/
`slab_set_field` for `"revealed"`, is **not player-parameterized** — the getter coerces the whole bitmask
to one boolean ("has *any* player revealed this"), and the setter clobbers the entire bitmask to 0 or 1.
A correct per-player observation API must call `map_block_revealed(mapblk, plyr_idx)` directly (or add a
proper binding); it cannot reuse the existing Lua field as-is. Full plan: `03-observation-api.md`.

### 2.5 Turn/tick timing and the pause mechanism

- Per-turn loop, and this matters for latency design: `update()` (`game_session_loop.cpp:121+`) calls
  `process_packets()` **then** `api_update_server()`, in that order, every turn (`:126-128`). So a
  `submit_action` command the API server reads in one turn's `api_update_server()` call is necessarily
  too late for that same turn's `process_packets()` (already run); it lands in `sim_packets[N]` for the
  *next* turn's `exchange_packets()`/`process_packets()` pair. **This is a clean one-turn latency model
  a protocol can rely on** ("submit now, it takes effect next turn"), not an ambiguity to design around.
- Turn rate: `kfx_sim_state.turns_per_second`, default **20** (50 ms/turn nominal), accumulated by delta
  time rather than hard frame-locked.
- **Pause**: `GOF_Paused`, toggled via `PckA_TogglePause`/`PckA_UpdatePause`. While paused,
  `input_lag_skips_processing()` makes `exchange_packets()` skip packet processing — so "pause while the
  agent thinks, submit one action, unpause" is directly supported by existing state — **except** the JSON
  API's `map_command`/`console_command` deliberately refuse to run while paused (§2.1). That gate would
  need relaxing for an agent-driven bridge, or the bridge must submit before unpausing through a different
  channel than the current JSON API gameplay commands.
- **Existing "tolerate a slow peer" precedent, and why it doesn't reach far enough**: multiplayer already
  has `wait_for_missing_packets()` (`net_exchange_gameplay.c:423-478`), blocking up to
  `FINAL_RESORT_RESYNC_RECOVERY` = **10 seconds** before forcing a resync, plus a dynamic input-lag window
  (`net_input_lag.c`) capped at `MAXIMUM_INPUT_LAG_TURNS` = **12 turns** (600 ms at 20 tps). Both are
  real, working mechanisms for absorbing latency — but both are sized for network jitter (hundreds of ms
  to a 10 s hard ceiling), not LLM inference latency (seconds to tens of seconds). This only matters for
  the out-of-scope networked case (§1); pause-on-think sidesteps it entirely for v1.

### 2.6 Existing precedent for driving a seat programmatically

- **No existing bot/network-client seat** for real gameplay. `PlaF_CompCtrl` seats run the AI code path;
  human seats run local device input only.
- **In-process fake transport for tests**: `src/ftests/ftest_net_fake.{h,c}` stands in for the real ENet
  `NetSP` so a single test process can drive `net_resync.cpp`/`net_exchange_*.c`/`packets*.c` without real
  sockets (`docs/refactor/todo/ftest-fake-multiplayer.md`) — proof the packet-exchange machinery is
  already designed to be driven in-process without a real network.
- **Packet injection precedent**: `ftest.c`'s `set_players_packet_action()` calls — exactly the mechanism
  a real bridge would use, already exercised from in-process test code, but only for discrete one-shot
  actions, never a multi-turn drag gesture (confirmed by §2.8 — see `05-testing-and-rollout.md`).
- **A second, complementary precedent**: `ftest_util_gui_click(bid)` drives a live `GuiButton` via
  `do_button_release_actions()` directly, bypassing the packet layer entirely — useful for menu-level
  actions (workshop/manufacture selection) a pure packet-level agent would otherwise find awkward.

### 2.7 New finding: seat identity is structurally blocked for a second local slot

A follow-up investigation (run specifically to check whether §2.3's "a `PlaF_CompCtrl`-clear slot is
indistinguishable from a human's" claim survives contact with the identity/dispatch layer underneath
`process_packets()`) found that **it does not, in a local skirmish game, without a code change**:

- `process_packets()` loops `NetUserId user < PACKETS_COUNT` and resolves the player it controls via
  `get_net_user_player_number(user)` (`net_game.c:177-186`). For a local (non-networked) game, that
  function is hardcoded: `if (!network_is_active() && !kfx_net_state.packet_load_enable) return (user ==
  SOLO_HUMAN_ID) ? my_player_number : -1;` — it returns `-1` for **every** `NetUserId` other than 0,
  unconditionally, without even consulting the `net_user_player_number[]` table it uses in the networked
  case. `network_is_active()` (`GSF_NetworkActive`) is only ever set on the real network-game entry paths
  (`game_session_loop.cpp:939`, `frontend.cpp:3024`) — never for skirmish.
- Consequence: **a second packet-driven slot's entry in `sim_packets[]` can never be reached by
  `process_packets()`'s dispatch loop in a local skirmish game today**, no matter how correctly its
  `PlayerInfo`/`PlaF_CompCtrl` flags are set up. The blocker is entirely in this one function, not in the
  dispatch loop itself or in any player/camera/UI state (those were checked and found to correctly treat
  a non-`my_player_number` slot like "some other player," which is the desired behaviour).
- Good news found alongside this: **checksums and `NetSP` are non-issues for v1.** Checksum computation
  (`update_turn_checksums()`, `net_checksums.c:263-383`) only ever writes into the *local* packet
  (`get_local_packet()`), and desync verification (`checksums_different()`) only runs when
  `network_is_active()` — never for a local game — so a bridge-fed packet can leave `checksum` at 0 with
  no effect on anything. Local skirmish also never calls `LbNetwork_Init()`
  (`setup_network_service()` early-returns for `FrontendNetSvc_Skirmish`, `net_game.c:140-160`), and
  `exchange_packets()` branches around every `netstate.sp`-touching call for `GKind_LocalGame` — so no
  fake/real `NetSP` is needed either.
- Structural ceiling: `MAX_NET_USERS = 4` (`net_main.h:39`) bounds `get_net_user_player_number()`'s
  argument regardless of `PACKETS_COUNT`/`PLAYERS_COUNT` (both 9) — so at most 4 simultaneous
  packet-addressable seats (human + external, combined) can ever exist per game structurally, even after
  the fix below.

Full plan for the fix and the rest of the seat abstraction: `04-seat-and-action-api.md` §1.

### 2.8 New finding: no existing ground truth for any continuous gameplay gesture

A second follow-up investigation (the full per-verb gesture catalog needed for
`04-seat-and-action-api.md`) confirmed something the original pass only guessed at: **there is no
existing captured packet trace, ftest, or unit test anywhere in the tree that drives build-room,
dig-marking, selling, slapping, trap/door placement, power casting, or hand pick/drop through the real
`PCtr_LBtnClick/Held/Release` + `pos_x/pos_y` ambient path across multiple turns.** The closest things —
`ftest_packet_capture.h` (compares discrete `PckA_SetPlyrState` selections, deliberately excludes
`pos_x`/`pos_y`/raw button bits from comparison) and the editor's `PckA_EditorPlaceTerrainRect`-style
one-shot rectangle actions (a different, simpler protocol built specifically to bypass the ambient-drag
mechanism) — are not ground truth for the classic gesture shape. The catalog in
`04-seat-and-action-api.md` §2 is therefore derived directly from reading
`packets_input.c`/`packets.c`/`roomspace.c`/`front_input.c`, not validated against an existing test; §3 of
that document flags where new ftest coverage should pin these sequences down *before* building a gesture
layer on top of them, so a misreading of the state machine is caught by a test rather than by the agent
silently failing to act.

### 2.9 New finding: the seat-identity fix (§2.7) does not survive a save/load

A third follow-up investigation — run specifically because this project just finished a large skirmish
save/load feature and the owner cares about save/load correctness — checked whether §2.7's planned fix
(populate `net_user_player_number[]` for the External seat) would itself survive a save/load. It does not,
without a second, distinct fix:

- `net_user_player_number[]` is a **file-local static** in `net_game.c`, never written to any save-file
  chunk. Today this is harmless because `get_net_user_player_number()` never reads it for a local game
  (§2.7) — but the moment that function is widened to admit a second local seat, the array becomes
  load-bearing, and nothing currently repopulates it after a load.
- By contrast, `PlayerInfo.user_id` (the field that must agree with the array) **is** saved wholesale, as
  part of `kfx_sim_state` (`game_saves.c:248-263`/`344-357`). So after a load, `player->user_id` is
  correctly restored but `net_user_player_number[player->user_id]` can point at a stale or wrong player —
  the two tables silently disagree.
- `my_player_number` itself follows a third pattern, useful as a contrast: it's restored from
  `kfx_net_state.local_plyr_idx`, itself part of the wholesale-saved `KfxNetState` chunk
  (`game_saves.c:256-263`/`359-372`) — so it *is* effectively save-restored, just indirectly, unlike
  `net_user_player_number[]` which isn't saved by any route.
- **A working precedent for the exact fix already exists**, just on a different load path:
  `restore_users_from_packet_save()` (`src/kfx_net/src/packets_misc.c:155-191`) — used only by the
  packet-replay/demo load path (`startup_saved_packet_game()`), not the ordinary save-game load path
  (`load_game()`) — resets and repopulates the whole array from saved user-mapping data. The ordinary
  save-game load path (`load_game()` → `load_game_chunks()` → `reinit_level_after_load()`,
  `game_saves.c:538-601`) has no equivalent call; `reinit_level_after_load()`'s only comparable "fix up
  process-global state after a load" hook is `restore_computer_player_after_load()`
  (`../00-overview.md` §7), which doesn't touch user↔player mapping at all.

Full plan: `06-lifecycle-and-robustness.md` §1.

### 2.10 New finding: no defined behaviour for disconnect, a stuck pause, or a decided seat

A fourth follow-up investigation checked two related operational questions the plan hadn't addressed at
all: what happens if the bridge process goes away while the game is paused waiting for it, and how the
agent's own seat learns it has won or lost.

- **Disconnect**: `api_update_server()`'s only reaction to a lost client (`api.c:1673-1696`) is closing the
  socket and clearing subscriptions — nothing unpauses the game, falls back to another controller, or
  otherwise reacts. **No watchdog exists anywhere for a `GOF_Paused` game staying paused indefinitely** (a
  targeted grep for AFK/watchdog/idle-timeout patterns found only two unrelated mechanisms — a
  matchmaking-relay heartbeat and a torture-room minigame's UI idle timer, neither touching gameplay
  pause). The nearest existing *pattern* (not a directly reusable mechanism) is multiplayer's
  `wait_for_missing_packets()` (`net_exchange_gameplay.c:423-478`): a hard wall-clock ceiling
  (`FINAL_RESORT_RESYNC_RECOVERY` = 10 s) after which it stops waiting, flags a degraded state, and
  synthesizes a fallback packet for the missing peer rather than hanging — the same *shape* a disconnect
  fallback for an External seat would need, but built for an actively-running turn exchange, not a fully
  paused sim, so it can't be reused as-is.
- **A "fall back to built-in AI" idea has a real, specific hazard**: the `COMPUTER_PLAYER` script command's
  full call sequence (`lvl_script_commands.c:6188-6193`) includes `init_creature_states_for_player()`,
  which forcibly calls `set_start_state()` on **every creature the seat owns**, interrupting combat,
  training, digging, or gold-carrying in progress. `script_support_setup_player_as_computer_keeper()`
  itself (the function that actually flips `PlaF_CompCtrl` and re-seeds the `Computer2` struct) is safe to
  call on a live, populated dungeon on its own — it preserves gold, rooms, and creature ownership, only
  `memset`s the AI decision struct — but it has never been exercised *without* the creature-state-resetting
  call the script command always pairs it with, so using it alone as a disconnect fallback is an untested
  path, not a proven-safe one.
- **No readable victory/defeat signal exists.** `PlayerInfo::victory_state` (`player_data.h:165`,
  `VicS_Undecided`/`VicS_WonLevel`/`VicS_LostLevel`) is real engine state, set by
  `set_player_as_won_level()`/`set_player_as_lost_level()` — but it is not exposed through `read_var`,
  `get_all_player_flags`, `get_current_game_info`, or any Lua player field. The only reachable proxies are
  `SVar_DUNGEON_DESTROYED` (heart lost) and `SVar_ALL_DUNGEONS_DESTROYED` (only one keeper left) — both
  imprecise, and there's no proxy for "this player specifically has won."
- **Worse: a non-local seat's win/loss doesn't stop the shared simulation.** The functions that actually
  end the level (`lose_level()`/`resign_level()`, `main_game.c:328-349`) are gated on `is_my_player()` —
  they do nothing for any seat other than the one locally displayed. So an External seat's `victory_state`
  can flip to `VicS_LostLevel` while the game keeps running other seats' turns indefinitely. The engine
  already has an explicit, named flag for this exact situation (`RUN_AFTER_VICTORY`,
  `lvl_script_commands_old.c:1420-1425`, gating `GSF_RunAfterVictory`) — "game decided but sim keeps
  going" is a designed state, not an oversight, which means an agent bridge **must** poll its own seat's
  terminal state directly once that's readable; it cannot infer "my game is over" from any global signal.

Full plan: `06-lifecycle-and-robustness.md` §2–§3.

## 3. What's actually missing — summary

Five workstreams (the original plan had three; §2.7's finding split "seat plumbing" into its own item,
and §2.9/§2.10 together add a fifth — lifecycle concerns beyond a single turn):

1. **Transport and protocol** — `02-transport-and-protocol.md`. Decide `api.c` extension vs. a dedicated
   channel, and design the request/response envelope around a concrete constraint found while
   investigating this: `api.c`'s existing response functions all serialize into a hardcoded
   `char json_string[1024]` buffer (`api.c:423-449` and siblings) — too small for a map/creature
   observation payload as-is.
2. **Observation API** — `03-observation-api.md`. The fog-of-war-correct per-player snapshot (§2.4).
3. **Seat and action API** — `04-seat-and-action-api.md`. The `External` seat (now including the required
   `get_net_user_player_number()` change, §2.7) and the gesture-sequencing layer (§2.8's catalog).
4. **Seat lifecycle and robustness** — `06-lifecycle-and-robustness.md`. The save/load fixup (§2.9),
   disconnect handling, and victory-state exposure (§2.10).
5. **Testing and rollout** — `05-testing-and-rollout.md`. Test strategy (including the ftest gap §2.8
   found), phasing across all four preceding workstreams, and open questions.

## 4. Concrete gaps carried over from `../00-overview.md` §9

Restated here because they were checked against §3's workstreams and found *not* to block them, so a
future implementer doesn't waste time re-deriving this:

- **No mid-flight decision-suppression flag** distinct from `PlaF_CompCtrl` (§9.3 there) — not needed for
  the `External` seat design, since that seat never has `PlaF_CompCtrl` set in the first place (it's
  packet-driven) and so never runs the `Computer2` tick at all. Flagged only so a future "let an LLM take
  over a seat the built-in AI already started" feature doesn't assume this flag exists — it would need to
  be built.
- **The global 100-entry task pool** (`../00-overview.md` §9.4) is irrelevant — an `External` seat never
  touches `ComputerTask`.
- **Determinism** (`../00-overview.md` §9.6) is the reason the seat must be packet-driven at all; already
  addressed by that choice.

## 5. Design decisions this plan is making explicit

### 5.1 Seat type: packet-driven pseudo-human, confirmed

Already argued in §0 and `../00-overview.md` §9.6. Stated here as the load-bearing decision everything
else depends on: **the `External` controller is a human seat with no local input device**, not a
`Computer2` variant, not a hybrid. §2.7 refines this: it's a human seat whose `NetUserId` mapping also
needs to exist, which today it structurally doesn't for anything but the one local human.

### 5.2 Scope: single-machine skirmish only, for v1

Matches skirmish/01 §5.3/§9's existing framing. A packet-driven agent's actions are ordinary packets, so
they *would* sync correctly in principle in a networked game — but §2.5's latency-tolerance gap makes a
networked agent seat a distinct, harder problem that shouldn't be conflated with getting a local skirmish
seat working first. §2.7's `MAX_NET_USERS = 4` ceiling is also framed in terms of local seats for v1;
whether the identity-mapping fix should be scoped narrowly (local-only) or generally (also usable for a
future networked seat) is an open question (`05-testing-and-rollout.md` §3).

### 5.3 Latency model: pause-on-think for v1, not a queued/lag-window model

Given §2.5's findings — including the newly-confirmed one-turn latency model (submit this turn, effect
next turn) — the pragmatic v1 shape is: the sim pauses (`GOF_Paused`) while the agent computes its next
action, submits exactly one action (or explicitly "no action this turn"), then unpauses. This reuses an
existing, correct mechanism rather than inventing a new lag-tolerance system, at the cost of the
*player's* experience (the game visibly pauses whenever it's an agent seat's turn to act) — acceptable
for a skirmish testbed. A queued-multi-turn-actions model is a plausible v2 if pause-per-decision proves
too disruptive, but adds real complexity not worth taking on before v1 is proven.

### 5.4 Sandboxing and the TCP API's cheat-mode dependency

Whatever transport `02-transport-and-protocol.md` lands on, if it's built on or alongside `api.c`, it
inherits that file's `GKind_LocalGame`-only gating and (for the Lua-bridge prototype specifically)
cheat-mode gating. A dedicated `submit_action`/`get_player_view` command pair should **not** require
cheat mode — it's a deliberately scoped, validated action set (build/dig/sell/cast, not arbitrary Lua), so
it doesn't carry the same risk the `lua` console command does.

## 6. Relationship to the config JSON surface (`00-config-json-spec.md`)

Complementary, not overlapping, restated for clarity since both live under `docs/refactor/AI/LLM/`:

| | `00-config-json-spec.md` | This plan |
|---|---|---|
| When | No game running, or running but editing static config | Game running, turn-by-turn |
| What | `.cfg` files via `kfx_config` (`ConfigStack`/`ConfigSchema`/`ConfigContentWriter`) | Live sim state via `kfx_sim`/`kfx_game`, through packets and a read-only snapshot |
| Effect | Changes take effect on next load/reload | Changes are this turn's action, effective next turn (§2.5) |
| Transport | Unspecified in that doc (§9 lists CLI / MCP / a `config` family on the in-game TCP API as options) | `02-transport-and-protocol.md`: extend the same TCP API, or a dedicated channel |

If the transport doc extends `api.c`, the two surfaces would share one transport with two command families
(`config.*` per that doc's §9, `player.*`/`action.*` per this one) — worth deciding together once either
is actually implemented, not before.
