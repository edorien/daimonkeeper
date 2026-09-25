# Live LLM player-seat — save/load, disconnect, and game-end lifecycle

Status: **investigation and plan, no code written.** Date: 2026-09-23. Builds on
[`01-integration-plan.md`](01-integration-plan.md) §2.9–§2.10 and
[`04-seat-and-action-api.md`](04-seat-and-action-api.md) §1 (the `get_net_user_player_number()` fix this
document's §1 is a direct sequel to). This document exists because two follow-up investigations, run
specifically to check what the first pass of sub-documents had missed, found that the seat's identity and
fate need explicit handling *beyond a single turn* — across a save/load, across the bridge process
disconnecting, and across the seat's own game ending — none of which the transport/observation/action
documents address.

## 1. Save/load: a second identity fixup, distinct from §04's fix

### 1.1 The problem

`04-seat-and-action-api.md` §1 fixes `get_net_user_player_number()` to consult
`net_user_player_number[]` for local games instead of hardcoding a single entry. That fix alone is not
enough: the array itself is **process-global state, never part of any save-file chunk**
(`01-integration-plan.md` §2.9). `PlayerInfo.user_id` — which must agree with the array — *is* saved
wholesale as part of `kfx_sim_state`. So after a load: `player->user_id` is correct (restored from the
save blob), but `net_user_player_number[player->user_id]` holds whatever the array contained before the
load (most likely stale from an earlier game in the same process, or zero-initialized) — not necessarily
the seat's actual `PlayerNumber`. The seat's packets would stop being dispatched after any save/load,
silently, with no error.

### 1.2 The fix

Add a post-load fixup, modeled directly on the one working precedent for this exact operation:
`restore_users_from_packet_save()` (`src/kfx_net/src/packets_misc.c:155-191`), which does this same
reset-and-repopulate for the packet-replay/demo load path. The ordinary save-game load path has no
equivalent — it needs one:

```
restore_user_player_mapping_after_load():
    for user in 0..MAX_NET_USERS:
        set_net_user_player_number(user, -1)          // same reset step restore_users_from_packet_save() does
    for plyr_idx in 0..PLAYERS_COUNT:
        player = get_player(plyr_idx)
        if player_exists(player) and player->user_id >= 0:
            set_net_user_player_number(player->user_id, plyr_idx)
```

Call site: alongside `restore_computer_player_after_load()` in `reinit_level_after_load()`
(`src/kfx_game/src/main_game.c`, after `my_player_number` has already been set from
`kfx_net_state.local_plyr_idx` at `game_saves.c:594`, and after `kfx_sim_state.players[]` has already been
restored from the `SGC_KfxSimState` chunk — both preconditions are already satisfied by the point
`restore_computer_player_after_load()` itself runs, so this fixup can sit right next to it). This is a
general fix (it correctly re-establishes the mapping for the local human too, not just an External seat)
rather than something special-cased to the new seat type — the local human's `SOLO_HUMAN_ID` mapping
happens to be masked from ever needing this today only because `get_net_user_player_number()`'s local-game
short-circuit currently bypasses the array entirely; once `04-seat-and-action-api.md` §1's fix removes that
short-circuit, the human's own mapping needs exactly the same post-load restoration as the External seat's.

### 1.3 What this means for phasing

This fixup cannot be deferred past `04-seat-and-action-api.md` §1's own fix — the two are effectively one
unit of work with two call sites (fresh-start and post-load), not two independent features. Landing §1's
fix without this one would produce a seat that works until the first save/load, then silently stops being
controllable — a regression that's easy to miss in testing if the test suite doesn't specifically include
a save/load cycle (which, per `05-testing-and-rollout.md` §1, it should: `04-seat-and-action-api.md` §1's
own regression test needs a save/load case added to it, not just a fresh-game case).

## 2. Bridge disconnect and a stuck pause

### 2.1 The problem

Two related facts, neither previously addressed:

- `api_update_server()`'s disconnect handling (`api.c:1673-1696`) only closes the socket and clears
  subscriptions — nothing reacts at the game-state level.
- **No watchdog exists anywhere for `GOF_Paused`.** If the pause-on-think model (`01-integration-plan.md`
  §5.3) pauses the game while waiting for a `submit_action` that never arrives (bridge crashed, network
  blip, agent hung), the game stays paused indefinitely. Nothing times this out.

The nearest existing pattern, `wait_for_missing_packets()`'s 10-second ceiling
(`net_exchange_gameplay.c:423-478`), operates during an actively-running turn exchange between networked
peers — a different situation (the sim is running, waiting on one peer's packet) from a fully paused local
sim waiting on an external decision-maker. It's evidence the codebase already accepts "give up after a
bounded wait and do something sensible" as a legitimate pattern, not evidence of a mechanism to reuse
directly.

### 2.2 Options for a fallback

**Option A — timeout, then submit a no-op action and unpause.** Simplest: after N seconds paused with no
`submit_action`, unpause with the seat doing nothing that turn (equivalent to a human player who didn't
click anything). Safe (no state mutation beyond what "no action" already means), but does nothing to
recover a genuinely-dead bridge — the game would just pause again next time it's that seat's turn, forever,
until something else intervenes.

**Option B — timeout, then convert the seat to built-in AI.** More useful (the game becomes playable again
without the agent), but §2.3 below found this has a real, specific hazard that needs resolving first.

**Option C — timeout, then pause and surface a notification to whoever's actually watching** (the local
human, if any, or a log/alert for a headless test run) rather than silently doing anything on the seat's
behalf. Safer than B, less automatically-recoverable than either A or B, but honest about "something needs
human attention" rather than picking a fallback behaviour that might surprise whoever's watching.

**Recommendation**: Option A for v1 (cheapest, safest, matches "a human who's AFK just doesn't act"), with
Option B as an explicit opt-in for longer-running unattended sessions (e.g. an automated agent-vs-agent
test harness that shouldn't stall forever on one crashed bridge) — not the default, given §2.3's hazard.
Option C is worth offering as a UI-level notification regardless of which of A/B is chosen as the automatic
behaviour, since a human sharing the session should know their opponent's agent went quiet.

### 2.3 If Option B is used: the creature-state-reset hazard

`04-seat-and-action-api.md`-adjacent finding: the `COMPUTER_PLAYER` script command's real call sequence
(`lvl_script_commands.c:6188-6193`) always pairs `script_support_setup_player_as_computer_keeper()` with
`init_creature_states_for_player()`, which calls `set_start_state()` on **every creature the seat owns** —
interrupting combat, training, digging, or gold-carrying in progress, as a side effect of the conversion
itself. `script_support_setup_player_as_computer_keeper()` alone (without that second call) is safe to
call on a live, populated dungeon — gold, rooms, and creature ownership are untouched, only the `Computer2`
decision struct is reset — but calling it *without* its usual companion is an untested code path (the
existing script command never does this, and no test exercises it). Before Option B is implemented:

1. Confirm (by reading `setup_a_computer_player()`'s full effect again, and/or by writing a targeted
   Catch2/ftest) that calling `script_support_setup_player_as_computer_keeper()` alone, without
   `init_creature_states_for_player()`, leaves creatures' in-progress jobs/states intact and produces a
   working `Computer2` that can pick up from wherever the creatures currently are.
2. If it doesn't (e.g. some creature states assume a just-initialized `Computer2` and misbehave without
   the reset), decide whether that's fixable narrowly or whether Option B needs the full reset after all —
   in which case its cost (interrupting every creature's current activity) should be stated plainly as a
   known, accepted side effect of the fallback, not discovered by a player mid-session.

### 2.4 A watchdog needs a design decision, not just a timer

Whatever option is chosen, "how long is too long to wait" (the timeout value) should probably differ from
`FINAL_RESORT_RESYNC_RECOVERY`'s 10 seconds — that value was picked for network jitter, not for "the agent
might legitimately be thinking." A reasonable v1 default is a separate, longer, configurable value (tens
of seconds), decided alongside whatever latency budget the agent integration actually needs — not
something this document can size correctly on its own.

## 3. Victory/defeat signaling

### 3.1 The problem

`PlayerInfo::victory_state` is real, already-set engine state — but nothing reads it externally today
(`01-integration-plan.md` §2.10): not `read_var`, not `get_all_player_flags`, not any Lua player field.
The only reachable proxies (`SVar_DUNGEON_DESTROYED`, `SVar_ALL_DUNGEONS_DESTROYED`) are imprecise and
don't distinguish "I won" from "the game is nearly over."

Worse: because `lose_level()`/`resign_level()` (the functions that actually end a level) are gated on
`is_my_player()`, an External seat's `victory_state` can resolve to won/lost while the shared simulation —
and every other seat's turn processing — keeps running. `RUN_AFTER_VICTORY`/`GSF_RunAfterVictory`
(`lvl_script_commands_old.c:1420-1425`) confirms this is a deliberately supported engine state, not a bug:
"game decided but sim keeps going" is normal, expected behaviour here.

### 3.2 The fix

1. **Expose `victory_state` directly**, either as a new `SVar_*` reachable through the existing
   `read_var`/`get_condition_value` machinery (consistent with how everything else in this family is
   exposed, and automatically available to Lua too via `lua_api_player.c`'s fallthrough to
   `parse_get_varib`), or as a dedicated field in `02-transport-and-protocol.md`'s `get_player_view`
   response (`03-observation-api.md` §4's sketch should gain a `"victory_state"` field alongside `turn`/
   `paused`). Recommend both are unnecessary — pick the `SVar_*` route, since `03-observation-api.md`
   already plans to reuse existing read surfaces rather than duplicate field-level access, and a
   `get_player_view` implementation can simply include the new `SVar_*`'s value in its own payload.
2. **The observation API's contract must say explicitly**: a `get_player_view` response's `victory_state`
   reflects *this seat's own* fate, and the absence of a global "game over" signal is deliberate, not an
   oversight — the bridge must check its own seat's state every turn (or at least every time it's about to
   decide an action) rather than assuming it'll be told externally that the game ended.
