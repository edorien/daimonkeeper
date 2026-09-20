# FX plan 02 — closing the Lua script gap

Status: **L1 (round trip), L2 (Lua tab), L3 (Validate) and L4 (Lua page in Commands) built**; L5 built (banner
`editor_lua_override_banner()` in Script Setup, Availability and Objective/Message when the level has a
.lua; classic Commands tab gets "Insert into Lua" = `RunDKScriptCommand("...")`, which replaces the
planned "insert as classic command" idea); L6 built (New Map
"Lua script" option -> template, no .txt, one-shot `editor_set_new_map_lua`; Lua-tab **Snippets**
menu generated from the `Register*Event` stubs; **Find (Ctrl+F)** button using the widget's own
find/replace). L7 built (pack modules): "Open required file" asks Read-only (default) / Edit original / Edit copy.
A copy goes to the levels folder (searched first, so it shadows the module for every level in that
folder; Save As elsewhere does not carry it); existing copies are opened, never overwritten. Editable
module tabs have Save file (atomic write) and Revert; a modified tab or the window cannot be closed
until saved or reverted. All six slices done; live-test pending for L2-L6. L4: `editor_lua_stubs` (parses `---@param/@return` + doc comments from `bindings/` and
`triggers/`, ~250 functions; call templates leave out optional parameters), a **Lua** tab in the
Commands window (groups = stub file, filter, detail pane, double-click inserts into the Lua tab via
`editor_lua_insert_at_cursor`). "Insert as classic command" not built (needs a Lua↔classic name map). L3:
`editor_lua_validate` (LuaJIT syntax parse in a throwaway state; unknown-function warnings from the
stub-file names, script-defined names and `require`d modules; `RunDKScriptCommand("...")` strings run
through the classic validator), Validate on the Lua tab, Lua problems in Verify Map; Catch2 incl. a
sweep of the 8 shipped level scripts (must report nothing). Also: Script > Word Wrap for all tabs. L2: `editor_lua_support`
(stub-file API scan, template, `require` lookup; Catch2 `editor_lua_support_test`), `editor_lua_syntax_apply`,
and tabs in `editor_script.cpp` (Script (.txt) / Lua (.lua) with Add/Remove Lua script, "Open required
file" into read-only module tabs; Apply commits both buffers; Validate stays .txt-only until L3;
inserting a command/token switches to the .txt tab). Live check pending. L1: `MapContent::lua_text/has_lua`,
`MapContentReader::read_lua`, `MapContentWriter::write_lua` (removes a stale `.lua` when the map has
none), no stub `.txt` for a Lua-only level, session accessors `editor_current_level_{has_lua,lua_text}`
/`editor_set_current_level_lua_text`, `.lua` no longer a sidecar. Tests: 3 Catch2 round-trip cases,
ftest `editor_session` action 005 (open → Save As both formats → identical bytes, Lua-only, removal).
Live check still to do: open `map07001`, Save As, compare the `.lua`.
Prerequisites already in place: sidecar warning on Save As and sidecar copy for
Playtest (plan 01), the classic script validator and problem list (`editor_script_validate`), the
command browser, syntax colouring, and `editor_sidecars`.

## 1. What the engine does with Lua (verified in the source)

Start-up order for a level (`main_game.c`, `lua_base.c`, `lua_triggers.c`):

1. `open_lua_script(lvnum)` — creates the level's Lua state, then runs, in this order: the engine's
   `fxdata/lua/init.lua` (loads the API: `core`, `triggers`, `classes`, `managers`), each enabled
   mod's Lua, the campaign's `lua/init.lua`, then **`map%05d.lua`** (with `package.path` set so
   `require` finds `?.lua` in the level folder and `lua/?.lua` in the campaign config folder).
   This happens *before* the configs are loaded and before the classic script is parsed.
   The file is `luaL_dofile`d, so at this point it should only **define functions and register
   triggers**.
