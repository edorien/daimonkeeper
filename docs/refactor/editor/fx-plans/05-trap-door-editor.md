# FX plan 05 — trap and door editor

Status: **plan, first pass.** Nothing built. Depends on [03](03-content-editors-foundation.md). The
smallest and best-supported of the entity editors (its file is fully table-driven), so it is the
recommended **first** editor after the Rules editor.

## 1. Goal

Let an author tune the workshop's traps and doors for a level or a campaign — health, cost to build,
how the trap fires, how fast it reloads, what a door is worth — compare with stock, and Playtest it.

## 2. What the engine gives us

- `trapdoor.cfg`: 10 `[trapN]` sections (~55 keys each) and 7 `[doorN]` sections (~17 keys).
  Both are **table-driven**: `trapdoor_trap_named_fields_set` and `trapdoor_door_named_fields_set`
  (exported in `config_trapdoor.h`), so the schema is derived, not hand-written (foundation §4).
- Layers: base → campaign `<CONFIGS_LOCATION>/trapdoor.cfg` → level `map%05d.trapdoor.cfg`; partial
  by section index + `Name` (shipped example: `levels/classic_cfgs/trapdoor.cfg`, 14 lines).
- Cross references the editor must respect: a trap's `EffectType = SHOT_BOULDER` names a **shot** in
  `magic.cfg` ([06](06-spell-ability-editor.md)); `Crate = WRKBOX_BOULDER` names an **object**
  (`objects.cfg`); a door's `SlabKind = DOOR_WOODEN DOOR_WOODEN2` names **slabs** (`terrain.cfg`,
  [07](07-room-editor.md)); `PlaceSound`/`TriggerSound` are sound names; `PanelTabIndex` is the
  workshop panel position; `ActivationLuaFunc` / `UpdateFunction` are functions (read-only, S3).
- Manufacture: `ManufactureLevel` (workshop size needed) and `ManufactureRequired` (points) drive how
  fast the workshop builds it; `SellingValue` / `Unsellable` the refund.
- Damage is **not** on the trap: it lives in the linked shot's `Damage`, `MaxRange`, `Speed`. The
  editor shows that number read-only with a link into the Spell and Ability editor.

## 3. Screen design

```
┌ Trap and Door Editor ─ Target: [Campaign: Classic ▾] [Level: — ▾] ──────────────────────────┐
│ ┌ workshop ─┐  Traps ▾  BOULDER  (trap1)                    [Advanced ☐]                   │
│ │ 4×4 icon  │  ┌ Build ┬ Behaviour ┬ Placement ┬ Look & sound ┬ Advanced ┐                  │
│ │ panel     │  │ ManufactureRequired  25000   ▲ Base 25000            │                   │
│ │ (traps,   │  │ ManufactureLevel         3   ● Level 2 (base 3) [Reset]                   │
│ │  doors,   │  │ SellingValue          1000                                              │
│ │  ● edited)│  └──────────────────────────────────────────────────────┘                   │
│ └───────────┘  Fires: SHOT_BOULDER  → Damage 150, Range 12  (read-only, open in Spells ▸)   │
│                                              [Apply] [Close]                                │
└─────────────────────────────────────────────────────────────────────────────────────────────┘
```

- The list is the **workshop panel layout** (`PanelTabIndex`, 4×4 pages, thumbnails from the
  manufacture icons already used by the Thing palette), so authors recognise it; *Traps / Doors*
  switch.
- Tabs group the schema by purpose: **Build** (manufacture, selling, crate), **Behaviour**
  (`TriggerType`, `ActivationType`, `EffectType`, `Shots`, `TimeBetweenShots`, `InitialDelay`,
  `ActivationLevel`, `Health`, `HitType`, `Destructible`, `Slappable`, `Hidden`, `DetectInvisible`,
  `TriggerAlarm`, `Unstable`), **Placement** (`PlaceOnBridge`, `PlaceOnSubtile`, `InstantPlacement`,
  `Unsellable`), **Look & sound** (icons, sprites, sounds — read-only in v1), **Advanced**
  (animation ids, sizes, lighting, transparency flags, function names).
- Widgets: ints with the field's `min`/`max`; enum combos from the field's `namedCommand`
  (`TriggerType`, `ActivationType`, `EffectType`); flag lists; name pickers with icons for `Crate`
  (objects) and door `SlabKind` (slab thumbnails).
- Helpers: **"Recompute cost"** hint (`ManufactureRequired` in workshop-minutes at the standard
  workshop speed, from `rules.cfg [workers]`/creature `ManufactureValue`), read-only. **Level
  availability** note: which script `TRAP_AVAILABLE` / `DOOR_AVAILABLE` lines the open map has (map
  editor host only).

## 4. Where edits go

`levels/map%05d.trapdoor.cfg` (level) or `<CONFIGS_LOCATION>/trapdoor.cfg` (campaign): sections
`[trapN]` / `[doorN]` with the base `Name` repeated first (matches shipped style), then only the
changed keys. Reset deletes the key; an empty section is dropped; an empty file is deleted.

## 5. Slices

| Slice | Content | Tests |
|---|---|---|
| **T-S1 spike** | Confirm `Shots`/`EffectType`/`ActivationType` → shot resolution; which keys are Lua-coupled; the manufacture speed formula for the hint. | notes only |
| **T1** | Read-only viewer with layers and source badges; derived schema; workshop-layout list. | correctness anchor: `ConfigStack` base values == live `trap_cfgstats` / `door_cfgstats` for all 17 items |
| **T2** | Editing + Reset + diff-only write for both scopes. | edit → write → real loader reads it back (ftest) |
| **T3** | Icon pickers (`Crate`, door slabs), flag checkboxes, enum combos. | picker lists equal engine tables |
| **T4** | Linked-shot summary with jump to [06]; manufacture hint; balance table (all traps side by side: cost, health, shots, damage, range). | summary numbers equal the shot config |
| **T5** | Map Editor integration: *Tools ▸ Trap and Door Editor* on the level target; Thing palette shows a marker on items this level modifies; Verify Map line. | — |

## 6. Interactions

- **Map Editor:** the Thing tab's *Traps & Doors* palette and the Availability grid use these items;
  changing `PanelTabIndex` affects only the in-game workshop panel, not the palette. Doors placed by
  the editor use the level's effective door stats.
- **Plan 06:** trap → shot link; **Plan 07:** door → slab link; **Plan 04:** none.
- **Plan 08:** the campaign editor's *Config files* page opens this editor for `trapdoor.cfg`.

## 7. Non-goals

New traps or doors (a new section index also needs a crate object, panel icon, sprites and often
Lua); animations; rewriting the linked shot from here (that is the Spell and Ability editor).

## 8. Risks

| Risk | Mitigation |
|---|---|
| `PanelTabIndex` collisions (two items on one workshop tile) | Validate: warn when two items share an index in the effective config |
| A door's `SlabKind` pointing at a slab kind that is not animated | Validate against `terrain.cfg` `Animated`/`IsDoor` in the effective config |
| Editing Lua-coupled keys | Read-only, with the Lua function name shown |
