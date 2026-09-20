# Skirmish level-setup tab — feasibility investigation

Status: **investigation only, no code written.** Date: 2026-09-19.
Question: can the Skirmish frontend screen gain a tab that lets the player customise the level's
*setup* (the things File > Level Settings and Script > Availability edit in the in-game editor),
effectively overriding those parts of the level's own script, while every other script command is
carried over unchanged?

> **Revised.** Scope has since grown (research, win/lose, AI slots) and a script-version hazard
> (`LEVEL_VERSION` 0 vs 1) was found. [01-scope-expansion-research-winlose-ai.md](01-scope-expansion-research-winlose-ai.md)
> supersedes §4's text-splicing design with a forced-v1 "prelude + line-preserving mask" design, and
> §8's phasing. Read 01 first; this file remains the baseline description of the current code.

## 1. Verdict

**Feasible, moderate effort, no engine-level blockers.** The editor already contains the parser and
generator for exactly the commands in scope, and the game reads a level's script from exactly two
call sites, so an in-memory override has a narrow hook point. Four things need real design work
rather than plumbing, in order of risk:

1. **The editor's "managed block" model is not enough on its own** — real shipped scripts scatter
   the same commands outside any block, and several are *additive*, so an override must **strip**
   the originals, not just add lines (§4).
2. **Layering:** the parse/generate code lives in `kfx_editor`, which ranks *above* `kfx_frontend`.
   It has to move down before Skirmish can use it (§5).
3. **Lua-only levels** (the 4 classic `map0700x.lua` maps) have no `.txt` to override (§6).
4. **Override lifetime:** a stale override leaking into a later campaign / free-play / network
   level would be a nasty bug; it needs an explicit clear-on-leave rule (§7).

## 2. Current state

### 2.1 Skirmish screen

- Main-menu button → `frontend_start_skirmish_resolve()` (`frontend.cpp:1775`): sets
  `fe_network_active = 0`, `net_service_index_selected = FrontendNetSvc_Skirmish`, goes to
  `FeSt_MAPPACK_SELECT`.
- That is the **shared Free Play screen**, `frontgui_freeplayselect_frame()`
  (`frontgui_screens.cpp:1292`): mappack list + level list on the left, land preview + detail panel
  on the right, Return / Play at the bottom. Skirmish is told apart only by
  `frontend_freeplay_is_skirmish()` (`frontmenu_select.c:689`), which just tests
  `net_service_index_selected`.
- Commit path, `frontend_freeplay_enter_resolve()` (`frontmenu_select.c:558`): sets
  `selected_level_number`, and for skirmish sets `fe_computer_players = 1`, then
  `FeSt_START_KPRLEVEL`. **No level customisation of any kind today.**
- Skirmish levels are `.lof KIND = MULTI` levels from `mp_mappacks_list` (`core_files/multiplayer/*`,
  82 `.txt` scripts, 4 `.lua`). Player count comes from `.lof PLAYERS =` (2–4 in shipped maps).
  Uninitialised player slots become AI via `setup_computer_players()` when
  `is_fe_computer_players_active()` is true (`main_game.c:687`, `thing_list.c:1290`).

### 2.2 What the editor edits (the in-scope commands)

`kfx_editor/src/editor_script_managed.{h,cpp}` (dialog: `editor_dialogs.cpp`, availability grid:
`editor_availability.cpp`) reads/writes a delimited block in the script text:

| Editor route | Script command(s) | Data in `ManagedSetupValues` |
|---|---|---|
| File > Level Settings | `SET_GENERATE_SPEED(n)` | `generate_speed` |
| | `START_MONEY(PLAYERn,n)` | `start_money[]` |
| | `MAX_CREATURES(PLAYERn,n)` | `max_creatures[]` |
| | `ADD_CREATURE_TO_POOL(kind,n)` | `creature_pool[]` |
| Script > Availability | `CREATURE_AVAILABLE`, `ROOM_AVAILABLE`, `MAGIC_AVAILABLE`, `TRAP_AVAILABLE`, `DOOR_AVAILABLE` `(ALL_PLAYERS\|PLAYERn, item, a, b)` | `availability[]` (kind/player/item/a/b) |

