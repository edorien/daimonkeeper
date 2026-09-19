# Phase 8 — in-editor GUI layout

Status: **superseded.** This doc's full redesign (icon tool rail, moving the whole palette onto
the game's reused sidebar/minimap column, splitter inspector) was never built — the tab-strip
toolbox actually built in phase 2/9
([`09-toolbox-remainder.md`](09-toolbox-remainder.md)) is the accepted direction instead, and the
toolbox panel/tab-strip layout stays as-is. Only this doc's **menu-bar and dialog** ideas (§2's
menu bar row, §5's New/Open/Save As dialogs) were carried forward, into
[`phase3/02-slice3-dialogs-menubar.md`](phase3/02-slice3-dialogs-menubar.md) — kept below for
reference on those two points; treat everything else here (the rail, the reused control column,
the splitter inspector) as an abandoned proposal, not a plan.

Design stance (settled with the user): **reuse the running game's own HUD furniture — the
right-side control column, the scanner minimap, the room/trap/power sprite-grid panels — in
their existing position and style.** The 1998 editor was the game with an editing shell bolted
on ("the Scanner Map … is exactly the same as the one in Dungeon Keeper"); keep that. Add only
what the game has no panel for.

Constraints:
- **No ImGui docking** — `frontgui_widgets` exposes none; panels are anchored each frame with
  `SetNextWindowPos` / `SetNextWindowSize`.
- **Panels sit over the live 3D view**; the viewport is the uncovered rectangle.
- Built from `frontgui_widgets` + the bronze/parchment/blood-red skin (`frontgui_style.cpp`),
  and — for the reused parts — the **same ImGui components the in-game-GUI migration already
  built** ([`../ingame-gui/05-sidebar-frame-and-minimap.md`](../ingame-gui/05-sidebar-frame-and-minimap.md),
  [`06-tab-content-panels.md`](../ingame-gui/06-tab-content-panels.md)).
- Every tool + panel toggle is a definable key ([`07`](07-investigation-findings.md) D6).

---

## 1. What is reused vs. new

