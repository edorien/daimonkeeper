# Skirmish setup tab — scope expansion and refined plan

Status: **investigation only, no code written.** Date: 2026-09-19. Builds on
[00-feasibility-setup-tab.md](00-feasibility-setup-tab.md); where the two disagree, **this file wins**
(specifically §2 replaces 00's §4 "merge problem" solution).

Scope added by the owner: **research**, **win/lose conditions** (the editor's new rules from commit
`849c5db84`), and **AI options** — the latter chosen with a longer-term goal of letting an LLM agent
(Claude or another) replace the built-in computer players, so the AI slot model must be designed for
that, not just for picking a preset.

Data below comes from scanning all 81 shipped multiplayer `map*.txt` scripts
(`core_files/multiplayer/*`) and reading the engine source; the scan script is throwaway.

## 1. New finding that changes the design: script version (`LEVEL_VERSION`)

**49 of 81 shipped skirmish scripts have no `LEVEL_VERSION`** and run as version 0
(`DEFAULT_LEVEL_VERSION 0`, `lvl_filesdk1.h:33`); 32 declare `LEVEL_VERSION(1)`. The version is a
*global that changes how every later line is parsed*, not metadata:

| Command | v0 | v1 |
|---|---|---|
| `CREATURE_AVAILABLE(p, c, a, b)` | `a` **ignored**, `b` = available, force = 0 (`lvl_script_commands_old.c:1261`) | `a` = available, `b` = force |
| command tables | old table (`lvl_script_commands.c:7114+`): `RESEARCH` acts as `RESEARCH_ORDER`, `COMPUTER_PLAYER(p, N)` takes a number only | new table: `RESEARCH` = update-or-add amount, separate `RESEARCH_ORDER`, `COMPUTER_PLAYER(p, N\|ROAMING\|OFF)` |
| `ALLY_PLAYERS` | always "allied", no lock | third arg is a bit-field |
| variable names in `IF` (`parse_get_varib(..., level_file_version)`) | v0 name set | v1 name set |

Shipped data confirms it: v1 files write `CREATURE_AVAILABLE(ALL_PLAYERS,TROLL,1,0)` (1475 times);
v0 files write `…,1,1` (526 times) — the same meaning ("available, not forced") in different
encodings. **The same `(1,1)` means "forced" in v1.**

Consequences:

1. **The setup text cannot simply be spliced into a script that the block does not share a version
   with.** Emitting v1 lines into a v0 script silently *disables every creature* (`b=0` → not
   available under v0 rules).
2. **(Resolved — see §12.1: half right)** Suspected existing bug in the editor's managed block (to verify with a test, not yet
   confirmed by running it): `editor_script_replace_managed_region()` inserts a fresh block "at the
   very start of the script, ahead of everything else". For a v1 script that means the block is
   parsed at v0 *before* `LEVEL_VERSION(1)` is read, so its `CREATURE_AVAILABLE` lines are
   misinterpreted; for a v0 script it emits v1-form lines. Either way availability written by the
   editor into an existing level may be wrong. New editor maps probably escape this only if the
   template writes `LEVEL_VERSION(1)` first — worth checking. A follow-up task has been flagged for
   the editor side; the shared module in this plan fixes it once and for both.
3. Reading (seeding the tab from the original script) must be version-aware for the same reason.

## 2. Revised merge design: "prelude" instead of text splicing

00 proposed generating a block and splicing it into the script text. The version finding makes that
fragile (where do you put it relative to `LEVEL_VERSION`; which encoding do you emit?). Better:

**The override is two parts, applied by the script loader itself:**

1. **Mask** — the original script with the tab-owned lines blanked (same strip rules as 00 §4:
   depth 0, not preceded by `NEXT_COMMAND_REUSABLE`). Line count is preserved (blank lines, not
   deletions) so script error messages keep their line numbers.
2. **Prelude** — a generated block, always written in **v1 syntax**, that `preload_script()` and
   `load_script()` scan *first* with `level_file_version` forced to 1, then reset to
   `DEFAULT_LEVEL_VERSION` before scanning the (masked) file so the file's own `LEVEL_VERSION` line
   behaves exactly as before.

Why this is better:

- One encoding to generate and test; no per-version emitters. v0 levels get v1 semantics for the
  prelude only.
- Ordering is by construction: prelude commands run before any file command, so no dependence on
  where the file's own header sits.
- The file's bytes are never re-emitted, so hand-written content cannot be corrupted; "other commands
  are copied" is literally true (they are still the file).
- The seam stays a single point: an override struct `{ lvnum, prelude, masked_text }` consulted from
  the two loop entry points in `lvl_script.c` (both go through one shared line-loop already —
  `parse_txt_data()` for preload, the inline loop in `load_script()`; these should be unified while
  we are there, they are near-duplicates).

Cost: the prelude commands parse in a context the file did not set up, so anything version-sensitive
in the *file* that our block *depends on* (nothing, by design — the prelude is self-contained) is
the only thing to watch. Limits: prelude lines add to `CONDITIONS_COUNT` / `WIN_CONDITIONS_COUNT` /
`DUNGEON_RESEARCH_COUNT` budgets — see §3.3 and §4.

**Runtime blocks stay.** 1264 setup-command occurrences sit inside `IF` blocks and 52 after
`NEXT_COMMAND_REUSABLE` in the shipped set; these are game logic (e.g. Herocove's Reaper-spell
cooldown toggles `MAGIC_AVAILABLE` at runtime) and must never be masked. Only 5390 depth-0 lines
(of which 629 occur *after* the first `IF`, i.e. genuinely scattered) are owned by the tab.

## 3. Research

### 3.1 What actually exists

- **Default research is not in the script.** It is `[research]` in `rules.cfg`
  (`Research = MAGIC POWER_HAND 250`, …), loaded per *campaign/mappack* into every player at config
  load (`config_rules.c:466-504`, `add_research_to_all_players`). **0 of the 81 shipped skirmish
  scripts contain `RESEARCH`/`RESEARCH_ORDER`** — skirmish research is entirely the campaign default
  today. So there is nothing to strip in shipped data, and nothing to "seed" from the script: the
  tab's seed is the live `rules.cfg` order/costs for the selected mappack.
- **Commands** (v1): `RESEARCH(player, TYPE, NAME, points)` — update the cost of an existing entry,
  else append; `RESEARCH_ORDER(player, TYPE, NAME, points)` — the *first* call for a player wipes
  that player's list and sets `research_override`, later calls append (`lvl_script_value.c`,
  `room_library.c:174-290`). So a full custom order is: `RESEARCH_ORDER` once per item, in order,
  per player. Types: `MAGIC`, `ROOM`, `CREATURE` (`research_desc`).
- Research only affects items that are **researchable but not yet buildable**: `research_needed()`
  (`room_library.c:199`) checks `room_buildable & 1 == 0` + `room_resrchable`, magic
  `magic_resrchable && level == 0`, creature `allowed && !force_enabled`. In script terms:
  `ROOM_AVAILABLE(p, room, 1, 0)` (researchable, not built) — i.e. **the availability grid's second
  column decides which items research even applies to**. A research row for an item the grid marks
  "buildable now" or "unavailable" is inert.

### 3.2 Design implications

- The tab's Research page must be **derived from the Availability page**: only rows where the item is
  "researchable, not yet buildable" are editable; the rest are shown greyed with the reason. Changing
  availability must re-derive research, not the other way round.
- Per-player vs global: `rules.cfg` order is global; the script commands are per-player. v1 of the tab
  should edit one order applied to `ALL_PLAYERS` (fewer lines, matches the campaign model) with an
  optional per-player override later.
