# FX plan 10 — config content model, readers and writers (investigation and design)

Status: **investigation done, design proposed, nothing built.** This replaces the module sketch in
[03](03-content-editors-foundation.md) §4 (`cfg_document`, `cfg_stack`, `cfg_edit`, …) with a design
that follows the map writer's precedent: an **engine-decoupled content layer** with a **parent /
child class family for writers and readers**. The editors ([04]–[09]) are one client of it; the
longer-term goal (an LLM, or any external tool, reading and producing content as structured data)
is the other, and it shapes every decision here.

## 1. Why a separate content layer

The map editor's save path deliberately does **not** touch live engine state: `MapContent` (plain data,
`kfx_sim/map_content.h`) plus `MapContentWriter` / `MapContentReader` and one concrete subclass per
format take an explicit directory and are unit-tested without a game
(`phase3/00-slice1-native-save.md`, "Architecture for this slice"). The rationale recorded there —
Strategy pattern (`LensEffect`, `IPlatform` style), testability, and "the structured, engine-decoupled
format the user's longer-term LLM-facing goal needs" — applies unchanged to configuration:

- the editors must be able to **write** creature / trap / spell / room / rules / campaign / text
  files, and read them back;
- the same read/write code must run with **no window, no ImGui, no loaded engine config**, so a
  tool can drive it: read the effective content of a target, apply a change, validate, write;
- the production loaders stay untouched (as with `load_tngfx_file`), so gameplay loading is not
  destabilised; **correctness is proven by tests that compare the two** (§8).

## 2. What already exists (verified)

| Piece | Where | Use for us |
|---|---|---|
| Map DTO + writer/reader hierarchy | `kfx_sim/include/map_content*.h` | The pattern to copy: concrete `write()` orchestrates; shared pieces concrete; format pieces virtual; explicit `dir` argument; Catch2 round-trip tests in a scratch dir |
| Atomic file write | `LbFileSaveAtomic` (`kfx_platform`), used via `save_text()` | Every config write goes through it |
| Line-preserving keyed patcher | `keeperfx_cfg_write_values_to_file(fname, edits[], n)` (`config_keeperfx.c:1447`) | Proof that **patch-in-place** is the accepted approach for hand-maintained `.cfg`; but flat (no sections) and **not atomic** (`LbFileOpen NEW`) |
| Declarative schema for settings | `SettingOption` table (`config_settingschema.c/.h`): key, type, range, category, get/set | A schema pattern already accepted in this codebase; our `FieldSpec` is its config-file cousin |
| Field tables | `NamedFieldSet` / `NamedField` for rules, trapdoor (door, trap), terrain (slab, room), objects, effects, cubes, lenses, crstates, compp, magic (shot, powers) | Source of names, types, min/max and static enum tables |
| Exported key tables for hand-written parsers | `creatmodel_attributes_commands[]`, `…_annoyance_…`, `magic_spell_commands[]`, `magic_special_commands[]`, … (non-static `NamedCommand`) | Key lists for creature and spell/special files **without transcribing them** |
| In-game TCP JSON API | `kfx_script/src/api.c` (`api_enabled`, port 5599; `map_command`, `console_command`, subscribe) | A channel to the **running game**; not for authoring files. The content layer is the offline counterpart |
| Flat CMake globs, C++ allowed in `kfx_config` | `src/kfx_config/CMakeLists.txt` | New files go in `src/kfx_config/src/` with a `cfgc_` prefix; `kfx_config` depends only on `kfx_platform` |

## 3. Findings that shape the design

**F1 — The files in the wild are messy; the model must be lossless and line-oriented.** A survey of all
857 shipped `*.cfg` (149 150 lines): 121 934 key lines, 10 117 sections, 6 489 comments, 10 599 blanks;
**165 files use CRLF**; **159 repeated keys inside one section** (legitimate lists such as
`[research]` / `[sacrifices]`); lines with **no `=`** that the loader accepts (`Name  THIEF`,
`AreaDamage 2 0 -40`); 23 values with an inline `;`; **banner "sections"** such as
`[#### MELEE BASED ####]`; lower-case keys; a stray `^Z` byte. A regenerate-from-scratch writer would
destroy authors' comments and layout; edits must **touch only the lines they change**.

