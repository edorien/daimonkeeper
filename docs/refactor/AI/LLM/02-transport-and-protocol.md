# Live LLM player-seat — transport and protocol

Status: **investigation and plan, no code written.** Date: 2026-09-23. Stage 1 of
[`01-integration-plan.md`](01-integration-plan.md)'s four sub-documents; read that document's §0–§2
first for shared context (in particular §2.1's `api.c` findings and §2.5's turn-timing findings, both
assumed below).

## 1. The decision: extend `api.c`, or build a dedicated channel

Two real options, not mutually exclusive in principle, but one should be picked as the primary path.

### Option A — extend the existing TCP JSON API (`src/kfx_script/src/api.c`)

Add a new command family (`get_player_view`, `submit_action`, maybe `get_gesture_status`) to
`api_process_buffer()`'s dispatch, gated the same way the existing gameplay commands are
(`kfx_sim_state.game_kind == GKind_LocalGame`).

**In favour:**

- Reuses proven infrastructure: the accept/poll loop (`api_update_server()`), the JSON parsing
  (`json_dom_parse`/`value_dict_get`), the `"ack"` request-matching convention, the `"player"` field
  resolution already built into `api_process_buffer()` (`api.c:1083-1094` — falls back to
  `my_player_number`, accepts an int or a name), and the subscribe/push mechanism
  (`subscribe_event`/`subscribe_var`) for a possible future "your turn" notification.
- One binding surface for both this feature and `00-config-json-spec.md`'s future `config.*` family
  (`01-integration-plan.md` §6) — a single client library, a single port, a single set of connection
  semantics for anything scripting a running KeeperFX instance.
- Already the channel `00-config-json-spec.md` §9 itself anticipated ("a `config` command family
  forwarding to the same library for the *running* game's data tree").

**Against, and this is where the real design work is (§2 below):**

- The existing response builders (`api_err`, `api_ok`, `api_return_data`) each serialize into a
  **hardcoded 1024-byte stack buffer** — sized fine for the existing commands' small payloads, not for a
  map/creature observation snapshot.
- `api.c` was built for config/debug use (one command, one small response, mostly fire-and-forget) — a
  `get_player_view` request that needs to return potentially hundreds of visible tiles/creatures/rooms is
  a different traffic shape than anything the file does today.
- The existing gameplay-command gate refuses to run `map_command`/`console_command` while `GOF_Paused`
  (`api.c:1281-1286,1316-1321`) — directly in tension with the pause-on-think model
  (`01-integration-plan.md` §5.3), so a new command needs its own, deliberately different, gating rule
  (§4 below).

### Option B — a dedicated channel

A new TCP (or other IPC) server, separate from `api.c`, purpose-built for the request/response cadence an
agent bridge actually wants (block until the next turn's snapshot is ready, submit exactly one action,
repeat).

**In favour:** doesn't inherit `api.c`'s existing scope/gating assumptions; can use a response format and
buffer strategy sized correctly from day one; a protocol bug in the agent channel can't affect the
config/debug channel and vice versa.

**Against:** a second protocol surface, a second port, a second set of "is a client connected" bookkeeping
duplicating `api.c`'s `ApiGlobals`/single-active-socket pattern almost exactly (`api.c:59-65`) — real
duplication for not much genuine independence, since both channels are gated on `GKind_LocalGame` and
both want request/response-with-ack semantics anyway.

### Recommendation

**Option A (extend `api.c`), with the response-buffer problem solved explicitly (§2) rather than worked
around.** The two channels' actual requirements (local-game-only, single active client, JSON
request/response, optional push events) are close enough that a second implementation would mostly be
copy-paste of the first, and this repository's own `00-config-json-spec.md` already assumed this file
would grow a second command family. The buffer-size issue is real but bounded (§2) — worth fixing once in
one file rather than as a reason to fork the transport.

This recommendation is not yet an owner decision — see `05-testing-and-rollout.md` §3, open question 1.
Everything past this point assumes Option A for concreteness, but the request/response *shapes* (§3, §4)
would carry over unchanged to Option B if that's chosen instead.

## 2. The response-size constraint

Found directly while reading `api.c` for this plan (not previously flagged anywhere in the codebase's own
comments or docs):

```c
// api.c:423-449, api_return_data() — identical pattern in api_err() (:307-352) and api_ok() (:362-421)
char json_string[1024];
struct dump_buf_state dump_state = {json_string, sizeof(json_string) - 1};
int64_t json_dump_return_value = json_dom_dump(json_root, json_value_dump_writer, &dump_state, 0, JSON_DOM_DUMP_MINIMIZE);
```

Every response path builds its JSON into a **1024-byte stack buffer**, independently in each of the three
response functions (not a shared constant — three separate literal `1024`s). `json_dom_dump` respects the
buffer size it's given, so overflow isn't the risk; **silent truncation-to-error is**: a response that
doesn't fit returns `json_dump_return_value != 0`, which `api_return_data` turns into a
`FAILED_TO_CREATE_JSON` error sent to the client instead of the data it asked for (`api.c:454-458`).

This is not hypothetical for existing code, either: `get_all_player_flags` (`api.c:1354-1385`) builds one
dict entry per player (`ALL_PLAYERS`, up to `PLAYERS_COUNT = 9`) times `get_max_flags()` flag entries each
— a mappack with a generous flag count could already be at risk of hitting this ceiling today, independent
of anything this plan adds. Worth a one-line note to whoever owns `api.c` even if this plan goes no
further, but not this plan's problem to fix retroactively.