The functions `editor_script_parse_managed_setup()` / `editor_script_generate_managed_setup()`
are **pure text → struct → text**, with no live-session dependency (the file header says so
explicitly). That is the reusable core, and it already round-trips the raw `(a, b)` integer pair
losslessly (e.g. `CREATURE_AVAILABLE`'s force count, `TRAP_AVAILABLE`'s stock).

Not covered by the editor today and therefore not in this scope unless we choose to extend it:
`RESEARCH`/`RESEARCH_ORDER`, `COMPUTER_PLAYER`, `SET_HAND_RULE`, `SET_TIMER`, win conditions.

## 3. How the game consumes a level script

- Two call sites read the script, both `load_single_map_file_to_buffer(lvnum, "txt", …)`:
  - `preload_script()` (`lvl_script.c:911`) — parse-only pass before map load (`main_game.c:457`).
  - `load_script()` (`lvl_script.c:931`) — the real execution pass, from `post_init_level()`
    (`main_game.c:540`).
- Both then run the same line-by-line loop into `script_scan_line()`. So **one seam** — supplying
  the `.txt` bytes from memory instead of disk — covers both passes and cannot let them disagree.
- `load_single_map_file_to_buffer()` is `kfx_sim` (`lvl_filesdk1.c:136`) and is also used for
  `clm/dat/tng/apt/wib/own/slb/…`; an override must be keyed on extension `"txt"` only, and on the
  level number, so map data is untouched.
- Skirmish is `fe_network_active = 0`: single machine, no packet-stream script sync to worry about.
  (For a future networked variant, both peers would need the identical override text — out of scope.)

## 4. The merge problem (the main design finding)

The naive design — "generate a managed block and prepend/append it" — does **not** work, because of
how the commands execute:

- **Additive commands.** `START_MONEY` calls `player_add_offmap_gold()` (`lvl_script_commands_old.c:236`);
  `ADD_CREATURE_TO_POOL` calls `add_creature_to_pool()`; `TRAP_AVAILABLE`/`DOOR_AVAILABLE` use
  `…_and_add_to_amount()`. A shipped level that already has `START_MONEY(PLAYER0,20000)` plus our
  override would give the sum, not our value.
- **Overwrite commands** (`MAX_CREATURES`, `SET_GENERATE_SPEED`, `CREATURE/ROOM/MAGIC_AVAILABLE`)
  are last-writer-wins, so *append-after* would work for these but *prepend-before* would be
  silently undone by the original lines. Real level scripts also contain further availability
  commands after the setup section.
- **Real scripts scatter these commands.** e.g. `map00223.txt` (Herocove) has them at top level in
  free-form sections (`REM ALL PLAYERS`, `REM CREATURE_AVAILABILITY`, …) with no editor markers, and
  also has *conditional* `MAGIC_AVAILABLE(…)` inside `IF … NEXT_COMMAND_REUSABLE` blocks (the Reaper
  spell cooldown logic) that are runtime behaviour and must **survive**.

**Required transform** (pure text, testable in Catch2 like the existing managed-region code):

1. Walk the script tracking `IF`/`ENDIF` nesting depth (the engine tracks the same with
   `get_script_current_condition()`).
2. **Remove** lines that are (a) one of the in-scope commands **and** (b) at depth 0 **and** (c) not
   preceded by `NEXT_COMMAND_REUSABLE`. Those are exactly the "static setup" lines the tab owns.
   Leave everything at depth > 0 or `REUSABLE` alone (runtime logic).
3. Insert the generated setup block at the top (after `LEVEL_VERSION(...)`, which must stay first —
   it affects how later lines parse: see `file_version` in `lvl_script_commands_old.c:1261`).
4. Handle `/* … */` multi-line comments (the engine strips them via `process_multiline_comment()`),
   `REM`, and `;` comments so we neither delete nor keep the wrong lines.

Consequence: only a *subset* of the script is rewritten; all other commands (win conditions,
timers, parties, messages, action-point logic, Lua hooks) pass through verbatim, which matches the
"other commands would need to be copied" requirement.

