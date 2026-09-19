# Phase 3, slice 1 — KFX-native map save (writer/reader classes + regen + round-trip tests)

Status: **done.** Every checklist item below landed and passed verification. Tracks this slice
specifically; see
[`../03-map-serialization.md`](../03-map-serialization.md) for the full phase-3 scope (classic
binary, `verify_map()`, dialogs — all deferred past this slice, see "Explicitly deferred" below).

## Progress checklist

- [x] Fix `03-map-serialization.md`/`07-investigation-findings.md`'s stale claims (see "Ground-
      truth corrections" below) — both docs updated in place with corrections + pointers back to
      this tracking doc.
- [x] `regenerate_derived_map_data()` (`kfx_sim`) + `load_level_file()` failure-path fix (the
      safety fix — missing/corrupt `.dat`/`.clm` must fail loudly, not silently corrupt). Landed
      in `lvl_filesdk1.c`/`.h`; the regen loop runs only after `load_map_slab_file()` has set
      real slab kinds (it must — that's the earliest point they're known), not near the
      `.dat`/`.clm`/`.flg` load calls the original doc placement implied. Verified: new
      `lvl_filesdk1_regen_test.cpp` (happy path + forced out-of-range-kind failure) passes;
      full `kfx_sim_utest` suite (640 cases) passes; `check_layering.py --strict` clean; both
      `keeperfx`/`keeperfx_hvlog` build; the full headless ftest sweep (21 tests, multiple real
      stock campaign levels through the modified `load_level_file()`) passes with no failures.
