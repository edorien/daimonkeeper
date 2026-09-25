# Live LLM player-seat — observation API

Status: **investigation and plan, no code written.** Date: 2026-09-23. Stage 2 of
[`01-integration-plan.md`](01-integration-plan.md)'s four sub-documents; read that document's §2.2 (Lua
API surface) and §2.4 (fog of war) first — this document builds directly on both rather than re-deriving
them, and assumes `02-transport-and-protocol.md`'s envelope (`get_player_view`, `"player"` required,
works while paused).

## 1. What exists vs. what's missing, restated precisely

`01-integration-plan.md` §2.4 already established the core fact: a correct per-player fog-of-war primitive
exists (`map_block_revealed(mapblk, plyr_idx)`, `map_data.c:299-330`, correctly honouring
`allies_share_vision`) and per-thing `revealed` bits exist for traps/doors independently of tile reveal —
but the one existing scripting exposure of tile reveal (`lua_api_slabs.c`'s `"revealed"` field) collapses
the whole per-player bitmask to a single boolean and cannot be reused as-is.

So the work here is **not** "invent fog of war" — it's:

1. A correct per-player tile-reveal query, either fixing `lua_api_slabs.c`'s binding or (simpler, no
   behaviour-changing risk to existing Lua scripts that rely on the current coercing behaviour) adding a
   new function that calls `map_block_revealed()` directly without going through the existing Lua field.
2. A filtering/aggregation layer that assembles a coherent "what does player N know right now" snapshot
   from data sources that mostly already exist (§2), rather than new field-level getters.
3. A response shape compact enough to fit `02-transport-and-protocol.md` §2's buffer constraints without
   needing paging for a typical dungeon.

## 2. Data sources already available (from `01-integration-plan.md` §2.2)

Reuse these rather than duplicating field access in new C++:

| Snapshot section | Existing source |
|---|---|
| Own dungeon: gold, room list, creature roster, research/availability | `lua_api_player.c` field access (`controls`, `available`, `heart`), `Room`/`Thing` bindings, `GetRoomsOfPlayerAndType`, `GetThingsOfClass` |
| Own creatures: position, health, state, job | `lua_api_things.c` field access (`pos`, `health`, `state`, `instance`, kind-specific sub-fields) |
| Map tiles: owner, kind, room | `lua_api_slabs.c` field access (`owner`, `kind`, `room`, `centerpos`) — minus the broken `revealed` field, §1 |
| Current turn / pause state | `get_current_game_info` (`api.c:1529-1545`), `GOF_Paused` |
| Enemy presence, once filtered by reveal | Same `Thing`/`Room`/`Slab` bindings as "own," just iterated over all players and filtered (§3) |

No new field-level engine code should be needed for most of this — the missing piece is exactly the
filtering-and-aggregation layer, plus the one fixed/added reveal query from §1.

## 3. Filtering rule

For each tile, creature, room, trap, or door **not owned by the requesting player**, include it in the
snapshot only if:

- **Map tiles / slabs**: `map_block_revealed(mapblk, plyr_idx)` is true (or `_directly()` if
  `allies_share_vision` semantics are deliberately excluded for some query — default should be the
  ally-aware version, matching what the player would actually see in the UI).
- **Creatures**: standing on (or otherwise located at) a revealed subtile at snapshot time. No creature-
  specific stealth/invisibility layer was investigated here — if KeeperFX has creature-level concealment
  effects independent of tile reveal (e.g. invisibility spells), that's a follow-up check before this
  filtering rule is taken as complete; not found in the sources consulted for this document.
- **Rooms**: revealed if any of their constituent slabs are revealed (partial visibility — a room can be
  "partially known" the same way a human player experiences it).
- **Traps / doors**: gated additionally by their own per-thing `revealed` bit
  (`thing->trap.revealed`/`doortng->door.revealed`), independent of tile reveal — an unrevealed trap on a
  revealed tile stays hidden, matching real gameplay.
