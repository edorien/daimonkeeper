# FX plan 07 — room editor (and slabs)

Status: **R1-R4 and R5 (menu entry) built** (§9). Not built: icon grids/thumbnails, the Map Editor palette marker and Verify line, "N slabs of this kind on the map". Depends on [03](03-content-editors-foundation.md).

## 1. Goal

Let an author change how rooms behave for a level or a campaign — what they cost, how tough they
are, how many creatures or items they hold, which creatures they attract — and, optionally, the
properties of terrain slabs (digging health, gold held, ownability), and see the result in Playtest.

## 2. What the engine gives us

- `terrain.cfg` holds **17 `[roomN]`** sections and **62 `[slabN]`** sections plus a
  `[block_health]` table. Both are **table-driven** (`terrain_room_named_fields_set`, exported;
  `terrain_slab_named_fields_set`, currently `static` — needs the small accessor from foundation §4).
- A room section: `Name`, `Cost`, `Health`, `SlabAssign` (which slab kind builds it), `Roles`
  (`ROOM_ROLE_*` list — what creatures and the AI use it for), `Properties`
  (`CANNOT_BE_SOLD`, `HAS_NO_ENSIGN`, …), `Messages`, `TotalCapacity` / `UsedCapacity` /
  `StorageHeight` (capacity rules by name), `SlabSynergy`, `CreatureCreation`, `AmbientSndSample`,
  panel/tooltip/icon keys (`PanelTabIndex`, `SymbolSprites`, `PointerSprites`, `NameTextID`).
- A slab section: `Name`, `Category`, `BlockFlags`/`NoBlockFlags`, `BlockHealthIndex`, `IsDiggable`,
  `IsOwnable`, `Indestructible`, `GoldHeld`, `Animated`, `WlbType`, `FillStyle`, wibble, `SlbID`.
- Layers: base → campaign `terrain.cfg` → level `map%05d.terrain.cfg`. Shipped data uses them
  (level `terrain` overrides on 3 levels; `Tyraels_realms` ships a campaign one).
- Related numbers live elsewhere: room-wide rules in `rules.cfg [rooms]` (Rules editor, 03 §9),
  creature room preferences in creature files ([04]), room availability in the map script helper.
- The room ↔ slab ↔ door links matter: `SlabAssign` names a slab kind; door slabs (`IsDoor`
  block flag) are referenced by [05]; `Roles` drive which creatures enter (`EntranceRoom` in [04]).

## 3. Screen design

Two tabs: **Rooms** and **Terrain** (slabs).