**F2 — Section identity is `basename + index`, matched case-sensitively; keys case-insensitively.**
`parse_named_field_blocks` (`config.c:1216`) uses `memcmp` on the block basename (`trap`, `door`, …) and
a decimal index; a block whose index is beyond the current count **extends the list**. So a partial
file can already *add* items — the readers must model it even though v1 editors do not create content.

**F3 — The loader's per-field parse functions are not pure or reusable for validation.** `value_name`
writes straight into the live struct (`config.c:640`); `value_default` **silently clamps** out-of-range
numbers with a warning; messages use global `text_line_number`. My earlier claim (03 §2.5) that
parse/assign separation makes validation free was too strong. The content layer needs its **own value
grammar** per field kind and must *report* what the loader would clamp.

**F4 — Names are dynamic registries.** 21 `NamedCommand` arrays (`creature_desc`, `trap_desc`,
`door_desc`, `slab_desc`, `room_desc`, `object_desc`, `shot_desc`, `spell_desc`, `power_desc`,
`instance_desc`, `effect_desc`, …) are **filled at load time** from each section's `Name =`. In a
headless tool they are empty. The content layer builds an equivalent **`NameRegistry` from the
documents** (base + campaign + level), and a test checks it equals the engine's tables.

**F5 — "Table-driven" files still contain bespoke blocks.** Checking the key baseline against the field
tables: `trapdoor` (61 table fields vs 64 keys in shipped files; extras: `DoorsCount`, `TrapsCount`),
`terrain` (33 vs 45; `[block_health]` and the `*Count` keys), `objects` (43 vs 46), `rules` (93 vs 108;
the `[sacrifices]` and `[research]` list blocks, `MkCreature`, `PosUniqFunc`, …). So each kind needs a
small hand-written child for its non-table blocks, on top of generic table handling.

**F6 — List-shaped keys need merge rules.** `[research]` replaces the whole list when a layer contains
the block (`config_rules.c:465`); `Cost` / `Power` are fixed-length arrays; `Creatures =` is an
ordered name list. Each is declared in the schema as `List{replace | per-element}` and handled by the
kind's writer (foundation Spike S5 still applies).

**F7 — Creature and spell/special parsing is hand-written but its key tables are exported**
(`creatmodel_*_commands[]`, `magic_spell_commands[]`, `magic_special_commands[]`). Keys come from
them; types, ranges and enum tables are a small curated overlay (03 §4, 04 §4, 06 §4), checked by
a coverage test that fails when the parser gains a key the schema lacks.

**F8 — TOML files (`effects`, `slabset`, `columnset`, …) are parse-only in the tree** (CentiTOML; the map
writer hand-emits TOML). They are out of the first cut; the hierarchy leaves a slot for a
`TomlConfigWriter` child.

**F9 — Writes must be multi-file transactions.** Editing a campaign touches several files (a `.cfg`,
a config file, a string file). Map save already tolerates partial failure and reports it; for
content we add a `WriteBatch` (stage all as temp files, then rename) so a failure leaves nothing half-written.

## 4. Design

### 4.1 Placement and rules

New files in **`kfx_config`** (rank 2, below everything that would consume them): `cfgc_document.*`,
`cfgc_content.*`, `cfgc_schema.*`, `cfgc_registry.*`, `cfgc_stack.*`, `cfgc_writer*.*`,
`cfgc_reader*.*`, `cfgc_json.*`. Hard rules, enforced by tests that never load engine config:

1. no use of `kfx_config_state`, `load_config`, `FGrp_*` path lookups, `text_line_number`, or any
   engine global; every function takes explicit paths / documents;
2. no ImGui, no `kfx_editor` include (the editors depend on this layer, not the reverse);
3. deterministic output (stable ordering, no timestamps) so diffs and tests are exact.

### 4.2 The model (five plain-data pieces)