For `get_player_view`, the payload is unavoidably larger: even a modest dungeon (a few rooms, a dozen
creatures, some revealed enemy presence) serializes to well over 1024 bytes as flat JSON. Three ways to
handle it, not mutually exclusive:

1. **Enlarge the buffer for data-bearing responses.** Simplest fix; `api_return_data` moves its buffer to
   the heap (`malloc`/a growing buffer) or a much larger fixed size (e.g. 64 KiB) sized to a worst-case
   snapshot. Costs nothing structurally since `api.c` already handles one client at a time.
2. **Page the response**, mirroring `00-config-json-spec.md` §5.3's own `limit`/`after` convention for its
   `read` operation — `get_player_view` takes an optional cursor and returns `next` when more remains.
   Consistent with the sibling spec's style, but adds round trips to what should be a fast "look at the
   board" call.
3. **Scope the view down.** Rather than one giant snapshot, offer several narrower queries (own dungeon
   summary, visible-enemies-only, map-tile-summary) that each comfortably fit a moderate buffer, and let
   the agent (or a client-side aggregation helper) combine them. Mirrors the existing `read_var`/
   `get_all_player_flags` granularity, but pushes complexity onto the client/bridge rather than the
   server.

**Recommendation**: (1) — enlarge the response buffer for the new commands (or globally; the existing
commands' payloads are all tiny, so a larger shared buffer costs nothing observable) — combined with (3)'s
spirit at the schema level (§3's envelope keeps the view compact by construction: revealed-but-unremarkable
tiles collapse to a run-length or count, not one entry per subtile). Paging (2) is worth keeping as a
fallback for pathological map sizes, not the primary mechanism.

## 3. Request/response envelope

Mirrors `api.c`'s existing conventions exactly (§1's reason for choosing Option A) rather than inventing a
new shape:

```json
{ "action": "get_player_view", "ack": 42, "player": 0 }
```

```json
{ "ack": 42, "success": true, "data": { "turn": 18042, "paused": true, "own": {...}, "visible": {...} } }
```

```json
{ "ack": 42, "success": false, "error": "NOT_IN_LOCAL_GAME" }
```

- `"player"` resolved exactly as `api_process_buffer()` already does (`api.c:1083-1094`) — a request
  omitting it defaults to `my_player_number`, which for an agent-driven seat is *wrong by default* (the
  agent is not the local human) — **every request in this family must require an explicit `"player"`**,
  a deliberate deviation from the existing fallback-to-local-human convention, enforced with a
  `MISSING_PLAYER`/similar error rather than silently defaulting.
- Gating: `game_kind == GKind_LocalGame` (existing rule, unchanged) for both new commands.
  `get_player_view` should work **while paused** (needed for the pause-on-think model,
  `01-integration-plan.md` §5.3) — a deliberate difference from `map_command`/`console_command`'s
  paused-refusal. `submit_action` should also work while paused, for the same reason (§4).
- Error codes follow the existing style (`NOT_IN_LOCAL_GAME`, `MISSING_COMMAND`, …): add
  `MISSING_PLAYER`, `INVALID_PLAYER` (player number out of range or not `player_exists()`),
  `NOT_A_VALID_SEAT` (player exists but isn't an `External`-controlled seat — see
  `04-seat-and-action-api.md`), `ACTION_ALREADY_QUEUED` (§4), and per-gesture validation errors defined
  in that same document.

## 4. Submission timing, restated precisely

`01-integration-plan.md` §2.5 already established the mechanical fact: `update()` calls `process_packets()`
then `api_update_server()`, every turn, in that order (`game_session_loop.cpp:126-128`). Consequences for
`submit_action`'s contract, stated explicitly since this is easy to get subtly wrong:

- A `submit_action` processed during turn *T*'s `api_update_server()` call writes into
  `sim_packets[N]` for the seat. That packet is consumed by turn *T+1*'s `exchange_packets()`/
  `process_packets()` pair, not turn *T*'s (already finished for this seat by the time the API server ran).
- This means `get_player_view`'s response should be understood by the client as **"the state as of the end
  of the turn that just finished processing"**, and `submit_action`'s effect as **"queued for next turn"**
  — i.e. the natural agent loop is: pause (or already paused) → `get_player_view` → decide → `submit_action`
  → unpause (or let the existing pause-per-turn cycle continue) → next turn's processing reflects the
  action. No new synchronization primitive is needed beyond this ordering fact plus `GOF_Paused` itself.
- `submit_action` should reuse the existing "don't overwrite an already-queued action" discipline
  `ftest_packet_capture.h` documents (`get_players_packet_action(player) != PckA_None` is respected, not
  clobbered) — a second `submit_action` call for the same seat before its queued action has been consumed
  should return `ACTION_ALREADY_QUEUED` rather than silently replacing it, so a bridge bug (double-submit)
  fails loudly instead of corrupting the turn sequence.

## 5. What this document does not decide

- The exact JSON shape of `get_player_view`'s `data` payload — that's `03-observation-api.md`.
- The exact JSON shape of `submit_action`'s per-verb request bodies (the gesture vocabulary) — that's
  `04-seat-and-action-api.md`.
- Whether/how a "turn advanced" push event (reusing `subscribe_event`'s existing mechanism) would let a
  bridge avoid polling `get_current_game_info` in a loop while paused-waiting-for-its-turn — a plausible
  nice-to-have, not required for v1's pause-on-think model where the bridge already controls the
  pause/unpause transitions itself, but worth a line in `05-testing-and-rollout.md`'s open questions.
