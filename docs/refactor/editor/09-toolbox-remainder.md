# Phase 2 (companion) — toolbox remainder

Status: **tracking doc, live.** Companion to [`02-editing-toolbox.md`](02-editing-toolbox.md),
written once every tool in that doc's own tool list had a real, working implementation and the
question "is phase 2 done?" needed an honest answer rather than a yes/no. Short version: every
*tool* works end to end (placement, area ops, fill, brush, query, undo/redo), but several things
the original doc called for are either scoped-down first slices or not started at all. This doc
is the punch list — read `02`'s own inline status notes first for *why* each cut was made; this
is just the consolidated "what's left" view across all of them, so nothing quietly falls off the
list.

Nothing here blocks using the editor to build a map today. Treat this as backlog, not as a gate.

---

## 1. Scoped-down first slices (working, but narrower than the original design)

| Item | What exists | What's missing |
|---|---|---|
| **Brush / Stamp** (§2.4) | **Done, minus Lights/APs.** Capture + stamp a rectangle of **terrain slabs** (`SlbT_BRIDGE`/`GEMS`/`GUARDPOST` excluded from capture, Heart/Portal excluded from stamp) *and* (new) **things** -- creature/hero/digger/object/trap/door -- captured at subtile precision alongside the slab-snapped terrain buffer, via a second `BrushThingEntry` buffer and the same RMB-drag-capture/LMB-click-stamp gesture. All direct sim calls (`create_creature()`/`create_object()`/`player_place_trap_without_check_at()`/`player_place_door_without_check_at()`), same D2 single-player-local exception as terrain stamping. Neither slab nor thing stamps are undo/redo-journaled (consistent with each other, not a new gap). | **Lights/Action Points capture is deferred to phase 5** ([`05-script-and-level-settings.md`](05-script-and-level-settings.md)) alongside those tools' own placement paths -- nothing to capture until they exist. |
| **Eyedropper** (§2.10) | **Done.** Samples a terrain slab's kind+owner, *or* (new) a creature/object/trap/door thing's model+owner, into the matching picker -- `handle_eyedropper_thing_sample()` reuses Query's own creature-vs-thing detection, new `PckA_EditorEyedropperThing` verb syncs whichever server-side field (`CheatSelection`/`UserState`) the sampled class is backed by; Object stays a local-only update, same F17 gap `PckA_EditorPlaceObject`'s own picker already works around. | Nothing outstanding. |
| **Undo/Redo** (§4) | **Done, minus Brush/Flood-Fill.** Full undo+redo for **thing placement** (creature/hero/digger/object/trap/door), Ctrl+Z/Ctrl+Y, journal-backed, redo correctly restores position via dedicated `PckA_EditorRedo*` verbs. Plus (new) **rect-terrain-op undo+redo**: `PckA_EditorPlaceTerrainRect`/`_RectClearEarth`/`_RectSetOwner` each have a clean drag-release boundary to journal against, unlike free-hand paint/fill -- `packets_cheats.c` snapshots each slab's pre-mutation kind+owner into the journal (`EditorJournalCallbacks::record_rect_terrain`, a new callback, called *before* the mutation) and `editor_journal.cpp` applies the snapshot (undo) or reapplies the recorded op (redo) directly against `kfx_sim`, same D2 single-player-local exception as Brush/Stamp -- no packet round trip, since a packet can't carry a variable-length per-slab buffer anyway. | **Free-hand Terrain brush painting and Flood Fill stay deferred** -- genuinely no client-visible stroke boundary to journal against (per-tile-per-frame / recursive-flood dispatch). **`PckA_EditorRectDeleteThings` undo also stays deferred** -- restoring arbitrary deleted things (full creature state, etc.) faithfully needs a much bigger snapshot than a slab kind/owner pair. |
| **Object placement** (§2.6) | Model-grid picker + click-to-place, owner from the bottom bar. Clicking an *existing* object (rather than empty ground) selects it into a new edit panel (`draw_object_edit_panel()`) instead of stacking a second object on the same square: **X/Y/Z position** (raw map units, 256/subtile -- needed for genuinely fine control, e.g. moving a wall torch up its wall, not just re-placing it at subtile granularity) via new `PckA_EditorSetThingPosition`, plus a **gold-amount Value field** (gold-family objects only) via `PckA_EditorSetGoldValue`. Both verbs are thing_idx-keyed and deliberately generic (not object-specific) so Lights/Action Points can reuse the same panel once those tools exist. | "Spellbook power"/"special kind" from the original design have no per-instance field in the current data model to tweak — gold amount and position are the only genuinely per-instance object properties that exist, so they're the only ones implemented. No property panel for Lights/Action Points themselves yet -- those tools don't exist at all (§2 below). |
| **Set Owner (room transfer)** (§2.2) | Done properly — `delete_room_slab()` + re-place transfers room ownership correctly, not just non-room slabs. | Nothing outstanding here; listed only so it's not mistaken for still-open when skimming `02`'s own history of back-and-forth on it. |