- **Own player's own state** (`plyr_idx == player being queried`) is never filtered — a player always sees
  their own dungeon completely, including undug/unrevealed-to-others areas of their own territory.

This is a direct translation of how the human UI already gates visibility (`01-integration-plan.md` §2.4's
"used pervasively for real gameplay gating" list); the snapshot builder's job is to apply the same rule
uniformly across every category in one pass, not to invent new visibility semantics.

## 4. Response shape (sketch, subject to the buffer-size discipline from `02-transport-and-protocol.md` §2)

```json
{
  "turn": 18042,
  "paused": true,
  "own": {
    "gold": 4820,
    "rooms": [ { "kind": "TREASURE", "slabs": 9, "efficiency": 0.8 }, ... ],
    "creatures": [ { "id": 1042, "kind": "IMP", "pos": [34,58], "health": 12, "state": "digging" }, ... ],
    "available": { "rooms": [...], "creatures": [...], "spells": [...], "traps": [...], "doors": [...] }
  },
  "map": {
    "revealed_own_territory": "<compact encoding, not one entry per subtile>",
    "revealed_enemy_tiles": [ { "pos": [40,60], "owner": 2, "kind": "WALL" }, ... ]
  },
  "visible_enemies": {
    "creatures": [ { "id": 2091, "owner": 2, "kind": "DEMONSPAWN", "pos": [41,61] }, ... ],
    "rooms": [ { "owner": 2, "kind": "LAIR", "slabs_seen": 3 }, ... ],
    "traps": [ { "owner": 2, "kind": "ALARM", "pos": [42,60] }, ... ]
  }
}
```

Compactness rules worth deciding before implementation (deferred to actual design, not this investigation):
map tiles are the dominant cost for a large map — a naive one-JSON-object-per-subtile encoding will blow
the response budget long before creature/room lists do. A run-length or per-slab (not per-subtile) encoding
of "revealed, owned-by-whom, kind" is the obvious compaction, matching the game's own `Slab`-granularity
data model rather than `Subtile`-granularity (`docs/data_structure.md`'s Slab→Subtile→Column→Cube
hierarchy) — a slab-level summary is both smaller and closer to what a Keeper actually "sees" as a unit.

## 5. Non-goals for this API

- **Not a generic Lua-equivalent query language.** `get_player_view` returns one fixed, curated shape per
  request (possibly with a few scoping parameters — e.g. "creatures only," "map only" — per
  `02-transport-and-protocol.md` §2's option 3), not an arbitrary field-path query. An agent that needs
  something this shape doesn't cover is a signal the shape needs to grow a new section, not that the API
  should become general-purpose.
- **Not a substitute for the config JSON spec's `read`/`describe`.** Static config (what a room/creature
  *type* is capable of, its base stats) stays `00-config-json-spec.md`'s job; this API is about live
  per-instance/per-player state only. A `get_player_view` response should reference config entities by
  name/kind (e.g. `"kind": "TREASURE"`) rather than re-embedding their static definitions.

## 6. Open items for implementation (not decided here)

1. Fix `lua_api_slabs.c`'s `"revealed"` binding to be player-parameterized, or add a separate,
   non-Lua-exposed C function the snapshot builder calls directly, leaving the existing (arguably
   already-relied-upon-by-some-script) Lua behaviour untouched? The latter is lower-risk; recommend it
   unless there's a reason to fix the Lua binding for its own sake.
2. Exact slab-level compaction encoding for `map.revealed_enemy_tiles` (§4) — needs a short follow-up
   investigation into typical revealed-tile counts for a mid-game skirmish dungeon to size the encoding
   against `02-transport-and-protocol.md`'s buffer budget realistically, rather than guessing.
3. Whether creature-level concealment beyond tile reveal exists (§3) and needs its own filter — flagged,
   not resolved, by this document.
