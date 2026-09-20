# FX plan 03 — content editors: shared foundation

Status: **plan, third pass** (replaces the first-pass "03 level content editors"). Nothing built. This
document is the shared base for the editors, one plan each:

| Plan | Editor | Files it edits |
|---|---|---|
| [04](04-creature-editor.md) | Creature editor | `creature.cfg`, `creatrs/<name>.cfg` |
| [05](05-trap-door-editor.md) | Trap and door editor | `trapdoor.cfg` |
| [06](06-spell-ability-editor.md) | Spell and ability editor | `magic.cfg`, `[instanceN]` of `creature.cfg` |
| [07](07-room-editor.md) | Room editor (+ slabs) | `terrain.cfg` |
| [08](08-campaign-editor.md) | Campaign / mappack editor | `campgns/<id>.cfg`, `levels/<pack>.cfg`, campaign config folders |
| [09](09-text-strings-editor.md) | Text editor | campaign `text_<lang>.dat`, level `map%05d.<lang>.dat` |

Plus the first consumer that proves the pipeline: the **Rules editor** (§9, F4), a level/campaign
dialog for `rules.cfg`.

## 1. Decisions (from the user, this session)

- The old decision "per-campaign rules are out of scope" is **reversed**: campaigns get their own
  editor, and every content editor works on a **scope** (§3): a *level* or a *campaign / mappack*.
- The editors are reached from a **Tools** menu; the in-game editor is renamed **Map Editor**.
- Each editor has its own plan file (this set).
- Building is incremental: foundation first, then one editor at a time; each slice is shippable.

Answers to the first review (all confirmed):

1. **Standalone hosting:** the content editors open **from the main menu with no map loaded**. The
   main use case is editing a campaign's own definitions (e.g. `campgns/necro_cfgs/`).
2. **Campaign context:** a map can be **tested as part of a campaign** (its campaign layer then
   applies), and a **campaign can be play-tested from the Campaign Editor** (§6).
3. **v1 edits existing content only** (creatures, traps, doors, spells, abilities, rooms, slabs); no
   creation. The folder workflow (item 5) deliberately keeps the door open for creation later.
4. **Text is in scope.** A campaign's `text_<lang>.dat` (objectives, level names) is edited, and so
   is a level's `map%05d.<lang>.dat`: new plan [09](09-text-strings-editor.md).
5. **Workflow:** *build the campaign definition first, then make and edit its maps inside the
   campaign's own folder.* A map that lives in the campaign folder sees the campaign's creature,
   trap, spell and room definitions from the start, and later additions of new content (out of scope
   now) will apply to it without copying anything. Consequence: the "duplicate / copy a level between
   campaigns" features are **not needed** for this workflow (kept as an optional late slice in 08).

## 2. What the engine gives us (verified in the source)

