# Phase 3 — map serialization (Save / Load / New) and verification

Status: **all 7 slices done.** Save (both formats), the menu bar, New/Open/Save/Save As dialogs,
native file pickers, Level Settings, Playtest, the Save As overwrite confirm, atomic file writes,
real `.lif` Free Play discovery, and `verify_map()` are all implemented and passing their own test
suites; a live click-through pass by the user is still pending on the whole dialog/menu-bar
surface. Every item from the original §8 "what must exist after phase 3" list is either built or
a deliberately-scoped-out, explicitly-documented deferral (room/portal checks, script checks,
classic vanilla ID-range enforcement, a "target mode" selector, the tile-highlight overlay —
each with its own reasoning in the relevant slice doc, not silently dropped). See
[`phase3/00-slice1-native-save.md`](phase3/00-slice1-native-save.md) through
[`phase3/06-slice7-verify-map.md`](phase3/06-slice7-verify-map.md) for what actually landed in
each slice, including several corrections to this doc's own pre-implementation claims (flagged
inline below where they matter) found by re-verifying against the current code before/while
building — trust the tracking docs over this one on any conflict.
Depends on phase 1.

Goal: `editor_save_map(lvnum, dir, flags)` in `kfx_editor` — the mirror of `kfx_sim`'s
`load_map_file()` — in **two output formats** (KFX-native TOML and classic binary — D5/F20),
plus new-map creation, pre-save verification, and the Save / Save As / Open / New dialogs.

**"Reverse the reader" is the whole strategy.** The engine already parses every map file
(both the KFX-native `tngfx`/`lgtfx`/`aptfx` loaders *and* the still-present classic
`.tng`/`.lgt`/`.apt` loaders); the format is *defined* by those parsers. Each `write_*` reads
its `load_*` twin and emits the same bytes/TOML. `file formats.doc` and ADiKtEd/Unearth are
cross-checks for the classic layouts, not the source of truth.

`kfx_editor` sits above `kfx_sim`, so the writers read `kfx_sim_state` / the map & slab arrays
/ the thing, light and action-point lists **directly** — no callback needed for the read side.

---

## 1. Current state: load-only — and the reader ↔ writer map

[`lvl_filesdk1.c`](../../../src/kfx_sim/src/lvl_filesdk1.c) has `load_*` for every map file and
**no writer**. `load_level_file()` (line ~1442) reads, in order:

| Data | File(s) | `kfx_sim` reader (the spec) | New `kfx_editor` writer | Src of truth? |
|---|---|---|---|---|
| Slabs | `map%05d.slb` | `load_slab_file` / `load_map_slab_file` | `write_slb` | **yes** |
| Ownership | `map%05d.own` | `load_map_ownership_file` | `write_own` | **yes** |
| Subtile→column idx | `map%05d.dat` | `load_map_data_file` | *(regen — §2)* | derived |
| Columns | `map%05d.clm` | `load_column_file` | *(regen — §2)* | derived |
| Wibble | `map%05d.wib` | `load_map_wibble_file` | *(regen — §2)* | derived |
| Things | `map%05d.tngfx` / `.tng` | `load_tngfx_file` (TOML, `load_kfx_toml_file`) / `load_thing_file` (binary) | `write_tngfx` | **yes** |
| Static lights | `map%05d.lgtfx` / `.lgt` | `load_lgtfx_file` / `load_static_light_file` | `write_lgtfx` | **yes** |
| Action points (hero gates → things) | `map%05d.aptfx` / `.apt` | `load_aptfx_file` / `load_action_point_file` | `write_aptfx` | **yes** |
| Base texture set | `map%05d.inf` | `load_and_setup_map_info` (1 byte) | `write_inf` | **yes** |
| Per-slab texture override | `map%05d.slx` | `load_ext_slabs` (`w*h` bytes) | `write_slx` (only if >1 pack used) | **yes** |
| Level name / meta | `map%05d.lof` / `.lif` | `level_lof_file_parse` / `level_lif_file_parse` | `write_lof` | **yes** |
| Script | `map%05d.txt` / `.lua` | script loader (`kfx_script`) | `write_script` (verbatim buffer) | **yes** |
| Map info (size, ambient, flags) | — | `load_and_setup_map_info` | into `write_lof` / `write_inf` | **yes** |