## 2. Not started

- **Lights & effect-emitter, Action-point toolbox entries** (`02` §2.8/§2.9). **Deferred to
  phase 5** ([`05-script-and-level-settings.md`](05-script-and-level-settings.md) §2/§3) rather
  than picked up here as placeholder stubs — both need a real properties panel
  (intensity/radius/height for lights, range/numbering for APs) to be worth anything, and phase 5
  is where that panel work belongs. Nothing exists yet — no tool-strip button, no picker, no
  placement path — but Object placement's value-property follow-up (§1 above) already built the
  reusable plumbing phase 5 should build on: a generic thing_idx-keyed
  `PckA_EditorSetThingPosition` packet and `draw_object_edit_panel()`'s X/Y/Z editing shape, both
  deliberately not object-specific for exactly this reason. Whoever picks this up in phase 5
  should also decide whether a toolbox entry makes more sense as its own tab from the start
  (§3 below) rather than a stub bolted onto an existing one.
- **Definable keybindings** (`02` §3, D6). **First slice landed** -- see
  [`10-definable-keybindings.md`](10-definable-keybindings.md) for the full writeup, including
  three pre-existing subsystem gaps this exercise surfaced (deliberately -- the point of doing a
  slice at all was to stress-test the `settings.kbkeys[]`/`Gkey_*`/Define-Keys-menu path end to
  end). Short version: `Gkey_EditorEraseTool` (default `R`) is the first real definable editor
  key, a new capability (no keyboard route to the Erase tool existed before). **Ctrl+Z/Ctrl+Y
  explicitly accepted as staying hardcoded** (user's own call -- standard everywhere, D6's
  "definable" concern doesn't really apply to them) -- Undo/Redo instead got toolbox buttons (next
  to Menu, always visible) plus a **History tab** (`draw_history_tab()`), so a mapmaker doesn't
  need to know the shortcut exists at all. RMB-drag for Brush capture is still hardcoded and
  arguably doesn't even belong in this key-binding system (`10`'s own §3) -- along with mapping
  the other 11 tools to hotkeys, that's the real remaining scope, and it's a product-scoping
  decision (which tools get a hotkey, what key) as much as an engineering one, not a quick fix.
