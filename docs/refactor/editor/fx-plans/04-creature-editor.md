# FX plan 04 — creature editor

Status: **C1, C2, C3, C5 and C6 (menu entry) built** (§10). Icon pickers with thumbnails (C4's icon part) are not. Depends on the shared base in
[03](03-content-editors-foundation.md) (targets, layers, `cfg_*` modules, Tools menu). Abilities
(`[instanceN]`) and creature spells are edited in [06](06-spell-ability-editor.md); this plan links to
them but does not duplicate them.

## 1. Goal

A mapmaker or campaign author can rebalance an existing creature — its health, strength, pay, hunger,
what attracts it, what annoys it, how it levels — for **one level or a whole campaign**, compare it
with the stock value, and see the result in Playtest. This is what editing `creature.txt` was for in
the 1998 editor, with source badges and Reset instead of a whole-file replacement.

## 2. What the engine gives us

Two file families (verified):

| Family | File | Layers | Content |
|---|---|---|---|
| Global | `creature.cfg` | base → campaign `CONFIGS_LOCATION` → level `map%05d.creature.cfg` | `[common]` (creature list, counts), `[experience]` (per-level percentage growth), `[jobN]` ×24, `[angerjobN]` ×10, `[attackprefN]` ×3, `[instanceN]` ×57 |
| Per creature | `creatrs/<name>.cfg` (36 files) | base → campaign `CREATURES_LOCATION/<name>.cfg` → level `map%05d.<name>.cfg` | nine sections: `[attributes]` (27 keys), `[attraction]`, `[annoyance]` (24), `[senses]`, `[appearance]`, `[experience]`, `[jobs]`, `[sprites]` (24), `[sounds]` (11) |

- File name = lower-case creature code name (`get_conf_parameter_text(creature_desc, …)`); the creature
  list order in `[common] Creatures =` matters and is **never touched**.
- The per-creature parser is hand-written (`config_crtrmodel.c`), so there is no field table to derive
  a schema from. The **schema is curated** (§4), seeded from the parser's key list and checked by a
  test that fails when a key the parser accepts is missing from the schema (grep-style extraction in
  the test, or a parser `--dump-keys` test seam).
- Shipped examples of partial creature files are tiny (`levels/classic_crtr/imp.cfg` is 7 lines), so
  diff-only output matches how authors already work.

**Typical use** (decision 1): open the editor from the main menu, pick a campaign such as *Necro*, and
edit its creature definitions in its own folders (`CONFIGS_LOCATION` = `necro_cfgs/`, creature files in
`CREATURES_LOCATION`), with no map loaded. The layout keeps room for adding creatures later (each
creature is already a file of its own in the campaign's creature folder).

## 3. Screen design

```
┌ Creature Editor ─ Target: [Campaign: Tyraels Realms ▾] [Level: — ▾] ─────────────────┐
│ ┌ creatures ─┐  ARCHER            Layer: Base                    [Advanced ☐]        │
│ │ icon grid  │  ┌ Attributes ┬ Attraction ┬ Annoyance ┬ Jobs ┬ Experience ┬ More ▾ ┐ │
│ │ (phase 6   │  │ Health      300  ▲ Base 300        [Reset]                       │ │
│ │  thumbs),  │  │ Strength     20                                                  │ │
│ │ filter box,│  │ Pay          63   ● Campaign 80 (base 63)   [Reset]              │ │
│ │ ● = edited │  │ Properties  [x] BLEEDS  [x] HUMANOID_SKELETON  [ ] FLYING …      │ │
│ └────────────┘  └───────────────────────────────────────────────────────────────────┘ │
│  Level preview: L1 300 | L2 405 | L3 … (from [experience] percentages)   [Apply][Close]│
└────────────────────────────────────────────────────────────────────────────────────────┘
```

- **Creature list**: the icon grid from phase 6 (`editor_thumbs`), one tile per creature; a dot on
  every creature the **target layer** already customises; a filter box; heroes vs evil grouping as the
  Creature tool does.
- **Tabs mirror the file sections**; "More" holds `Senses`, `Appearance`, `Sprites`, `Sounds`
  (advanced; read-only by default because they are indices into sprite/sound tables).
- **Field rows** come from `content_ui::field_row` (03 §4): label, widget by kind, source badge,
  base-value tooltip, Reset. Widgets: int (with the schema's range), int pair (`Size = 200 512`),
  enum (`AttackPreference`), flag list (`Properties`, checkboxes from the property name table), name
  picker (`LairObject` → object list; `EntranceRoom` → room list with room icons; `Powers` →
  spell/ability list from [06]), text id (shows the resolved string beside the number).
- **Experience preview**: computed table of the stats for levels 1–10 from `[experience]`
  (`HealthIncreaseOnExp`, `StrengthIncreaseOnExp`, `PayIncreaseOnExp`, …). Helps the author see what
  a change does at level 5. Formula copied from the engine's own level scaling; a unit test pins them.
- **Compare view**: toggle showing only the keys where the target layer differs from base, across
  *all* creatures — the campaign's "balance sheet".
- **Global tab** (creature.cfg `[experience]` percentages, `[common]` counts read-only, jobs and anger
  jobs, attack preferences): behind *Advanced*, because `JobsCount`, `AngerJobsCount` and the lists
  must stay consistent with the tables and are not a balancing tool. The percentages are.

## 4. The schema (hand-written, tested)

`creature_schema.cpp`: for each of the nine sections a table `{key, kind, min, max, names, help,
advanced}`. Help text is read from the base file comments at run time. Kinds and name tables map to
existing engine tables: `creature_desc` (creature names), `creatrstate_desc`, room names
(`room_desc`), power names (`power_desc`), object names (`object_desc`), the creature property flags
(`creatureprop_desc` — confirm the table name during S-C1), attack preference names. Keys that are
function names or animation ids are `advanced` and shown read-only in v1.

## 5. Where edits go

| Key belongs to | Level scope writes | Campaign scope writes |
|---|---|---|
| a creature section | `levels/map%05d.<creature>.cfg` | `<CREATURES_LOCATION>/<creature>.cfg` |
| `creature.cfg` global part | `levels/map%05d.creature.cfg` | `<CONFIGS_LOCATION>/creature.cfg` |

The `[creatureN]`-style indexing does not apply here: model files are per creature, so the file name
is the identity and sections are named. (Foundation rule 3 still holds for `[instanceN]`, `[jobN]`.)

## 6. Slices

| Slice | Content | Tests |
|---|---|---|
| **C-S1 spike** | Enumerate what `config_crtrmodel.c` accepts per section (keys, value shapes, ranges) into `creature_schema`; confirm `IgnoreErrors`/partial behaviour of the level layer for a model file; confirm the property/room/object name tables. | schema-vs-parser key coverage test |
| **C1** | Read-only viewer: creature list + tabs showing the effective value and its layer (Base / Campaign / Level). | stack values equal live `creature_stats` for all 36 creatures (correctness anchor) |
| **C2** | Editing: field rows, Reset, diff-only write to the right file for the active scope, dirty/Apply. | edit → write → real loader reads it (ftest with the scratch level number) |
| **C3** | Experience preview and the compare ("balance sheet") view. | formula test against engine values at levels 1–10 |
| **C4** | Pickers with icons (rooms, objects, spells), Properties checkboxes, text-id resolution. | picker lists equal engine name tables |
| **C5** | Global tab (`[experience]` percentages, jobs, attack preferences) behind Advanced. | round trip |
| **C6** | Map Editor integration: *Tools ▸ Creature Editor* opens on the map's level target; Creature tool tooltip "edited in this level"; Verify Map line. | — |

Order: S1 → C1 → C2 → (C3, C4 in either order) → C5 → C6. C2 is the usable milestone.

## 7. Interactions

- **Map Editor:** Level Settings' creature pool and the availability grid stay as they are (who
  appears); this editor changes *what they are*. Query tool shows effective stats (already live).
- **Plan 06:** a creature's `Powers` / `PowersLevelRequired` and the `[instanceN]` it uses are edited
  through the ability picker; "open in Spell and Ability Editor" jumps there.
- **Plan 08:** the campaign editor's *Config files* page lists creature files it finds and opens them here.
- **Lua:** creature Lua templates are read-only (foundation §2.9).

## 8. Non-goals

New creatures (adding a model, its sprites, its entry in `Creatures =`), sprite and sound editing,
editing the creature list order, changing the count keys (`JobsCount`, …).

## 9. Risks and open questions

| Risk | Mitigation |
|---|---|
| Hand-written parser drifts from the schema | Key-coverage test; correctness-anchor test at base layer |
| Balance values with hidden coupling (`SlapsToKill`, `GoldHold`, `HungerRate`) | Help text from the file; *Advanced* toggle; preview table |
| A campaign already carries a near-full copy of a creature file | Layers are per key, so it shows as "Campaign" on every key; the compare view makes the real differences visible; Reset per key or "reset all to base" removes them |

Open: should the compare view export a text report (for changelogs)? Should heroes and evil creatures
be edited side by side (a two-creature diff)?

## 10. Built (results)

**Curated schema** (`cfgc_schema_creature.cpp`, kfx_config): the nine sections of a creature model file (kind `creaturemodel`)
and `creature.cfg` (new kind `creature`: `[common]`, `[experience]`, `[instanceN]`, `[jobN]`, `[angerjobN]`, `[attackprefN]`). Keys
come from the exported key tables; each key's value shape is a compact spec (numbers, names from a registry with literal
extras such as `NULL`, flag lists from a static table or a registry, arrays such as `Powers` = ten instance names and
`LevelsTrainValues` = nine numbers). Flags such as `Properties` and `SpellImmunity` use the loaders' own name tables
(`creatmodel_properties_commands`, `spell_effect_flags`). New registries from creature.cfg: `instance`, `creaturejob`,
`angerjob`, `attackpref`; a layer's `Creatures` list **replaces** the one beneath (the loader clears its table). Tested:
every shipped creature file (37 model files + `creature.cfg`) validates with **no warning**; the schema coverage test now
covers `creature` too (two dead keys recorded: `InstancesCount`, `SwapCreatures`).

**Creature Editor window** (in both Tools menus): a list of the creatures in creature.cfg order (marked `*` when this layer
has that creature's file), tabs for the nine sections (Attributes, Attraction, Annoyance, Jobs, Experience, Appearance, Senses,
Sprites and Sounds; the last two are shown read only, they are table indices), **Preview** (each stat at experience levels 1-10
from the creature's values and creature.cfg `[experience]`: `base + percent * base * level / 100`, the engine's own formula),
**Compare all** (every creature side by side: health, strength, armour, defence, dexterity, luck, pay, hunger, gold, slaps to kill,
coloured by layer) and **Global** (the `[experience]` growth percentages, creature.cfg). Edits go to the creature's own file
for the layer (`<CREATURES_LOCATION>/<creature>.cfg`, or `map%05d.<creature>.cfg`); global edits to creature.cfg of the layer;
Apply writes both, one file at a time, each atomically.

**Form additions:** flag/name-list popups for registries too (job lists, `HostileTowards`, `SpellImmunity`, `Properties`); an empty
list writes an empty value; a value list with a repeating name part is one popup.

**Tests:** Catch2 for the experience formula, the creature file shapes, and editing a model file on its own layer file (header,
unknown-property diagnostic); ftest `config_content_creature_editor` (C2 acceptance: a creature's Health, the global
`[experience] HealthIncreaseOnExp` and an ability's Time edited at level scope before the level loads; the running game uses all
three, and the preview formula equals the engine's `compute_creature_max_health()` at level 4); the smoke ftest draws every tab
for two creatures in both hosts; **C1 correctness anchor** (in `config_content_anchor`): for all 36 creatures the stack (base,
campaign creature folder, level file) equals the live model for eight numeric attributes (284 values).

**Finding:** a level's *script* can change creature stats at run time (keeporig level 1 has `SET_CREATURE_ARMOUR(KNIGHT,30)`); that is not a
config layer, so the live value differs from every layer. The anchor leaves such (creature, attribute) pairs out. The editor does
not yet warn about them (a banner listing the script's `SET_CREATURE_*` lines is a follow-up, like the Lua banner of plan 03 §6).

**Not built:** icon pickers and the creature tile grid (need phase 6 thumbnails), room/object pickers with icons, text-id
resolution, the `[jobN]`/`[angerjobN]`/`[attackprefN]` editors (advanced), creating creatures.