**KFX-native maps use the TOML forms** (`tngfx`/`lgtfx`/`aptfx`) — v1 writes only those (O6).
Their exact schemas are documented in [`07-investigation-findings.md`](07-investigation-findings.md)
§F3, read straight out of the loader callbacks (`thing_create_thing_adv`,
`light_create_light_adv`, `actnpoint_create_actnpoint_adv`). Container: `[common]` +
count field, then a `<name>` array of flat dicts. Hand-emit — no TOML writer library
(`tomlc99` is parse-only). **Correction:** hero gates serialize as `tngfx` **objects** with a
`HerogateNumber` key, *not* in `aptfx` (which is action-points-only: `PointNumber`,
`SubtileX/Y`, `PointRange`).

Classic-binary layouts (from `file formats.doc`, for the deferred O6 "Export classic" only —
confirm against the readers if/when built; **corrected against the actual reader code**, see
[`phase3/00-slice1-native-save.md`](phase3/00-slice1-native-save.md)'s "ground-truth corrections"
for the full detail):
- **`.slb`** — 85×85 × `u16` LE, the **full 16 bits are the slab kind** (not "low byte = kind" —
  `load_map_slab_file()` reads `n = lword(&buf[i])` and assigns it whole), row-major.
- **`.own`** — subtile-map, one owner byte per subtile.
- **`.apt`** — `u32` count, 8-byte entries: a **4-byte 2D** *Location* (`LegacyCoord2d`, not the
  6-byte 3D one things/lights use) **+ `u16 range` + `u16 num`** (the doc originally omitted
  `range` entirely).
- **`.tng`** — `u16` count, 21-byte entries: Location, type, subtype, owner, 12 type-specific.
- **`.lgt`** — `u32` count, 20-byte entries.
- **`.dat`** `u16` subtile-map (−clm index) / **`.clm`** **`u32` count + 4 reserved bytes** (not a
  `u64` count) + 24-byte entries / **`.wib`** `u8` subtile-map — *regenerated, never written
  (§2).*
- *Location*: `sx tx sy ty sz tz` — `tx,ty` subtile, `sx,sy` sub-subtile, `tz` cubes above
  floor, `sz` height within cube. (This 6-byte form is `LegacyCoord3d`, used by `.tng`/`.lgt`
  only — `.apt` uses the 4-byte 2D `LegacyCoord2d` instead, see above.)

**Key fact — corrected:** the loader does **not** actually require `.dat`/`.clm` to load
successfully today. `load_map_data_file()`/`load_column_file()` do return `false` when the file
is absent, but `load_level_file()` **discards both return values** and its final result gets
separately clobbered by whichever `load_tngfx_file`/`load_thing_file` call runs last — so a
missing `.dat`/`.clm` **silently produces a broken/blank-looking map** (empty column table)
rather than a load failure. This is arguably worse than "requires and fails" — it was fixed as
part of slice 1 (both the missing regen path *and* this silent-corruption bug), not just
"taught to regenerate." And the engine already regenerates columns from slabs at runtime:
`create_columns_from_list()` ([`lvl_filesdk1.c:1013`](../../../src/kfx_sim/src/lvl_filesdk1.c)),
`init_columns()` ([`map_columns.c:378`](../../../src/kfx_sim/src/map_columns.c) — **not**
neighbour-aware itself; it's a slab-agnostic bitfield-finalization pass over whatever
`columns_data[]` entries already exist, the neighbour-aware derivation is
`place_single_slab_type_on_map()` alone, below), and per-slab in `place_slab_type_on_map()` →
`update_map_collide()` ([`slab_data.c:609`](../../../src/kfx_sim/src/slab_data.c)).

## 2. Decision O1 — regenerate derived data (RESOLVED, see [`07`](07-investigation-findings.md) F2)

