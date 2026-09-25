# Live LLM player-seat — seat identity and the action/gesture API

Status: **investigation and plan, no code written.** Date: 2026-09-23. Stage 3 of
[`01-integration-plan.md`](01-integration-plan.md)'s four sub-documents; read that document's §2.3, §2.7
and §2.8 first — this document is where those three findings turn into a concrete build plan. Assumes
`02-transport-and-protocol.md`'s envelope (`submit_action`, one-turn latency, `ACTION_ALREADY_QUEUED`
discipline).

This document has two genuinely separate parts that must both land before an agent can act at all: **§1**
makes the seat *reachable* (an engine-level identity fix); **§2** makes actions *expressible* (the gesture
catalog and its JSON translation layer). §1 is a small, mechanical change; §2 is the bulk of the design
work.

## 1. Seat identity: the `get_net_user_player_number()` fix

### 1.1 The problem, restated from `01-integration-plan.md` §2.7

```c
// src/kfx_net/src/net_game.c:177-186
static PlayerNumber net_user_player_number[MAX_NET_USERS];
PlayerNumber get_net_user_player_number(NetUserId user)
{
    if ((user < 0) || (user >= MAX_NET_USERS)) return -1;
    if (!network_is_active() && !kfx_net_state.packet_load_enable) {
        return (user == SOLO_HUMAN_ID) ? my_player_number : -1;   // local-game branch
    }
    return net_user_player_number[user];   // only reached when network_is_active() or replaying
}
```

`process_packets()` (`packets.c:1590`) loops `NetUserId user < PACKETS_COUNT` and skips any user this
function maps to a negative player number. For a local (`GKind_LocalGame`) skirmish, **every** `NetUserId`
except `SOLO_HUMAN_ID` (0) resolves to `-1`, regardless of what's actually in `net_user_player_number[]` —
the local-game branch never even reads that table. So a second packet-driven seat's entry in
`sim_packets[]` is structurally unreachable today, independent of how correctly its `PlayerInfo` flags are
set up.

### 1.2 The fix

Generalize the local-game branch to consult a small local mapping instead of hardcoding one entry:

```c
static PlayerNumber net_user_player_number[MAX_NET_USERS]; // already exists
// populate net_user_player_number[SOLO_HUMAN_ID] = my_player_number at the same point that's
// currently implicit, plus one more entry per External seat, at seat-creation time (§1.3)

PlayerNumber get_net_user_player_number(NetUserId user)
{
    if ((user < 0) || (user >= MAX_NET_USERS)) return -1;
    if (!network_is_active() && !kfx_net_state.packet_load_enable) {
        return net_user_player_number[user];   // now consults the table locally too
    }
    return net_user_player_number[user];
}
```

Collapsing to "always consult the table" is the simplest form of the fix — the networked and local
branches become identical once the table is populated correctly in both cases; the `if` may not even need
to survive, pending confirming nothing else relies on the two branches genuinely differing (a
Catch2/ftest check, not just a read-through, given `net_main.c`'s general caution around this file's
global state per `docs/refactor/todo/ftest-fake-multiplayer.md`'s findings about `bflib_enet.cpp`'s
similar global-state fragility).