2. `load_script(lvnum)` — the classic `map%05d.txt` is parsed and its setup commands run.
3. `lua_on_game_start()` — calls the globals `OnCampaignGameStart()` and then the level's
   **`OnGameStart()`**. This is where Lua levels do their setup, usually through
   `RunDKScriptCommand("SET_GAME_RULE(...)")`, i.e. the classic command language embedded in Lua.
4. During play the engine calls global handlers the level defines: `OnGameTick`, `OnPowerCast`,
   `OnCreatureDeath`, `OnSpecialActivated`, `OnTrapPlaced`, `OnDungeonDestroyed`, `OnChatMsg`, ...
   (`Builtins.lua` feeds them into the trigger system; `RegisterTimerEvent`, `CreateTrigger` etc.).
   Trigger state is saved with the game (`Game` table, `serialisation.lua`).

Consequences for the editor:

- **A level can have `.lua` only.** `dungeon_architect` has `map07001.lua` (`require
  "dungeon_architect"`, the real code sits in that pack's `cfg/lua/`), no `.txt` of its own. The
  editor's writer always writes a `.txt` (an empty `REM` stub when there is no script): harmless, but
  it should not create one next to a Lua-only level.
- **Both scripts run; the `.txt` setup runs first, `OnGameStart()` after.** A Lua level that sets the
  same game rule wins. The editor's managed setup block (start gold, availability, creature pool)
  therefore still works on a Lua level, but Lua can override it.
- **Lua is a superset:** `RunDKScriptCommand("...")` executes any classic command, so a classic
  validator can check those strings; and Lua can also supply config (`config-api/*-templates.lua`).
- The Lua API is documented by stub files that ship with the game: `bindings/*.lua` (16 files,
  EmmyLua `---@param`/`---@return` plus doc comments), `classes/*.lua`, `triggers/*.lua`,
  `aliases.lua` (`---@alias` unions such as `power_kind`, `creature_type`). They are the source of
  truth for names and signatures, and they are read at run time, so custom builds stay in sync.
- Shipped Lua: 8 levels in the tree (4 of them `require` a 13 000-line pack module). Most Lua will
  be short glue or a big pack module; the editor's job is to show, edit, check and preserve it.

## 2. Goal and non-goals

Goal: a mapmaker can open a level that has a `.lua`, read and edit it, get useful feedback, playtest
it, and save it — without the editor destroying, ignoring or silently duplicating it. New levels can
opt in to a Lua script from a small template.

Non-goals: a Lua IDE (debugger, refactoring, full type checking), visual scripting, editing the
pack modules in a campaign's `cfg/lua/` (opening them read-only is fine), live hot-reload of Lua in
a running game, Lua *config* editing (that belongs to plan 03).

## 3. Data model

- `MapContent::lua_text` (string) and `MapContent::has_lua` (bool). The flag distinguishes "no
  file" from "empty file": the writer creates a `.lua` **only if the source had one or the user
  created one**, never an empty one by accident.
- Read in `MapContentReader::read_script()` next to the `.txt`; written verbatim (bytes,
  including CRLF and any BOM) by `MapContentWriter::write_lua()`, in both formats.
- `write_script()` stops writing the `REM Empty script` stub when `has_lua` and the `.txt` text is
  empty and no `.txt` existed (a Lua-only level stays Lua-only).
- Session copy in `editor_session.cpp` beside `s_editor_script_text`, with the same accessors and the
  same "Apply" model as the `.txt` editor (`editor_current_level_lua_text()` etc.), so unsaved Lua is
  part of Playtest, Save and the dirty flag.
- Sidecar helpers stop listing `.lua` as a sidecar (it is now saved), and Playtest copies it as part
  of the normal save instead of by file copy.

## 4. Slices

Each slice is independently shippable, with its own tests, and ends with a live-test note.

### L1. Round trip (removes the data-loss risk) — small

- Fields and reader/writer above; `has_lua` handling; no stub `.txt` for Lua-only levels.
- Save As / Playtest carry the Lua (the sidecar list shrinks accordingly).
- Tests: Catch2 round trip — present, absent, empty, CRLF, non-ASCII, BOM, 1 MB; a `.lua` next to a
  classic-format save and a KeeperFX-format save; unit test that a Lua-only level is not given a
  `.txt`. ftest: `editor_session` extended to open a Lua level (use the `dungeon_architect` pack
  copied to the test tree, or a two-line fixture) and confirm save/reload is byte-identical.
- Acceptance: open `map07001`, Save As to a new number, the new folder has an identical `.lua`.

### L2. Lua tab in the script editor — medium

- The script window becomes two tabs: **Script (.txt)** and **Lua (.lua)**. The Lua tab exists when
  the level has a `.lua`; otherwise the window offers **Add Lua script**, which creates one from a
  template (a header comment, an empty `OnGameStart()`, and an example `RegisterTimerEvent`).
- Same `TextEditor` widget with `Language::Lua()`, plus KeeperFX names as "known identifiers" in the
  existing known-value colour: API functions and constants from `bindings/*.lua`/`aliases.lua`,
  player and creature names from the engine tables.
- Apply, Reload, Validate, problem list: shared with the `.txt` tab (one problems list, each row
  tagged with its file).
- A one-line banner explains the run order (§1): "Runs before the .txt; setup you put in OnGameStart
  runs after it."
- **Pack modules** (`require "dungeon_architect"`): a "Open required file" action resolves a
  `require "name"` under the cursor through the same search path the engine uses and opens it
  **read-only** in a second tab. Editing pack code stays out of scope, but reading it is what a
  mapmaker needs to know what a level does.
- Acceptance: open, edit, Apply, Save, reopen: text identical; colouring shows API calls.

### L3. Validate — medium

Three layers, each reported in the shared problems list with file, line and severity:

1. **Syntax.** `luaL_loadbuffer` on the text in a throwaway `lua_State` (LuaJIT is linked;
   `kfx_editor` ranks above `kfx_script`). Nothing executes, no engine state is touched. The
   message's `[string]:LINE:` gives the row; clicking jumps there.
2. **API names (best effort, warnings).** Tokenise; identifiers followed by `(` that are not Lua or
   LuaJIT globals, not defined in the buffer (`function name`, `local name =`), not declared in the
   stub files, and not a field of a known table are flagged "unknown function". The stub files are
   parsed once at editor open (`---@meta` files: `function Name(`, `---@param`, `---@class`,
   `---@alias`). Known false-positive sources (dynamic tables, `require`d pack modules) are why this
   is a warning, and why symbols from a resolved `require` are added to the known set.
3. **Embedded classic commands.** For every literal string argument of `RunDKScriptCommand(...)`
   run the existing classic validator (`editor_script_validate`), so `"SET_GAME_RULE(PayDaySpeed"`
   is caught with the same messages as in a `.txt`.
   The `.txt` tab keeps its own checks; **Verify Map** aggregates all of them (as it already does for
   the `.txt`).

- Tests: Catch2 for the tokeniser, stub-file parser (feed it real `bindings/*.lua` from
  `config/fxdata/lua`), name check and string extraction; a sweep test over the 8 shipped `.lua`
  files that must report no syntax errors and few warnings (the same "must not cry wolf" test used
  for the classic scripts, `editor_strokes`).
- Acceptance: a typo in a function name and a missing `end` are both found; the shipped levels are clean.

### L4. Command / API browser for Lua — medium

- The Commands window gets a **Lua** page listing every function from the stub files: name,
  signature (`---@param`/`---@return` text), the doc comment, group = source file (`players`,
  `things`, `map`, ...). Filter box as on the classic page.
- Insert at cursor: a call template with placeholders taken from the `@param` names (`SetDigger(player,
  creature)`), and **Insert as classic command** for the many functions that have a `.txt` twin.
- Because the stubs carry real descriptions, this page will be better documented than the classic
  page (which only has ~35 descriptions).
- Tests: parser output for two known stubs; template generation.

### L5. Helpers that understand Lua levels — small

- The managed setup block, availability grid and message helper still write classic commands to the
  `.txt` (they run before `OnGameStart`). On a level with Lua each of them shows the one-line
  banner: "This level also has a Lua script; values it sets in OnGameStart override these."
- Level Settings, on the same condition, notes that `START_MONEY` etc. may be overridden by Lua.
- Option evaluated and rejected: generating Lua for these helpers (double the generators, and reading
  hand-written Lua back is unreliable).
- Optional later: a "Show in Lua" toggle that inserts the equivalent `RunDKScriptCommand` lines
  into `OnGameStart` instead of the `.txt`, for mapmakers who want a Lua-only level.

### L6. New-level template and quality of life — small, last

- New Map dialog checkbox "Lua script": creates the template `.lua` (and no `.txt`).
- Snippets menu in the Lua tab: timer, win condition, "when creature dies", "when power cast",
  taken from `triggers/Events.lua` signatures so they stay correct.
- Find/replace, if the widget offers it cheaply.

## 5. Interaction with the other plans

- **Plan 01 (sidecars):** L1 removes `.lua` from the sidecar list; directory-style sidecars
  (`cfg/`, `lua/` of a pack) remain a warning, and Playtest's copy of them stays.
- **Plan 03 (content editors):** a level's Lua can override config through the `config-api`
  templates; the config tools show "this level also has Lua" the same way the helpers do (L5).
- **Playtest:** Lua text is part of the snapshot that is written to the scratch level, so unsaved
  edits are played.
- **Preview Motion / undo:** unaffected (script text is not part of the journal; Apply marks dirty).

## 6. Risks and how they are handled

| Risk | Handling |
|---|---|
| Validator false positives annoy people | Syntax errors are errors; everything name-based is a warning; sweep test over shipped Lua; pack `require`s feed the known set. |
| Huge files (a pack module is 13 000 lines) | The pack file opens read-only in the same widget; syntax check runs on demand only, never per keystroke; measure before adding any live checking. |
| Encoding/line endings altered on save | Store and write bytes untouched; the message-helper CRLF rule is reused. |
| LuaJIT state creation cost in the editor | One short-lived state per Validate click; no state kept. |
| Level with both `.txt` and `.lua` edited by hand externally | Same behaviour as today for the `.txt` (the editor reads at open); no file watching. |
| Stub files missing in a stripped install | Name check silently disabled, syntax check still works; the browser page shows a note. |

## 7. Answers to the earlier open questions

- *Run order:* Lua file executes first (defining functions), then the `.txt` is parsed and run, then
  `OnGameStart()` — see §1.
- *Lua-only levels:* valid (`dungeon_architect`); the writer must not add a `.txt` (§3).
- *Fixtures:* the `dungeon_architect` pack is a good regression fixture for L1/L2 (needs its `cfg/`
  folder to open; copied into the test tree by the ftest data setup).

## 8. Order and size

L1 (1 session) → L3 syntax layer only (0.5) → L2 (1) → L3 name and embedded-command layers (1) →
L4 (1) → L5 (0.5) → L6 (0.5). L1 alone removes the data-loss risk; L1 + L2 + L3 syntax is the
minimum that makes Lua levels genuinely editable.

## 9. Still open

- Should Verify Map warn about a level whose `.txt` and `.lua` both set the same game rule? Needs a
  parser for `OnGameStart` bodies; probably not worth it.
- Whether Apply for Lua should offer to run Validate first (default off).
- Whether to surface `Game` table serialisation caveats ("things not in `Game` break save games")
  as a lint hint; a documentation link may be enough.