Teach the loader to regenerate `.dat` / `.clm` / `.wib` from `.slb` when they're absent;
`editor_save_map` writes **only source-of-truth files**.

**Why this is now settled, not "recommended":**
- `place_single_slab_type_on_map()`
  ([`map_blocks.c:1292`](../../../src/kfx_sim/src/map_blocks.c)) already produces a slab's full
  3×3 column set from the game-wide `kfx_sim_state.slabset[]` table — neighbour-aware
  (fortified walls, liquid edges, reinforced corners, torches, room prettiness). The engine
  runs this on **every dig and build**. A full-map sweep of it *is* the `.dat`/`.clm`
  regenerator.
- `.wib` already has an auto-generator: `initialise_map_wlb_auto()`
  ([`lvl_filesdk1.c:1105`](../../../src/kfx_sim/src/lvl_filesdk1.c)), the fallback when `.wlb`
  is absent.
- `file formats.doc` lists CLM/WIB/APT as engine-generatable; ADiKtEd's own note: "*.clm and
  .dat files are now auto-generated, nearly perfectly*".

**Current blocker (the one piece of new `kfx_sim` code) — corrected, see the "key fact" note in
§1 above:** it's not that absence is treated as failure — it's that absence is silently
*ignored*, propagating no failure at all while leaving the column table empty. There was **no
regenerate-on-missing path today** either way.

**Implemented** (slice 1 — [`phase3/00-slice1-native-save.md`](phase3/00-slice1-native-save.md)):
`regenerate_derived_map_data()` in `kfx_sim`/`lvl_filesdk1.c` (general loader robustness, not
editor-specific). One correction from the original plan: it can only run **after**
`load_map_slab_file()` has set real slab kinds from `.slb` (that's the earliest point they exist),
not "after `.slb`/`.own`/`.inf` load" as originally phrased — `load_map_slab_file()` itself is
called much later in `load_level_file()`'s own sequence than `.dat`/`.clm`/`.flg` are attempted.
Body: `for each slab: place_single_slab_type_on_map(slb->kind, x, y, slabmap_owner(slb))`, with a
bounds check that fails loudly (returns `false`) rather than silently clamping an out-of-range
slab kind. Does **not** additionally call `init_columns()`/`initialise_map_wlb_auto()` itself —
`init_columns()` already ran earlier in `load_level_file()`'s own sequence regardless of path, and
`load_map_slab_file()` already unconditionally calls `initialise_map_collides()`/
`initialise_map_health()`/`initialise_extra_slab_info()` (which itself falls back to
`initialise_map_wlb_auto()` when `.wlb` is absent) right after setting slab kinds — both are pure
functions of each slab's own kind, independent of the column table this function populates, so
nothing needed duplicating. `load_level_file()` calls it when `.dat`/`.clm` are absent and now
**propagates a `false` result if it fails**, closing the silent-corruption gap for good.

**Spike S1 is now a *validation* spike, not feasibility:** load a stock map normally vs with
`.clm` ignored + regen; diff the column table / collision / wibble. If a few stock maps encode
hand-tweaks the slabset table doesn't reproduce, they keep their `.clm` (loader tries the file
first); editor-authored maps never have one, so they're unaffected regardless.

Fallback if the spike fails: the serializer writes `.dat`/`.clm`/`.wib` by running the same
regeneration in-memory and dumping the arrays. Same code, just also persisted.

## 3. `editor_save_map()` — structure

**Revised for slice 1** (the user's own steer, given this is also a preliminary step toward a
structured map/game-state format an LLM could read or produce): the actual `write_*` format
knowledge does **not** live in `kfx_editor` as originally planned here. It's a small virtual
class family — `MapContentWriter` (abstract) + `KfxNativeMapContentWriter` (concrete, this
slice) — living in **`kfx_sim`** (`map_content_writer.h`/`.cpp`, alongside the `load_*` readers
it mirrors), operating on a plain-data `MapContent` snapshot (`map_content.h`, also `kfx_sim`)
decoupled from live `kfx_sim_state`. A `MapContentReader`/`KfxNativeMapContentReader` pair mirrors
it for the read side, reusing CentiTOML's real `toml_parse()`. See
[`phase3/00-slice1-native-save.md`](phase3/00-slice1-native-save.md) for the full design
rationale (Strategy pattern, modeled on this codebase's own `LensEffect`/`IPlatform` style;
classic-binary is a named, deferred future subclass, not a base-class rework).