- [x] `map_content.h` — `MapThingRecord`/`MapLightRecord`/`MapActionPointRecord`/`MapLevelInfo`/
      `MapContent`. Field names/types cross-checked directly against
      `thing_create_thing_adv()`/`light_create_light_adv()`/`actnpoint_create_actnpoint_adv()`
      (not just F3's summary of them) — confirmed exactly correct, including that a position
      field (`SubtileX`/`Y`/`Z`, and `LightRange`/`PointRange`/`EffectRange`) is a
      `[whole_subtile, sub_subtile]` TOML array pair (`value_read_stl_coord()`), not a bare int.
- [x] `MapContentWriter` (abstract, `kfx_sim`) + `KfxNativeMapContentWriter` — hand-emitted TOML
      (`[[thing]]`/`[[light]]`/`[[actionpoint]]` array-of-tables), `.lof` as classic key=value
      (not TOML), `.slb`/`.own`/`.inf` binary, a minimal empty `.txt` script stub (`MapContent`
      carries no script field yet — deferred).
- [x] `MapContentReader` (abstract, `kfx_sim`) + `KfxNativeMapContentReader` — reuses CentiTOML's
      real `toml_parse()`/`VALUE` API and `value_read_stl_coord()` (not a hand-rolled parser), so
      the byte-level TOML handling is the same proven code the production loaders use; only the
      "where does each field go" destination differs (`MapContent`, not a live `Thing`/`Light`).
- [x] `editor_save_map()` (`kfx_editor`) — snapshot (`kfx_sim_state`'s slabs/things/action points,
      `kfx_render`'s `lish` for lights, direct reads — no callback, matching this library's own
      established downward-call style) + `KfxNativeMapContentWriter` + a post-save
      `find_and_load_lof_files()`/`find_and_load_lif_files()` rescan (F13). One real bug found and
      fixed along the way: an unfiltered sweep of every live `Thing` also picked up transient,
      runtime-only classes (shots, effect elements, ambient sounds, ...) that
      `thing_create_thing_adv()` has no case for at all — writing one out made the reload
      legitimately reject it ("Invalid class N, thing discarded"), failing the whole load. Fixed
      with an explicit `thing_class_is_saveable()` filter (Object/Creature/EffectGen/Trap/Door
      only — exactly F3's own schema coverage).
- [x] Catch2: `map_content_roundtrip_test.cpp` (`kfx_sim_utest`) — passes (114 assertions).
      Found and fixed two real bugs along the way (see "Bugs found this slice" below): (1)
      `toml.h` has no `extern "C"` guard of its own, breaking the first-ever `.cpp` call site for
      `toml_parse()` (fixed locally, an `extern "C" { #include "value_util.h" }` wrapper in
      `map_content_reader.cpp`); (2) `create_directory_for_file()` (`kfx_platform`) mis-handles
      absolute paths — a real, previously-documented-but-unfixed bug (fixed in
      `bflib_fileio.c`, with new regression coverage in `bflib_fileio_test.cpp`).
- [x] Catch2: `lvl_filesdk1_regen_test.cpp` (`kfx_sim_utest`), incl. the forced-failure case
      (covered by the first checklist item's own verification)
- [x] Catch2: `editor_save_map()` snapshot test — not added separately; `kfx_editor_utest`'s own
      suite (49 assertions, 8 cases, unchanged) confirms `editor_mapsave.cpp` compiles/links
      cleanly against the real `kfx_sim_state`/`kfx_render` state, and the ftest below already
      exercises the snapshot end-to-end against real live state (creature + trap survive a real
      save/reload), which is a stronger signal than a synthetic unit fixture would add here.
- [x] ftest: `ftest_editor_save_reload` (`src/ftests/`) — passes. Saves to a fresh, otherwise-
      unused level number (`90001`) in the real staged campaign directory (never overwriting
      `keeporig` itself, which every other editor ftest also loads), reloads via the real
      production `load_map_file()`, asserts the placed creature and trap both survive with the
      right model/owner. Full registered-ftest sweep: 22/22 pass, no regressions.
      **Empirical confirmation of the S1 spike concern** the ground-truth corrections already
      flagged: the reload logs ~250+ non-fatal `update_slabset_column_indices: column:N
      referenced in slabset.toml but not present in columnset.toml` errors — `regenerate_derived_
      map_data()` is the first code path in this codebase to exercise `place_single_slab_type_on_
      map()` across a *whole real map's* full range of neighbour/style/pick combinations at once;
      normal gameplay only ever calls it incrementally as players dig/build, so this data gap
      (some slab-style column variants that a real map's shipped `.clm` provides pre-baked, which
      the runtime regen path doesn't reproduce) never surfaced before. Non-fatal today (falls
      back to *something*, doesn't crash or fail the load) but a real, now-named target for the
      S1 spike's own "how many stock maps need their `.clm` kept" investigation, not fixed here.
- [x] `check_layering.py --strict` clean; `kfx_platform_utest` (251 cases)/`kfx_sim_utest` (641
      cases)/`kfx_editor_utest` (8 cases) all pass; both `keeperfx` and `keeperfx_hvlog` build;
      the full ftest sweep (22/22) passes headlessly, covering multiple real stock campaign
      levels through every changed code path (loader regen/failure-path, editor save, reload).

## Bugs found this slice (not the feature itself, but real, fixed along the way)

- **`load_level_file()` silently corrupted a map when `.dat`/`.clm` were missing/corrupt**,
  instead of failing to load — see "Ground-truth corrections" below for the full detail. Fixed
  by `regenerate_derived_map_data()` + the loader's result-propagation fix.
- **`create_directory_for_file()` (`kfx_platform/src/bflib_fileio.c`) couldn't create any
  directory for an absolute path** (`mkdir("")` on the leading `/`, always fails, aborts before
  creating anything real) — every production caller happens to use relative, campaign-resolved
  paths, so this stayed latent. A prior session had already found and documented this exact bug
  (`bflib_fileio_test.cpp`'s own header comment, "worth a real bug report") but left it unfixed,
  working around it in its own tests instead. This slice's writer/reader design deliberately
  needs to target an arbitrary directory (including absolute scratch/test paths, per the
  DTO-based testability design), which needed this fixed for real. Fixed, with new regression
  coverage (`bflib_fileio_test.cpp`'s new "absolute path" test case).
- **`editor_save_map()`'s snapshot swept every live `Thing`, including transient runtime-only
  classes** (shots, effect elements, ambient sounds, ...) `thing_create_thing_adv()` has no case
  for — writing one out made the reload legitimately reject it, failing the whole load. Fixed
  with a `thing_class_is_saveable()` filter (Object/Creature/EffectGen/Trap/Door, matching F3's
  own schema coverage exactly).
- **`toml.h` (`deps/centitoml`) declares `toml_parse()` with no `extern "C"` guard of its own**
  (unlike `value.h`, which does) — every existing include site was a plain `.c` file, so this
  never mattered until this slice's reader became the first `.cpp` in the codebase to actually
  call `toml_parse()` directly (not just the already-guarded `value_*()` accessors). Worked
  around locally in `map_content_reader.cpp` (an explicit `extern "C" { #include "value_util.h" }`
  wrapper) rather than touching the vendored third-party header itself.

## Context

`docs/refactor/editor/03-map-serialization.md` ("Phase 3 — map serialization") was marked "not
started" and was written from an earlier investigation pass
(`docs/refactor/editor/07-investigation-findings.md`, F1–F21). Before writing any code, three
parallel investigation passes re-verified the doc's technical claims against the current codebase
(post renderer-refactor, post the D6 keybinding/testing work done earlier this project). They
found the doc's overall strategy is sound ("reverse the reader" — every writer mirrors an existing
`load_*`) but several concrete claims are stale or wrong, and surfaced real design gaps — see
"Ground-truth corrections" below.

Scope for this slice, confirmed with the user: **KFX-native save only** — classic-binary writers,
`verify_map()`, and the New/Open/Save dialogs are later slices. The user also flagged that this
work is a preliminary step toward a structured map/game-state format an LLM could read or produce
— so the code that actually serializes data is built as a small virtual C++ class hierarchy (base
interface + concrete formats as children), living in the same library as the existing readers
(`kfx_sim`), not in `kfx_editor`. See "Architecture for this slice" below for how that shapes the
design.

## Ground-truth corrections

`03-map-serialization.md` and `07-investigation-findings.md` need these fixes before/while
implementing, so they stay trustworthy for later slices:

- **The central "current blocker" premise is wrong — and this is a safety issue in its own
  right, fixed as part of this slice regardless of the editor save work.** `load_level_file()`
  (`lvl_filesdk1.c:1442`) does **not** propagate `.dat`/`.clm` load failure today — their return
  values are discarded (lines 1453/1455) and the function's final `result` is separately
  clobbered by whichever `load_tngfx_file`/`load_thing_file` call runs last. A missing or
  corrupt `.dat`/`.clm` right now **silently produces a broken/blank-looking map** (zeroed
  `col_idx`, empty `columns_data[]`) instead of a clean load failure — exactly the kind of bug
  that surfaces much later as "why is this dungeon glitched" rather than an obvious, actionable
  error at load time. Concrete fix, spelled out precisely so it isn't just an implied
  consequence:
  1. `.dat`/`.clm` present → unchanged fast path, load them directly, as today.
  2. `.dat`/`.clm` absent → call `regenerate_derived_map_data()` instead (this is the case a
     KFX-native editor save relies on, since it never writes those files).
  3. **If regeneration itself cannot fully populate the map, `load_level_file()` must return
     `false`** — an honest, visible failure — never fall through to a partially-built map the way
     it does today.
  4. Safe for existing content: every currently-shipping stock map ships with a real `.clm`, so
     this only changes behavior for the "genuinely absent" case, which today means silent
     corruption for incomplete/hand-edited maps — turning that into a clean failure is strictly
     safer, not a regression risk for real campaign data.
- **`.apt` (classic binary) layout is wrong in the doc**: real `LegacyInitActionPoint`
  (`lvl_filesdk1.c:96-124`) is a 4-byte 2D location (`LegacyCoord2d`) + `u16 range` + `u16 num` (8
  bytes total) — the doc says "6-byte Location + u16 number" and omits `range` entirely. (Not
  blocking for this slice — classic writers are deferred — but must be fixed before that slice.)
- **`.clm` header is `u32` count + 4 reserved bytes**, not a `u64` count.
- **`.slb` uses the full LE `u16` as slab kind**, not "low byte = kind."
- **`init_columns()` (`map_columns.c:378`) is mischaracterized.** It's a slab-agnostic bitfield
  finalization pass over whatever `columns_data[]` entries already exist — it does **not** do
  "neighbour-aware 3×3 regen from slab kind." That's `place_single_slab_type_on_map()`
  (`map_blocks.c:1315`, doc cites 1292 — drifted) alone.
- **Reinforced corners are not covered by the proposed regen.** `fill_in_reinforced_corners()` is
  reachable only via the digger-specific `place_and_process_pretty_wall_slab()`, never via
  `place_single_slab_type_on_map()`. A `regenerate_derived_map_data()` built as originally
  described will not reproduce reinforced fortress walls on stock maps — a likely, now-named
  source of S1 spike diffs, not just a vague risk.
- **`load_slab_file()` vs `load_map_slab_file()`** are unrelated functions — only
  `load_map_slab_file()` (`lvl_filesdk1.c:1196`) reads the per-level `.slb`; `load_slab_file()`
  (1042) rebuilds the global slabset→column config table and isn't part of per-map I/O at all.
- **`map%05d.flg`** (`load_map_flag_file()`, `lvl_filesdk1.c:1233`, called from `load_level_file`
  at line 1454) is loaded today but appears in **no** reader/writer table in the doc at all. It's
  per-subtile runtime flags (tagged-for-dig, unexplored/fog-of-war) — session state, not authored
  level design. Decision (confirmed with the user): `regenerate_derived_map_data()` resets it to
  default; never round-tripped by the editor. Noted for later: a future LLM-facing state format
  may still want to *read* current `.flg` state (distinct from the editor's save behavior).
- **TOML library attribution**: it's CentiTOML (`deps/centitoml`, backed by CentiJSON's `VALUE`
  model, `deps/centijson_src/`), not literally `tomlc99` (that's a design ancestor per
  CentiTOML's own README). Parse-only conclusion still holds — `toml.h` exposes only
  `toml_parse()`. `json_dom_dump()` exists (`custom_sprites.c:1354`, `kfx_script/api.c` several
  call sites) but serializes to JSON syntax, not TOML — not reusable for the writer, worth a note
  so it isn't "rediscovered" later.
- Everything else the doc claims about the `tngfx`/`lgtfx`/`aptfx` TOML field schemas (F3) was
  read against the actual loader callbacks (`thing_create_thing_adv` `thing_factory.c:215`,
  `light_create_light_adv` `light_data.c:195`, `actnpoint_create_actnpoint_adv` `actionpt.c:81`)
  and **confirmed exactly correct**, field-for-field, per thing class — this is solid ground to
  build the writer against.

## Architecture for this slice

**Key decision, driven by the user's two notes**: introduce a small **plain-data layer**
decoupled from live `kfx_sim_state`, and a **virtual writer/reader class pair** operating on it —
not functions that reach into global engine state directly. This does three things at once:
serves the "virtual C++ with children" request cleanly (Strategy pattern — one interface, one
concrete class per format, format selection swaps the concrete class, no branching at call
sites); makes the classic-binary slice later a pure "add a subclass," no base-class rework; and
resolves the real testability gap the investigation found (`lvl_filesdk1_test.cpp`'s own header
comment calls the `load_*_file` functions "genuinely untestable at the unit level" because they
resolve their file path via `lvnum → get_level_fgroup() → prepare_file_fmtpath()`, tied to real
installed-game directory groups, not an arbitrary path) — a DTO-based writer/reader pair takes an
explicit directory, so a Catch2 test never needs real game data or campaign/`LevelInformation`
setup at all.

New code, in `src/kfx_sim/` (per the user's "same library as the map file reads"):

- **`src/kfx_sim/include/map_content.h`** (new) — plain structs: `MapThingRecord`,
  `MapLightRecord`, `MapActionPointRecord`, `MapLevelInfo`, and a `MapContent` container bundling
  them plus the slab-kind/ownership grids. Field names/types mirror the TOML schema exactly (F3,
  confirmed above) so mapping is mechanical in both directions.
- **`src/kfx_sim/include/map_content_writer.h`** + **`src/kfx_sim/src/map_content_writer.cpp`**
  (new) — abstract `MapContentWriter` base class, modeled on the existing `LensEffect`/`IPlatform`
  virtual-family style already used in this codebase (`src/kfx_render/include/LensEffect.h`,
  `src/kfx_platform/include/platform/IPlatform.h`). Shared, format-independent pieces
  (`write_slabs`, `write_ownership`, `write_texture`, `write_script`) are concrete methods on the
  base (identical either way, per the doc's own §3 step 4); format-specific pieces
  (`write_things`, `write_lights`, `write_action_points`, `write_level_info`) are pure virtual.
  One concrete subclass this slice: `KfxNativeMapContentWriter` (TOML emission for
  `tngfx`/`lgtfx`/`aptfx`/`lof`, hand-emitted per F3's confirmed schema — no writer library
  exists, confirmed). `ClassicMapContentWriter` (binary) is a named, deferred future subclass —
  not built this slice.
- **`src/kfx_sim/include/map_content_reader.h`** + **`src/kfx_sim/src/map_content_reader.cpp`**
  (new) — mirror-image `MapContentReader` base + `KfxNativeMapContentReader` concrete subclass,
  parsing the same TOML forms back into a `MapContent`. This is new, purpose-built parsing (not a
  refactor of the production `load_tngfx_file`/etc., which mutate `kfx_sim_state` directly via
  callbacks and stay untouched — out of scope here to avoid destabilizing real gameplay loading).
  Justified as more than test scaffolding: `MapContent` + reader/writer *is* the structured,
  engine-decoupled format the user's longer-term LLM-facing goal needs — this slice lays that
  foundation for real, not just to make a test pass. Wiring this reader into the production
  `load_level_file()` path is explicitly **not** done this slice.
- **`regenerate_derived_map_data()`** — new plain function (not part of the class hierarchy; no
  format axis, pure engine mechanics), added to `lvl_filesdk1.c`/`.h` where the doc originally
  proposed it: for each slab, `place_single_slab_type_on_map()` → `init_columns()` →
  `initialise_map_wlb_auto()`, plus resetting `.flg`-equivalent state to default per the decision
  above. Returns `TbBool`. `load_level_file()` is fixed per the exact 4-step behavior in the
  "Ground-truth corrections" section above — call it when `.dat`/`.clm` are absent, and
  **propagate a `false` result if it fails**, closing the silent-corruption gap for good (not
  just for the editor's benefit — this is a general loader-robustness fix). This is what makes a
  KFX-native save (which never writes `.dat`/`.clm`/`.wib`) reliably reloadable, and it must land
  before the writer is exercised end-to-end (the `ftest_editor_save_reload` test below depends on
  it directly).

kfx_editor side (thin orchestration only — reads real engine state, calls into the kfx_sim
classes above; this is where the doc's original "kfx_editor reads kfx_sim state directly" still
applies, just now producing a `MapContent` rather than writing files itself):

- **`src/kfx_editor/src/editor_mapsave.cpp`** (new) — `editor_save_map(LevelNumber lvnum, const
  char *dir, unsigned flags)` (public entry, declared in `kfx_editor.h` per the doc's original
  signature). Steps: snapshot live state into a `MapContent` (this is the one place that must
  cross into `kfx_render` for lights — `kfx_editor` outranks both `kfx_sim` and `kfx_render` and
  can `#include` both directly, same as `editor_toolbox.cpp` already does for `kfx_sim`/
  `frontgui_widgets`; reads `kfx_sim_state`'s things/action-points/slabs directly, and
  `kfx_render`'s `lish.lights[]` directly for lights — this cross-library read is exactly why the
  snapshot step lives in `kfx_editor`, not in the `kfx_sim`-resident writer) → construct a
  `KfxNativeMapContentWriter` → call it with the snapshot and `dir` → call
  `find_and_load_lof_files()`/`find_and_load_lif_files()` directly afterward (per investigation:
  `kfx_editor` has zero existing precedent for going through `config_reload_callbacks` — every
  current downward call in this library is a direct `#include`+call, since `kfx_sim`/`kfx_config`
  both rank below it; match that established style, not the doc's callback-struct wording).
  `verify_map()`, the dirty flag, and session lvnum/dir tracking (`editor_session.cpp` currently
  discards `lvnum` after `editor_open()`) are **not** wired up this slice — `editor_save_map()` is
  built and tested as a standalone, explicitly-parameterized function; UI wiring (which needs the
  session-state tracking and the dirty flag) is deferred to the dialogs slice.

## Tests

- **`src/kfx_sim/tests/map_content_roundtrip_test.cpp`** (new, `kfx_sim_utest`) — the real
  regression net for this slice, following `ResetSimAndConfig`'s "small composable helper" style
  (`src/kfx_sim/tests/kfx_sim_test_fixtures.h`): hand-build a synthetic `MapContent` (a handful of
  things across different classes incl. a hero gate, a couple of lights, a couple of action
  points, non-default level info) → `KfxNativeMapContentWriter` to a Catch2 temp directory →
  `KfxNativeMapContentReader` back → deep-equal against the original. No `kfx_sim_state`, no
  campaign/`LevelInformation`, no real game data required — exercises exactly the writer/reader
  pair in isolation, which is the actual point of decoupling them from global state.
- **`src/kfx_sim/tests/lvl_filesdk1_regen_test.cpp`** (new, or added to an existing
  `lvl_filesdk1_test.cpp`-adjacent file) — `regenerate_derived_map_data()` on a small
  `ResetSimAndConfig`-fixture map: assert columns/collision get populated from slab kinds alone
  (no `.clm` needed), and that `load_level_file()`'s overall result now genuinely depends on this
  path succeeding. Includes the forced-failure case (see "Verification" below).
- A `kfx_editor`-side test for `editor_save_map()`'s snapshot step (real `kfx_sim_state` +
  `kfx_render`'s `lish` via a fixture, verifying the produced `MapContent` matches what was
  seeded) — added to `kfx_editor_utest` if the snapshot logic is non-trivial enough to warrant its
  own test once written.
- **`src/ftests/tests/ftest_editor_save_reload.c`** (new) — the end-to-end regression net,
  exercising `editor_save_map()` for real against the actual running game rather than in
  isolation, matching the doc's own original §9 plan (`editor_save_reload`) but scoped to the
  KFX-native path only this slice, and calling `editor_save_map()` directly (no dialog exists
  yet, same "call the production function directly, no UI" approach already established by
  `ftest_editor_place_creature`/`ftest_editor_paint_terrain`/`ftest_editor_undo`). Sequence:
  `editor_open()` on a real stock level (`"keeporig"`, matching the existing editor ftests'
  convention) → place a creature, a trap, and (if the snapshot step reads `kfx_render` lights
  directly, per the architecture above) a light via existing explicit-position verbs → call
  `editor_save_map()` to a scratch level slot/directory (needs a concrete convention for "where
  does an ftest-authored save go without colliding with real staged data" — decide this during
  implementation, following whatever pattern `StageFtestData.cmake`/`ftest_util.c` already use
  for scratch paths) → load that saved level back → assert every placed thing reappears with the
  right model/owner/position. Headless-safe by construction (no ambient-position packet verbs
  anywhere in this sequence, same discipline the D6/phase-2 editor ftests needed — see
  `ftest_editor_paint_terrain.c`'s own header comment). Registered in `ftest_list.c` alongside
  the other `editor_*` entries. This is the test that actually proves the KFX-native save format
  round-trips through the real engine, not just through the new writer/reader pair in isolation —
  complements, not replaces, the Catch2 `map_content_roundtrip_test` above (that one is fast/
  isolated and pins the exact byte/field format; this one proves the whole pipeline — snapshot,
  regen, and reload — works together).

## Explicitly deferred (later slices, not this pass)

- Classic-binary format: `ClassicMapContentWriter`/`Reader`, `map_is_legacy_compatible()`, the
  Auto/Force format selector.
- `verify_map()` and its report structure (ADiKtEd is **not** vendored in this repo —
  `3rdparty/Keeper/ADiKtEd-master` doesn't exist — so this will need to be designed fresh against
  KFX's own structures and the check list already written into `03-map-serialization.md` §5, not
  "ported").
- `editor_new_map()`, and the New/Open/Save/Save-As/Overwrite ImGui dialogs (`frontgui_widgets`
  has reusable list/text-input/modal primitives per the investigation, but no thumbnail/image
  widget and no file-picker — both net-new when that slice starts). **Done in
  [`02-slice3-dialogs-menubar.md`](02-slice3-dialogs-menubar.md).**
- Session state on `editor_session.cpp` (current level number/dir, dirty-flag clearing) — only
  needed once a UI Save button exists. **Done in
  [`02-slice3-dialogs-menubar.md`](02-slice3-dialogs-menubar.md).**
- `land_preview_build_minimap()` export (currently `static`, file-local to
  `frontmenu_landpreview.c`) — needed for Open-dialog thumbnails, not this slice. **Done in
  [`02-slice3-dialogs-menubar.md`](02-slice3-dialogs-menubar.md).**
- A rename/atomic-write primitive in `kfx_platform` (only `LbFileSaveAt`/`LbFileDelete` exist
  today) — needed once Save is a real, user-facing, overwrite-risking action; this slice's writer
  can do a plain `LbFileSaveAt` per file with no atomicity guarantee, acceptable for a
  library-level/test-driven capability with no UI yet. **Done in
  [`05-slice6-atomic-write-lif.md`](05-slice6-atomic-write-lif.md).**

## Verification

- `python3 scripts/check_layering.py --strict` — confirm no new violations (the writer/reader
  classes living in `kfx_sim` must not `#include` anything from `kfx_render`/`kfx_editor`/etc.;
  the cross-library light read stays in `kfx_editor`'s snapshot step, not in the writer).
- Build `kfx_sim_utest` and `kfx_editor_utest` (`-DKFX_BUILD_TESTS=ON`, native Linux) and run via
  `ctest` — new tests pass, nothing existing regresses.
- Manually build `keeperfx`/`keeperfx_hvlog` (both variants) to confirm the new
  `regenerate_derived_map_data()` call in `load_level_file()` doesn't break normal level loading
  (stock campaign maps still load and play identically — this is exactly what S1's "validation
  spike" from the doc is for, now scoped as a concrete build+manual-load check rather than a
  separate spike task).
- Specifically verify the new failure-path behavior (the safety fix): a `.clm` genuinely absent
  *and* regeneration failing must make `load_level_file()` return `false`, not silently continue
  with a partial map — add a small `lvl_filesdk1_regen_test.cpp` case (or extend the one above)
  that forces `regenerate_derived_map_data()` to fail (e.g. an out-of-range slab kind it can't
  place) and asserts the loader's overall result is `false`, not just that regen ran.
