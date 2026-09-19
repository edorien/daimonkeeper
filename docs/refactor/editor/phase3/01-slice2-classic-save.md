# Phase 3, slice 2 — classic binary map save + `map_is_legacy_compatible()`

Status: **done.** Builds directly on slice 1
([`00-slice1-native-save.md`](00-slice1-native-save.md)) — same `MapContent` DTO, same
`MapContentWriter`/`MapContentReader` base classes, adding the `ClassicMapContentWriter`/
`ClassicMapContentReader` subclasses slice 1 always intended as a "pure add a subclass" job.

## Progress

- [x] `ClassicMapContentWriter`/`ClassicMapContentReader` (`map_content_writer.h/.cpp`,
      `map_content_reader.h/.cpp`) — `.tng`/`.lgt`/`.apt` binary read/write, per the verified byte
      layouts below. `.lif` intentionally not written this slice (see "explicitly out of scope"
      above). **Done in [`05-slice6-atomic-write-lif.md`](05-slice6-atomic-write-lif.md)** — turned
      out to be the mechanism real shipped classic content actually uses for Free Play discovery,
      and per-level (not shared-registry) writing sidesteps the merge complexity flagged below.
- [x] `map_is_legacy_compatible()` (`map_content_compat.h/.cpp`) — Auto-format predicate; ID-range
      checks deliberately left out, documented as a known gap (see below).
- [x] `EditorSaveFormat` enum + `editor_save_map(lvnum, dir, format)` signature
      (`kfx_editor.h`) and format-selection logic (`editor_mapsave.cpp`).
- [x] `DoorLocked` bug found and fixed while building the classic writer: was emitted as a TOML
      boolean (`append_bool`) instead of the int the production reader (`value_int32()`) and
      creature-Dynamic convention actually expect — fixed to `append_int(... ? 1 : 0)`.
- [x] Catch2: `map_content_roundtrip_classic_test.cpp` (round-trip through a legacy-compatible
      sample: slabs/ownership/texture, per-class thing fields, lights, action points) and
      `map_content_compat_test.cpp` (8 cases covering every implemented compat criterion). Full
      `kfx_sim_utest` suite: 649 test cases, 2080 assertions, all passing.
  `ftest_editor_save_reload` extended (actions 009-011): Force-Classic-saves the same
      already-reloaded state from slice 1's own actions to a second scratch level number
      (`90002`), reloads it via real `load_map_file()`, and re-asserts the creature/trap survived
      — proving the classic path round-trips through the real engine, not just in isolation. Ran
      standalone (`-ftests editor_save_reload -exitonfailedtest -headless`, EXIT 0) and as part of
      the full ftest sweep (22/22 passing, EXIT 0).
- [x] `check_layering.py --strict` clean; `keeperfx`/`keeperfx_hvlog` both build clean.

## Known gap carried forward

`map_is_legacy_compatible()` does not check slab-kind/creature/object/trap/door model IDs against
the classic vanilla ID range (D5/F20's own criterion) — no reliable "base roster vs modded config"
marker was found in the config structs to check against, so this was left undone rather than
guessed at. Concretely: a map using only modded IDs but otherwise legacy-shaped content (85×85, no
unsupported fields set) is currently reported compatible by `Auto` when it may not actually load in
real vanilla DK/ADiKtEd/Unearth. Not a correctness risk for KeeperFX itself (KFX doesn't care where
an ID came from) — only for the "can vanilla tools open this" promise `Auto`-classic implies.
Revisit if/when a real base-roster-boundary marker turns up, or drop the ambition and document
`Auto` as "KFX-shape compatible," not "vanilla-ID compatible."

## Scope for this slice

Following slice 1's own discipline (ship a well-scoped, fully-tested slice; defer the rest,
document why):

**In scope:**
- `ClassicMapContentWriter`/`ClassicMapContentReader` for `.tng`/`.lgt`/`.apt` (the classic
  binary equivalents of `.tngfx`/`.lgtfx`/`.aptfx`). `.slb`/`.own`/`.inf`/`.txt` are unchanged —
  the base class's shared writers already handle them identically either way.
- `map_is_legacy_compatible(const MapContent&)` (`kfx_sim`) — the Auto-format predicate.
- `editor_save_map()` gains back a format selector (Auto / `ForceKeeperFX` / `ForceClassic`) —
  slice 1 dropped this from the signature since there was only one format; now there are two.

**Explicitly out of scope, deferred:**
- **`.lif`** — found live while implementing this slice: unlike `.lof` (one file per level,
  `map%05d.lof`), `.lif` is a **shared, multi-entry registry file** scanned by
  `find_and_load_lif_files()` (globs `*.lif` across the whole campaign dir, not indexed by level
  number) and parsed line-by-line as `num, Name` pairs by `level_lif_entry_parse()`
  (`lvl_filesdk1.c:188`). Writing one entry means read-existing-then-append-or-update, a
  materially different operation from every other per-level writer here — and, like `.lof`'s own
  discoverability wiring, isn't needed for `load_map_file(lvnum)` to succeed (proven in slice 1's
  own ftest). Deferred to the dialogs slice alongside the rest of "show up in Free Play" wiring.
  **Done in [`05-slice6-atomic-write-lif.md`](05-slice6-atomic-write-lif.md)** — the "shared,
  multi-entry" framing turned out to be avoidable: `find_and_load_lif_files()` globs *every*
  `.lif` file it finds, so a per-level `map%05u.lif` (one entry) works identically to one big
  shared file, with none of the read-existing-and-merge complexity this note originally assumed.