- **Rooms**: the room icon grid (the same icons as the map editor's Terrain palette) with a dot on
  edited rooms; detail tabs **Build** (`Cost`, `Health`, `SlabAssign`, `Properties`), **Capacity**
  (`TotalCapacity`, `UsedCapacity`, `StorageHeight`, with the named rule shown as text and a
  one-line explanation from the base comment), **Roles** (checkbox list of `ROOM_ROLE_*`,
  *Advanced*), **Look & sound** (read-only), **Advanced** (`SlabSynergy`, `CreatureCreation`).
- **Terrain**: slab list grouped by `Category` with the thumbnails the map editor already has;
  detail: **Digging** (`IsDiggable`, `BlockHealthIndex` → the `[block_health]` value it points at,
  editable through a separate small "Health table" panel), **Ownership** (`IsOwnable`,
  `Indestructible`), **Gold** (`GoldHeld`), **Block flags** (checkbox list, *Advanced*).
- Warnings inline where the base comment or schema says a change has wide reach (`Roles`,
  `BlockFlags`); a "this affects every slab of that kind on the map" note on slab edits.
- **Compare view:** all rooms by cost/health/capacity with base highlights; useful for campaign
  balancing.

## 4. Where edits go

`map%05d.terrain.cfg` / `<CONFIGS_LOCATION>/terrain.cfg`; `[roomN]` / `[slabN]` sections keyed by
base index with the base `Name` first; `[block_health]` as its own named-key section (no index).

## 5. Slices

| Slice | Content | Tests |
|---|---|---|
| **R-S1 spike** | Accessor for the slab set; which room keys are safe to edit per scope (e.g. `SlabAssign` cannot change on a running map); how `[block_health]` layering works. | notes |
| **R1** | Room viewer: layers, badges, correctness anchor vs live room stats (17 rooms). | anchor |
| **R2** | Room editing (Build, Capacity), Reset, both scopes. | round trip through the real loader |
| **R3** | Slab viewer + editing (Digging, Ownership, Gold) and the health-table panel. | anchor vs live slab stats (62) |
| **R4** | Roles and block-flag editors (Advanced), compare views, inline warnings. | flag round trips |
| **R5** | Map Editor integration (*Tools ▸ Room Editor*, level target; the terrain palette shows a marker for slabs this level modifies; Verify line). | — |

## 6. Interactions

- **Map Editor:** the room/slab palette reads effective stats from the live config, so it already
  reflects Playtest-time values after a level restart. **Room placement rules in the editor**
  (`place_slab_type_replacing_room`) do not change.
- **Plan 05:** door slabs; **Plan 04:** `EntranceRoom` / `RoomSlabsRequired`; **Plan 06:** none.
- **Rules editor:** `rules.cfg [rooms]` sits beside this editor's capacity numbers; each links to the other.

## 7. Non-goals

New rooms or slabs, slab-set/column/cube editing (`slabset.toml`, `columnset.toml`, `cubes.cfg`),
changing room shape rules, texture packs (already in Level Settings).

## 8. Risks

| Risk | Mitigation |
|---|---|
| Changing `Roles` / `BlockFlags` breaks AI, pathing or digging in ways a mapmaker cannot see | *Advanced* only, inline warnings, "reset all advanced" button |
| `SlabAssign` mismatch (two rooms building on one slab kind) | validate in the effective config |
| Terrain edits change the meaning of a saved map | show "N slabs of this kind on the map" in the Map Editor host |

## 9. Built (results)

**Room Editor** (both Tools menus), a configuration of the shared entity window (`content_entity`; `content_rooms`):

- **Rooms** (17): tabs **Build** (`Cost`, `Health`, `SlabAssign`, `Properties` flags, `PanelTabIndex`), **Capacity** (`TotalCapacity`,
  `UsedCapacity`, `StorageHeight`, `SlabSynergy`), **Roles** (the role flags, with a warning that they drive creatures and the computer
  players), **Look and sound**, **Advanced**, and **Compare all** (cost, health, storage height, capacity rule, panel position). A room's
  `SlabAssign` shows what slab it is built on (block flags, diggable, ownable, health index), read only, from the same file. Warnings when two rooms
  share a panel position or a slab kind (plan 07 §8).
- **Terrain** (62 slabs): **Digging** (`IsDiggable`, `BlockHealthIndex`, `Indestructible`, `GoldHeld`; a note that it changes every slab of that kind
  on the map), **Ownership**, **Block flags** (with a warning about pathing and digging), **Look and type**, and **Compare all**.
- **Health table**: the `[block_health]` block as one item (its ten values: how many hits each block kind takes); a slab's `BlockHealthIndex` is the
  position in it. The entity window gained *single-block* modes and per-tab notes for this.
- Everything else is as in the other editors: layers with badges, Reset, Apply / Revert, "keys with no effect" toggle.

**Tests:** Catch2 for the tab grouping (every key of room, slab and health table lands in a tab); ftest `config_content_room_editor` (R2/R3 acceptance:
a room's `Cost`, a slab's `GoldHeld` and a `[block_health]` value edited at level scope before the level loads; the running game uses all three,
including the health table in the sim's live `block_health[]`); the smoke ftest draws every mode and tab in both hosts; the existing anchor covers
the 17 rooms and 62 slabs (block counts, names, about 850 values).

**Not built:** room/slab icon grids and thumbnails (phase 6), the Map Editor palette marker, the Verify Map line, "N slabs of this kind on the map",
"reset all advanced".