**Seeding the tab.** Run the same parser over the *original* (un-stripped, depth-0) lines to fill
the tab's initial values, so the tab opens showing the level's real defaults, and only lines the
player *changes* differ. `editor_script_parse_managed_setup()` already does this over a body string;
it needs to be pointed at "all depth-0 setup lines" rather than only a marked region. Its
"later line wins" rule is right for overwrite commands but wrong for the additive ones (START_MONEY,
pool, trap/door amounts should sum) — the parser needs an additive mode for the *seed* path.

## 5. Layering

`kfx_platform → kfx_config → kfx_sim → kfx_render → kfx_net → kfx_game → kfx_frontend → kfx_script →
kfx_apploop → kfx_editor`. `editor_script_managed.cpp` includes only `kfx_config` headers
(`config_creature.h`, `config_terrain.h`, `config_magic.h`, `config_trapdoor.h`, `bflib_basics.h`),
so it has **no reason to sit in `kfx_editor`**. Plan:

- Move `editor_script_managed.{h,cpp}` (and the new strip/merge routines) to a lower library —
  `kfx_config` is the lowest that satisfies its includes; `kfx_game` (next to `lvl_script.c`) is the
  alternative. Recommend **`kfx_config`**: it keeps the module reachable from `kfx_sim`, `kfx_game`
  and `kfx_frontend` alike with no callback struct. The editor then includes it from below.
- The **override storage + hook** goes in `kfx_sim` beside `load_single_map_file_to_buffer()`:
  a small `set_level_script_override(lvnum, text)` / `clear_…()` / lookup used only when
  `fext == "txt"`. `kfx_frontend` (above `kfx_sim`) can call it directly; `kfx_game`'s two script
  readers already go through it. No new callback struct needed.
- Run `python3 scripts/check_layering.py --strict` after the move (CI-blocking).
- Catch2: existing editor tests for this module move with it (`kfx_editor/tests` → the new home).

## 6. Lua-only levels

The 4 `classic/map07001–4.lua` skirmish maps have **no `.txt`**: `preload_script()` returns false and
`open_lua_script()` supplies everything (`main_game.c:415/479`). Their setup lives in Lua, which we
cannot rewrite textually. Options:

- **A (recommended for v1):** show the tab as *disabled* with a one-line reason for these levels.
- B: apply the override *after* the Lua script by running the equivalent Lua/Script API calls —
  needs a separate investigation of which of the in-scope commands have Lua equivalents and their
  ordering relative to Lua's own start hooks; not worth blocking v1.

Also: a level with **both** `.txt` and `.lua` runs both; the override only touches the `.txt`, so
Lua setup on such maps would still apply on top. Detect and warn (or disable) if a `.lua` exists.

## 7. UI and lifecycle

**UI.** `frontgui_freeplayselect_frame()` currently has no tab bar. Add an ImGui tab bar for the
Skirmish case only (`frontend_freeplay_is_skirmish()`): **Level** (existing list + preview) and
**Setup** (new). The Setup tab mirrors the editor's two dialogs, reusing the `Fe*` widget set the
frontend already uses:

- General: generation speed, per-player start gold / max creatures (sized to the level's
  `.lof PLAYERS`), creature pool table.
- Availability: the grid from `editor_availability.cpp` (creatures / rooms / spells / traps / doors ×
  players + ALL_PLAYERS), with a "reset to level default" per row and globally.
- Per-player labels: slot 0 = human (`default_loc_player`), the rest AI. Since
  `fe_computer_players` is currently a fixed `1`, the AI-slot count is a natural extra field here,
  but it is *not* part of the requested scope — flag, don't build.