| Piece | What it is |
|---|---|
| **`ConfigDocument`** | Lossless line-oriented model of one text file: raw lines kept verbatim (EOL style, BOM, `^Z`, banners, comments), with an **index** of sections (`name`, first/last line) and key lines (`key`, value text, trailing comment, `=`-or-space). Parse never fails; unknown lines are preserved. Untouched documents serialise **byte-identically**. |
| **`ConfigContent`** | Structured, *sparse* view of one file at one layer: `kind`, `partial`, ordered `sections[]` → `{ basename, index, name, fields[] }`, each field `{ key, values[] }` (repeatable). What an editor or a JSON tool reads and proposes. |
| **`ConfigSchema`** (with `FieldSpec`) | Per kind: sections (basename, numbered or named, count key), fields (key, `FieldKind`, min, max, enum source, list rule, help, `advanced`, `luaCoupled`). Built by **reflection** over `NamedFieldSet` and the exported key tables, plus curated overlays; help text lifted from base-file comments. Exportable as JSON. |
| **`NameRegistry`** | `kind → ordered names` derived from documents (F4); used for enum pickers and validation; layered like config. |
| **`ConfigStack`** | The layers of a file for a **`ConfigTarget`** (base / campaign / level directories passed in explicitly): parse lazily, resolve `effective(section, key)` → value, source layer, value beneath. Applies the merge rules of F6. |

`ConfigTarget` is plain data: `{ base_dir, campaign_cfg_dir, campaign_crtr_dir, level_dir, level_number }`.
A small adapter in `kfx_editor` / the frontend fills it from the live game (the analogue of what
`editor_save_map()` does for maps); tools fill it from command-line paths.

### 4.3 The class family (parent / child)

```
ConfigContentWriter                      (abstract parent; the map writer's role)
│   public:  ChangeResult apply(ConfigDocument&, const ChangeSet&)   // patch an existing file
│            std::string  create(const ConfigContent&)               // brand-new file text
│            bool write(const ConfigTarget&, const ChangeSet&, WriteBatch&)
│   shared concrete: atomic write via WriteBatch, EOL/BOM preservation, header for created files,
│            "no-op edits vanish", empty-section / empty-file removal, ordering rules (§4.4),
│            validation hook, diagnostics collection
│   virtual:  schema(), section_identity(), format_field(), merge_rule(), validate_block()
│
├─ TableConfigWriter                     (generic; driven entirely by a NamedFieldSet-derived schema)
│   ├─ TrapDoorConfigWriter            `[trapN]` `[doorN]` + `[common]` counts                  (05)
│   ├─ TerrainConfigWriter             `[roomN]` `[slabN]` + `[block_health]`                    (07)
│   ├─ ObjectsConfigWriter, EffectsConfigWriter, CubesConfigWriter, LensesConfigWriter, …
│   └─ MagicConfigWriter               `[shotN]` `[powerN]` (table) + `[spellN]` `[specialN]` (curated)   (06)
├─ RulesConfigWriter                     named blocks + list blocks `[research]` `[sacrifices]`  (03 §9)
├─ CreatureConfigWriter                  `creature.cfg`: `[common]`, `[experience]`, `[jobN]`, `[instanceN]`, …  (04, 06)
├─ CreatureModelConfigWriter             one file per creature, nine named sections                (04)
├─ CampaignConfigWriter                  `[common]`, `[strings]`, `[speech]`, `[mapNNNNN]`, list keys (08)
└─ TomlConfigWriter                      (later) hand-emitted TOML kinds
StringsFileWriter                        (separate small family: NUL-separated entries, code page)  (09)

ConfigContentReader                      mirror hierarchy: text → ConfigDocument → ConfigContent
  (same children; readers share the parent's tokenising and delegate kind rules)
ConfigJson                               ConfigContent / ChangeSet / ConfigSchema ⇄ JSON  (§4.6)
```