`kfx_editor` keeps only the thin orchestration: `src/kfx_editor/src/editor_mapsave.cpp`
snapshots live state into a `MapContent` (the one place that must cross into `kfx_render` for
lights, which `kfx_sim` itself can't reach) and hands it to `KfxNativeMapContentWriter`. Public
entry (as actually shipped, simpler than originally planned — no `flags` param yet, since
`verify_map()`/format-selection aren't in scope this slice): `TbBool editor_save_map(LevelNumber
lvnum, const char *dir)` in `kfx_editor.h`.

1. `verify_map()` (§5) — abort with a report on `VERIF_ERROR` unless `flags & SAVE_FORCE`.
   This also computes `map_is_legacy_compatible()` (§5.1 / [`07`](07-investigation-findings.md)
   F20).
2. **Choose format** (`flags`: Auto / ForceKeeperFX / ForceClassic — §6):
   Auto → **classic** if `map_is_legacy_compatible()`, else **KFX-native**.
3. `regenerate_derived_map_data()` in-memory (needed for `.dat`/`.clm`/`.wib` in the classic
   path; harmless otherwise).
4. Write. Formats that are **identical** either way: `write_slb` (`u16` grid, rock/earth
   ownership → neutral first), `write_own` (per-subtile), `write_inf` (texture byte),
   script `.txt` (verbatim, or a minimal stub).
   **KFX-native path:**
   - `write_tngfx()` — TOML `[common] ThingsCount` + `thing` array, keys per class (F3:
     `ThingType`, `SubtypeStringID`, `Ownership`, `SubtileX/Y/Z`, `CreatureLevel`, `GoldValue`,
     `DoorOrientation`/`DoorLocked`, `EffectRange`, `HerogateNumber` for hero gates, …).
   - `write_lgtfx()` — TOML `light` array: `Dynamic`, `SubtileX/Y/Z`, `LightRange`,
     `LightIntensity`, `ParentTile` (F3).
   - `write_aptfx()` — TOML `actionpoint` array: `PointNumber`, `SubtileX/Y`, `PointRange`
     (F3). Action points only — hero gates are in `tngfx`.
   - `write_lof()` — `NAME_TEXT` + `KIND` + `PLAYERS` + `MAPSIZE w h` (if ≠85) + ambient +
     optional author/desc. Auto-discovered, no `levels.txt` step (F4).
   - `write_slx()` — per-slab texture grid, only if >1 pack used (F15).
   **Classic path** ([`07`](07-investigation-findings.md) F20 — reverse of the still-present
   classic loaders):
   - `write_tng()` — binary `LegacyInitThing[]` (0x15), `u16` count. Hero gates as things.
   - `write_lgt()` — binary `LegacyInitLight[]` (0x14), `u32` count.
   - `write_apt()` — binary `LegacyInitActionPoint[]` (8), `u32` count.
   - `write_dat()` / `write_clm()` / `write_wib()` — dump the arrays step 3 built.
   - `write_lif()` — `num, Name` (classic name file; not `.lof`).
5. Atomic-ish: temp names + rename (or `.bak` copy — a `kfx_platform` rename primitive is TBD;
   only `LbFileSaveAt` is confirmed).
6. **First save of a new map:** call `config_reload_callbacks->find_and_load_lof_files()` +
   `…lif_files()` to re-scan so the map appears in Free Play *without a campaign reload*
   ([`07`](07-investigation-findings.md) F13). Near-idempotent (`add_single_level_to_campaign`
   guards on the kind flag) — confirm no duplication in S3.
7. Clear the editor dirty flag.

No cross-layer callback needed for save itself: the Save UI, the verify pass, the writers and
the dirty flag all live in `kfx_editor`, which reads `kfx_sim` directly and calls `kfx_platform`
file I/O directly. The only editor↔frontend edge is that the in-session Save *dialog* is drawn
with `frontgui_widgets` (a `kfx_frontend` API `kfx_editor` links against — a normal downward
call, no callback).

## 4. New Map creation

`editor_new_map(w, h, texture_set, name)` (phase 1 §5 introduced it) fully specified here since
it shares the write path:
- Writes directly into `kfx_sim`'s map/slab/ownership arrays; in-memory only until first Save.
  Slab grid (earth + rock border), ownership (neutral), texture set, map info (size, name,
  default ambient), empty thing/light/AP lists, a default script stub.
- First **Save** runs `editor_save_map()` to the chosen slot/name.

## 5. Verification (`verify_map()`)

`verify_map()` lives in `kfx_editor` (`editor_verify.c`).

Port ADiKtEd's `level_verify_struct` / `level_verify_logic`
([`lev_data.c:1264`](../../../../3rdparty/Keeper/ADiKtEd-master/libadikted/lev_data.c)) to
KFX structures. Checks, each `WARN` or `ERROR`:

| Check | Level |
|---|---|
| Player 0 has a Dungeon Heart | ERROR (original blocks save) |
| Every placed heart's 3×3 is intact & reachable | WARN |
| Portals present for players that need generation | WARN |
| No thing embedded in a solid column / impenetrable rock | ERROR |
| Thing/creature/AP counts vs the **target's** cap ([`07`](07-investigation-findings.md) F18: engine = 12288 things / 1024 creatures / 256 APs; classic = 255 creatures, `u16` counts) | WARN near cap / ERROR at cap |
| Action point numbers unique & contiguous-ish; script-referenced APs exist | WARN |
| Hero gate numbers unique (hero gates are `tngfx` objects with `HerogateNumber`) | WARN |
| Script: balanced `IF`/`ENDIF`, ≤48 conditions (classic), party/AP refs resolve, `WIN_GAME` reachable | WARN (ERROR on unbalanced IF) |
| Map border ring untouched (all impenetrable/rock) | WARN |
| Rooms ≥ min size, not split, have required adjacent access | WARN |

**Target mode** (level-settings field, R9): *KeeperFX* (default — WARN only near the real
engine caps) vs *classic-compatible* (ERROR at 255 creatures, 48 script `IF`s, `u16`
thing/AP counts). Governs which rows above are ERROR vs WARN.

Output: a list `{ severity, message, slab_x, slab_y }` the Save dialog renders, each row
"zoom to" (`PckA_ZoomToPosition`). Reuses phase 4's overlay to flag offending tiles.

### 5.1 `map_is_legacy_compatible()` (D5 / [`07`](07-investigation-findings.md) F20)

A predicate computed alongside verification: **true iff the map uses no KeeperFX-only
feature**, so it can be saved in the classic binary format (playable by vanilla DK / ADiKtEd /
Unearth, and by KeeperFX). Returns **false** on any of:
- a slab kind outside the classic 0–53 set (any modded slab);
- a creature / object / trap / door model outside the classic ID ranges (any modded model);
- a thing carrying a TOML-only field (`CreatureName`, `CreatureInitialHealth`, `CustomBox`,
  effect-generator `EffectRange`, per-thing `Orientation` where classic had none);
- a `.lua` script, or `.txt` using a KeeperFX-only script command, or >48 `IF`s, or
  flag/timer/param values outside classic ranges;
- map size ≠ 85×85; a `.slx` (multi-tileset); a texture id outside the classic range;
- a static light with a KeeperFX-only flag; >255 creatures; thing/AP counts exceeding `u16`.

The precise marker list is a phase-3 sub-task — cross-check each candidate against the classic
loaders and `file formats.doc`. When false, the Save dialog shows *why* (which markers).

## 6. Dialogs (ImGui, drawn from `kfx_editor` via `frontgui_widgets`)

- **New Map** (`FeSt_EDITOR` browser, phase 1 §2 — this one screen *is* frontend code since
  it's pre-session): size / texture set / name / players.
- **Open Map**: writable-map list + thumbnail via `land_preview_build_minimap(lvnum)`
  ([`07`](07-investigation-findings.md) F14 — reads `.slb`/`.own` directly, no level load).
  Needs the map's `.lof` scanned into `LevelInformation` first (F13).
- **Save As**: name + destination (the "Editor Maps" mappack's levels dir by default,
  [`07`](07-investigation-findings.md) F4). Original's "800 slots" becomes "pick a free level
  number or name"; `.lof` `NAME_TEXT` makes the number cosmetic.
- **Save** (in-session, Editor menu): re-save current; on first save falls through to Save As;
  shows the verification report; **Save Anyway** for WARN, blocked on ERROR.
- **Format selector** (Save / Save As): **Auto** (default — classic binary if
  `map_is_legacy_compatible()`, else KeeperFX TOML), **Force KeeperFX**, **Force Classic**
  (lists the blocking markers if the map isn't legacy-compatible). The dialog shows the
  resolved format and, for Auto-classic, a note that the map stays vanilla-DK-compatible.
- **Overwrite confirm** for an existing map.

## 7. Spikes

- **S1 — derived-data regen equivalence** (validation, not feasibility — see §2 /
  [`07`](07-investigation-findings.md) F2). Load a stock campaign map normally; separately load
  it with `.dat`/`.clm`/`.wib` ignored + `regenerate_derived_map_data()`; diff column table,
  `col_idx` map, wibble, collision. Stock maps that differ keep their `.clm`; editor maps never
  have one. This mainly sizes "how many stock maps need their `.clm` kept".
- **~~S2 — TOML schema~~ — done** ([`07`](07-investigation-findings.md) F3). Schemas read from
  the loader callbacks; hand-emit, no library.
- **S3 — round-trip, both formats.** Save a freshly loaded stock map (classic-format path,
  since stock maps are legacy-compatible), reload, assert sim state identical. Repeat for a
  KFX-feature map via the KFX-native path.
- **S4 — classic writer conformance.** A map saved by the classic path loads cleanly in
  ADiKtEd / Unearth (and ideally vanilla DK) without warnings.

## 8. What must exist after phase 3

1. `editor_save_map()` with **both output formats**:
   - KFX-native: `write_tngfx` / `write_lgtfx` / `write_aptfx` / `write_lof` / `write_slx`.
   - Classic binary: `write_tng` / `write_lgt` / `write_apt` / `write_dat` / `write_clm` /
     `write_wib` / `write_lif` (F20).
   - Shared: `write_slb` / `write_own` / `write_inf` / script `.txt`.
2. `map_is_legacy_compatible()` + the Auto / Force KeeperFX / Force Classic selector.
3. `regenerate_derived_map_data()` in `kfx_sim` and the loader path that uses it.
4. `editor_new_map()` (incl. `set_map_size`) fully wired to New Map.
5. `verify_map()` with the §5 checks + `map_is_legacy_compatible()` and a structured report.
6. New / Open / Save / Save As / Overwrite dialogs.
7. `.lof` write + post-save `find_and_load_lof_files()` re-scan → map in Free Play, no
   `levels.txt`, no campaign reload.
8. Round-trip integrity (S3) both formats + classic-writer conformance (S4) in CI, incl.
   non-85×85 and a multi-pack `.slx` map.

## 9. Tests

- Catch2 `map_roundtrip` (in `kfx_editor`'s test target): synthetic 20×20 map (slabs,
  ownership, 5 things, 3 lights, 2 APs, a name) → `editor_save_map` → `load_map_file` →
  deep-equal.
- Catch2 `verify_map`: fixtures for each ERROR/WARN check.
- ftest `editor_save_reload`: enter editor on stock map, place a heart + creatures + a light,
  Save As, exit, Open the saved map, assert everything present, Playtest, assert it runs.
- ftest `editor_new_map_playable`: New 40×40, place heart + portal + treasury + a bit of gold,
  add `WIN_GAME` stub, Save, Playtest, assert no load error and turn advances.
- Regression: save + reload every stock campaign map, assert no verification ERROR and
  identical column tables (guards S1).
