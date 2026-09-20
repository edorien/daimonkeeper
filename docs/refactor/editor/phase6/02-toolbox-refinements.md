# Phase 6, slice 3 — toolbox refinements after the first live look

Status: **built, live-test pending** (tab behaviour in particular is only judged live).

1. **Icon-only tiles.** No text inside a tile; the name is the tooltip (as in the in-game grids).
   Every grid is 5 columns now; a tile falls back to text only when it has neither an icon nor a
   thumbnail.
2. **Terrain > Walls.** Room walls (`TREASURY_WALL`, `LIBRARY_WALL`, …) moved out of Other into a
   new *Walls* tab, each shown with its room's icon (tooltip "Treasure Room wall"). The wall→room
   link is the config's own: a wall's `SlbID` equals its room floor's (`TREASURY_AREA`/`_WALL` are
   both 6). Real config: 18 terrain, 16 rooms, 14 walls, 13 other.
3. **Vertical button lists → tabs, tinted per level.** The toolbox is now nested tab bars —
   Terrain | Things | Utility | History, then that tab's tools (Terrain, Fill / Creature, Thing,
   Points / Eyedropper, Stamp, Paint Tex, Query, Erase), then a tool's own pages (Terrain's Brush /
   Rectangle / Clear Earth / Delete Things / Set Owner modes, then Terrain / Rooms / Walls / Other;
   Thing's Spells / Specials / Traps & Doors / Decor). Each level's tab colours are blended a little
   towards a different hue (theme → warm red → green → blue), mixed into the theme's own tab colours
   so it follows the UI style.
4. **View menu checkboxes.** The `[x]`/`[ ]` label hack is gone; every toggle is a real checkbox.
5. **Eyedropper reflects the sample.** Sampling switches the toolbox to the matching tool and
   pages: a slab → Terrain tool, Brush mode, the tab it lives in; a creature → Creature tool (owner
   incl. Hero); an object → Thing tool on its tab (Spells / Specials / Decor, crates under Traps &
   Doors); a trap/door → Thing on Traps & Doors. Mechanism: the sample now writes the local
   selection (`UserState`) directly instead of the old `PckA_EditorEyedropper*` packets, because
   switching tool is its own packet and two in one click overwrite each other (one slot per turn);
   one-shot "force" requests make the tab bars follow. The tool shortcuts (E/T/Q/…) use the same
   path, so the tabs follow them too.
6. **Closable toolbox.** The title-bar X closes it; *View > Toolbox* reopens it. Closing only hides
   the panel — the active tool stays active.
7. **Tools no longer leak across tabs.** A tool's palette is drawn inside its own tab, so switching
   to another tab removes it (previously the picker was chosen by the active tool, so Terrain's
   icons stayed under Utility). Showing a tool's tab makes it the live tool, so switching top tabs
   also switches tools. While the History tab is showing, no tool is live: clicking the map places
   nothing.

## Known limits / to watch in the live test

- Tab selection forcing (eyedropper, shortcuts) is the intricate part; it can't be exercised
  headlessly (the toolbox needs the game's UI fonts), so if a tab fails to follow a sample, that is
  the place to look (`s_force_*` in `editor_toolbox.cpp`).
- The Terrain tool now has four tab rows stacked (tab / tool / mode / group); if that is too tall,
  the mode row is the one to fold.
- The obsolete `PckA_EditorEyedropper*` packet handlers remain in `packets_cheats.c`, unused by
  the editor.

## Verification

Layering clean; `kfx_editor_utest` 277/56; ftest sweep 26/26 (`editor_palette` now checks the
Walls group); production builds.