1. **Layered, partial config.** `load_config()` (`kfx_config/src/config.c:2422`) loads a file as:
   base `fxdata/<file>` → mods → campaign `CONFIGS_LOCATION/<file>` → mods → level
   `levels/map%05d.<file>` → mods. Campaign and level layers are read with `AcceptPartial |
   IgnoreErrors`, so **a layer file only lists what it changes**. Shipped data is written that way:
   every campaign and mappack carries a `*_cfg` folder of partial files ("KeeperFX Partial Traps and
   Doors Configuration… do not replace global config with this file"); e.g. `levels/classic_cfgs/
   trapdoor.cfg` is 14 lines. In `core_files` every campaign and mappack that has a config folder (20 or more)
   ships partial files there, mostly `rules`, `creature`, `magic` and `trapdoor` (plus `keepcompp`,
   `objects`, `terrain`, `effects`, `slabset`, `columnset` in a few). Per-level overrides exist too:
   `map%05d.rules.cfg` (7 levels), `objects` (4), `magic` (4), `creature` (4), `trapdoor` (3), `terrain`
   (3), and individual creature files such as `map0000N.thief.cfg`, `spider`, `horny`, `ghost`.
   **Campaign scope is the common case;
   level scope is the rarer one.**
2. **Creature model files are layered per level too** (Spike S2, answered): `load_creaturemodel_
   config` (`config_crtrmodel.c:2890`) reads `creatrs/<name>.cfg` (base) → `CREATURES_LOCATION/<name>.cfg`
   (campaign) → `levels/map%05d.<name>.cfg` (level), each with `IgnoreErrors` after the first success.
3. **Sections are numbered, keyed by position, and carry a `Name`.** A partial file says
   `[door2]` + `Name = BRACED` + the changed keys. The index must match the base file's index; the
   editor maps *name ⇄ index* from the base document. Adding items beyond the base count is
   "new content" (non-goal for v1).
4. **The loaders already have typed field tables** for most files: `struct NamedField` (`name, type,
   min, max, namedCommand, parse_func, assign_func`) grouped in `struct NamedFieldSet`. Present for
   rules, trapdoor (door, trap), terrain (slab, room), objects, effects, cubes, lenses, crstates,
   compp, and magic (shot, powers). **Not** table-driven: `creature.cfg` and the creature model files
   (`config_crtrmodel.c`, 3 039 lines of hand-written parsing) and the spell/special parts of magic.
5. **Parsing and assigning are separate steps** (`parse_named_field_value` vs `assign_named_field_value`),
   but the parse functions are **not** a safe validator: `value_name` writes into the live struct,
   `value_default` silently clamps, and messages use engine globals. Validation therefore uses the
   content layer's own value grammar ([10](10-config-content-model-and-writers.md) F3).
6. **The base `.cfg` files are documented in-line** (`; comment` above nearly every key, e.g. all of
   `creature.cfg [instanceN]`). Field help text can be lifted from the base file at run time, so custom
   builds stay in sync.
7. **No live re-apply.** Config is (re)loaded at level/campaign start and its loaders have side
   effects (`post_load_func`). Edits take effect on the next level start — **Playtest** for a level,
   the next launch for a campaign. This is v1's rule; live apply is out (Spike S4).
8. **Some keys are lists.** `[research]` repeats `Research = MAGIC POWER_HAND 250` once per item, and
   `Power = …` / `Cost = …` in `[powerN]` are per-level arrays. A layer that contains a `[research]`
   block **replaces** the whole list (`config_rules.c:465`, "clear research list if there's new one in
   this file"), it does not append. Each list-shaped key therefore needs its own merge rule in the
   stack (replace-whole vs per-element) and its own widget (ordered list editor, per-level array).
   Confirming the rule for every list key (`[research]`, `[sacrifices]`, `Creatures =`, `[jobs]` lists)
   is **Spike S5**, done before the editors that use them.
9. Lua can also override config (`config-api/*-templates.lua`); `ActivationLuaFunc`, `UpdateFunction`
   and similar keys are shown read-only, never rewritten (Spike S3).

## 2a. Spikes (status)

| Spike | Question | Status |
|---|---|---|
| S1 | Can values be validated / read per layer without a loader dry-run? | **Answered** (§2.4–2.5): text-level layer reader + `parse_func` validation; no loader changes beyond exporting field sets |
| S2 | Are creature model files layered per level? | **Answered** (§2.2): yes, `map%05d.<name>.cfg` |
| S3 | Lua config templates | Open: list the Lua-coupled keys per file while building each schema; read-only |
| S4 | Which keys the running game re-reads mid-session | Open, only needed if live apply is ever wanted (v1: no) |
| S6 | `change_campaign()` from an editor session; scratch level vs campaign level folders (§10) | Open; before the Playtest-as-campaign slice |
| S5 | Merge rule for list-shaped keys (`Research`, `Cost`, `Power`, `Creatures`, `[sacrifices]`) | Open; before F4 (research) and 06 |

## 3. Scope: what a target is

```
Target := Level(lvnum, levels folder)                       -> levels/map%05d.<file>, map%05d.<creature>.cfg
        | Campaign(campaign or mappack .cfg)                -> CONFIGS_LOCATION/<file>, CREATURES_LOCATION/<creature>.cfg
Base   := fxdata/<file>          read-only, shown as "Base" in every comparison
```

Effective value of a key at a target = the last layer that sets it (base → campaign → level). Each
field row shows the **source layer** (Base / Campaign / Level) and, when the target's own layer
overrides something, the value it replaces. **Reset** deletes the key from the target's file.

A Level target implicitly sees its campaign's layer beneath it (from the level's location: the
campaign that lists it, or none for the Editor Maps mappack / a plain mappack). Mods
(`mods_conf`) are read as layers for display but never written.

## 4. Architecture

**The data, reader and writer layer is its own plan: [10](10-config-content-model-and-writers.md).** It
lives in `kfx_config`, is engine-decoupled (explicit paths, no loaded config, no ImGui), and follows the
map writer's precedent: plain-data content, a schema reflected from the engine's field tables, a
lossless line-oriented document model, and a **parent / child family of writers and readers** (one child
per file kind: trap/door, terrain, magic, rules, creature, creature model, campaign, strings). The same
layer is what a future LLM or command-line tool will use.

What stays in `kfx_editor` (this plan):

| Module | Job |
|---|---|
| `content_ui` | Shared ImGui widgets: `field_row` (label, widget by kind, source badge, base tooltip, Reset; text-id fields show the resolved string beside the number and a link into the Text editor, [09](09-text-strings-editor.md)), icon pickers (phase 6 thumbnails), diff panel, dirty marker. No new toolkit. |
| `content_tools` | Registry of tools, one `open(tool, target)` entry point, the shared Apply/Revert/Close chrome, the host callbacks (§5), and the adapter that fills a `ConfigTarget` from the live game or the campaign picker. |
| per-editor screens | Built in [04]–[09] on `ChangeSet`s and `ConfigStack` reads. |

Layering: the editors depend on the content layer, never the reverse. The only cross-layer additions are
the `NamedFieldSet` accessors in `kfx_config` and the host callbacks (§5), in the usual `*Callbacks` pattern
(`kfx_config` header, implemented in `kfx_editor`, wired in `main.cpp`).

## 5. Hosts, the Tools menu and the rename

**Two hosts, one implementation** (standalone is the primary one: decision 1).

- **Main menu → Tools** (a modal today with one button, `frontgui_screens.cpp:1461`): entries become
  *Map Editor*, *Creature Editor*, *Trap and Door Editor*, *Spell and Ability Editor*, *Room Editor*,
  *Campaign Editor*, *Text Editor*, *Back*. The content tools need no map, so they open as ImGui windows over the
  menu. Because `kfx_frontend` ranks below `kfx_editor`, the frontend calls a `ContentToolsCallbacks`
  struct (`open(tool)`, `frame()`, `is_open()`), implemented in `kfx_editor`, wired in `main.cpp`.
  The frontend draws `frame()` each menu frame.
- **Map Editor menu bar → new `Tools` menu**: the same entries, opened with the **Level target
  preselected** to the map being edited (the map must be saved once so it has a folder and number).
  The Campaign Editor opens with no target.

**Target picker (standalone).** A level-or-campaign chooser at the top of every content window:
pick a campaign / mappack from the campaign list the frontend already builds (`config_campaigns`),
optionally a level inside it. Default: the last-used target (remembered in the editor settings).

**Rename.** User-visible strings only; the library, files and CMake targets keep their names.

| Where | Change |
|---|---|
| Main menu Tools modal | "Editor" → "Map Editor" |
| Map editor menu bar / window titles / dialogs | "Editor" wording → "Map Editor" where the tool is named |
| Key-binding tab | "Editor" → "Map Editor" (`frontgui_screens.cpp:703`) and the `Editor:` prefixes in `config_settings.c` descriptions |
| `docs/level_editor.txt` | Retitled "Map Editor", file renamed `docs/map_editor.txt` (update links), new "Content editors" chapter |
| Architecture / CLAUDE.md | one-line mentions |

## 6. Save, Save As, Playtest, Verify

- **Level scope**: overrides are `map%05d.<file>` beside the map. Plan 01 already copies every
  `map%05d.*` sidecar on Save As and to the Playtest scratch level, so **no new copy code** is needed;
  the Save As confirm list (which names sidecars) simply stops listing files the editors own once
  Save As carries them by default (a checkbox, default on, in the existing confirm).
- **Campaign scope** files are written immediately on Apply (they are not part of any one map's
  save). The dirty marker and an "unsaved changes" prompt guard closing.
- **Playtest** of a level picks up level-scope edits. Campaign-scope edits apply when the level is
  started *as part of that campaign*; the Editor Maps mappack has no campaign layer (§10 question 2).
- **Verify Map** adds: "this level overrides N config files", and schema diagnostics for override
  keys the loader would ignore.
- **Lua** (plan 02): when a level has a Lua script the tools show a banner ("Lua may override these
  values at run time"), as the script helpers already do.

## 7. Safety and clarity rules for every editor

1. Never write a key equal to the layer beneath it (no-op edits vanish; Reset is deleting).
2. Never edit the base `fxdata` files. Ever.
3. Show name **and** index (`[trap3] BRACED`), and never re-number sections.
4. Unknown keys, comments and ordering in a target file are preserved on write.
5. Each key has one of three states in the UI: inherited (grey), overridden here (normal + Reset),
   overridden here *and* identical to base (offer to drop).
6. "Advanced" keys (function pointers, animation ids, sprite indices, role flags) are hidden behind a
   per-editor toggle so the default view is what a mapmaker balances.
7. Changing **balance-critical global keys at campaign scope** (e.g. creature `Health` for every level
   of the campaign) shows a one-line note of how many levels the campaign lists.

## 8. Slices

| Slice | Content | Size |
|---|---|---|
| **F0** | Rename to *Map Editor* (§5 table); Tools modal lists the new entries (greyed "coming soon" until built); `Tools` menu in the map editor bar; `ContentToolsCallbacks` skeleton and an empty *content_tools* window host. | small |
| **W0–W5** | The content layer of [10](10-config-content-model-and-writers.md) (lossless document, schema reflection, layer stack, writer parent + `TableConfigWriter` + `TrapDoorConfigWriter` + `RulesConfigWriter`, anchor and read-back tests). **Replaces the former F1 and F2.** | large (see 10 §8) |
| **F3** | **Raw config editor**: pick a file, edit the target's layer as text with `.cfg` colouring, Validate (schema diagnostics), Apply. Includes the target picker (level / campaign) and the "effective view" side pane. Ships first as the immediate escape hatch for everything (also `.toml` files as plain text). | medium |
| **F4** | **Rules editor** (`rules.cfg` `[game] [computer] [creatures] [magic] [rooms] [workers] [health] [research] [sacrifices]`): first structured editor, grouped as the file groups it; proves widgets, source badges, Reset, Playtest round trip. | medium |
| **F5** | Playtest campaign context (§10): session campaign context, "Test as part of", `change_campaign` round trip and restore. | small-medium (after S6) |
| then | [05 trap/door] → [07 room] → [06 spell/ability] → [04 creature]; [08 campaign] and [09 text] in parallel (they need only F0 and the content layer, [10]). | see each plan |

Acceptance for F0, W0–W5, F3–F4: rename done and searchable; a level with `map%05d.rules.cfg` opens in the Rules
editor showing Level/Campaign/Base values; changing `[game] PayDaySpeed`, Save, Playtest: the running
game uses it; Reset removes the key and, if it was the last, the file.

## 9. The Rules editor (F4) in brief

`rules.cfg` is table-driven (`rules_named_fields_set`, `ruleblocks[8]`), so its editor is generated
from the schema plus a small grouping table: *Game* (gold, mana, speeds, pay day), *Creatures*
(limits, breeding, fight rules), *Magic*, *Rooms*, *Workers* (imps), *Health*, *Research* (order and
costs; a level-indexed list widget), *Sacrifices* (recipe list). At campaign scope it replaces the
first pass's "C1 level rules"; at level scope it is the same window with the Level target.

## 10. Campaign context and Playtest (decision 2)

Today Playtest saves the map to a scratch level number and starts it like the `-level` argument, which
runs it under whichever campaign is current (none for the Editor Maps mappack), so no campaign layer
applies. Design:

- The Map Editor session gets a **campaign context**: the campaign or mappack the map belongs to
  (found from its folder: the pack whose `LEVELS_LOCATION` contains it; chosen in Save As, see 08 §3).
  Shown in the menu bar next to the map name and in Level Settings.
- The Playtest confirm gains **"Test as part of: [campaign ▾]"** (default: the map's own campaign;
  "None" = base + level only). Playtest calls the engine's own `change_campaign()` with that pack
  before starting the scratch level, so `CONFIGS_LOCATION`, `CREATURES_LOCATION` and the campaign
  strings apply; the previous campaign is restored when the session returns.
- The Campaign Editor has **Play level…** (any level in its lists) and **Play campaign from level N**,
  through the normal single-player start; the run returns to the Campaign Editor when it ends, using
  the same "return to origin" mechanism as Playtest (`editor_playtest_begin`, generalised from
  "the map editor" to an origin of *map editor* or *content tool*).
- Scratch-level sidecars still come from plan 01; a level inside a campaign folder is played in
  place (no scratch copy is needed for Campaign Editor runs).

Spike **S6**: confirm that `change_campaign()` and the scratch level's config layering behave as
above when called from the editor session (the level folder for a campaign level differs from the
scratch folder). Done before the Playtest work.

## 11. Non-goals

Mod authoring (`mods/`), sprite / animation / sound editing, column and slab-set editing, new
creatures/traps/spells/rooms, live apply, editing the base `fxdata` files.

## 12. Risks

| Risk | Mitigation |
|---|---|
| Hand-written creature/spell parsing has no field table | Curated schemas (04, 06) seeded from the parser's key list and the base file's comments, with the correctness-anchor test per key. |
| Section index drift between base and a customised campaign base | Always resolve name ⇄ index against the *campaign-effective* document, not the stock base. |
| Editing a value the loader silently ignores | Schema-only keys; the raw editor lists unrecognised keys. |
| Large surface | F0–F4 each ship alone; every editor after F4 is independent and droppable. |