- ~~**"Thing counter"**~~ **Done** (`02` §5 item 4: "creature/hero/digger placement with a
  model-grid picker **+ thing counter**"). A "Things: N / SYNCED_THINGS_COUNT" readout
  (`FeCaption`, header row next to Undo/Redo) counts synced things only -- the unsynced bucket
  (kfx_sim_state's own synced/unsynced split) is transient local effects, not map content.
  Deliberately just the raw engine ceiling, not the "classic-compatible vs KeeperFX target mode"
  ceiling [`07-investigation-findings.md`](07-investigation-findings.md) F18 describes -- that
  needs `verify_map()`'s own target-mode selector (phase 3), which doesn't exist yet.
- **RMB-delete shortcut in other tools** (`02` §2.10: "Delete ... RMB in most tools; also an
  explicit eraser tool"). The explicit Erase tool exists and works; the *secondary* RMB-to-delete
  shortcut while another placement tool is active does not.
- **Broader test coverage** (`02` §6) — done, with two scope exclusions:
  - `editor_place_creature`, `editor_paint_terrain`, `editor_undo` (all confirmed passing against
    a real install) cover creature placement/undo, a rect-terrain op, and trap place/undo/redo
    respectively. See `editor_place_creature`'s own file-header comment for two non-obvious things
    reused by the others (the `PckA_EditorRedoCreature`-not-`PckA_CheatMakeCreature` dispatch
    choice, and the `editor_open()`-freezes-`get_gameturn()` turn-scheduling trap).
  - `editor_fill` was **not** written: `PckA_EditorFloodFill`'s seed position lives *only* in the
    packet's ambient `pos_x`/`pos_y`, with no explicit-param alternative the way the placement
    verbs have. Worse, an ftest action can't even read that field -- `ftest_update()` runs before
    `input()` each tick, and `clear_packets()` wipes it right after `process_packets()` consumes
    it, so a test's own read always sees the just-wiped 0. Probing it (not committed) also showed
    it isn't steerable via camera position under the SDL dummy video driver. See
    `ftest_editor_paint_terrain.c`'s own header comment (which hit the same ambient-position issue
    for one of `PckA_EditorPlaceTerrainRect`'s two corners, and worked around it by only asserting
    the *other*, fully-explicit corner) and `ftest_list.c`'s own comment at the exclusion site.
  - `editor_brush` was **not** written either: Brush/Stamp's capture+stamp mechanism calls sim
    mutation primitives directly (the D2 single-player-local exception) with no packet verb at
    all, and ftests can only inject packets -- this tool's client-side-only logic is unreachable
    from that layer.
  - The suggested Catch2 "palette generation from a synthetic config" coverage turned out to
    already exist: `draw_terrain_picker()` just loops and calls `slab_code_name()`, and
    `src/kfx_config/tests/config_terrain_test.cpp` already tests that function's fallback behavior
    directly -- no separate kfx_editor-side test needed. The "journal inverse-arg capture" idea is
    covered by `src/kfx_editor/tests/editor_journal_test.cpp` (`kfx_editor_utest`, wired into the
    root `coverage` target): `record_placement`/`record_rect_terrain` gating, `describe_undo`/
    `describe_redo` labels, `do_undo`/`do_redo` packet dispatch (including the four ambient-
    position placement verbs' Redo-counterpart remap), and a `stack_push()` overflow/eviction
    regression test for the memmove-on-a-vector-owning-struct bug fixed earlier in this effort.
    Needed one new test-only seam: `editor_journal_test_force_active()` (`editor_journal.h`/
    `.cpp`), since `record_placement`/`record_rect_terrain` gate on `editor_is_active()`, which had
    no other exported override and a real `editor_open()` needs a genuinely loaded level/player.

## 3. Toolbox UI: tabs

The toolbox's tool strip grew to 12 entries across this phase (Terrain, Fill, Creature, Hero,
Digger, Object, Trap, Door, Eyedropper, Stamp, Query, Erase) — combined with a tool's own picker
below it (a 320px-tall list, for the bigger ones) and the bottom bar, the whole window stopped
reliably fitting on screen. Fixed by grouping the tool strip into four tabs
(`FeBeginTabBar`/`FeTab`, the same wrapper the settings screen's own Game/Graphics/Sound/Mouse
tabs use):

- **Terrain** — Terrain, Fill
- **Creatures** — Creature, Hero, Digger
- **Things** — Object, Trap, Door
- **Utility** — Eyedropper, Stamp, Query, Erase

Switching tabs only changes which tool *buttons* are visible; it doesn't change the active tool
or picker underneath. This is a first pass at the grouping, not a final taxonomy — if Lights/FX/
Action Points get picked up (§2 above), they'll need a home too, and "Utility" in particular is
already a bit of a catch-all.

Note this is a much lighter-weight response to the same "toolbox is too big for one screen"
problem [`08-gui-layout.md`](08-gui-layout.md) already proposed a full answer to (menu bar +
icon tool rail + right-side control column reusing the game's own HUD furniture + status bar).
That proposal was never implemented — what actually got built this phase is a single
`ImGuiWindowFlags_AlwaysAutoResize` window with a text-button tool strip, nothing from `08`'s
own widget inventory (`FeMenuBar`, `FeSplitter`, icon buttons, the reused minimap/sidebar). The
tab grouping here is a pragmatic patch on top of that simpler shape, not a step toward `08`'s
design — if the toolbox keeps growing, revisit `08` rather than adding more tabs indefinitely.