Existing skirmish UI code is in `kfx_frontend`, so the editor's ImGui grid can't be reused
directly (it's above); only the *data model* is shared, the widget code is re-written against
`Fe*` helpers (the editor grid is ~176 lines in `editor_icon_grid.cpp` plus `editor_availability.cpp`
217 lines — reasonable to re-implement or to factor its layout-agnostic parts down too).

**State.** New `SkirmishSetup` struct (overrides + "dirty" flag) in `kfx_frontend_state`
(architecture.md §6.2: not `struct Game`); it is frontend-transient so needs no migration story.
Seed when the highlighted level changes; rebuild the override text at Play.

**Lifecycle (risk).**
- Install the override at `frontend_freeplay_enter_resolve()` only when
  `frontend_freeplay_is_skirmish()` and the tab was actually changed; otherwise leave the disk
  script untouched (zero behaviour change for anyone not using the tab).
- **Clear it** on leaving the level (`frontend_shutdown_state`, and on any entry to Free Play /
  Campaign / network / editor). The comment at `frontgui_screens.cpp:1537` shows this class of
  "prior Skirmish visit leaks into later entry" bug has already happened once with
  `net_service_index_selected`, so this must be enforced, not assumed. Keying the override on
  `lvnum` gives a second line of defence.
- **Editor playtest interaction:** the editor playtest uses `EDITOR_PLAYTEST_LEVEL_NUMBER` with
  its own scratch files; a stale skirmish override on a different `lvnum` would not match, but the
  clear-on-leave rule should still cover it.
- **Saved games:** `open_lua_script()` is re-invoked on load (`game_saves.c:460`); check whether a
  save made from an overridden skirmish re-reads the `.txt` (it does not appear to — script state
  is in the save blob — but confirm before shipping).

## 8. Effort and phasing

| Phase | Work | Size |
|---|---|---|
| S1 | Move managed-setup parser/generator to `kfx_config`; keep editor building; move its Catch2 tests; layering check | small |
| S2 | Script **strip + insert** transform with nesting/comment handling; additive-aware seed parse; Catch2 over the 82 real shipped skirmish scripts (assert: parse→regenerate→re-parse is stable, non-setup lines byte-identical) | medium — the real work |
| S3 | `kfx_sim` override hook keyed on `(lvnum,"txt")`; clear-on-leave; tests | small |
| S4 | Setup tab UI (general + availability) in `frontgui_freeplayselect_frame()` | medium |
| S5 | Functional test in `src/ftests/`: apply an override (gold, pool, one disabled room) to a shipped skirmish map, assert the values in the running sim | small–medium |
| S6 (optional) | Lua-level support, AI slot count, presets saved to disk | separate investigation |

Testing lever worth using: S2 can be validated **offline against every shipped multiplayer `.txt`**
before any UI exists — it is the highest-risk piece and fully unit-testable.

## 9. Risks / open questions

1. **`preload_script` vs `load_script`** both scan commands; some commands behave differently in
   preload (`script_scan_line(buf, true, …)`). Confirm no in-scope command's *preload* behaviour
   changes when moved to the top of the file (expected not to — none have check/process hooks
   except `SET_GENERATE_SPEED`, which uses `set_generate_speed_check/process`).
2. **Item names not in config** (mod/campaign-specific creatures, traps): the generator already
   skips unknown items (`item_name[0]=='\0'`); the tab must show only what the loaded config knows.
3. **`LEVEL_VERSION(0)` scripts** parse arguments differently (`CREATURE_AVAILABLE` argument
   shift, `lvl_script_commands_old.c:1261`). Detect and either handle or disable the tab.
4. **Duplicate/aliased commands** (e.g. `RESEARCH` mapped to `RESEARCH_ORDER` in one table): out of
   scope now, but a script that uses research to gate spells means "MAGIC_AVAILABLE" overrides
   may not do what the player expects; surface a note.
5. **Balance intent:** overriding a hand-tuned map's setup is by design, but a "Reset to level
   defaults" affordance should be prominent.
6. **Scope decision needed from the owner:** should the tab also expose `RESEARCH`, `COMPUTER_PLAYER`
   type, and AI count? The editor doesn't today; each is a small addition to the same
   parse/strip/generate machinery once S1–S2 exist.

## 10. Recommendation

Proceed with S1–S3 first (pure logic, offline-testable against shipped maps, no UI risk), then S4–S5.
Ship v1 for `.txt`-scripted, `LEVEL_VERSION ≥ 1` levels only; disable the tab elsewhere with an
explanatory line. Do not extend the editor's own managed block to solve the merge problem — the
stripping transform is the right primitive and should be shared by both consumers.