- Emitting: if the order/costs equal the mappack default, **emit nothing** (zero behaviour change);
  only diff-from-default emits `RESEARCH_ORDER…` (order changed) or `RESEARCH…` (cost-only change,
  cheaper and doesn't set `research_override`).
- Budget: `DUNGEON_RESEARCH_COUNT` limits entries per dungeon; the tab must count and refuse to exceed
  it (the engine logs and silently drops the tail).
- **v0 quirk:** in the old command table `RESEARCH` behaves like `RESEARCH_ORDER`. Not an issue under
  the prelude design (always v1), which is another reason for it.
- A level whose `.lof`/campaign sets a research-room requirement (`RoRoF_Research`, checked in
  `process_player_research`) will not research at all without a research room; surface this as a hint
  when the availability page marks everything "buildable now".

### 3.3 Editor parity

The editor has no research UI today, so this is new shared code (parse/generate/seed/diff) for the
same module; add it once in the shared library and the editor can adopt it (Script > Research) as a
by-product. The editor's own scope needn't change for this plan.

## 4. Win / lose conditions

### 4.1 What the editor now does (commit `849c5db84`)

`editor_script_managed.h`: `WinLoseClause { player, variable, op, value }` and
`WinLoseRule { bool win; vector<clause> }`; written as plain `IF(PLAYERn, VAR op N) … WIN_GAME|LOSE_GAME
… ENDIF`, several clauses as nested `IF`s (all must hold). `editor_script_check_duplicate_win_lose()`
warns when a hand-written win/lose outside the block duplicates a managed rule. Round-trip tests exist
(`editor_script_managed_test.cpp`). These types are pure data — movable to the shared library with the
rest of the module (00 §5).

### 4.2 What shipped skirmish scripts contain (scan result)

- **All 207 `WIN_GAME` occurrences are the same shape:** depth 1,
  `IF(PLAYERn,ALL_DUNGEONS_DESTROYED == 1) / WIN_GAME / ENDIF`. **No `LOSE_GAME`. No block has any other
  content** (207 pure blocks, 0 impure). Multiple blocks per level (one per player: "player n wins when
  player n's enemies are all destroyed" — variable is checked on each player). One file has none.
- So masking win/lose is **easy and safe for the shipped set**: remove pure win/lose blocks
  (`IF … WIN_GAME|LOSE_GAME … ENDIF` with nothing else inside) — masking *whole blocks* (blank all
  their lines), never a lone `WIN_GAME` line. A block that contains anything else (possible in
  mod/community maps) is **left alone** and reported: the tab shows "N custom win/lose rules kept from
  the level script" plus an option to *add* rules rather than replace them. Never silently rewrite a
  block with mixed content.
- The tab needs three modes: **Level default** (mask nothing, no prelude rules — the default),
  **Replace** (mask the pure blocks, emit the tab's rules), **Add** (keep all, append the tab's rules).
- Budget: `WIN_CONDITIONS_COUNT` and `CONDITIONS_COUNT` are finite; each rule consumes one/several
  (nested `IF` per clause). Count and validate in the tab. The engine warns "No WIN GAME conditions in
  script file" (`lvl_script.c:980`) but the game then has no end — an empty rule set in Replace mode
  must be a **blocking** validation error, not a warning.
- Skirmish-specific semantics: victory should be defined per **human slot** vs **AI slots**, not just
  PLAYERn — the tab should offer templates ("last keeper standing", "destroy all enemy hearts",
  "survive N turns", "gold target") instantiated for the chosen slot roles, using the same
  variables the editor's clause dropdown already lists.
- Variable names are version-sensitive (§1); prelude is v1, so use v1 names and validate them with the
  same routine the editor's Verify Map uses.
- Lose-when-human-dies: today it is implicit via `ALL_DUNGEONS_DESTROYED` on the *opponents*; a
  skirmish that only defines "player 0 wins" leaves an AI winning path undefined. Offer an explicit
  `LOSE_GAME` template for the human slot; worth an ftest.

## 5. AI options — designed for a future LLM controller

### 5.1 Current mechanics (verified)

- **Who is AI:** `fe_computer_players = 1` (set in `frontend_freeplay_enter_resolve`, skirmish only) →
  `is_fe_computer_players_active()` (callback, `main.cpp:1097`) → after players are initialised,
  `setup_computer_players()` gives **every unoccupied player slot** `script_support_setup_player_as_
  computer_keeper(plyr, 0)` — **model 0, "General build, defend and attack"**, hard-coded
  (`thing_list.c:1277-1300`). There is no per-slot choice and no difficulty choice today.
- **Model table:** `keepcompp.cfg` `[computerN]` (17 entries incl. "Skirmish Defensive/Peculiar/
  Balanced/Rush", "Computer Assist…", "Rapid Gold Digging…"), `COMPUTER_MODELS_COUNT 64`, loaded per
  campaign/mappack (so the available list is **dynamic**; the tab must read `comp_player_conf`, not
  hard-code names). Skirmish-named presets already exist but the skirmish path never selects them.
- **Script control:** `COMPUTER_PLAYER(PLAYERn, model|ROAMING|OFF)` (shipped skirmish use: 11 lines,
  all `(PLAYER1,0)` in 4 maps; when present it re-initialises the slot at runtime:
  `computer_player_process`, `lvl_script_commands.c:6156`). Tuning commands: `SET_COMPUTER_GLOBALS`
  (7 values: dig stack, process interval, click rate, max room tasks, turn-begin delay, simulate-
  before-dig, min drop delay), `SET_COMPUTER_PROCESS`, `SET_COMPUTER_CHECKS`, `SET_COMPUTER_EVENT`,
  `COMPUTER_DIG_TO_LOCATION`, plus Lua equivalents (`lua_api.c:1565-1700`).
- **Player count/slots:** `.lof PLAYERS = 2..4` (shipped). Human slot = `default_loc_player`. Slots in
  `PLAYERS` beyond the human are AI candidates; slots with no heart cannot be AI (need a start
  position) — count hearts from the map, as the editor's Level Settings already does.

### 5.2 Design: a per-slot *controller* abstraction from day one

Do **not** model the tab's AI section as "AI preset dropdown". Model it as a slot table:

```
SlotSetup { role: Human | Controller; controller: Controller; team/ally; start_gold; max_creatures; ... }
Controller = Builtin{ model:int, tuning:{globals?, process/check/event overrides?} }
           | Roaming
           | Off (zombie: no AI, no play — for sandbox/testing)
           | External{ agent_id:string, params }     // future LLM; unimplemented in v1
```

- v1 ships `Builtin`/`Roaming`/`Off` only, but the data model, save format (presets) and the prelude
  generator already have the `External` variant reserved, so adding it later is additive.
- Prelude output for a slot: `COMPUTER_PLAYER(PLAYERn, model|ROAMING|OFF)` + optional
  `SET_COMPUTER_GLOBALS(PLAYERn, …)`. Because these are runtime-processed values, verify they take
  effect for a slot the frontend has *also* marked AI via `fe_computer_players` (double-init) — the
  4 shipped maps that do it show it works, but confirm ordering vs `setup_computer_players()`
  (which runs *before* `load_script`, `main_game.c:687` vs `540`) in an ftest.
- **Per-slot allies/teams** (`ALLY_PLAYERS`, v1 bit-field) belong to this table too (skirmish 2v2 is an
  obvious ask) — low cost, same generator, but out of the requested scope; keep the field.
- Global defaults: a "Difficulty/Style" quick-pick that fills every AI slot's model (Skirmish
  Defensive/Balanced/Rush exist), plus per-slot override.

### 5.3 What the LLM-controller future needs from this design (so v1 doesn't paint us in)

1. **A stable seat abstraction** — the `External` controller above. When implemented, the seat is
   started as `PlaF_CompCtrl` player with the built-in per-turn AI **disabled** (`comp->flags`/model
   with empty processes, or a new "externally driven" flag) so the built-in and the agent don't fight.
2. **Observation API** — read-only snapshot of a player's view (map knowledge respecting fog, own
   creatures/rooms/traps, gold, research state, visible enemies). Much of it exists behind the Lua API
   (`lua_api_player.c`, `lua_api_things.c`); the question to investigate later is fog-of-war
   correctness (an agent must not see what a human couldn't) — that is a *design* item, not a code
   detail.
3. **Action API** — the built-in AI acts by calling sim functions directly, **not** through packets, so
   it is deterministic per peer but also invisible to packet recording. An LLM's actions should go
   through **packets** (`PckA_*`, as the editor already does) so they are (a) recorded/replayable,
   (b) rate-limited to sim turns, (c) validated like human input. That means an "AI-driven human
   player" seat rather than a `PlaF_CompCtrl` seat — a materially different (and probably better)
   mechanism; decide before building the `External` variant.
4. **Latency model** — LLM replies take seconds; the sim is turn-based real time. Needs either pause-
   on-think, or an action queue applied over subsequent turns, with a documented time/tokens budget per
   agent. Skirmish is single-machine (`fe_network_active = 0`) so nondeterminism from the agent
   isn't a sync risk *if* actions arrive as packets (they become the recorded input stream).
5. **Sandbox/permissions** — seat spec should carry which agent, which model, and be logged; nothing
   in v1 needs it but the preset file format should already have a free-form `params` map.

None of this is built now; it is why §5.2's model is a table and not a dropdown.

## 6. Updated architecture

The shared library (00 §5; `kfx_config` recommended) now holds: setup values (existing), **research**
model, **win/lose** rule model, **slot/controller** model, the **version-aware reader** (seed), the
**mask** transform, and the **prelude generator**. Suggested split of files (all pure, all
Catch2-testable, no ImGui, no sim state):

| File | Role |
|---|---|
| `script_setup_model.h` | structs: setup values, availability, research, win/lose, slots/controllers |
| `script_setup_reader.cpp` | version-aware parse of a script → model (seed), tracks nesting, comments |
| `script_setup_mask.cpp` | line-preserving mask of tab-owned lines (setup, pure win/lose blocks, research) |
| `script_setup_prelude.cpp` | model (+ diff against seed/defaults) → v1 prelude text |
| `script_setup_validate.cpp` | budgets (conditions, win conditions, research count), inert-research hints, empty-win-set error |

`kfx_sim`: `level_script_override` (set/clear/lookup keyed by lvnum); `kfx_game`: `lvl_script.c` consumes
it (single unified loop). `kfx_frontend`: `SkirmishSetup` state + tab UI. `kfx_editor`: re-points to the
shared module (its own managed block keeps working; it stays a *saved* representation, this plan's
override is *ephemeral*).

## 7. Revised phasing

| Phase | Work | Notes |
|---|---|---|
| S0 | **Verify the suspected editor bug** (§1.2) with a Catch2 test; fix in the shared module rather than the editor copy | small; flagged as a separate task |
| S1 | Move module to the shared library; layering check | small |
| S2a | Version-aware reader + mask (setup, research, win/lose) with **corpus tests over all 81 shipped scripts**: mask keeps every non-owned line byte-identical; reader→generator→reader is stable; v0 and v1 files yield identical models for equivalent input | the real work |
| S2b | Prelude generator + validators | medium |
| S3 | Unify `preload_script`/`load_script` line loops; override seam with forced-v1 prelude; clear-on-leave | small–medium; touches core loop → run existing ftests |
| S4 | Setup tab UI: General, Availability, **Research**, **Win/Lose**, **Slots & AI** | medium–large |
| S5 | ftests: gold/pool/availability/research/win-rule/AI-model each verified in the running sim; a v0 map and a v1 map | medium |
| S6 | Presets (save/load a setup as a file); AI tuning fields (`SET_COMPUTER_GLOBALS`) | small |
| S7 (future) | `External` controller: observation + packet-based action API; own investigation doc | separate |

## 8. Issue log (things found that could bite)

| # | Issue | Severity | Mitigation |
|---|---|---|---|
| 1 | v0/v1 script semantics differ for the very commands in scope (`CREATURE_AVAILABLE`, `RESEARCH`, `COMPUTER_PLAYER`, `ALLY_PLAYERS`, variable names) | **high** | forced-v1 prelude; version-aware reader; corpus tests |
| 2 | Editor's managed block inserted before `LEVEL_VERSION` (suspected, unverified) | high (existing) | S0 test + fix; separate task |
| 3 | Additive commands (`START_MONEY`, pool, trap/door) stack with original lines | high | mask depth-0 originals |
| 4 | Research is inert unless availability says "researchable, not built"; default lives in `rules.cfg` not the script | medium | derive research page from availability; seed from live config |
| 5 | Empty win set = game with no end; engine only warns | medium | blocking validation |
| 6 | Mixed-content win/lose blocks in community maps | medium | never rewrite; Add-mode, report |
| 7 | Budgets: `CONDITIONS_COUNT`, `WIN_CONDITIONS_COUNT`, `DUNGEON_RESEARCH_COUNT`; engine truncates silently | medium | count in validator |
| 8 | `setup_computer_players()` runs before `load_script`; a script `COMPUTER_PLAYER` re-inits the slot | medium | ftest ordering; document |
| 9 | AI model list is per-mappack (`keepcompp.cfg`) and may contain fewer than 17 or custom entries | low | read `comp_player_conf` |
| 10 | Slots without a heart can't be AI | low | derive from map hearts |
| 11 | 4 Lua-only skirmish maps (`map07001–4.lua`) | medium | disable tab (00 §6); revisit after editor's Lua support |
| 12 | Override leaking to a later level | high | clear-on-leave + lvnum key + ftest |
| 13 | `preload_script` and `load_script` are near-duplicate loops; an override must hit both identically | medium | unify in S3 |
| 14 | LLM seat design decisions (§5.3 — CompCtrl vs packet-driven seat; fog; latency) | future | document now, decide in S7 |

## 9. Open questions for the owner

1. **Research granularity for v1:** one order for all players (recommended) or per-player from day one?
2. **Win/lose modes:** are "Level default / Replace / Add" the right three, or should Replace be the only
   non-default mode?
3. **AI seat for the future LLM:** prefer a *packet-driven* seat (agent acts like a human through the
   normal input path — replayable, rate-limitable) over an in-sim `CompCtrl` seat? It affects how
   much of the built-in `Computer2` machinery the tab needs to expose.
4. **Team/ally per slot** (2v2): include in v1 since the slot table has room for it?
5. Confirm the prelude design (§2) is acceptable — it is a slightly larger core-loader change than
   pure text splicing, in exchange for removing the version hazard.

## 10. Owner decisions (2026-09-19) — these supersede §3, §4.2 modes and §9

1. **Research is not editable in the tab.** The order and costs stay controlled by the mappack's
   `rules.cfg`. Consequences: no research model, no `RESEARCH*` generation, no research mask; §3's
   findings survive only as a **read-only hint** on the availability page ("researchable, not built"
   items are governed by the mappack's research list; research needs a research room). Any
   `RESEARCH*` lines in a level script are left untouched (none in the shipped skirmish set).
2. **Win/lose: keep / replace only.** No "add" mode.
3. **Defaults come from the level script; no keep/replace/add distinction needed.** The reader parses
   the level's static setup into the model and the tab always shows it; the override is *the model
   regenerated in canonical v1*. Untouched tab = no override installed (byte-identical behaviour).
   See §10.1 for where this stops being lossless.
4. **LLM seat:** do not preclude it (§5.2's controller table stays, `External` reserved and
   unimplemented), but it is a very large task with many unfinished precursors — no work on it here,
   and no v1 scope depends on it.

### 10.1 What "parse everything, regenerate as v1" can and cannot cover

Fine for the **owned command classes** — setup, availability, win/lose rules, computer-player
commands — because each has a small, closed grammar, and the reader translates v0 → the model
(e.g. v0 `CREATURE_AVAILABLE(p,c,_,avail)` → `available=avail, force=0`) and the generator only ever
writes v1. A future v2 changes one generator, not the reader, tests, or UI. Keep the reader's
per-version translation table explicit and corpus-tested.

Not fine to extend to the **whole script** (rest of §2 still stands):

- Everything outside the owned classes (timers, parties, `ALLY_PLAYERS`, messages, `IF` variable
  names, action-point logic …) has version-dependent meaning too, and translating each command is
  a much larger, riskier project. That residual is passed through verbatim **under its own
  version**, which is why the prelude runs in forced v1 and the version is restored before the
  residual. (A general "upgrade script to v1" tool is a separate editor feature, if ever wanted.)
- The seed is the level's **static** (depth-0) state. Setup commands inside `IF` blocks or after
  `NEXT_COMMAND_REUSABLE` (1264 + 52 occurrences in the shipped set) are runtime logic; they stay in
  the residual and can still change values after the prelude. The tab should say so where it matters
  (e.g. Herocove's runtime-toggled Reaper spell).
- **Win/lose blocks that don't fit the model** (mixed content, unrecognised conditions) cannot be
  round-tripped. With keep/replace only: *keep* leaves the level's rules; *replace* removes only
  blocks the reader fully understood and lists any it could not parse as "custom rule kept — cannot
  be replaced from this tab". Shipped data has none such (207/207 pure), so this only affects
  community maps. Replace with zero rules left is a blocking error (§4.2).

### 10.2 Plan deltas

- S2a/S2b: drop research from the model/mask/generator; reader keeps a v0→model translation table for
  the owned classes.
- Tab UI (S4): General, Availability (+ read-only research hint), Win/Lose (keep|replace), Slots & AI.
- Open items: 2v2 team/ally per slot in v1 (unanswered); prelude loader change (assumed accepted).

## 11. Suitability flag (owner question, 2026-09-19)

**Viable, and cheap** — it falls out of the reader/mask pass (§2, §10.1) with no extra parsing: the
reader already classifies every owned command as *static* (depth 0, not `NEXT_COMMAND_REUSABLE`) or
*runtime* (inside `IF`, or reusable). A level is unsuitable exactly where a runtime command targets
something the tab would let the player change.

Prototype over the shipped corpus (throwaway script, runtime = owned command inside `IF`/reusable):

| Mappack | Levels | No runtime setup | Runtime setup found |
|---|---|---|---|
| original | 45 | 44 | 1 (`map00150`: 4× `ROOM_AVAILABLE`) |
| classic | 15 | 14 | 1 (`map00055`: 8× `MAX_CREATURES`) — plus 4 Lua-only maps, no `.txt` |
| dk2maps | 11 | 0 | 11 (4× `MAGIC_AVAILABLE` each — the Reaper toggle) |
| biervampir | 10 | 0 | 10 (~120× `CREATURE_AVAILABLE`, 5× `ROOM_AVAILABLE`, `COMPUTER_PLAYER`) |

So the split is very clean: original/classic are almost entirely static (matching the owner's point),
dk2maps and biervampir are runtime-driven throughout.

**Recommended: three levels of verdict, computed per level and cached with the mappack listing.**

1. **Supported** — no runtime writes to owned keys. Full tab.
2. **Partial** — some `(command, player, item)` keys are runtime-controlled (dk2maps: only the Reaper
   row per player; `map00055`: `MAX_CREATURES`). The tab stays enabled, but those *specific rows/fields*
   are locked with a "controlled by level script" marker and excluded from the prelude. This is better
   than disabling the whole level — dk2maps stays largely customisable (gold, pool, every other
   spell/room/creature) while the one toggle keeps working. Key = `(kind, player-or-ALL, item)`; a
   runtime `ALL_PLAYERS` write locks that item for every player.
3. **Unsupported** — tab disabled with a reason: no `.txt` (Lua-only), unparseable/unknown `LEVEL_VERSION`,
   or so many runtime-owned keys that little remains editable (threshold to be tuned; biervampir, where
   the creature list is runtime-managed, is the likely case — decide after the corpus run whether it is
   "Partial with creatures locked" or "Unsupported").

Extras worth adding:

- **Author override in the `.lof`** (e.g. `SKIRMISH_SETUP = ALLOW | LOCKED`, absent = auto-classify):
  the map author, who knows the runtime logic, gets the final say; auto-classification is only the
  fallback. Costs one keyword in the `.lof` parser; `LOCKED` is the safe choice for maps with logic
  the static scan can't see.
- **Blind spots the scan cannot see:** Lua alongside a `.txt` (warn: detect `map*.lua`), values changed
  through `SET_FLAG`/`IF` chains that don't name a setup command directly (e.g. flags that gate unlock
  logic) — the static classifier catches direct writes only. `LOCKED` in the `.lof` is the escape hatch.
- **Informational only:** `ADD_CREATURE_TO_LEVEL` spawning (all of dk2maps/biervampir) is game logic
  that doesn't conflict with setup values; don't count it against suitability.
- The existing skirmish/scenario split can serve as the coarse gate (scenario = not offered the tab),
  with the classifier as a second, per-level check inside skirmish mappacks.
- Testing: the corpus test asserts the verdict per level (table above becomes the golden expectations),
  so a reader change that alters classification fails CI.

## 12. Resolutions (2026-09-20)

### 12.1 Editor version-bug check — result, and a correction to §1/§2

The spawned check (commit `1581f2bda`, 20 cases / 86 assertions under `[script_managed]`) found:

- **Block before `LEVEL_VERSION(1)` is harmless** — my §1.2 suspicion was wrong. `preload_script()`
  scans the whole file first and `LEVEL_VERSION` is one of the commands it runs, so the version is
  the file's *final* declared version; `load_script()` then parses **every line, including lines
  above the `LEVEL_VERSION` line, at that version** (nothing resets it). The version is therefore a
  **whole-file property, last declaration wins**, not "applies from this line on".
- **v1 forms written into a v0 script: confirmed, creatures only.** v0 `CREATURE_AVAILABLE` reads
  the 4th arg; the editor's "available" cell wrote `(1,0)` → creature disabled. Room/magic/trap/door
  availability, `START_MONEY`, `MAX_CREATURES`, pool, `SET_GENERATE_SPEED` and `IF/WIN_GAME/
  LOSE_GAME/ENDIF` are identical in both versions. Fixed in the editor
  (`editor_script_level_version()`, optional `level_version` on generate/parse; v0 writes `(a,a)`, the
  force flag is inexpressible at v0 and reads back 0).
- **Open caveat from that check:** `variable_desc` (v1) and `dk1_variable_desc` (v0) are different
  tables; a win/lose rule using a v1-only variable name is rejected at v0. Not audited.

Effects on this plan:

1. **Reader:** derive the file version with the same rule (scan whole file, last `LEVEL_VERSION`
   wins) and apply it to *all* lines when classifying — reuse `editor_script_level_version()` (moves
   with the module, §6).
2. **Prelude (§2) — correct the restore step:** save `level_file_version`, force 1, scan the
   prelude, then **restore the saved value** (not `DEFAULT_LEVEL_VERSION`). In the preload pass the
   saved value is whatever the file has declared so far; in the load pass it is the final version
   preload left behind. Getting this wrong would parse the whole file at v0 (or v1) incorrectly.
3. **Win/lose seed for v0 files:** the reader must map `dk1_variable_desc` names → model names (the
   prelude always writes v1 names). Audit both tables for names that differ or exist in only one;
   unmappable v0 variables make that rule "custom, kept" (§10.1), never guessed.
4. Because most owned commands are version-identical (above), the reader's per-version translation
   table is small: essentially `CREATURE_AVAILABLE` and the variable-name table. The corpus tests
   still assert v0 and v1 files yield identical models for equivalent input.
5. The editor now has a partial private copy of this logic; when the module moves (S1) the editor's
   fix moves with it — do not fork.

### 12.2 Owner decisions (answers to §9 / earlier open questions)

- **Prelude loader change: accepted** (incl. unifying the two line loops).
- **2v2 team/ally per slot: in v1, kept small** — one ally-group choice per slot, generating
  `ALLY_PLAYERS` in v1 form (bit-field third argument) in the prelude; no per-pair lock UI.
- **`.lof` keyword `SKIRMISH_SETUP = ALLOW | LOCKED`: approved** (§11); absent = auto-classify.
- **Shared module location: `kfx_config`**, consistent with the other script/config parsers.
- **Lose condition is optional.** Heart-destroyed loss is hard-coded in the engine, so a skirmish
  needs no explicit `LOSE_GAME`; the tab offers it as an extra rule, never requires it, and never
  emits one by default. The blocking "empty win set" error (§4.2) still applies to **win** rules in
  Replace mode.
- **biervampir classification:** still to be settled from the full corpus run (§11) — remains
  the only open classification question.

### 12.3 Saved games — needs a test (owner request)

What the code shows: `load_game_chunks()` (`game_saves.c`) restores the gameplay state from chunks
and only re-opens **Lua** (`SGC_LuaData`); it does not call `preload_script()`/`load_script()`. But
the level-init path a loaded save goes through (`main_game.c:457`, `preload_script(level)`) *does*
re-read the `.txt` from disk, with the override already cleared. Not yet confirmed:

1. Whether a save-load actually runs that `preload_script()` (and with which script text).
2. That skipping the override at that point changes nothing: only preload-active commands run there,
   so prelude commands should be inert, but `LEVEL_VERSION`, condition/win-condition counts and any
   script-value tables built at preload must not diverge from what the saved state assumes.
3. That gold, availability, pool, max creatures, win rules and AI models from the override are all
   restored from the save blob (they are sim state, so expected), and win rules still fire.

**Test to add to S5 (`src/ftests/`):** start a skirmish with an override (non-default gold, one room
disabled, a custom win rule, non-default AI model on a slot) → save → return to menu (clears
override) → load the save → assert every value above in the running sim, that the win rule still
ends the game, and that the log has no script warnings from the second `preload_script()`. A second
case: save on an *un-overridden* skirmish level to confirm the baseline path is unchanged.
Add: the override must be cleared *before* that load, so the test also proves the save doesn't depend
on it.

### 12.4 Remaining open items

- biervampir verdict (Partial-with-creatures-locked vs Unsupported) — decide from the full corpus run.
- `dk1_variable_desc` vs `variable_desc` audit (12.1.3).
- Save-load preload behaviour (12.3) — verify by reading the load path, then by test.
- Ordering of `setup_computer_players()` (before `load_script`) vs a prelude `COMPUTER_PLAYER` — ftest.
- `.txt` + `.lua` levels: Lua still applies on top; warn in the tab.

Everything else in §9 is closed. Implementation can start at S1/S2a (parser move + reader/mask
with corpus tests); nothing blocks it.

## 13. Variable-name audit (`variable_desc` v1 vs `dk1_variable_desc` v0) — done 2026-09-20

Method: parsed both tables from `lvl_script_commands.c` (`variable_desc` l.300, 58 names;
`dk1_variable_desc` l.367, 21 names), then cross-checked every `IF` variable used in the 81 shipped
skirmish scripts against the table for that script's effective version (§12.1 rule).

### 13.1 Table differences

| Class | Names |
|---|---|
| **Identical name, identical meaning (20)** | `ALL_DUNGEONS_DESTROYED`, `BATTLES_LOST`, `BATTLES_WON`, `CREATURES_ANNOYED`, `CREATURES_SCAVENGED_GAINED`, `CREATURES_SCAVENGED_LOST`, `DOORS_DESTROYED`, `DUNGEON_DESTROYED`, `GAME_TURN`, `GOLD_POTS_STOLEN`, `MONEY`, `ROOMS_DESTROYED`, `SPELLS_STOLEN`, `TIMES_BROKEN_INTO`, `TOTAL_AREA`, `TOTAL_CREATURES_LEFT`, `TOTAL_DOORS`, `TOTAL_GOLD_MINED`, `TOTAL_RESEARCH` (+ `TOTAL_CREATURES` is the exception below) |
| **Alias** (v0-only spelling, same variable) | `TOTAL_IMPS` (v0) = `TOTAL_DIGGERS` (v1, `SVar_TOTAL_DIGGERS`). Translate v0 → v1 spelling. |
| **Same name, different meaning** | `TOTAL_CREATURES`: v0 → `SVar_CONTROLS_TOTAL_CREATURES` (active creatures **minus** those in enemy custody/dying, `lvl_script_conditions.c:339`); v1 → `SVar_TOTAL_CREATURES` (all active creatures, l.178). The v1 equivalent of the v0 meaning is `IF_CONTROLS(…, TOTAL_CREATURES …)`, not `IF`. |
| **v1-only (38, rejected at v0)** | `ACTIVE_BATTLES, BONUS_TIME, CONTROLLED_THING, CREATURES_CONVERTED, CREATURES_FROM_SACRIFICE, CREATURES_SACRIFICED, CREATURES_TRANSFERRED, CURRENT_SALARY, DOORS_SOLD, EVIL_CREATURES, EVIL_CREATURES_CONVERTED, GHOSTS_RAISED, GOOD_CREATURES, GOOD_CREATURES_CONVERTED, HEART_HEALTH, KEEPERS_DESTROYED, MANAGE_SCORE, MANUFACTURED_SOLD, MANUFACTURE_GOLD, PLAYER_SCORE, SCORE, SKELETONS_RAISED, TIMES_ANNOYED_CREATURE, TIMES_LEVELUP_CREATURE, TIMES_TORTURED_CREATURE, TOTAL_DIGGERS, TOTAL_DOORS_MANUFACTURED, TOTAL_DOORS_USED, TOTAL_MANUFACTURED, TOTAL_SALARY, TOTAL_SCORE, TOTAL_SLAPS, TOTAL_TRAPS, TOTAL_TRAPS_MANUFACTURED, TOTAL_TRAPS_USED, TRAPS_SOLD, VAMPIRES_RAISED, VIEW_TYPE` |
| v0-only names other than the alias | none |

Names *not in either table* are not necessarily errors: `parse_get_varib()` falls through to
creature names, room names, `TIMERn`, `FLAGn`, door/trap names, campaign flags and
`BOXn_ACTIVATED`/`TRAPn_ACTIVATED`/`SACRIFICED[…]`/`REWARDED[…]` **for both versions**
(`lvl_script_commands.c:~515-560`). Those resolve identically at v0 and v1 (subject to the separate
v0 `IF`→controls conversion in `lvl_script_commands_old.c:~1190`, which applies to creature/door/
trap/room counts).

### 13.2 What the corpus uses

- Every win rule (207/207) uses `ALL_DUNGEONS_DESTROYED` — present, same meaning, in both tables.
- All other `IF` variables in the corpus are fall-through names (`FLAGn`, `TIMERn`, `BOXn_ACTIVATED`,
  creature/room names like `IMP`, `REAPER`, `PRISON`, `WORKSHOP`, `ENTRANCE`) plus `GAME_TURN` and
  `VIEW_TYPE` (v1 file, v1 name). **No shipped script uses a v1-only table name at v0, or
  `TOTAL_CREATURES`/`TOTAL_IMPS` at all.** So no shipped level is affected today.

### 13.3 Decisions this drives

1. **Win/lose rule variables are whitelisted, not free text.** The model accepts a variable only if
   it is in the *identical-meaning* set (plus the `TOTAL_IMPS`→`TOTAL_DIGGERS` alias). Anything else in
   a script's win/lose clause → that rule is "custom, kept" (§10.1), never guessed. Practical v1
   whitelist for the tab's rule editor: `ALL_DUNGEONS_DESTROYED`, `MONEY`, `GAME_TURN`,
   `TOTAL_GOLD_MINED`, `TOTAL_AREA`, `TOTAL_DOORS`, `DUNGEON_DESTROYED`, `TOTAL_CREATURES_LEFT`, and the
   others in the identical set as wanted by UI.
2. **`TOTAL_CREATURES` is excluded from the seed/round-trip at v0** (different meaning). A v0 rule
   using it stays "custom, kept". For *new* rules the tab may offer it, written as v1 `IF` (all
   active creatures) with the tooltip stating that meaning — or offer the `IF_CONTROLS` form later.
3. **v1-only variables are only ever offered for new rules**, never read from a v0 file (a v0 file
   can't contain them — the engine would have rejected the line). Since the prelude is always v1
   (§2), the tab can use them freely; this is the one place the forced-v1 prelude *adds*
   capability over the file's own version.
4. **The editor's existing dropdown is unaffected for v1 scripts but wrong for v0.**
   `editor_script_collect_name_groups()` (`editor_script_names.cpp:45`) and the Level Settings clause
   combo offer all of `variable_desc`; for a v0 script the editor writes `IF` clauses with v1-only
   names into a v0 file, which the engine then rejects ("Unknown variable name") and — since a
   rejected `IF` still opens a block — can leave the block unbalanced. Not triggered by shipped data;
   it needs the same version-awareness as the availability fix (`1581f2bda`): filter the clause combo
   to the identical-meaning set when the script is v0 and disallow `TOTAL_CREATURES`. Flagged as a
   follow-up (editor task, see below).
5. **Layering:** the tables live in `kfx_game`, but the shared module goes in `kfx_config` (§12.2) and
   must not include `kfx_game`. So the module carries its **own small constant whitelist**
   (name → canonical v1 name), and correctness is enforced by a test in `kfx_game`/`kfx_editor` tests
   (which may see both) asserting, for every whitelisted name, that `parse_get_varib(name, …, 0)` and
   `(…, 1)` resolve to the same `SVar_*` id (after alias translation). A future table edit that breaks
   equivalence then fails CI instead of silently mis-seeding.
6. The reader must also reproduce the v0 `IF` count-semantics conversion (creature/room/door/trap
   names count differently at v0) *only if* those names are ever modelled — they are not in v1 scope
   (rules are restricted to the whitelist), so no work here beyond leaving such rules as "custom".

### 13.4 Follow-ups

- Editor: make the win/lose clause combo (and Script > Names "Variables" group) version-aware; add
  a test mirroring the `CREATURE_AVAILABLE` ones. Small; separate from this plan.
- Module test (13.3.5): per-name equivalence across `variable_desc`/`dk1_variable_desc` via the real
  `parse_get_varib`.
- Open item "variable-name audit" (§12.4) is **closed**.

## 14. Save-load path check (code reading) — done 2026-09-20

Closes §12.3 items 1–2 by reading the code; item 3 (values survive) is confirmed structurally below,
and the ftest in §12.3 remains the way to lock it in.

### 14.1 Findings

1. **Loading a save never re-reads the level script.** The load entry is `FeSt_LOAD_GAME`
   (`game_session_loop.cpp:934`) → `load_game()` → `load_game_chunks()` → `reinit_level_after_load()`
   (`main_game.c:194`). None of these call `init_level()`, `preload_script()` or `load_script()`
   (those are on the *new game* path: `init_level()` `main_game.c:391` → `preload_script` l.457, and
   `post_init_level()` l.530 → `load_script` l.540). **My §12.3 worry — that a save load runs
   `preload_script()` with the override already cleared — does not apply.** The only script thing
   re-opened on load is **Lua** (`SGC_LuaData`, `game_saves.c:451-463`), by level number, from disk.
2. **All effects of the prelude are in the save blob**, because they are ordinary sim/game state
   that the chunk writer memcpy's wholesale:
   - `kfx_game_state.script` (`struct LevelScript`, `lvl_script.h:155`) — `values[]` (script values
     still pending/reusable), `conditions[]`, `win_conditions[]`, `lose_conditions[]`, party/tunneller
     triggers, level `strings[]` — saved as `SGC_KfxGameState`. So a prelude win rule is a
     `win_conditions[]` entry + a `conditions[]` entry and is restored intact.
   - Dungeon state in `SGC_KfxSimState` (gold, `max_creatures_attracted`, availability/buildable
     arrays, creature pool, research list, generate speed) — the executed setup commands wrote
     directly into these.
   - `kfx_sim_state.computer[]` (`Computer2` per player, `kfx_sim_state.h:246`) — model, processes,
     checks, events and `SET_COMPUTER_*` tuning; `restore_computer_player_after_load()`
     (`player_computer.c:1629`) only re-points `comp->dungeon` and zeroes slots for non-existent/
     inactive players; it does **not** rebuild processes from the model template, so per-slot tuning
     survives a load.
3. **Consequence for the design:** a saved skirmish is fully self-contained; the override needs no
   persistence, no reload hook, and clear-on-leave (§7) cannot break a later load. **Lower risk than
   assumed; §7's "confirm" item is resolved.**

### 14.2 Remaining edge cases (pre-existing, but the prelude must not worsen them)

- **`level_file_version` is a process-global and is not saved.** After loading a save in a fresh
  process it is `DEFAULT_LEVEL_VERSION` (0) regardless of the level's real version. Anything that
  parses script text *at runtime* afterwards — Lua `RunDKScriptCommand`, console script commands —
  then parses at v0. This is an existing quirk (independent of the override) and only matters for a
  v1 level that runs script text after load. The prelude changes nothing here **provided the loader
  restores `level_file_version` after the forced-v1 prelude (§12.1.2)** — do not leave it at 1.
  Worth an ftest line, and worth reporting as a separate engine issue (save/restore the value, or
  re-derive it from the level on load).
- **Lua is re-opened from disk by level number.** A skirmish level with a `.lua` alongside its `.txt`
  keeps running its original Lua after a load whether or not the setup was overridden — consistent with
  the "warn on `.txt` + `.lua`" item.
- **Older saves** made before this feature contain no override marker and need none — same reason.
- The recorded-packet path (`startup_saved_packet_game`, packet demos) is separate: a demo replays
  packets against a level start, so it *would* need the identical override text to reproduce a game
  that started with one. **Decision:** store the override (or its hash + prelude text) in the packet
  file header so demos of overridden skirmishes replay correctly, or explicitly mark such games
  non-replayable in v1. Not investigated further; flagged.

### 14.3 Test plan update (§12.3)

The ftest still applies but its purpose changes from "prove preload doesn't diverge" to "prove state
round-trips": start overridden skirmish → save → menu → load → assert gold, availability, pool, max
creatures, win rule fires, AI slot model + one `SET_COMPUTER_GLOBALS` value, and
`level_file_version` handling. Drop the "no script warnings from a second `preload_script()`"
assertion — there is no second call.

### 14.4 Open items after this check

- biervampir verdict (needs the full corpus run).
- `setup_computer_players()` vs prelude `COMPUTER_PLAYER` ordering ftest.
- `.txt` + `.lua` levels warning.
- Packet-demo/replay policy for overridden games (14.2, last bullet).
- (Separate issue, optional) unsaved `level_file_version` global.

## 15. biervampir verdict and the per-key lock rule — done 2026-09-20

Extended the §11 prototype to key runtime writes by `(kind, player, item)` and to record the `IF`
context each one sits under (throwaway script; static = depth 0, runtime = inside `IF` or after
`NEXT_COMMAND_REUSABLE`).

| Mappack | Levels | Static keys / level | Runtime-controlled keys / level | Runtime contexts |
|---|---|---|---|---|
| original | 45 | 26–53 | 0 (one level: 4) | one-off |
| classic | 15 | 47–58 | 0 (one level: `MAX_CREATURES` ×8) | one-off |
| dk2maps | 11 | 52–55 | **2** (the Reaper spell, one per player) | `IF(Pn,REAPER == 1)` / `IF(Pn,FLAG7 == 0)` |
| biervampir | 10 | 42 | **54** (creatures, per player) | `IF(Pn,FLAG7 == 0/1)` ×72, `IF(Pn,BOX1_ACTIVATED>0)`, `BOX2…` |

### 15.1 What biervampir actually is

A **faction-choice map, not a plain skirmish.** At start each player is offered "boxes"
(`SET_BOX_TOOLTIP(1,"Play as Keeper")`, …); activating a box runs a block that sets the player's
creature availability for that faction (`CREATURE_AVAILABLE(PLAYER0,FLY,1,0)` ×~20), sets flags and
prints a message. A random-faction path (`SET_FLAG(PLAYERn,FLAG7,DRAWFROM(0,1))`) drives the same
blocks. It also decides the AI itself at runtime:
`IF(PLAYER1,VIEW_TYPE == 0) … COMPUTER_PLAYER(PLAYER1,0) … ENDIF` ("Check if Blue is AI in case of
-1player mode"), plus `SET_GAME_RULE(PayDaySpeed,0)`. So for these maps: creature availability is
determined by the player's in-game faction choice (a tab edit would be overwritten the moment a box is
chosen), and PLAYER1's AI model is script-decided.

### 15.2 Verdict — decided

**biervampir = Partial, not Unsupported.** Rationale: the locks are exactly the per-key rule already in
§11 and the remainder is genuinely editable (start gold, max creatures, generation speed, pool, every
room/spell/trap/door row, win rules). Consequences:

1. **Unsupported is reserved for structural reasons only:** no `.txt` (Lua-only), unreadable/unknown
   `LEVEL_VERSION`, script fails the reader. Never for "many runtime keys" — drop the tuning-threshold
   idea from §11.3.
2. **Lock granularity:** `(kind, player, item)`, with `ALL_PLAYERS` runtime writes locking the item for
   every player. For biervampir that locks the creature grid for PLAYER0/PLAYER1 wholesale, in effect.
   UI: the creature page shows every row disabled with one banner ("Creatures are chosen in-game on
   this level") rather than 40 individual "locked" markers — **a page-level banner when >50% of a
   grid's rows are locked, per-row markers otherwise.**
3. **New lock class — AI controller:** a `COMPUTER_PLAYER`/`SET_COMPUTER_*` (or `ALLY_PLAYERS`) command
   under any condition/reusable locks that *slot's* controller (and ally group) in the Slots page:
   shown greyed with "decided by the level script". Here: PLAYER1 in biervampir. Static top-level
   `COMPUTER_PLAYER` (the 4 shipped maps in §5.1) are *not* locks — they are defaults the tab seeds
   from, then replaces via the mask (same as any static setup line).
4. **Informational tag (no lock):** levels that use in-game selection mechanics (`SET_BOX_TOOLTIP`,
   `BOXn_ACTIVATED` conditions) or global rule edits (`SET_GAME_RULE`) get a one-line note
   ("This level has in-game faction/rule choices; some setup is applied when you choose"). Detect by
   command name; cheap and explains why some rows are locked.
5. **Author override still wins** (`SKIRMISH_SETUP = LOCKED` in the `.lof`, §11): a map like this could
   simply declare itself locked; auto-classification is the fallback.

### 15.3 Test expectations (golden, from this run)

The corpus test (S2a) asserts, per mappack: original/classic → Supported except the two levels above
(Partial); dk2maps → Partial, exactly the Reaper key per player locked; biervampir → Partial, all 54
creature keys locked, PLAYER1 controller locked, banner tag present; classic Lua-only (map07001–4) →
Unsupported (structural). Any reader change that shifts these fails CI.

### 15.4 Open items after this

- `setup_computer_players()` vs prelude `COMPUTER_PLAYER` ordering (ftest) — **now sharper**: biervampir
  does `COMPUTER_PLAYER(PLAYER1,0)` under a runtime `IF` *and* skirmish sets `fe_computer_players`; test
  both paths agree, and that a tab-chosen model for a *non-locked* slot isn't overwritten by such
  runtime blocks.
- `.txt` + `.lua` levels warning.
- Packet-demo/replay policy for overridden games (§14.2).
- (Optional, separate) unsaved `level_file_version` global.

## 16. Progress log

**S1 done (2026-09-20, uncommitted in the working tree): managed-setup module moved to `kfx_config`.**

- `git mv`: `kfx_editor/{include,src}/editor_script_managed.{h,cpp}` → `kfx_config/{include,src}/script_setup.{h,cpp}`;
  its Catch2 file → `kfx_config/tests/script_setup_test.cpp` (tags `[kfx_config][script_setup]`).
- Symbols renamed off the `editor_` prefix: `script_setup_extract_region`, `_replace_region`, `_parse`,
  `_generate`, `_level_version`, `_win_lose_operators`, `_availability_{command_name,desc,item_name,find}`.
  Types (`ManagedSetupValues`, `AvailabilityEntry`, `WinLoseRule/Clause`) keep their names. Editor callers
  (`editor_dialogs.cpp`, `editor_availability.cpp`, `editor_script_validate.cpp`) updated.
- The one editor-coupled function, the duplicate WIN_GAME/LOSE_GAME warning, was split: the shared module
  now exposes `script_setup_find_duplicate_win_lose()` returning plain `{line, win}` records;
  `editor_script_check_duplicate_win_lose()` (now declared in `editor_script_validate.h`) wraps them as
  `ScriptIssue`. Its test moved to `kfx_editor/tests/editor_script_duplicate_win_lose_test.cpp`.
- Verified: `check_layering.py --strict` clean; `kfx_config_utest` 2259 assertions / 379 cases and
  `kfx_editor_utest` 578 / 74 all pass (`out/ut-editor`); the `keeperfx` target links.
- No behaviour change intended — pure move + rename + one function split.

Next: S2a — version-aware reader + mask + suitability classifier, with corpus tests over the shipped
skirmish scripts (golden verdicts from §11/§15).

**S2a done (2026-09-20, uncommitted): version-aware reader + mask + suitability classifier.**

New files (all `kfx_config`, pure text, no name-table lookups — item names stay strings):
- `include/script_setup_analysis.h`, `src/script_setup_analysis.cpp`:
  `script_setup_analyse(text, players, has_lua)` → `SetupAnalysis` { `level_version` (whole-file, last wins),
  normalised v1 `seed` (generate speed, money [additive], max creatures, pool [additive], availability
  per player [ALL_PLAYERS expanded; trap/door amounts accumulate], controllers, pure win/lose rules),
  `locks` (runtime-controlled `(field, player, item)` keys), per-line `owned_setup` / `owned_win_lose`,
  `custom_win_lose`, `stray_endif`, info tags (`uses_boxes`, `uses_game_rule`, `has_lua_companion`),
  `verdict` Supported/Partial/Unsupported + `reason` }, and
  `script_setup_mask(text, analysis, mask_win_lose)` (owned lines blanked; line count and every other
  byte incl. line endings preserved).
- `script_setup.{h,cpp}` gained exported `script_setup_trim/_split_args/_parse_if_clause` helpers.
- Win variables: the §13 whitelist is mirrored as constants (19 identical + 38 v1-only + the v0
  `TOTAL_IMPS`→`TOTAL_DIGGERS` alias; `TOTAL_CREATURES` excluded).

Tests: `kfx_config/tests/script_setup_analysis_test.cpp` (17 cases, incl. corpus invariants and golden
verdicts over all 81 shipped scripts) and `kfx_editor/tests/script_setup_variable_tables_test.cpp`
(4 cases cross-checking the whitelist against the engine's real `variable_desc`/`dk1_variable_desc`
tables via `parse_get_varib`, incl. exact table-size counts so a new engine variable forces a decision).
Result: `kfx_config_utest` 29284 assertions / 396 cases, `kfx_editor_utest` all pass, layering clean.

Corpus results (now enforced as goldens): original 44 Supported + 1 Partial (`map00150`, PRISON room);
classic 11 Supported + 4 Partial; dk2maps 11 Partial (exactly `POWER_REAPER` for players 0 and 1);
biervampir 10 Partial (54 creature keys + PLAYER1's controller; `uses_boxes`; start gold stays editable);
207 win rules, all modelled, 0 custom.

Findings during S2a:
- **Stray `ENDIF`s are real**: dk2maps scripts have 6 extra `ENDIF`s with no `IF`; the engine logs
  "unexpected ENDIF" (`pop_condition()`) and continues. The reader tolerates and counts them
  (`stray_endif`); only a never-closed `IF` is Unsupported. (My first cut treated any imbalance as
  Unsupported and wrongly classified every dk2maps level — caught by the golden test.)
- **Classic: 3 of the 4 Partial levels lock on runtime `ALLY_PLAYERS`** (alliances toggled under `IF`),
  which the §11 prototype had not counted — the 4th is `map00055`'s runtime `MAX_CREATURES`. So the
  Ally field lock (§15.2.3) is exercised by real data.

Not yet done (S2b): prelude generator (model + diff vs seed → v1 text), validators (budgets, empty
win set), and the loader seam (S3).

**S2b done (2026-09-20, uncommitted): prelude generator + validators.**

New in `kfx_config`: `include/script_setup_prelude.h`, `src/script_setup_prelude.cpp`.

- `SetupChoices` = the tab's state (a `SetupSeed`-shaped value set, `replace_win_lose` + `rules`, extra
  `allies`); `script_setup_default_choices(analysis)` starts it at the level's own defaults.
- `script_setup_build_override(text, analysis, choices, options)` → `SetupOverride { active, prelude,
  masked, issues }`. **Untouched tab ⇒ `active=false`, nothing installed** (judged *after* locked-field
  edits are dropped, so an edit that can only be ignored is also a no-op). `options.force` overrides this
  for tests.
- **The prelude re-emits the whole effective state**, not just diffs, because the mask blanks every owned
  line: ALL_PLAYERS when every slot agrees (shrinks ~2000-line availability blocks), per-player lines
  otherwise; ordering: generation speed, money, max creatures, pool, availability by kind, controllers,
  alliances, then Replace-mode win/lose rules as nested `IF`s. It contains **no `LEVEL_VERSION` line** (the
  loader forces v1 for it; a version line would leak into the file's own parse).
- **Locks enforced in the generator**: a locked field is emitted with the level's own value whatever the
  choices say, with one warning per field kind ("ignored where the level script controls it").
- Validators (Errors block the override; Warnings don't): unsupported analysis; negative money/max
  creatures/pool/availability/generation speed; players outside the level's slots; controller model
  outside 0–63; self-alliance; malformed names; unknown items (optional `item_exists` callback, since name
  tables are filled at config-load time); win/lose variable not in the §13 whitelist or bad operator;
  **empty win set in Replace mode** (needs ≥1 win rule counting the file's own remaining ones);
  **budgets** — conditions (512), win rules (12), lose rules (12) — computed as *file as written − blocks
  Replace removes + new rules*. Warnings: gold above `SENSIBLE_GOLD` (engine clamps), generation speed 0.
- `SetupAnalysis` gained `if_count`, `win_count`, `lose_count` for the budget maths. The engine limits are
  mirrored as constants (`kSetup*`) and cross-checked against the real `CONDITIONS_COUNT`,
  `WIN_CONDITIONS_COUNT`, `SENSIBLE_GOLD` macros in `kfx_editor/tests/script_setup_variable_tables_test.cpp`.

Tests: `kfx_config/tests/script_setup_prelude_test.cpp` (8 cases). The corpus case runs every shipped
script, both win/lose modes: defaults are recognised as "nothing to do", and a *forced* override's prelude
read back through the S2a reader reproduces the level's own state exactly (generation speed, money, max
creatures, pool, availability, controllers, rule count; zero-valued money/pool entries normalised, since
they are no-ops). `kfx_config_utest` and `kfx_editor_utest` fully pass, layering clean, `keeperfx` links.

Known simplifications carried forward:
- Availability entries the user "removes" fall back to the engine default (unavailable); explicit (0,0)
  entries are kept and emitted.
- Static `SET_COMPUTER_*` / `ALLY_PLAYERS` lines in the file are not masked, so they run *after* the
  prelude (tuning applies to the controller the prelude installs; file alliances are additive to the
  tab's).
- The engine's item-name tables are not consulted here; the UI passes `item_exists` from the loaded config.

Next: S3 — unify the two script line loops in `lvl_script.c`, add the `kfx_sim` override seam
(`{lvnum, prelude, masked}`; prelude scanned under forced v1 with the version restored afterwards),
clear-on-leave, and ftests.

**S3 done (2026-09-20, uncommitted): loader seam.**

- New `kfx_sim` module `include/level_script_override.h` + `src/level_script_override.c`:
  `level_script_override_set(lvnum, prelude, masked)` (copies both), `_clear`, `_is_set`, `_matches(lvnum)`,
  `_level`, `_prelude`, `_masked`. Lives beside `load_single_map_file_to_buffer()` so the frontend (installs)
  and the loader (consumes) both reach it directly — no callback struct, layering clean.
- `kfx_game/src/lvl_script.c`, three small helpers/edits (the non-override path is byte-for-byte the same
  read as before):
  - `load_level_script_text()` — the one text source for both passes: the override's masked script when one
    is installed for this level, else `map%05u.txt`. Buffer contract unchanged (writable, +16 zero padding).
  - `scan_level_script_prelude()` — scans the prelude line by line **at forced version 1**, then restores
    `level_file_version` **to its value on entry** (the file's own version, per §12.1), and `text_line_number`
    (prelude errors read as "line 0…n"). Called from both `preload_script()` (only preload-type commands run
    there — none of the prelude's do) and `load_script()` (before the file's lines).
  - **One-shot lifetime:** `load_script()` clears the override once the script has executed (and on its
    error path); `preload_script()` discards an override installed for a *different* level with a warning.
    So it cannot leak into a later campaign/free-play/network level even if a frontend clear is missed.
- **Deliberate deviation from §2: the two line loops were not unified.** They differ on line-ending handling
  (preload cuts at `\r` or `\n`; load cuts at `\n` and strips a `\r`), so merging them would change parsing
  of lone-`\r` files for a benefit this feature does not need. Both loops are untouched; only their input
  source and a prelude call were added. Worth doing separately, with its own tests, if wanted.
- Tests `kfx_game/tests/level_script_override_test.cpp` (7 cases, real `preload_script`/`load_script`, no
  level files): storage semantics; masked file + prelude both executed then consumed (conditions 2, win 1,
  lose 1); **prelude parsed at v1 over a v0 file** (`TOTAL_DIGGERS`, a v1-only variable, is accepted); the
  **control** — the same line as an ordinary v0 file line is rejected (so the test above really depends on
  the forcing); file version survives the prelude in both directions; stale override discarded; no-override
  behaviour unchanged.
- Verified: layering clean; `kfx_game_utest` 55 cases, `kfx_sim_utest` 687, `kfx_config_utest` 404,
  `kfx_editor_utest` 79 all pass; whole tree incl. `hvlog` variants builds (`ninja -k 0`). One unrelated,
  pre-existing failure: `kfx_platform/tests/bflib_fileio_test.cpp:169` (`system()` result ignored, `-Werror`
  under GCC 15) — file untouched here; spun off as its own task.

Not covered yet (needs the UI and a running game, S4/S5): a full real-level ftest (`src/ftests/`) starting a
skirmish with an override, the frontend install/clear calls, and the save→load round-trip of §14.

Next: S4 — the Setup tab in `frontgui_freeplayselect_frame()`: state struct in `kfx_frontend_state`,
seed on level highlight (`script_setup_analyse` over the `.txt`), pages General / Availability / Win-Lose /
Slots & AI, locked-row markers and banners, `.lof SKIRMISH_SETUP`, and install/clear around Play.

**S4 done (2026-09-20, uncommitted): Setup tab — state, UI, and Play integration.** *Not yet seen on
screen*: everything below is unit/headless-frame tested; the visual pass needs a live look (see "Needs a
live look").

Layout (per owner direction): the Skirmish screen's right column becomes a tab bar — **Map** (the existing
land preview + description) and **Setup** (`Setup *` when edited) — with the Setup tab a scrolling area of
four collapsing headers: **General**, **Availability**, **Win / Lose**, **Slots & AI**. Free play keeps the
plain layout (tab bar is Skirmish-only, `frontend_freeplay_is_skirmish()`).

Files:
- `kfx_config`: `.lof` keyword **`SKIRMISH_SETUP = AUTO|ALLOW|LOCKED`** → `LevelInformation::skirmish_setup`
  (`config_campaigns.{h,c}`, parsed in `kfx_sim/lvl_filesdk1.c` and the campaign-cfg level block); documented
  in `docs/custom_levels_play.txt`. `get_colored_icon_idx_for_color()` (explicit-colour player symbols: the
  menu has no dungeons, so the live-dungeon-colour lookup would give stale colours).
- `kfx_frontend/skirmish_setup.{h,cpp}`: the state module (no ImGui) — seeds from the highlighted level
  (`skirmish_setup_sync`, called every frame; reads the `.txt` via `load_single_map_file_to_buffer`), edit
  operations (availability states per kind, pool, gold, max creatures, generation speed, controllers, teams,
  win/lose templates), `skirmish_setup_build()` (teams → `ALLY_PLAYERS`, item names validated against the
  level's own script *and* the loaded config), `install_for_play` / `play_blocked` (C-callable).
  **Deliberately a file-static, not in `kfx_frontend_state`** (that struct is memcpy'd into save games;
  this holds `std::string`/`std::map`).
- `kfx_frontend/frontgui_skirmish_setup.{h,cpp}`: the drawing. New widget wrappers `FeInputInt`,
  `FeCollapsingHeader` (`frontgui_widgets`); test seam `FeStyleTestUseDefaultFont` (`frontgui_style`).
- `frontgui_screens.cpp`: tab bar in `frontgui_freeplayselect_frame()`; Play disabled with the reason while
  the setup has errors; Return forgets the tab's state.
- `frontmenu_select.c`: `frontend_freeplay_enter_resolve()` installs the override for Skirmish (or clears any
  for ordinary Free play), and refuses to start while `skirmish_setup_play_blocked()`.

Availability tab design (the icon question): the same tiles as the in-game sidebar and the editor toolbox —
`fe_hud_cell` + creature/room/spell/trap icons from the panel sprites (`get_creature_model_graphics(…,
CGI_HandSymbol)`, `medsym_sprite_idx`, manufacture table for traps/doors, in the in-game `panel_tab_idx`
order) with `GUI_ICON_PACK` PNG overrides and a text-tile fallback; items the level's script names but the
base config lacks (mappack creatures) are appended as text tiles. **Players are the coloured player symbols**
(as the editor's owner row) plus **All**: pick a player, click a tile to cycle its state, right-click = off.
State shown on the tile: dimmed = off, green dot = available, `R` = researchable, `F` = forced (creatures),
`*` = differs between players (All view), stock count badge for traps/doors (edited in a Stock row below),
**padlock** = runtime-controlled by the level script (ignores clicks; a page note appears when most of a
grid is locked, e.g. biervampir's creatures). Creature pool uses the same tiles with a count badge:
click +1, right-click −1, Shift ±5.

Tests: `kfx_frontend/tests/skirmish_setup_test.cpp` (9 cases incl. **end to end: an installed override
drives the real `preload_script`/`load_script`** — replaced win rules counted, one-shot consumed, version
untouched), `frontgui_skirmish_setup_smoke_test.cpp` (draws every section in a headless ImGui frame:
enabled, edited, error, small window, disabled, nothing loaded), `kfx_sim/tests/lvl_lof_skirmish_setup_test.cpp`
(the keyword), `config_spritecolors_test` (explicit colour), `config_campaigns_test` (default). All unit
suites pass (`kfx_config` 405 cases, `kfx_sim` 693, `kfx_game` 55, `kfx_frontend` 85, `kfx_editor` 79,
`kfx_script`, `kfx_net`), layering clean, whole tree incl. `hvlog` builds.

Known gaps / decisions:
- **Needs a live look**: visual layout/spacing at real resolutions and UI_FONT_SCALE, icon rendering with real
  sprites (unit tests have none), whether creature `CGI_HandSymbol` sprites resolve in the menu (they should —
  panel sheet is loaded at video-mode setup — but untested), tab-bar height budget inside the fixed-height
  window, and click behaviour. The smoke test only proves it draws and balances.
- `SKIRMISH_SETUP = ALLOW` is parsed but has no effect today (only `LOCKED` does); the editor's Level Settings
  does not write it yet.
- Availability item lists come from the *base* config loaded at the menu (mappack-specific `creature.cfg`
  loads at level start); levels' own names are appended, and `item_exists` accepts them, so nothing is
  wrongly rejected — but a mappack creature the level never mentions is not offered.
- Custom win/lose rules are one clause each in the UI (templates + the level's own rules cover the rest);
  the model/generator support nested clauses.
- The AI model list is the base `keepcompp.cfg`; a mappack that adds models is picked up only if the config
  is already loaded.
- Research remains out of the tab (per decision); the availability page's "Research" state only marks
  items as researchable — the order/costs stay with the mappack `rules.cfg`.
- Not done: ftest in a real running game (S5), the `.txt` + `.lua` warning is shown as an info line only,
  packet-demo replay policy (§14.2), a checkbox-free "Level default" affordance for teams.

Needs a live look, in order of risk: (1) does the Setup tab fit and scroll inside the fixed-height window;
(2) icons — creature/room/spell/trap sprites vs text tiles, and the coloured player symbols; (3) Play with a
changed setup on an original-pack level and on a dk2maps level (Reaper rows padlocked), then check
`keeperfx.log` for the "Discarding a stale level script override" warning (must not appear) and script
errors from the prelude (line numbers "0…n").


## 17. First live look (2026-09-20) — findings and fixes

Live run on the DK2 map pack (Setup tab, then Play on level 220). Setup tab **fits and scrolls**; three problems:

1. **Magenta checkerboard tiles (player symbols, and some room/spell tiles).** The checkerboard is the engine's
   `bad_icon` placeholder (`get_panel_sprite()` returns it for any index that is neither in the base panel
   sheet nor a currently-loaded custom sprite). Two causes, both "custom sprites only load with the level":
   (a) `get_player_colored_icon_idx()` remaps player symbols through the campaign's `colored_sprites.zip`
   (custom sprites) — nothing behind those indices in the menu; (b) rooms/spells/traps whose icons live in
   campaign/mod sprite zips. **Fix:** player symbols use the fixed base-sheet sprites
   (`GPS_plyrsym_symbol_player_{red,blue,green,yellow}_std_b`, 488–491; white for slots past four), and any
   item whose sprite is not drawable yet (`idx >= GUI_PANEL_SPRITES_COUNT && !is_custom_icon(idx)`) falls
   back to the text tile. The `get_colored_icon_idx_for_color()` helper added in S4 is removed (unused).
2. **Right-click went back a screen.** `frontscreen_end_input()` (legacy input pass) treats *any* right-click on
   this screen as "back", before/alongside ImGui. **Fix:** the Setup tab records the frame the mouse is over it
   (`frontgui_skirmish_setup_captures_right_click()`); `frontend_input()` clears `right_button_clicked` for
   Skirmish when it is set. Escape still goes back (also while typing in a number field — a possible
   follow-up: swallow Escape while an `InputInt` is active).
3. **No way to tell from the log whether an override ran.** Added `JUSTLOG` lines: "Skirmish setup: installed a
   script override for level N" (Play) and "Level N: running the Skirmish setup override" (loader). The run's
   log has neither, and (correctly) none of the failure signatures: no "Discarding a stale level script
   override", no prelude script errors, script resources normal (16/2048 values, 20/512 conditions). So it
   is **unknown whether that run installed an override** — re-run with the new build and look for the two lines.

**Correction to §5.1: the default skirmish AI is not "model 0".** The log line
`No model defined for Player 1, assigned computer model 14` comes from `setup_computer_players2()`
(`player_computer.c:1592`): every *active* player gets a **random model between `SkirmishFirst` and
`SkirmishLast`** in `keepcompp.cfg` (13–16: Defensive, Peculiar, Balanced, Rush), the local player gets
`player_assist_default`. This runs in `post_init_level()` *before* `load_script()`, so a prelude
`COMPUTER_PLAYER(PLAYERn, model)` re-initialises the slot afterwards (the same thing biervampir does). The
`setup_computer_player()` → model 0 path (`thing_list.c`) is the *other* route (zombie/unassigned slots), not
the normal skirmish one. UI consequence (done): the Slots page's default reads "Random skirmish AI (default)"
(or "Level default: AI n" when the level's script names one), and the models in the skirmish range are tagged
`[skirmish preset]`. Design consequence: "level default" is *non-deterministic across runs* (random preset), so a
tab that pins a model also makes the run reproducible — worth saying in the tooltip/docs if AI comparisons
(and later the LLM seat) need repeatable opponents.

Also seen in the log (unrelated, pre-existing, not touched): `MULTI_LEVELS` unrecognised in
`dk2maps.cfg` (the setting is documented in the file but the config parser rejects it), many
"Couldn't read DESCRIPTION" warnings for `.lof` files, and `get_navigation_colour_for_door` errors on
level 220's row 108. None come from the setup tab.

### 17.1 Follow-up: the checkerboard's real cause — no panel sprite sheet in the menu (2026-09-20)

Second live run, with the new log line: `BARRACKS` (sprite 69) and eight powers (452, 809, 772, 424, 436, 550,
412, 406) all reported "no drawable menu icon", although sprite 69 is a normal 38×46 Barracks icon in
`gui2-64.dat` (decoded and checked by hand). Owner-confirmed: with `GUI_ICON_PACK = NONE` **no room icon
loads at all** in the Setup tab.

**Cause.** The menu runs in "minimal resolution" mode: `vidmode.c` `LoadVResMinimal` loads only the *button*
sprites and the frontend fonts. `gui_panel_sprites` (`gui2-*.dat` — every room/spell/trap/creature/player
icon) and `engine_palette` (`data/palette.dat`) exist **only in-game**. So in the menu
`get_panel_sprite()` had nothing to return but `bad_icon` (the magenta checkerboard), and
`FeGuiPanelTexture()` cached that by index. The icon pack (`dk1`) had been masking it: any item with a
`room_*/power_*/creature_icon_*` PNG never touched the sheet, so only items *without* a PNG
(Barracks — the pack ships `room_armory`, not `room_barracks` — and eight powers) failed. That is also why
it looked like "some standard icons": it was all sprite-sheet icons, and the pack covered most of them.

**Fix (general, in `frontgui_sprite_tex.cpp`, not Skirmish-specific):** when `gui_panel_sprites == nullptr`
(menu), `FeGuiPanelTexture()` uses a **private copy** of `data/gui2-64.dat` + `data/palette.dat`, loaded
lazily on first use and released automatically as soon as the real in-game sheet exists (its own texture
cache, so the placeholder can never be cached under a real index). The in-game path is unchanged
(byte-identical, including the visible placeholder for a genuinely missing custom icon). New public
`FeGuiPanelSpriteAvailable(idx)` (true for a drawable sprite in either context) drives the Setup tab's
text-tile fallback; `FeGuiPanelReleaseMenuSheet()` for cleanup/tests. `render_sprite` was split so the palette
is an explicit argument. **Still text tiles in the menu, by design:** custom campaign/mod icons (index ≥ 950,
loaded from sprite zips only when a level starts) and unresolved icon names (`INT16_MAX`).

**Test against real data** (`frontgui_menu_panel_sprites_test.cpp`, runs in `core_files/`, skipped if absent):
sprites 69/452/809/772/424/436/550/412/406 and the player symbols 488–491 are available with no in-game
sheet; 0, −3, 920, 950 and 32767 are not; and without game data nothing is drawable and nothing crashes.
The texture-building path needs the renderer, so it is verified only live. `kfx_frontend_utest` 88 cases;
all suites, layering and the whole tree pass.

Side effect worth knowing: any other menu screen that draws panel sprites through `FeGuiPanelTexture` now
gets real icons instead of the placeholder. If anything in the menu *depended* on the placeholder look, it
will change — none known.

### 17.2 Regression from 17.1: mod creature icons vanished (2026-09-20)

Live report: with the `Creature_Portraits` mod active the creature tiles no longer drew (mod disabled: fine;
before 17.1: fine). **Cause:** the mod's replacement portraits are *custom sprites* (index ≥
`GUI_PANEL_SPRITES_COUNT`, from sprite zips loaded at startup — the log's `process_icon_from_list: Overriding
icon …/BIRD_PORTRAIT`), which live in `custom_sprites` and **are** present in the menu. 17.1's menu path
routed every panel sprite through the private base sheet, which only covers indices < 920, so it called every
custom icon unavailable. **Fix:** in the menu branch of `FeGuiPanelTexture()`/`FeGuiPanelSpriteAvailable()`,
custom indices (`is_custom_icon`) go back through `get_panel_sprite()` as before (palette: the game's if a level
loaded one, else the private copy of the same file); only base indices use the private sheet. **Regression
test:** `frontgui_menu_panel_sprites_test.cpp` swaps in a synthetic custom sprite sheet and checks index 950 is
available while loaded, 951 and 32767 are not, and 950 is gone again after — verified to fail with the 17.1
behaviour restored. Lesson recorded: the menu's sprite world is *base sheet absent, custom sheet present*;
the two must be handled separately.

**S5 done (2026-09-20, uncommitted): functional tests in a real running game (headless).**

- **Framework:** `FTestConfig` gained an optional `pre_start_func` (`ftest.h`, called from `ftest_setup_test()` after
  the campaign/mappack and level are selected, before the level loads) — the one thing an override test needs
  that `init_func` (runs after load) cannot give. Other tests are unaffected (field defaults to NULL).
- **`skirmish_setup_override`** (`ftests/tests/ftest_skirmish_setup.{h,cpp}`, original-pack multiplayer map 50, a
  v0 script): `pre_start` drives the same state module the UI uses — gold per player, max creatures,
  generation speed, pool (edit + remove), creature/room/spell availability incl. a *Forced* creature, a pinned
  skirmish AI model, Replace-mode win rules (last-keeper ×2 + survive) — and installs the override as Play does
  (`fe_computer_players = 1` too). Action 1 (turn 30) checks **28 live-sim values**: edited ones took effect and
  untouched ones (player 1 max creatures 17, DRAGON pool 20, player 1's creatures *not forced* …) are still the
  level's own — the latter is the v0→v1 translation proof; also `level_file_version == 0` afterwards (prelude's
  forced v1 did not leak), override consumed, `win_conditions_num == 3`, player 1 computer-controlled model 13.
  Action 2 does **save → break the live state → load → re-check** (59 checks total, and the mappack
  `original.cfg` and level 50 come back): §14's claim that a saved skirmish is self-contained is now tested,
  not just read.
- **`skirmish_setup_locks`** (dk2maps map 220, a v1 script with the Reaper toggle): the locked row rejects the
  edit (`POWER_REAPER` stays the level's researchable-only), edits elsewhere apply (gold for both players, temple),
  the level's own runtime conditions and its two win rules (Keep mode) are loaded; 9 checks.
- **Verification:** both pass headless (`-headless -ftests <name> -exitonfailedtest`, exit 0). **Negative control:**
  with the install call disabled the same test fails (exit 255; gold 10000 vs the expected 20000, "override installed
  before load: got 0"). **Full sweep** (`-headless -ftests -exitonfailedtest`, 5m12s): all 30 registered tests pass,
  exit 0. Log shows the two new lines ("Skirmish setup: installed a script override for level 50 (3194 prelude
  bytes)" / "…running the Skirmish setup override") and no script errors from the prelude.
- **A test-harness lesson recorded in the test:** calling `save_game()` without first filling the catalogue
  entry (`fill_game_catalogue_slot`, as the Save screen and console `save <slot> <name>` do) makes the load fall
  back to the default campaign ("Loaded level 0 from Dungeon Keeper original campaign"); the test now fills it and
  asserts the mappack and level survive.
- **How it was run (scratch, per the ftest-scratch-build notes):** `cmake -S . -B out/ft-skirmish -G Ninja
  -DCMAKE_BUILD_TYPE=Release -DKFX_FUNCTESTING=ON`, then a run directory `out/run-skirmish/` with symlinks to the full
  data in `core_files/` (the staged `out/run-editor` tree has no multiplayer level files) and a `keeperfx.cfg` with
  a fixed `INGAME_RES`. Not committed; `out/` is git-ignored.

Not covered by an ftest (by design): the ImGui tab itself (covered by the headless-frame smoke tests and your
live runs), replay/packet-demo of an overridden game (§14.2, still undecided), `.txt` + `.lua` levels.

## 18. Lua warnings and polish (2026-09-20)

**Lua companion warnings.** A level's `map*.lua` can change the same fields as the tab (shipped campaign Lua
calls `WinGame`, `AllyPlayers`, `CreatureAvailable`, `MagicAvailable`, `AddCreatureToPool`, ...). Top-level Lua
runs at level init (before the classic script) and `OnGameStart` runs after it (`main_game.c` `post_init_level`:
`load_script` then `lua_on_game_start`), so the final value can differ from what the tab shows.
- `script_setup_scan_lua()` (kfx_config, pure text): a comment- and string-aware token scan (line/long
  comments, quoted and long strings, `function X(` definitions and bare references are not calls) that reports
  which of the tab's fields the Lua calls: gold, creature limit, arrival interval, pool, the five availability
  kinds, computer players (`ComputerPlayer`, `SetComputer*`), alliances, win/lose (research: info only).
  `script_setup_analyse(..., lua_text)` fills `analysis.lua`; a level that would be Supported becomes **Partial**
  with a reason naming the fields, and an already-Partial one gets the sentence appended.
- It is a **warning, not a lock** (it cannot know what values Lua passes): fields stay editable, and each section
  of the tab shows a highlighted note listing what Lua also changes there (General / Availability / Slots & AI /
  Win-Lose, where it says Replace cannot remove Lua's rules). An inert companion shows a neutral line instead.
- The four Lua-only classic skirmish maps scan as touching everything (asserted), which is consistent with them
  being Unsupported. No shipped level has both a `.txt` and a `.lua`, so the on-disk companion read is covered by
  unit tests only.

**Polish.**
- Slots & AI shows which slots have a **Dungeon Heart** (read from the map's own thing file: classic `.tng` or
  native `.tngfx`, via the game's `MapContentReader`); a controller chosen for a heartless slot gets a note
  ("has no Dungeon Heart on this map, so its computer player setting has no effect") — a warning, Play stays open.
  Asserted live on original map 50 (classic) and dk2maps map 220 (native): both keepers have hearts.
- Availability page: one-line legend for the tile markers (dot, R, F, *, padlock).
- The creature-pool grid also lists creatures named only by the level's own pool (mappack creatures the menu's
  base config lacks).
- Custom win/lose rule: up to four conditions, all of which must hold (was one).

**Bug found and fixed on the way (not in this feature's code): the native map reader could not read
hand-authored maps.** `MapContentReader` (the editor's reader, kfx_sim) only understood `[[thing]]` arrays with
integer `ThingType`, while the game's own loader (`load_kfx_toml_file`, `thing_create_thing_adv`) also accepts
numbered tables (`[thing0]`, `[thing1]`, ... with `[common] ThingsCount`), class names (`ThingType = "Object"`) and
`SubtypeStringID`. The dk2maps pack uses the numbered/name forms, so opening it in the editor would have shown
**no things, lights or action points** (my first hearts probe reported 0/0 for map 220). The reader now accepts both
layouts for things, lights and action points and uses the engine's `value_parse_class`/`value_parse_model`.
Test: `kfx_sim/tests/map_content_native_forms_test.cpp` (numbered + names, the editor's array + integers, lights
and APs).

Verification: `kfx_config` 407 cases, `kfx_sim` 696, `kfx_game` 55, `kfx_frontend` 91, `kfx_editor` 79,
`kfx_script`, `kfx_net`, `kfx_render`, `kfx_platform` all pass; layering clean; full headless ftest sweep exit 0.
Still not seen live: the new notes/legend/multi-condition rule UI (unit- and smoke-tested only).