- **`.dat`/`.clm`/`.wib`** — not written for the classic path either. `regenerate_derived_map_data()`
  (slice 1) already makes a classic-saved map reloadable *by KeeperFX* without them. Writing
  real, pre-baked derived files is specifically about vanilla-DK/ADiKtEd/Unearth compatibility —
  exactly what the original phase-3 doc's **S4 spike** ("classic writer conformance... run
  offline, not in CI") already scoped as separate, deferred work, not part of MVP save
  correctness.
- Save/Save As dialogs, `editor_new_map()` — same deferrals as slice 1. **Done in
  [`02-slice3-dialogs-menubar.md`](02-slice3-dialogs-menubar.md).**
- `verify_map()` — still deferred.

## Classic `.tng`/`.lgt`/`.apt` byte layout — verified directly against the readers

(`load_thing_file`/`load_static_light_file`/`load_action_point_file`, `lvl_filesdk1.c:745`/
`1254`/`877`, and their construction targets `thing_create_thing()`/`light_create_light()`/
`actnpoint_create_actnpoint()` — **not** the `_adv` TOML-path callbacks, which support more
fields than the classic path does.)

- **`.tng`**: `u16` count, then `struct LegacyInitThing` (21 bytes, `#pragma pack(1)`):
  `mappos.x/y/z` (`u16` each — narrowed `MapCoord`, safe because `map_is_legacy_compatible()`
  requires an 85×85 map, whose max raw coordinate `255*256=65280` fits `u16`), `oclass`(u8),
  `model`(u8), `owner`(u8), `range`(u16), `index`(u16), `params[8]`. **`params[]` meaning is
  class-specific, confirmed by reading `thing_create_thing()`'s own switch**:
  - Object: `index`=ParentTile, `params[1]`=HerogateNumber *or* CustomBox kind (mutually
    exclusive, same as TOML). **No GoldValue support** — classic object creation has no
    gold-pile-amount case at all, unlike the TOML path.
  - Creature: `params[1]`=`exp_level`, **already 0-based** (passed straight to
    `init_creature_level()`, unlike the TOML reader which subtracts 1 from the file's 1-based
    `CreatureLevel`). **No CreatureGold/CreatureInitialHealth/CreatureName support.**
  - EffectGen: the struct's own top-level `range` field (not `params`) = effect range;
    `index`=ParentTile.
  - Trap: `index`=ParentTile. **No `params` read at all** — no orientation, nothing else.
  - Door: `params[0]`=orientation, `params[1]`=locked (0/1).
  - **Not representable in classic at all, any class: `Orientation`** (Object/Creature/Trap) —
    `thing_create_thing()`'s switch never sets `move_angle_xy` anywhere. This is a genuine format
    limitation the original F20 finding didn't call out — added to `map_is_legacy_compatible()`'s
    checks below.
- **`.lgt`**: `u32` count, then `struct LegacyInitLight` (20 bytes): `radius`(i16),
  `intensity`(u8), `flags`(u8 — written 0; the TOML path itself only supports `Dynamic` today,
  same "not implemented yet" limitation, not a regression), 3×`i16` unused, `mappos.x/y/z`(u16
  each), `u8` unused, `is_dynamic`(u8), `attached_slb`(i16).
- **`.apt`**: `u32` count, then `struct LegacyInitActionPoint` (8 bytes): `mappos.x/y`(u16 each),
  `range`(u16), `num`(u16). Clean 1:1 field mapping, no class-specific quirks.

## `map_is_legacy_compatible()` — criteria implemented this slice

Per D5/F20's own criteria list, checked against what `MapContent`/the classic writer can actually
represent:
- Map size must be exactly 85×85 (`DEFAULT_MAP_SIZE`, `map_data.h:52`) — required for the `u16`
  coordinate narrowing above to be lossless, not just a format nicety.
- No thing has a non-zero `orientation` on a class where classic can't store it
  (Object/Creature/Trap) — newly-found limitation, see above.
- No creature has `creature_gold`/`creature_health_percent` set, or a `creature_name` — classic
  has no fields for any of these.
- No object gold pile has a `gold_value` — classic can't carry a custom amount.
- Slab kind / creature / object / trap / door model IDs within the classic vanilla range.
  **Caveat, stated plainly:** the exact classic ID ceilings (doc's own "0–53" for slab kinds,
  similar unstated ranges for thing models) are the design doc's own estimates, not re-derived
  from a "here's where the base game's roster ends and mods start" config marker (none was found
  this slice) — treated as a best-effort `WARN`-tier check, not load-bearing for save
  correctness, consistent with D5/F20's own framing of this whole predicate as advisory (governs
  which format `Auto` picks, not a hard gate).
- `.slx` (multi-tileset) presence disqualifies (not written by either writer today, so always
  passes trivially this slice).
- `>255` creatures — checkable directly against `MapContent.things`.
- Script/light-flag/thing-count-`u16`-overflow checks: **not implemented this slice** —
  `MapContent` carries no script field yet (slice 1's own deferral) and light-flag/thing-count
  edge cases are low-value without real script content to test against; noted as a gap for
  whichever slice adds script round-tripping.
