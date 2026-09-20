# Phase 6, round 3 — Terrain modes, stuck selections, KeeperFX-format script gaps

## Toolbox changes

- **Terrain tab** now holds one tool whose modes are the painting ones: Brush / Rectangle / **Fill**
  (Fill was its own tool tab). Every mode shows the terrain palette.
- **Utility tab** gained **Clear**, **Delete** and **Owner** (were Terrain modes: Clear Earth,
  Delete Things, Set Owner). They act on a marked area and have no palette. A top tab with a single
  tool no longer draws a one-tab row.
- **Stuck selection.** Two defences, neither provable headlessly (needs a live UI):
  1. Terrain / creature / owner selections write the local `UserState::cheatselection` directly
     instead of `PckA_CheatSwitch*` (a packet set in the render phase can be overwritten by the
     same turn's `input()` packet; the highlight moved but the kind did not).
  2. `reconcile_work_state()` re-sends the active tool's work state whenever the engine's has
     drifted (only in the plain editing view, never over a queued packet, never on the History
     tab). Visiting another tab used to be the only thing that repaired it.

## KeeperFX-format scripts: what is left (investigation)

The editor reads/writes only `map%05d.txt` (classic DK script) as `MapContent::script_text`.
KeeperFX also loads, per level: `map%05d.lua` (runs in addition to the `.txt`),
`map%05d.rules.cfg`, `map%05d.sounds.cfg`, `map%05d.tmap*.dat`, `map%05d.zip` (custom sprites), a
`cfg/` directory (creature/objects/effects/slabset overrides) and `lua/`. Shipped: 8 `.lua`,
16 `.rules.cfg`, 3 `.zip`, 10 `cfg` sets (vs 451 `.txt`).

Gaps, most important first:

1. **Sidecars are dropped by Save As / new level number.** In-place saves leave them untouched, but
   `MapContentWriter` writes none of them, so saving a KFX level elsewhere silently loses its Lua,
   rules and sounds. Fix: copy known sidecars (renaming `mapNNNNN.*`) on Save As, or make the
   writer round-trip them as opaque blobs like `script_text`.
2. **No Lua view/edit.** A level whose logic is in `.lua` shows an empty/`REM` script in the editor;
   the managed region, availability grid and message helper only emit classic commands, so their
   output would sit beside (not replace) Lua-driven logic. Minimum: show/edit the `.lua` in the
   script dialog (ImGuiColorTextEdit already ships `Language::Lua()`), and warn in the helpers.
3. **Per-level rules** (`.rules.cfg`) are not editable; ambient light etc. is a campaign/level
   config setting (already declared out of scope in phase5/03) but the file could be opened as text.
4. **Script validation** does not know KeeperFX-only commands' argument rules beyond what the
   command browser lists.
5. Lower value: `.tmap` texture overrides and `.zip` sprite packs are asset authoring, not level
   design — out of scope.