**This is the one required engine change for the whole feature.** Everything else in this document (and
in `03-observation-api.md`) is new/additive code; this is a change to existing dispatch logic in a file
several other systems depend on (`net_resync.cpp`, `net_checksums.c`, `packets_misc.c` all iterate
`NetUserId`s using this function's results) — it needs its own focused review and test, not a drive-by
edit alongside the rest of the feature.

### 1.3 Where the mapping gets populated for an External seat

Needs a small new setup routine, analogous to (but much smaller than) `init_players_local_game()`
(`player_utils.c:1138-1164`, the existing local-human setup):

```
external_seat_setup(NetUserId slot, PlayerNumber plyr_idx):
    net_user_player_number[slot] = plyr_idx
    // do NOT set PlaF_CompCtrl (that's the whole point — §1.4 confirms nothing else needs touching)
    // player->user_id, if that field is consulted elsewhere the same way SOLO_HUMAN_ID's is
    //   (packet_data.c's get_local_packet()/get_local_user() pattern) — needs the equivalent for a
    //   *non-local* "which packet does this player's input come from" resolution; confirm during
    //   implementation whether user_id is purely a local-human concept or genuinely per-seat
```

Slot numbering: `SOLO_HUMAN_ID` (0) stays the local human's; External seats take `1..MAX_NET_USERS-1` —
i.e. up to 3 simultaneous External seats structurally possible alongside the one local human, before
`MAX_NET_USERS = 4` is exhausted (`01-integration-plan.md` §2.7's ceiling). Comfortably enough for v1
(one agent seat) with headroom for testing multiple agents against each other later.

### 1.4 What does *not* need to change (confirmed by the investigation)

- **Checksums**: leave `checksum` at 0 for a bridge-fed packet; desync verification never runs for
  `GKind_LocalGame` (`01-integration-plan.md` §2.7).
- **`NetSP`**: not needed; `exchange_packets()` already branches around every `netstate.sp`-touching call
  for `GKind_LocalGame`.
- **Camera, `is_my_player()`, UI toggle gating**: already correctly treat any non-`my_player_number` slot
  as "some other player" — no incorrect single-human assumption blocks this specifically.
- **`front_input.c`**: structurally can't touch a non-local slot's packet (it only ever resolves through
  `my_player_number`), so no risk of it clobbering the External seat's bridge-written packet.

## 2. The gesture catalog

`01-integration-plan.md` §2.8 already flagged the headline finding: **no existing test or trace validates
any of this** — the catalog below is derived directly from reading production dispatch code
(`packets_input.c`, `packets.c`, `roomspace.c`, `front_input.c`), not from an existing golden example.
`05-testing-and-rollout.md` §1 covers building that missing ground truth as a prerequisite gate before
relying on this catalog to build the gesture-sequencing layer.

Core mechanics shared by every verb below: `enum PlayerStates` (`config_players.h:44-177`) holds the
player's persistent `work_state`, set via the one-shot `PckA_SetPlyrState` action
(`par1=work_state, par2=<model/kind if applicable>`); `enum TbPacketAction`'s `PCtr_*` control-flag family
(`packet_data.h:361-401`: `PCtr_LBtnClick/Held/Release`, `PCtr_RBtnClick/Held/Release`,
`PCtr_MapCoordsValid`, `PCtr_Gui`) rides on the packet alongside `pos_x`/`pos_y` every turn; dispatch is
`process_dungeon_control_packet_clicks()` (`packets_input.c:709-931`), `switch (player->work_state)` at
line 756.

### 2.1 Room building (`PSt_BuildRoom`)

Two-part mechanism — a client-facing "tag the shape" gesture, then a fully separate, packet-free,
per-turn background build process:

1. **Select**: `PckA_SetPlyrState(PSt_BuildRoom, RoomKind)` once.
2. **Configure drag mode** (optional, one-shot): `PckA_SetRoomspaceDrag`/`_DragPaint` for a rectangle drag,
   or leave default (`box_placement_mode`/others) for click-to-place.
3. **Tag the shape**:
   - *Drag mode*: `LBtnClick` (down, remembers start corner) → `LBtnHeld` each turn while the box updates
     live from `pos_x/pos_y` → **`LBtnRelease` commits** (`packets_input.c:138-141` — any other flag
     combination on the terminating turn does not build).
   - *Non-drag (default) mode*: a single `LBtnClick` on the target subtile commits immediately
     (`packets_input.c:143-154`); holding `LBtnHeld` re-attempts the same click every turn ("paint" —
     useful for laying multiple 1-slab claims quickly, not needed for a single request/response gesture).
4. Committing calls `keeper_build_roomspace()` (`roomspace.c:1203-1234`) **once** — this only seeds
   `player->roomspace` (start position, size, `is_active=true`); it does not itself place any slabs.
5. **Building happens automatically thereafter**, with zero further packets: `update_roomspaces()`
   (`player_utils.c:1191`, unconditional per-turn tick) → `keeper_update_roomspace()`
   (`roomspace.c:1269-1406`) builds exactly one slab per turn until the tagged area is exhausted.

**Gesture request shape**: `{ "verb": "build_room", "room_kind": "TREASURE", "rect": [[x0,y0],[x1,y1]] }`
for a drag-rectangle, or `{ "verb": "build_room", "room_kind": "TREASURE", "pos": [x,y] }` for a single
slab — the sequencing layer expands either into the click/hold/release turns above. The agent does not
need to track "is it still building" as part of *this* gesture — that's a background process visible
through `03-observation-api.md`'s own-dungeon room list, not part of the action's completion signal.

### 2.2 Dig-marking — corrected: `PSt_CtrlDungeon` + `CSt_PickAxe`, not `PSt_MkDigger`

**Correction to the original plan's assumption**: `PSt_MkDigger` is a *cheat* state (place a digger
creature directly, `packets_cheats.c:261-282`, `PckA_CheatMakeDigger`) — unrelated to everyday digging.
The real verb lives inside `process_dungeon_control_packet_dungeon_control()`
(`packets_input.c:275-555`), the handler for the *default* gameplay state `PSt_CtrlDungeon`, active when
the transmitted cursor context (`additional_packet_values`, `PCAdV_ContextMask`) is `CSt_PickAxe`.

Sequence, sharing the same `RoomSpace`/drag machinery as §2.1:

1. No explicit "select dig mode" `PckA_SetPlyrState` needed beyond being in the default `PSt_CtrlDungeon`
   state with the cursor context set to `CSt_PickAxe` on the packet.
2. `LBtnClick`: starts the gesture (`cursor_button_down=1`, remembers cursor state).
3. `LBtnHeld` each subsequent turn: **tags/untags slabs immediately, every turn** — no separate
   "commit" step; the imp AI picks up tagged slabs as soon as they're tagged. Multi-slab drags are
   interpolated (`remember_cursor_subtile()`/`packets_input.c:67-86`'s DDA-style walk from the previous to
   the current cursor slab) so a fast drag doesn't skip tiles between turns.
4. `LBtnRelease`: finalizes the last segment, clears drag state.

**Gesture request shape**: `{ "verb": "mark_dig", "rect": [[x0,y0],[x1,y1]] }` (or a path of points for a
non-rectangular tag) — the sequencing layer must reproduce the interpolation itself (or submit enough
intermediate points that the engine's own interpolation does it), since the actual engine effect happens
turn-by-turn during the hold, not at a single commit point like room building.

### 2.3 Selling (`PSt_Sell`)

Same drag-vs-click split as building: `process_dungeon_control_packet_sell_operation()`
(`packets_input.c:557-661`). Drag mode commits on `LBtnRelease` (sell everything in the rectangle);
default mode commits per-`LBtnClick` (one item per click). `ustate->full_slab_cursor` (set via
`PckA_SetRoomspaceSubtile`/`_WholeRoom`) picks whole-room-via-`keeper_sell_roomspace()` (itself another
incremental per-turn background process, like building) vs. single-trap/door-via-immediate-mutator.
Selling something you don't own is refused silently (a warning log, no packet-level error).

**Gesture request shape**: `{ "verb": "sell", "rect": [[x0,y0],[x1,y1]] }` or
`{ "verb": "sell", "pos": [x,y] }`.

### 2.4 Trap/door placement (`PSt_PlaceTrap`/`PSt_PlaceDoor`)

Single-click, no hold/preview-then-release. Model selection is a **separate, prior** one-shot
`PckA_SetPlyrState(work_state, model)` (mirroring the real UI's workshop-item selection,
`frontmenu_ingame_tabs.c:811-828`) — not a parameter on the placement click itself. Placement commits on
`LBtnClick` (`packets_input.c:684-706` for traps, `:871-893` for doors, both preview-validated every turn
via `tag_cursor_blocks_place_trap/door()`).

**Gesture request shape**: `{ "verb": "place_trap", "trap_kind": "ALARM", "pos": [x,y] }` /
`{ "verb": "place_door", "door_kind": "WOODEN", "pos": [x,y] }` — the sequencing layer issues the
model-select `PckA_SetPlyrState` first (if not already selected) then the single click, both potentially
in the same submitted turn's packet or across two turns depending on how the layer chooses to batch
one-shot setup actions with the terminal click (an implementation detail, not a protocol one, since the
agent only ever sees one request/response pair for the whole gesture).

### 2.5 Slapping (`PSt_Slap`)

Single click-release; no hold-to-repeat. Two redundant trigger paths exist in production
(`front_input.c:2036-2082`'s thing-targeted fast path emitting `PckA_UsePwrOnThing(PwrK_SLAP, thing_idx)`,
and an ambient fallback in `process_dungeon_control_packet_clicks()`'s `case PSt_Slap:`
(`packets_input.c:775-782`) that re-derives the target from cursor position on `LBtnRelease`) — for a
bridge, **use the ambient path** (position-only, no thing-index lookup needed client-side): submit
`pos_x/pos_y` plus `LBtnRelease` while in `PSt_Slap`.

**Gesture request shape**: `{ "verb": "slap", "pos": [x,y] }` (subtile) or
`{ "verb": "slap", "thing_id": 1042 }` (resolve to the thing's current position at submission time).

### 2.6 Casting a power on a subtile or thing (`PSt_CastPowerOnSubtile` / `PST_CastPowerOnTarget`)

Note the source's own inconsistent capitalization (`PST_CastPowerOnTarget`, capital `ST`) —
`config_players.h:52,63`; preserved here for grep-ability against the actual header. Power selection is a
prior one-shot `PckA_SetPlyrState(work_state, power_kind)`. Casting itself fires **once, on
`LBtnRelease`** (`packets_input.c:764-774` subtile, `:894-906` thing-targeted).

**Hold-to-overcharge is real and orthogonal to targeting**: `process_dungeon_control_packet_spell_overcharge()`
(`packets.c:158-216`) runs unconditionally every turn before the work-state switch; while `LBtnHeld` is
set, `player->cast_expand_level` increments each turn for overchargeable powers. The cast, when it fires
on release, reads whatever charge level accumulated. So the true gesture for an overchargeable power is
**click → hold N turns → release-to-cast-at-that-charge**, not a single click.

**Gesture request shape**: `{ "verb": "cast_power", "power": "LIGHTNING", "pos": [x,y], "overcharge_turns": 3 }`
or `{ "verb": "cast_power", "power": "HEAL_CREATURE", "thing_id": 1042 }` — `overcharge_turns` (default 0)
lets the agent request a charge level without needing to understand the hold-mechanism itself; the
sequencing layer holds for that many turns before releasing. This is the one verb where a gesture
request's declared duration directly controls a gameplay-meaningful outcome (charge level), not just how
the request gets encoded as packets — worth flagging to whoever designs the final JSON schema as a case
that needs its own validated range (0 up to the power's max overcharge level, from config).

### 2.7 Picking up / dropping a creature (Hand of Evil)

Confirmed genuinely single-packet at the protocol level — no drag/hold component in the packet fields
themselves (the *visual* of a held creature following the cursor across turns is pure client rendering
driven by `pos_x/pos_y`, not gated on any control flag):

- **Pick**: one-shot `PckA_UsePwrHandPick(thing_idx)` (`packets.c:823-830` →
  `use_power_hand(plyr_idx, ...)`, or `magic_use_available_power_on_thing(..., PwrK_HAND, ...)` if
  `PCtr_Gui` is set — i.e. picked via a GUI list rather than a map click; a bridge should use the
  non-GUI path).
- **Drop**: either an explicit one-shot `PckA_UsePwrHandDrop(x, y)` (`packets.c:831-833` →
  `dump_first_held_thing_on_map()`), or the ambient equivalent — `RBtnRelease` at the target subtile while
  a thing is held (`packets_input.c:518-528`). Use the explicit action; it needs no work-state/cursor-mode
  setup and is unambiguous.
- Moving the held creature to a new position before dropping needs no packet at all beyond ordinary
  `pos_x/pos_y` updates on whatever turns elapse between pick and drop — the sequencing layer can simply
  wait (submitting no-op/idle turns) between the pick and drop sub-actions if the design wants to model
  "carry it somewhere" as a distinct step, though for a single request/response gesture, one `pick` then
  one `drop` in the *same* logical request (two turns apart) is simplest.

**Gesture request shape**: `{ "verb": "pick_up", "thing_id": 1042 }`,
`{ "verb": "drop", "pos": [x,y] }` (or `{ "verb": "drop_at_origin" }` for
`PckA_DumpHeldThingToOldPos`) — modelled as two separate `submit_action` calls (pick, then later drop)
rather than one compound gesture, since a real agent may legitimately want to observe state between
picking something up and choosing where to drop it (e.g. re-evaluate the target after seeing what got
picked up).

### 2.8 Cancellation

No universal mechanism; verb-specific, from a small set of recurring primitives:

- **Opposite-button-during-drag** (build-room/dig only): `RBtnClick` while `LBtnHeld` sets
  `ignore_next_PCtr_LBtnRelease`, so the next release is swallowed and the roomspace resets to a neutral
  1×1 box rather than committing (`roomspace.c:791-843`).
- **Switching `work_state` mid-gesture**: `set_player_state()` clears the prior selection state
  (`player_data.c:476-482`); roomspace mode changes explicitly force-cancel an active drag
  (`roomspace.c:955-965`).
- **Releasing over an invalid target**: simply fails to commit that turn (refusal sound/message), without
  changing gesture state — for a drag, the box keeps tracking and can still succeed if released over valid
  ground on a later turn.
- **No generic cancel key** exists at this dispatch layer. A gesture-sequencing layer that wants to expose
  "abort" to the agent (e.g. the agent changes its mind mid-drag before submitting the release) should
  model it as: don't submit the release turn, and instead submit a `PckA_SetPlyrState` to some other state
  (or back to `PSt_CtrlDungeon`) to force-cancel via the "switching work_state" path above.

## 3. What the sequencing layer needs to track

Since a gesture spans multiple turns but the agent-facing protocol is one `submit_action` request per
gesture (§2's "gesture request shapes"), the sequencing layer needs its own small piece of per-seat state
(not part of `sim_packets[]`, purely bridge/engine-side bookkeeping): which gesture is in progress, which
turn-step it's on, and what packet to emit on the *next* turn — i.e. a small state machine per seat, driven
one step per turn from the same place `submit_action`'s single-turn actions are queued
(`02-transport-and-protocol.md` §4). A single-step verb (place trap/door, slap, cast, pick, drop) completes
in the same turn it's requested (given the one-turn latency already established); a multi-step verb
(build room via drag, mark-dig via drag, sell via drag) spans as many turns as the gesture's own click/
hold/release sequence requires, with the agent's `submit_action` call blocking (from the protocol's point
of view) only on the *first* step — subsequent steps run automatically without further agent input, since
by design the agent expressed the whole gesture (e.g. the rectangle) in one request.

This state machine is genuinely new code with no existing analogue to crib from structurally (per §2's
"no existing precedent" finding) — worth its own focused implementation task once §1's identity fix and
`03-observation-api.md`'s read side both exist, since it's the piece most likely to reveal further gaps in
the gesture catalog above once real packets are being generated and checked against the engine's actual
behaviour turn by turn.

## 4. Open items for implementation (not decided here)

1. Whether `player->user_id` (or an equivalent) needs a genuinely new per-seat concept beyond
   `net_user_player_number[]`, or whether populating that table alone is sufficient — needs confirming
   during implementation, not just read-through (§1.2).
2. Exact request-shape vocabulary: one shape per verb (as sketched in §2) vs. a smaller shared vocabulary
   (point-action / drag-rectangle / target-thing) that several verbs share — the sketches above already
   show verbs clustering into those three shapes naturally; a real schema design pass should decide
   whether to name that clustering explicitly in the JSON Schema or keep one schema per verb for clarity.
3. §2.6's overcharge-turns validation range needs pulling from config (`comp_player_conf`-adjacent power
   config, not investigated here) rather than being hardcoded.