| Element | Reuse? | How |
|---|---|---|
| **Scanner minimap** | **Fully.** Same top-of-right-column position, same render path (`render_minimap()` / `panel_map_draw_slabs`, already ImGui'd by ingame-gui phase 4), same zoom buttons. | Editor adds: click-to-jump, action-point-number + heart markers, a viewport rectangle. |
| **Right-side control column** | **Position + frame + style.** The editor's palette lives exactly where the game's sidebar is (right edge), using the same `status_panel` frame treatment and tab-strip look. | Widened (~260 px vs the game's ~140) to fit the editor's fuller grids. |
| **Room panel** | **The grid itself.** `GMnu_ROOM`'s paginated 3×N sprite-button grid *is* the editor's Rooms palette — every room shown (editor mode makes all available), plus a player selector. | Same for **Traps & Doors** (`GMnu_TRAP`) and **Objects/Powers** (`GMnu_SPELL` → spellbooks + the object catalogue). |
| **Creature panel** | **Style only.** The game's creature tab lists *your* creatures for jump-to; the editor needs a *placement* grid of every model. Same sprite-button look, different content. | |
| **Terrain (raw slabs), Lights, FX, Action Points** | **Style only** — the game has no panel for these. New sprite-button grids in the same frame. | Terrain gets a `Tiles│Rooms` sub-tab. |
| **Player + experience selector** | **Concept.** The game shows the owning-player symbol in the query panel; the editor makes it an always-visible bottom-bar control (the 1998 "coloured icons at the bottom of the Control Panel"). | |
| **Pull-down menus** | **New** (the game has none) but 1:1 with the 1998 editor's `File/Edit/View/Action Points` bar. | |
| **Tool rail** | **New.** The game switches "mode" *via* those room/power buttons; the editor has extra modes (Query, Brush, Fill, Mark, Eraser, Eyedropper) that aren't "pick a thing", so they need a strip of their own. | |

## 2. Main editing screen

```
┌─ MENU BAR ──────────────────────────────────────────────────────────────────────┐
│ File▾  Edit▾  View▾  Map▾  Script▾              Fluffy Dell *        [ ▶ Playtest ]│
├────┬─────────────────────────────────────────────────────────┬──────────────────┤
│ ▣T │                                                         │ ┌──────────────┐ │
│ ▤R │                                                         │ │   SCANNER    │ │ ← reused minimap,
│ ♞C │                                                         │ │   (jump)  + −│ │   game position
│ ◈O │             DUNGEON VIEW — live engine render            │ └──────────────┘ │
│ ⚷D │                                                         │  Terrain  ▸ Tiles│
│ ✦L │           ·   ·   ·   ·   ·   ·   ·   ·                  │  ┌───┬───┬───┐   │
│ ≈F │           ·   ·  ┌───────┐  ·   ·   ·                    │  │Rok│Gld│Ear│   │ ← palette =
│ ⌖A │           ·   ·  │ ghost │  ·   ·   ·                    │  ├───┼───┼───┤   │   game sprite-grid
│ 🔍Q│           ·   ·  └───────┘  ·   ·   ·                    │  │Pth│Lav│Wtr│   │   style, in the
│ ⧉B │           ·   ·   ·   ·   ·   ·   ·   ·                  │  ├───┼───┼───┤   │   game's column
│ ▦M │                                                         │  │Wal│Gem│Clm│…  │
│ ⌫E │                                                         │  └───┴───┴───┘   │
│    │                                                         │  ─── inspector ──│ ← lower half of
│    │                                                         │  Nothing selected│   the same column
│    │                                                         │  (Query fills it)│
├────┴─────────────────────────────────────────────────────────┴──────────────────┤
│ ● Red▾   ★ Lvl 5▾  │  placing: Rock  │  slab 42,17 · sub 3,2  │ Things 128/1024 ✓ │
└─────────────────────────────────────────────────────────────────────────────────┘
```

## 3. Regions

| # | Region | Anchor / size | Notes |
|---|---|---|---|
| 1 | **Menu bar** | top, full width, ~26 px | `File · Edit · View · Map · Script`, map name + dirty `*`, **Playtest**. New, but 1:1 with the 1998 pull-downs. `Esc` opens the same set as a hub. |
| 2 | **Tool rail** | left edge, ~40 px, full height | one `FeIconButton` per tool (Terrain, Rooms, Creatures, Objects, Traps&Doors, Lights, FX, Action points, Query, Brush, Mark, Eraser). Active = bronze fill; hover = name + current key. |
| 3 | **Control column** | **right edge — the game's sidebar position**, ~260 px, full height | top: the **reused scanner minimap** (same render, + jump/markers). Below: the active tool's palette — the game's `GMnu_ROOM`/`GMnu_TRAP`/`GMnu_SPELL` grids verbatim for those tools, same-styled grids for the rest, with the game's next-page pattern. Terrain adds a `Tiles│Rooms` sub-tab. Value tweaks (gold amount, spellbook power) inline. |
| 4 | **Inspector** | lower part of the control column (splitter), or a `Palette│Inspect│Verify` tab within it | Query result / selected light-AP-thing properties (`FeSlider`/`FeCombo`). Empty-state hint. Verification list (§5). |
| 5 | **Status bar** | bottom, full width, ~24 px | **player** (colour + `FeCombo`) and **experience** selectors — the 1998 bottom icons — plus placement value, cursor slab+subtile, thing counter vs format cap, map-valid badge. For Lights/FX/AP tools the player+XP pair is swapped for that tool's **intensity/radius/height** sliders (the 1998 keypad-light workflow, always visible). |
| 6 | **Viewport** | the uncovered rectangle | live render + editor overlays ([`04`](04-views-camera-overlays.md)): slab grid, coord tag, ghost cursor, ownership tint, thing/light/AP markers, mark rectangle, verify flags. |

## 4. Behaviour

- **`Tab` (definable) → clean view:** hides the control column, leaves menu bar + thin rail +
  status bar (the modern form of the 1998 `m` full-map habit).
- **Panels opaque, anchored, not floating.** The control column's internal splitter (minimap /
  palette / inspector) is a new `FeSplitter` wrapper; sizes persist to `settings`.
- **Narrow-window degradation:** below ~1100 px the inspector collapses into a tab of the
  control column; below ~900 px the control column floats semi-transparent over the viewport.
- **`Esc` → Editor menu** (centred `FeBeginModal`, pause-menu styling): Resume · Save (`^S`) ·
  Save As · New map · Open map · Level settings · Playtest (`^P`) · Help/keys · Exit to menu.
- **Playtest:** dims the chrome, `editor_save_map()` to a scratch slot, hands to the running
  game; a top-centre ribbon `◀ Return to editor · ⟳ Restart` stays for the session
  ([`07`](07-investigation-findings.md) F19).

## 5. Dialogs & report

- **New Map** (in the `FeSt_EDITOR` browser, pre-session): name, author, size (any — F12),
  texture set (`texture_pack_desc` + custom, F15), keeper count.
- **Open Map**: `FeBeginListBox`, each row = name + `land_preview_build_minimap` thumbnail +
  size + format badge (`KFX` / `classic`).
- **Save As**: name, destination (Editor Maps bucket default), **format selector**
  `Auto ▸ / Force KeeperFX / Force Classic` ([`07`](07-investigation-findings.md) F20).
- **Verification report**: a docked strip above the status bar (non-blocking — keep editing
  while fixing); each row `→ zoom` fires `PckA_ZoomToPosition` + pulses the slab overlay. Only
  a Save that hits an ERROR makes it modal.

## 6. Widget inventory

Reuse from `frontgui_widgets.h`: `FeBeginPanel` · `FeBeginScrollArea` ·
`FeBeginListBox`/`FeListRow` · `FeButton` · `FeIconButton` · `FeNavButton` · `FeSlider` ·
`FeCheckbox` · `FeCombo` · `FeTextInput` · `FeKeybindRow` · `FeBeginTabBar`/`FeTab` ·
`FeBeginModal`/`FeOpenModal` · `FeHeading`…`FeSeparator` · `FeHelpTooltip`.

Reuse from the in-game-GUI migration: the **sidebar frame + minimap texture** (ingame-gui
phase 4) and the **room/trap/power tab-content grids** (ingame-gui phase 6) — the editor is a
second consumer, ideally sharing the same draw functions with an "all kinds / editor" flag.

New wrappers (small; the in-game GUI project wants them too): `FeMenuBar`/`FeMenu`/`FeMenuItem`
· `FeSplitter` · `FeIconButton` selected-state · optional `FeGridButton`.

## 7. Open choices

- **Inspector: splitter vs tab in the control column.** Proposal: splitter by default (see
  palette *and* selection at once), collapsible; auto-becomes a tab on narrow windows.
- **Does the editor share the game's tab-grid draw code, or fork it?** Proposal: share, gated
  by an "editor / all-available" flag — the grids are identical apart from which kinds show.
  Confirm with whoever owns ingame-gui phase 6.
- **Minimap size in the editor.** The game's is small. Proposal: keep the game size by default,
  with a drag to enlarge it (eats palette height) — some mappers will want a big overview.