3. **Bridge-side behaviour once `victory_state != VicS_Undecided`**: the bridge should stop submitting
   actions for that seat (submitting to a decided seat isn't harmful per se — the packet dispatch itself
   doesn't check `victory_state` — but it's meaningless, and a well-behaved bridge should recognize its own
   game is over rather than continuing to poll/decide). This is a bridge/client-side responsibility, not
   something the engine needs to enforce — flagged here so it's not silently assumed to be "someone else's
   problem" when the actual agent integration gets built.

## 4. Open items for implementation (not decided here)

1. §2.2's fallback choice (A/B/C) and §2.4's timeout value are product decisions as much as engineering
   ones — carried into `05-testing-and-rollout.md` §3's open questions rather than decided here.
2. §2.3's untested-path question (does `script_support_setup_player_as_computer_keeper()` alone, without
   `init_creature_states_for_player()`, work correctly on a live dungeon) needs a small dedicated
   Catch2/ftest before Option B can be considered safe to ship, independent of anything else in this
   feature.
3. Whether §1's fixup should also run for a **fresh** game start (defensively resetting the array even
   though nothing should be stale yet) or only on load — cheap either way, but worth being deliberate about
   rather than only wiring it into the load path and leaving the fresh-start path relying on whatever
   `04-seat-and-action-api.md` §1.3's seat-creation call does on its own.