Why children per **kind** rather than per format (the map family's axis): here the *format* is the same
line-oriented text for all `.cfg` files, and what differs is section identity, list semantics and
bespoke blocks (F5–F7). `TableConfigWriter` absorbs the table-driven majority, so most children are
a schema plus a few overrides (a `TrapDoorConfigWriter` is expected to be well under 100 lines).

### 4.4 Write semantics (what "apply a change" means)

The unit of change is a **`ChangeSet`**: an ordered list of `Set(section, key, value…)`,
`Reset(section, key)`, `ResetSection(section)`, `ReplaceList(section, key, values[])`. It is the
single vocabulary shared by the editors, the raw editor, tests and JSON. `apply()`:

1. locates the section (by `basename+index`, falling back to `Name`), creating it at the end of the
   file with a blank-line separator if missing (the base `Name` written first, as shipped partial files do);
2. for a scalar `Set`: rewrites **the last occurrence** of the key in that section (the one the loader
   keeps), preserving the original spacing style, `=` vs space, key case and any trailing comment; if
   absent, inserts after the section's last key line (before its trailing comment/blank block) using
   the schema's canonical key case;
3. **no-op elimination:** a value equal to the layer beneath is never written; `Reset` deletes the
   key line (and its directly preceding comment only if the writer inserted it);
4. lists follow the schema's merge rule (replace-whole rewrites the block; per-element edits the line);
5. empty sections are dropped; a file left with no keys is deleted (or kept if it has a header the
   author wrote — configurable, default *keep authored, delete generated*);
6. `create()` (no existing file) emits a deterministic file: the standard header comment plus the
   "written by the map editor" line (§11.3), sections sorted by index.

Formatting rules for values come from `format_field()` per `FieldKind` (ints canonical, flag lists in
schema order, pairs space-separated, names as registered case). All rules have golden-file tests.

### 4.5 Validation

`validate()` uses the schema and the `NameRegistry`, and **reports** what the loader would silently
change (clamped numbers, unknown enum names, unknown keys/sections, `Name`/index mismatches,
duplicate names, list-length mismatches, missing referenced entities such as a `Crate` object). Diagnostics
carry `file:line`, severity and a machine-readable code (for the editor's problem list, Verify Map and
JSON). Validation never blocks a write by itself; the caller decides (editors warn, tools may refuse).

### 4.6 The LLM / external-tool surface (designed in now, built later)

Because content is plain data with a schema, the tool surface is small and uniform:

| Operation | Backed by |
|---|---|
| `describe(kind)` → JSON schema: sections, fields, kinds, ranges, enum values, help | `ConfigSchema` + `NameRegistry` |
| `read(target, file)` → JSON: effective content **with source layers**, or just the target layer's sparse content | `ConfigStack`, `ConfigContent` |
| `plan(target, changes)` → validated `ChangeSet` + diagnostics + a text diff of the files it would touch (dry run) | writers' `apply()` on in-memory documents |
| `apply(target, changes)` → writes atomically, returns the diff and diagnostics | `WriteBatch` |
| `strings.*`, `campaign.*` equivalents | the same pattern (04–09) |

Deliverables of this plan stop at the library and its tests plus `ConfigJson`. A command-line front
end (or a wrapper over the existing TCP API) is a later, separate decision; nothing in the design
requires it now, and nothing prevents it.

## 5. Typed views

`ConfigContent` is generic on purpose (about 300 keys across the kinds; one DTO per kind would be
thousands of lines and drift from the loaders). Editors that want typed access get **views**:
`TrapView`, `CreatureView`, … thin structs with `get(name)` / `set(name)` generated from the schema,
not hand-written models. Add a typed DTO only where structure demands it (the campaign's level lists;
the string file's entry vector).

## 6. Mapping to the editor plans

| Editor plan | Writer child | What it needs beyond the parent |
|---|---|---|
| 03 §9 Rules | `RulesConfigWriter` | list blocks (`[research]`, `[sacrifices]`), named-block rules |
| 05 Trap/door | `TrapDoorConfigWriter` | `[common]` counts; nothing else |
| 07 Room/terrain | `TerrainConfigWriter` | `[block_health]`; count keys |
| 06 Spell/ability | `MagicConfigWriter`, `CreatureConfigWriter` (instances) | curated spell/special/instance schemas; per-level arrays; writes instances to `creature.cfg` |
| 04 Creature | `CreatureModelConfigWriter`, `CreatureConfigWriter` | one file per creature; curated schema from the exported key tables |
| 08 Campaign | `CampaignConfigWriter` | `[mapNNNNN]` sections; list keys with column preservation; multi-file batch |
| 09 Text | `StringsFileWriter` | NUL-separated binary-ish format and code-page conversion; separate small family |

## 7. Tests and gates

1. **Document round trip:** every one of the 857 shipped `*.cfg` parses and re-serialises
   byte-identically (CRLF, `^Z`, banners, no-`=` lines included). Fixture-free: reads `core_files/`.
2. **Reader/writer round trip per kind:** `create(content)` → parse → equal content; `apply()`
   idempotence (applying the same `ChangeSet` twice changes nothing).
3. **Golden patches:** for each kind, input file + `ChangeSet` → expected file (spacing, key case,
   trailing comments, insertion position, list rules).
4. **Schema coverage:** every key in every shipped file is either known to the schema or listed in an
   explicit allow-list with a reason (measured baseline in W1; the survey above shows the size of
   the gap for the table-driven kinds).
5. **The anchor (needs a data-bearing tree, ftest):** load the real configs with the real loaders; for
   every `NamedFieldSet` and every curated kind compare `ConfigStack` base values with the live structs,
   and `NameRegistry` with the engine's `*_desc` tables. Any drift fails the test.
6. **Loader read-back (ftest):** write a level- and campaign-scope change, start the level with the
   real loader, check the live value; then `Reset` and check it is gone.
7. **Purity:** the content-layer unit tests link and run **without** loading engine config
   (they would fail on any hidden dependency on `kfx_config_state`).

## 8. Slices (these replace 03's F1 and F2)

| Slice | Content | Size |
|---|---|---|
| **W0** | Spike: run the document parser prototype over all 857 files, fix the model until the round trip is byte-identical; measure the schema-coverage baseline per kind. | small |
| **W1** | `ConfigDocument` (+ tests 1); `ConfigContent` reader for generic sections; `WriteBatch`. | medium |
| **W2** | `ConfigSchema` reflection (table kinds + exported key tables), `FieldKind` grammar and validation, `NameRegistry` (+ test 4 baseline). | medium-large |
| **W3** | `ConfigStack` with layers and merge rules; `ConfigTarget`. | medium |
| **W4** | `ConfigContentWriter` parent, `TableConfigWriter`, first children `TrapDoorConfigWriter` and `RulesConfigWriter`; golden tests (3, 2). | medium |
| **W5** | Anchor and loader read-back ftests (5, 6). | medium |
| **W6** | `ConfigJson`: schema, content, ChangeSet, diagnostics. | medium |
| then | remaining children with their editors: `TerrainConfigWriter` (07), `MagicConfigWriter` and `CreatureConfigWriter` (06, 04), `CreatureModelConfigWriter` (04), `CampaignConfigWriter` (08), `StringsFileWriter` (09). Each ships with its golden tests. | per editor |

**Order relative to the UI work:** F0 (rename, Tools menu, host wiring) is UI-only and can run first or
in parallel; **W0–W5 replace F1–F2**, and F3 (raw editor) and F4 (Rules editor) then sit on the writers.
The Rules editor is the first real consumer and the acceptance test for W4.

## 8a. W0 results (done)

**Built:** `ConfigDocument` (`kfx_config/include/cfgc_document.h`, `src/cfgc_document.cpp`), test file
`kfx_config/tests/cfgc_document_test.cpp`. Line kinds: blank, comment (`;`), section (`[name]`), key
(`Key = value` **or** `Key value`), other (junk, `^Z`, unterminated `[`). Per line it keeps the raw text
and its own end-of-line, so mixed and missing line endings survive. Index: sections (case-sensitive
`find_section`), `key_lines()`, `dominant_eol()`.

**Gate met:** all **857 shipped `*.cfg` files (core_files + config) parse and re-serialise
byte-identically**; arbitrary byte strings (empty, lone newlines, BOM, embedded NUL, unterminated
section) round-trip too. The full `kfx_config` suite (31 412 assertions) still passes; the
layering check is clean; the mingw build of `kfx_config` compiles.

**Schema-coverage baseline** (distinct keys used by the shipped files versus the engine's own tables;
printed by the `[baseline]` test):

| Kind | Files | Table / key-table fields | Distinct keys used | Not in tables |
|---|---|---|---|---|
| trapdoor | 29 | 62 | 64 | 2: `DoorsCount`, `TrapsCount` (the `[common]` counts) |
| rules | 35 | 94 (from `ruleblocks[]`; the field set itself has `named_fields = NULL`) | 108 | 15: `Research` (list block), 6 `[sacrifices]` keys (`MkCreature`, `MkGoodHero`, `PosSpellAll`, `NegSpellAll`, `PosUniqFunc`, `NegUniqFunc`), and 8 keys **the loader does not know** (below) |
| objects | 24 | 43 | 46 | 3: `ObjectsCount`, `LavaDestroyEffect`, `WaterDestroyEffect` |
| terrain | 19 | 18 (room set only) | 45 | 27: 15 are **slab** keys (the slab set is `static`, W2 exports it), 10 are `[block_health]` keys, and `RoomsCount`/`SlabsCount` |
| creature model files | 428 | 133 (key tables of the nine sections) | 137 | 4: `EntranceForce`, `GFX18`, `GFX21`, `Hurt`, all **unknown to the loader** |

**Findings from the baseline**

1. **Every residue is explained.** Nothing is a tokenizer problem: it is `[common]` counts, list
   blocks, `[block_health]`, or the not-yet-exported slab set — exactly the bespoke pieces plan 10 §3 F5
   predicted (their child writers), plus the exports W2 already lists.
2. **Dead keys in shipped data.** Fourteen keys appear in shipped files but **no loader recognises
   them**: rules `ArmegeddonTeleportNeutrals`, `BarrackTime`, `GameTurnsPerPrisonHealthGain`,
   `GemEffectiveness`, `GoldPerGoldBlock`, `InstanceDelayOnDrop`, `PrisonHealthGain`,
   `WinnerTortureSLoser`; objects `LavaDestroyEffect`, `WaterDestroyEffect`; creature `EntranceForce`,
   `GFX18`, `GFX21`, `Hurt`. Two of them sit in the **base** `fxdata/rules.cfg` and `objects.cfg` that ship
   with the game (`BarrackTime`, `InstanceDelayOnDrop`, `LavaDestroyEffect`). The schema therefore needs
   a third state, **`ignored`** ("present in files, no effect in this build"), so the editors and
   validators say so instead of offering a control that does nothing. W2's coverage test keeps an
   explicit allow-list with these and a reason.
3. **`rules_named_fields_set.named_fields` is `NULL`**; rules blocks live in `ruleblocks[8]`. The schema
   reflection must read `ruleblocks[]` for rules, and the generic `TableConfigWriter` must accept a
   kind whose blocks are named rather than numbered.
4. **`creatmodel_sprite_commands` is declared but never defined** in `config_creature.h`; the `[sprites]`
   keys come from `creature_graphics_desc`. (A stale declaration; worth deleting during W2.)

## 9. Corrections to earlier plans

- 03 §2.5 / S1: parse and assign are separate, **but** the parse functions are not safe or sufficient
  for validation (F3); validation is done by the content layer's own grammar.
- 03 §4: the module table (`cfg_document`, `cfg_stack`, `cfg_schema`, `cfg_edit`, `cfg_validate`) is
  superseded by §4.2–4.5 here; they live in `kfx_config`, not `kfx_editor`.
- 04–09 "Where edits go" sections stay valid; they call the writers through `ChangeSet`s.

## 10. Risks

| Risk | Mitigation |
|---|---|
| Content layer drifts from the loaders (two implementations of one format) | Anchor test (§7.5) and loader read-back (§7.6) in CI on a data tree; schema-coverage test |
| Patch writer mangles hand-maintained files | Line-preserving model, byte-identical no-op round trip on 857 files, golden tests, never regenerate existing files |
| Schema reflection depends on `NamedFieldSet`s that are `static` today | Export them via small accessors in `kfx_config` (one line each), tested for presence |
| Loader quirks (`=` optional, tolerated junk, `^Z`) | Modelled from the survey; the round-trip test is the gate |
| Scope creep into TOML kinds | Slot reserved, not built |

## 11. Decisions (confirmed by the user)

1. **Placement:** the content layer lives in **`kfx_config`** (flat `cfgc_*` files); editors in
   `kfx_editor` sit on top. No new library.
2. **JSON surface:** the `ChangeSet`, `ConfigContent`, schema-export and diagnostics shapes are
   **designed now** (W1–W4 define them, W6 documents them with golden JSON fixtures); the
   `ConfigJson` code is **built when the first tool needs it**.
3. **Generated header:** files the editor **creates** start with the standard partial-file line plus a
   "written by the map editor" line, e.g.
   `; KeeperFX Partial Traps and Doors Configuration file version 1.0 -- written by the map editor.`
   Existing files are never given a header; a file the editor did not create is never retitled.
4. **Emptied files:** when the last override in a file is reset, a **generated** file (one carrying the
   editor's header line) is **deleted**; an **authored** file is kept (as an empty-of-overrides file with
   its comments) — the writer decides by the header line.

## 12. Still open

- Spike S5 (merge rule per list-shaped key) and W0's measured schema-coverage baseline decide the
  final list of curated overlays; both are the first tasks of W0/W2.
