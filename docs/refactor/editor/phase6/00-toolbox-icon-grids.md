# Phase 6, slice 1 — toolbox icon grids and palette tabs

Status: **built, live-test pending** (the UI can only be judged by eye; see "What is and isn't verified").

Goal: make the editor toolbox look and behave like the in-game sidebar — creatures, spells, traps,
doors and rooms as icons in a table, not a scrollable list of code names — and sort the long lists
into tabs.

## What changed

**Icon palette widget** (`editor_icon_grid.h/.cpp`). A scrolling table of tiles laid out in a fixed
240 px column (the toolbox window auto-sizes, so the child needs an explicit width), 5 columns for
icon tiles, 3 for text tiles, with full-width group headings. Each tile is drawn with the *same*
cell primitive as the in-game sidebar grids (`fe_hud_cell`) and the same icon sources, resolved in
the same order: a `GUI_ICON_PACK` PNG override, else the legacy panel sprite, else a text tile.
Hover shows a tooltip; the selected tile gets the gold ring, hover the red one.

**Terrain picker → Terrain / Rooms / Other tabs** (`editor_palette.cpp` decides membership from the
live config, so modded slabs sort by what they are):

- *Rooms*: the floor slab of each room (portal, treasure room, library, …, dungeon heart pedestal,
  guard post, bridge), shown with the room's in-game icon.
- *Other*: doors, walls that belong to a room, and other obstacle slabs.
- *Terrain*: everything else — rock, earth, gold, gems, paths, water, lava, abyss, claimed walls.
  Real config: 18 terrain, 16 rooms (every room with a floor slab), 27 other.

**Creature / Hero / Digger → one Creature tool.** Investigation (`packets_cheats.c`): the Creature
and Hero tools ran identical code apart from which kind they remembered and the random pick; both
create with `chosen_player` as owner, so a hero *is* a creature owned by `PLAYER_GOOD`. The Digger
tool calls `create_owned_special_digger()`, which is `create_creature()` of the owner's digger model
plus list ordering. So there is one grid, grouped Evil / Heroes / Other (spectator) by the model's
own flags, with the creature's in-game hand-symbol icon, and the **bottom bar's owner row gained a
Hero button** (`PLAYER_GOOD`, second row with Neutral). Heroes, diggers and coloured objects/rooms
all follow the owner. The eyedropper's creature sample now sets the kind *and* owner together
(`PckA_EditorEyedropperThing` server handler simplified accordingly; the classic cheat menu still
has its own hero selection).

**Object / Trap / Door → one "Thing" tool with tabs:**

- *Spells*: the 25 spellbook objects, shown with the taught power's icon
  (`object_to_power_artifact` → power `medsym_sprite_idx`, tooltip = the spell's name).
- *Specials*: the 13 special boxes (text tiles — see limits).
- *Traps & Doors*: traps and doors with their workshop icons (the manufacture table the in-game
  workshop grid reads), then the 21 crates (workshop boxes) with the same icons.
- *Decor*: the remaining 125 objects (torches, furniture, gold, food, statues, hero gates, …).

Picking an item switches placement mode itself: objects use the render-phase click handler, traps
and doors their own work states. The trap/door *kind* is written straight into the local user state
(`chosen_trap_kind`/`chosen_door_kind`) instead of via `PckA_CheatSwitchTrap/Door`, because
switching from object mode also has to change the work state and the packet slot holds one action
per turn (the second would overwrite the first). Object position/value editing is unchanged.

**Tool strip:** Terrain tab (Terrain, Fill) · Things tab (Creature, Thing, Points) · Utility ·
History. The separate Creatures tab and the Hero/Digger/Trap/Door buttons are gone.

## What is and isn't verified

- Verified: layering clean; `kfx_editor_utest` 277/56 including a new headless-ImGui smoke test of
  the widget (layout, headings, empty grids, no asserts — the ftest sweep never draws ImGui, so
  nothing else runs this code); production builds; ftest sweep 25/25 including a new
  `editor_palette` ftest that classifies the real config (every room's floor slab lands under Rooms,
  ROCK under Terrain, every spellbook resolves to a power, no group empty).
- **Not verified by any test: how it looks.** Cell sizes (40 px icons, 72×34 text tiles), the
  300 px grid height, tab-label fit, and whether the icons resolve in the editor context (the
  panel sprites are the in-game ones; they should be loaded, but that is exactly what the
  live-test confirms). The picker functions in `editor_toolbox.cpp` themselves (as opposed to the
  widget) run only in the live UI.

## Known limits / follow-ups

- **Terrain, Other, Specials and Decor are text tiles**, not pictures: the game has no icon for a
  bare slab, special box or torch. Real thumbnails are possible but are their own piece of work —
  slab → column → cube → texture for slabs, and the object's world sprite animation frame for
  things, each needing a path to render a non-panel sprite into an ImGui texture (only panel and
  button sprites have one today). Not started; worth doing if the text tiles feel weak.
- The Rooms tab lists room floor slabs only; room *walls* are under Other (the engine builds them
  around rooms).
- Owner choices remain P0–P3, Neutral, Hero (P4–P6 are not offered, as before).
- Custom-creature/room/spell icons follow whatever sprite indices the config gives them; a modded
  entry with no sprite falls back to a text tile.
- The Points tool's own button moved to the Things tab (it was already there).
