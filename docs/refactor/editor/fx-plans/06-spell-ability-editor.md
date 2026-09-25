# FX plan 06 — spell and ability editor

Status: **S1, S2 and S3 built** for magic.cfg (powers, spells, shots, specials; §9), on the shared entity window. **Abilities (S4) are built** too (creature instances, written to creature.cfg; §10). The graph / Used-by panel (S5) are not. Depends on [03](03-content-editors-foundation.md).
"Spells" here means everything the player or a creature can cast or fire; "abilities" are the
creature **instances** (what a creature can do: swing, fire a shot, cast, heal).

## 1. Goal

Let an author rebalance magic for a level or a campaign — what a keeper power costs at each level,
how hard a fireball hits, how long a spell lasts, how fast a creature attacks or recharges — and
understand how the pieces connect, without reading 2 400 lines of `magic.cfg`.

## 2. What the engine gives us

Four kinds of definition in `magic.cfg` (counts in the base file) plus one in `creature.cfg`:

| Kind | Section | Count | What it is | Parser |
|---|---|---|---|---|
| Power | `[powerN]` | 30 | Keeper spells and panel powers: `Cost` (9 per-level values), `Power` (10 per-level strengths), `Castability` flags, `PlayerState`, `Spell`, `Artifact` (spellbook), icons, `CastExpandFunc`, `UseFunction` | table (`magic_powers_named_fields_set`) |
| Spell | `[spellN]` | 33 | What a cast does: `ShotModel`, `Damage`, `Duration`, `SpellFlags`, `SelfCasted`, `CastAtThing`, aura keys, `SummonCreature`, `CleanseFlags`, healing | hand-written |
| Shot | `[shotN]` | 42 | A projectile: `Damage`, `Speed`, `MaxRange`, `AreaDamage`, `HitType`, effects, sounds, physics | table (`magic_shot_named_fields_set`) |
| Special | `[specialN]` | 14 | Special boxes (reveal map, resurrect, …): `Artifact`, effect, speech, `Value` | hand-written |
| Instance | `[instanceN]` (`creature.cfg`) | 57 | A creature action: `Time`, `ActionTime`, `ResetTime`, first-person timings, `RangeMin/RangeMax`, `PrimaryTarget`, `Properties`, `Function = creature_fire_shot SHOT_X 0` | hand-written |

The pieces form a **graph** the editor should expose:

```
Power ─Spell→ Spell ─ShotModel→ Shot ─effects→ (effects.toml)
Creature [experience] Powers = <instance names> ─→ Instance ─Function args→ Shot / Spell
Trap (05) ─EffectType→ Shot
Research (rules.cfg [research]) ─→ Power
```

Layers and partial files are the same as elsewhere: base → campaign `magic.cfg` / `creature.cfg` →
level `map%05d.magic.cfg` / `map%05d.creature.cfg`. Instances live in `creature.cfg`, so **their edits
go to the creature file** even though this editor shows them (foundation §3 target rule).
Per-level arrays (`Cost`, `Power`) are whole-array keys: a layer that sets `Cost` replaces the
array (Spike S5 confirms this for these keys).

## 3. Screen design

Tabs: **Powers · Spells · Shots · Abilities · Specials**, each with a list on the left and detail
tabs on the right, like the other editors (foundation §4 `content_ui`).

- **Powers**: list with the panel icons; detail: *Cost by level* and *Power by level* as a compact
  10-cell array editor with a mini line chart (see the curve at a glance), `Castability` as flag
  checkboxes, `Properties`, `PlayerState`, the linked spell (jump), research cost (read from the
  effective `rules.cfg`, with a link into the Rules editor).
- **Spells**: numeric keys grouped (*Effect*, *Duration & aura*, *Healing*, *Summon*), `ShotModel`
  as a picker to a shot (jump).
- **Shots**: *Damage*, *Speed*, *Range*, *Area damage* (`radius min max`), *Hit rules*
  (`HitType`, `DestroyOnHit`, `IsMagical`, `Health`), physics and effects in *Advanced*.
- **Abilities**: timing group (`Time`, `ActionTime`, `ResetTime` in game turns with a seconds
  read-out at the game speed, first-person versions), targeting (`RangeMin`/`RangeMax`, `PrimaryTarget`,
  `Properties` flags), and the `Function` shown as *function + resolved arguments* (the shot/spell
  argument is a picker; the function name itself is read-only).
- **"Used by" panel** on every entity: which powers/creatures/traps reference it, from the graph
  (`magic_graph`, a pure module built from the effective documents). Editing a shared shot shows
  "also used by 3 other things" before Apply.
- **Balance tables**: all powers by cost at level 1 / max, all shots by damage and range, all
  instances by time and reset; sortable; only differences from base highlighted.

## 4. Schema

Derived where a field table exists (powers, shots); **hand-written** for spells, specials and
instances (`magic_schema.cpp`, `instance_schema.cpp`), with the same key-coverage and
correctness-anchor tests as the creature editor (04 §4). Function-pointer keys (`UseFunction`,
`CastExpandFunc`, `HitThingFunc`, `Function`) are read-only and shown by name.

## 5. Slices

| Slice | Content | Tests |
|---|---|---|
| **S-S1 spike** | Confirm `Spell`/`ShotModel`/instance `Function` argument grammar; per-level array layering; instance-to-creature slot rules (`Powers` slot count = 10). | notes + parser-derived key lists |
| **S1** | Viewer for **Shots** and **Powers** (derived schema): layers, badges, correctness anchor vs live `shot_configs` / `powers`. | anchor tests |
| **S2** | Editing for Shots and Powers (incl. the per-level array widget); write to `magic.cfg` of the target. | edit → write → loader round trip |
| **S3** | **Spells** (hand-written schema) and **Specials**, viewer + editing. | key coverage, anchor |
| **S4** | **Abilities** (instances): viewer, editing, writes to `creature.cfg`; the *Function* argument pickers. | anchor vs live `instance_info`, round trip |
| **S5** | Graph, "Used by" panel, balance tables. | graph built from fixtures equals the engine's own references (spot checks) |
| **S6** | Map Editor integration (*Tools ▸ Spell and Ability Editor*, level target; Verify line). | — |

Order: S1 → S2 is the first useful release (shots and power costs are the most-tuned numbers);
S3/S4 follow; S5 after S4.

## 6. Interactions

- **Plan 05:** trap `EffectType` → shot; the trap editor links here.
- **Plan 04:** a creature's `Powers` slots pick from the abilities list here; its spells' damage
  scales with `[experience] SpellDamageIncreaseOnExp` (shown in the creature preview, not here).
- **Rules editor (03 §9):** research order/costs live in `rules.cfg [research]`; the power page links
  to it. Availability (`MAGIC_AVAILABLE`) stays in the script helper.
- **Lua:** spell Lua templates (`config-api/magic-templates.lua`) are read-only.

## 7. Non-goals

New powers, spells, shots or instances; new visual effects (`effects.toml` is raw-editor only); icon or
sound editing; changing function bindings.

## 8. Risks

| Risk | Mitigation |
|---|---|
| A shared shot changed for one use, silently affecting others | "Used by" panel and a confirm when more than one user |
| Per-level array semantics (`Cost` 9 values vs `Power` 10) | Fixed lengths from the schema; validate on Apply |
| Instances live in another file (`creature.cfg`) than the rest of the tab | Badge each instance row with its target file; the write path is tested for both scopes |
| Large surface (five kinds) | S1+S2 ship alone; the rest are independent |

## 9. Built (results)

**Window** (*Spell and Ability Editor*, in both Tools menus): the shared entity window
(`content_entity`: picker, a mode switch, an entity list with layer markers, grouped form tabs, read-only summaries of linked
entities, a *Compare all* table, Apply / Revert / Close). Modes: **Powers** (tabs Cost and strength, Casting, Look and sound,
Advanced), **Spells** (Effect, Duration and aura, Advanced), **Shots** (Damage, Hit rules, Effects and sound, Size and physics,
Advanced), **Specials**. Tab grouping is `spells_group_of()`, tested to place every key of the four kinds.

- **Per-level arrays:** a power's `Cost` (9 values) and `Power` (10) are drawn as a row of small boxes with a curve under the
  key, edited cell by cell (each cell clamped to its range), written back as one line. (Confirmed from the field table: the
  first nine `Power` cells map to the loader's strength array and the duplicated last one is the loader's own quirk.)
- **Links (read-only summaries):** a power's `Spell` shows that spell's duration, damage, shot model and self-cast; a spell's
  `ShotModel` shows the shot's damage, speed, range and hit type; each with a note on where to change it. A trap's `EffectType`
  (05) uses the same mechanism. Compare tables include the linked shot's damage; panel-position clashes are called out.
- **Read only:** function-pointer keys (`UseFunction`, `CastExpandFunc`, `HitThingFunc`, the logic functions).
- **Unspecified keys:** spells and specials have no curated grammar yet (hand-written key tables, plan §4); their plain
  numbers get number boxes (inferred from the current value), the rest are text. A curated schema is still open.
- **Shared code:** the Trap and Door editor was re-expressed as a configuration of the same entity window (its old window code
  is gone); the form row gained the array widget.
- **Tests:** Catch2 for the grouping and the array shapes; ftest `config_content_spell_editor` (S2 acceptance: the session
  edits power2's first `Cost` cell and shot1's `Damage` at level scope before the level loads; the running game uses both);
  the smoke ftest draws every mode and tab in both hosts; the existing anchor covers the shot and power tables' loading.
- **Not built:** Abilities (creature instances in creature.cfg: needs a creature.cfg schema), the graph and *Used by* panel
  with the shared-shot confirm, spell/special curated schemas, balance sorting.

## 10. Abilities (S4, built)

The **Abilities** mode of the Spell and Ability Editor lists the `[instanceN]` blocks of `creature.cfg` (the editor's window now
holds one open file per mode, so powers/spells/shots/specials write `magic.cfg` and abilities write `creature.cfg`; Apply writes
each dirty file in turn, atomically per file). Tabs: **Timing** (`Time`, `ActionTime`, `ResetTime`), **First person**,
**Targeting** (`RangeMin`/`RangeMax` as numbers or names, `PrimaryTarget`, `Properties` flags, priorities), **Look and sound**,
**Advanced** (function names, read only). The `Function` key's second word is looked up: an ability that fires a shot or casts a
spell shows that shot's or spell's numbers, read only, with a pointer to the Shots / Spells tab. The instance names are the registry
the creature editor's `Powers` slots draw from. The curated `creature` schema (plan 04 §10) provides the key shapes.
Tests: grouping of every instance key; ftest `config_content_creature_editor` covers an ability's Time at level scope.
