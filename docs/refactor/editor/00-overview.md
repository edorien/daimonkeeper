# In-game Level Editor — investigation and roadmap

Status: **built.** The editor exists and is in live use (phases 1-6 done; gaps found afterwards
are tracked in [`fx-plans/00-audit-and-index.md`](fx-plans/00-audit-and-index.md); the user guide is
`docs/level_editor.txt`, the architecture note `docs/Architecture/architecture.md` §2.9a). This
document is the original shared-context +
decisions index, kept as the record of intent; each phase gets its own numbered doc as it is worked. Scope: a level editor
reachable from the frontend main menu (**Tools → Editor**) that runs *inside the engine* —
reusing the live sim, the real renderer, and the existing cheat/debug player-state machinery —
rather than a separate application.

Related existing work this builds on and must stay coherent with:
- [`../ingame-gui/00-overview.md`](../ingame-gui/00-overview.md) — the Dear ImGui in-game GUI
  migration. The editor's panels are **ImGui panels built with the same
  `frontgui_widgets` wrappers**; the editor is effectively another consumer of that project's
  seam. The editor should not add a second GUI toolkit.
- [`../renderer/`](../renderer/) — the renderer migration. The editor renders through whatever
  `RendererManager` provides; it needs no bespoke draw path.
- [`../../Architecture/architecture.md`](../../Architecture/architecture.md) — the layering graph
  and the callback-struct rules. The editor must respect them (§5 below).

---

## 1. What the original *Dungeon Keeper World Editor* did

Source: `Dungeon Keeper Editor Manual.doc` (Bullfrog, 1998) and `DKEditor QuickCard.doc`.
`Editor.exe` was a Windows-only SVGA build of the game engine with an editing shell bolted on.
Feature inventory:

| Area | Original editor capability |
|---|---|
| **File** | New map, Load (from 800 slots), Save (slot or new named slot), Quit-with-save-prompt |
| **Edit** | Flood **Fill** an enclosed area with a tile; **Brush** = grab a rectangular region (right-drag) and stamp it elsewhere; **Clear Map** (everything → earth) |
| **Views** | 1st-person walk-through, **Plan** (top-down functional, extra info, zoom further), **Isometric** (as in-game), **Lights** toggle (dynamic lights on/off), full-screen **Map**, **Wall Height** toggle |
| **Terrain** | Paint floor/wall tiles like a paint program (LMB paint, RMB = earth); per-player Claimed Floor & Reinforced Wall; `r` = reinforce selected player's whole perimeter; texture-set cycle (`t`) |
| **Rooms** | Paint room tiles per owning player; Portal & Dungeon Heart are atomic 3×3, delete-only via RMB; save blocked without a red Dungeon Heart |
| **Creatures** | Place any creature/hero for any player at a chosen experience level (bottom-panel selectors: player 0–3 / hero / neutral, level 1–10); RMB delete; 255-thing cap |
| **Traps & Doors** | Place per-player traps (incl. on unclaimed land); doors only on own claimed floor between two walls; Ctrl+LMB locks a door |
| **Items** | Spellbooks, gold, specials, decor — one per 3×3 sub-square, sub-tile-precise position from cursor |
| **Spells / Secrets** | Place findable spellbooks and dungeon specials |
| **Lights & FX** | Ambient (sourceless) static lights with adjustable intensity/size/height (keypad) before placing; Lava/Drip/Portal-Ice/Dry-Ice effect emitters with an adjustable radius (LMB-drag) |
| **Action Points** | Numbered circular trigger areas (LMB-drag radius); numbered **Hero Doors** (Hero Gates) as party/creature entry points |
| **Script** | Editor did *not* edit the script — you hand-edited `mapNNNNN.txt` (`WIN_GAME`, `IF(...)`, `ADD_CREATURE_TO_LEVEL`, `CREATURE_AVAILABLE`, `SET_CREATURE_*`, parties, `QUICK_OBJECTIVE`, …). §5 of the manual is a scripting reference, not an editor feature. |

The map model it edited is the classic file set:
`.slb` (slabs), `.own` (ownership), `.dat`/`.clm`/`.wib`/`.wlb` (derived subtile/column/wibble
data), `.tng` (things), `.lgt`/`.lif` (lights / level name), `.apt` (action points),
`.inf` (texture set), `.txt` (script).

## 2. Prior reconstruction attempts (reference only — not dependencies)

- **Unearth** (`/home/robin/projects/3rdparty/Keeper/Unearth-main`, Godot 3.5, GDScript).
  Standalone external editor. Re-implements the DK1/KeeperFX map file formats (read **and
  write**) in `Class/` + `gdunzip/`, ships its own slab/room/creature sprite atlases
  (`images_*`), renders a 2D top-down view, has a "Play" button that shells out to the game.
  Value to us: a **known-good, actively-maintained reference implementation of the file-format
  writers** (slab auto-columns, CLM/DAT/WIB regeneration, ownership normalisation, thing
  defaults per room) and a mature UX for slab/thing painting. Licence: check before copying
  code — treat as a spec, re-implement.
- **ADiKtEd** (`/home/robin/projects/3rdparty/Keeper/ADiKtEd-master`, C, `libadikted/`).
  Text-mode editor + a cleanly isolated C library. `libadikted/lev_files.c` `write_slb` /
  `save_mapfile` / `update_slab_owners` / `update_level_stats` and `lev_data.c`
  `level_verify_struct` / `level_verify_logic` are the canonical algorithms for
  **writing the classic binary formats, regenerating derived data from slabs, and verifying a
  map before save** (unclosed IFs, things trapped in solid columns, missing hearts, item
  counts). `obj_slabs.c` / `obj_column*.c` hold the slab→column templates. Again: spec, not a
  link target — `libadikted` predates KeeperFX's own slab/column config system.

Neither runs well on modern systems; neither shares code with the engine, so both perpetually
lag the engine's config (new slab kinds, new creature models, Lua). **Building the editor into
the engine is what removes that lag permanently** — the editor sees exactly the slab kinds,
room kinds, thing models, textures and script surface the running game does.

## 3. What KeeperFX already has (the "70–80%")

The engine's cheat / debug player-state machine already does most of the *editing actions* the
original editor did — it just isn't framed or persisted as an editor.

**Player work-states** (`enum PlayerStates`, driven from `gui_main_cheat_list[]` in
[`gui_boxmenu.c`](../../../src/kfx_frontend/src/gui_boxmenu.c) and dispatched in
[`packets_cheats.c`](../../../src/kfx_net/src/packets_cheats.c)):
- `PSt_PlaceTerrain` — cursor paint of **any slab kind** with a chosen owner
  (`player->cheatselection.chosen_terrain_kind` / `.chosen_player`), room deletion on overwrite,
  RSHIFT detail readout. This is the terrain + room + ownership brush already.
- `PSt_MkDigger`, `PSt_MkBadCreatr`, `PSt_MkGoodCreatr` — place diggers / creatures / heroes
  (owner + experience from `cheatselection`).
- `PSt_MkGoldPot`, `PSt_StealRoom`, `PSt_DestroyRoom`, `PSt_StealSlab`, `PSt_DestroyThing`,
  `PSt_KillCreatr`, `PSt_ConvertCreatr`, `PSt_LevelCreatureUp/Down`, `PSt_MkHappy/Angry`,
  `PSt_KillPlayer`, `PSt_HeartHealth`, `PSt_QueryAll` …

**Packet actions** (`enum PacketAction`, [`packet_data.h`](../../../src/kfx_sim/include/packet_data.h)):
`PckA_CheatPlaceTerrain`, `PckA_CheatMakeCreature`, `PckA_CheatMakeDigger`, `PckA_CheatStealSlab`,
`PckA_CheatStealRoom`, `PckA_CheatSwitchTerrain`, `PckA_CheatSwitchPlayer`,
`PckA_CheatSwitchCreature`, `PckA_CheatSwitchHero`, `PckA_CheatSwitchExperience`,
`PckA_CheatConvertCreature`, `PckA_CheatGiveDoorTrap`, `PckA_CheatApplySpell`,
`PckA_CheatKillCreature`, `PckA_CheatRevealMap`, `PckA_CheatAllRooms/Magic/Doors/Traps/Free` …

**Console commands** ([`console_cmd.c`](../../../src/kfx_game/src/console_cmd.c)):
`place.slab` / `slab.place` (`place_slab_type_on_map`, animated-slab aware,
`do_slab_efficiency_alteration`), `create.creature` / `create.object` / `create.thing`,
`give.trap` / `give.door` / `give.power`, `map.pool`, `gold.create`, `reveal` / `conceal`,
`player.heart.health`, `actionpoint.pos` / `actionpoint.reset` / `zoomto.*`,
`toggle.lights`, `possession.lock`, `magic.instance`, `cheat.menu`, `lua` (arbitrary Lua).

**Roomspace tooling** — `create_box_roomspace()`, drag-paint, whole-room / subtile / highlight
modes, `tag_cursor_blocks_place_terrain()` — the marking-rectangle and drag mechanics already
exist (`PckA_SetRoomspace*`).

**Live column/graphics regeneration** — `place_slab_type_on_map()` rebuilds columns, collision
and top-texture for the touched slab immediately; `init_columns()` /
`create_columns_from_list()` rebuild the whole column set from slabs at level load. The engine
already treats slabs as the source of truth and derives the rest.

**Views** — plan/map (parchment), possession = first-person, `toggle.lights`. Isometric-ish is
the normal view. Camera zoom/rotate exist.

## 4. Gap analysis — what is genuinely missing

1. **Persistence / "Save".** `lvl_filesdk1.c` has `load_*` for every map file and **no writer
   at all**. There is no map writer. It's the largest chunk of new code, but it is
   **mechanical**: every writer is the mirror of an existing `load_*` function that already
   defines the exact byte/TOML layout — reverse the reader, don't reverse-engineer the format
   (phase [`03`](03-map-serialization.md), which also lists the 1998 `file formats.doc` as a
   secondary cross-check). Covers `.slb`/`.own`, the KFX-native TOML `tngfx`/`lgtfx`/`aptfx`
   (loaders use `toml.h`), `.lif`/`.lof`, the script `.txt`. The only genuine unknown is the
   derived `.dat`/`.clm`/`.wib` (the current loader **requires `.dat`/`.clm` present** —
   returns `false` without them); the engine already regenerates columns from slabs at
   runtime, so the plan is to let the loader regenerate rather than serialize them (spike S1).
2. **A "New / blank map" path.** The loader has an "empty map" fallback but it's an error
   branch, texture-set 0, no heart. Needs to be a real, sized, texture-selected blank.
3. **An editor framing of the tools.** Today the cheat modes are a flat radio list with no
   palette, no "what am I placing" preview, no per-category selector UI (creature model,
   experience, trap kind, object kind, slab kind, light params, AP radius). The
   `cheatselection` struct exists but the pickers to drive it are ad-hoc
   (`PckA_CheatSwitch*` cycles, no grid).
4. **Editor session semantics.** Entering "edit this level" must load a map into the sim with
   AI, generation, economy, script and the clock suspended and the player in a god-mode build
   state. *Mostly not a gap:* `GOF_Paused` already freezes the whole per-turn loop while
   packets + rendering keep running (F1) — the editor just forces and locks it. New: the
   `simulation_suspended` wrapper flag + a trimmed startup (F5) + no-op cheat-enable (F10).
5. **Lights & effect emitters as first-class placeable/editable objects** with a properties
   panel (intensity/size/height/radius). Static lights exist in the sim (`lgtfx`), but there's
   no place/select/tweak/delete UI.
6. **Action Point & Hero Gate placement UI** with visible numbering and radius handles
   (query/zoom exists; placement/resize does not).
7. **Brush (copy-region-and-stamp)** and **Fill (flood)** — roomspace drag-paint is close to
   brush-paint but there's no grab-region-then-stamp, and no flood fill.
8. **Map verification** before save (ADiKtEd's `level_verify_*`): no heart for player 0,
   unreachable/!enclosed hearts, things in solid rock, >255 things, unclosed script `IF`s.
9. **In-editor script assistance.** Even a plain text box for `mapNNNNN.txt` + a "reload
   script" button + objective/param helper panels. Full visual scripting is out of scope for
   v1 (see [`05`](05-script-and-level-settings.md)).

**Not gaps (checked):** map resize (`.lof MAPSIZE`, F12), custom creature/object/texture-pack
palettes (config-count driven, F15), wall-height + top-down + first-person view
(`video_cluedo_mode` / parchment `draw_zoom_box` / spectator camera — F9/F21), free-play
discoverability (`.lof` auto-scan, F4), classic-format save (reverse the classic loaders,
F20).

## 5. Architecture approach — a dedicated `src/kfx_editor/` library

**Decision (with the user): the editor is its own internal `OBJECT` library,
`src/kfx_editor/`, not code sprinkled across `kfx_frontend` / `kfx_sim` / `kfx_game`.** The
editor is a large, self-contained, optional feature with its own UI, its own file I/O, and its
own session lifecycle; keeping it in one directory means it can be read, tested, and reasoned
about as a unit, and the rest of the engine carries near-zero editor-specific code.

### 5.1 Where it sits in the ladder

Today's ladder:
`kfx_platform → kfx_config → kfx_sim → kfx_render → kfx_net → kfx_game → kfx_frontend →
kfx_script → kfx_apploop → app_entry`
(`kfx_script` + `kfx_apploop` are special-ranked — may depend on anything below, nothing
depends on them).

`kfx_editor` is inserted **above `kfx_apploop`, immediately below `app_entry`**
([`07`](07-investigation-findings.md) F11):
`… → kfx_frontend → kfx_script → kfx_apploop → **kfx_editor** → app_entry`

- It **depends on everything below** — `kfx_config` (slab/room/creature/object/trapdoor defs),
  `kfx_sim` (world state structs, map arrays, `place_slab_type_on_map` & friends, `PacketAction`
  enum), `kfx_net` (`set_players_packet_action`), `kfx_game` (`startup_*` primitives),
  `kfx_frontend` (`frontgui_widgets` ImGui wrappers, `FrontendMenuStates`), `kfx_script`
  (DK-script / Lua parsing for validation).
- **Nothing below `#include`s it.** The per-frame hook rides the renderer's existing single
  ImGui-frame callback (`RendererSetImGuiFrameCallback`, registered in `main.cpp`, wrapped to
  also call `editor_frame()`). `kfx_apploop` gets **one `case FeSt_START_EDITOR:`** in its
  post-menu `switch` calling `startup_local_game_for_editor()` (a `kfx_game` function) — a
  `case` label, not a header dependency ([`07`](07-investigation-findings.md) F16).
- `scripts/check_layering.py` — insert `"kfx_editor"` into `LIBRARY_ORDER` right before
  `"app_entry"`. `src/kfx_editor/` is then auto-mapped.
- `src/kfx_editor/CMakeLists.txt` — same glob-based `OBJECT` library pattern as the others;
  compiled twice (std / hvlog) like every `kfx_*` lib. New files under `src/kfx_editor/src/`
  are picked up automatically.

### 5.2 What lives in `kfx_editor`

| Component | Notes |
|---|---|
| **Editor session** — `editor_open()`, `editor_close()`, `editor_frame()`, `editor_is_active()`, `editor_notify_playtest_end()`, dirty tracking | The lifecycle. `editor_frame()` runs every frame from `main.cpp`'s ImGui-frame wrapper; on its first tick after `FeSt_START_EDITOR` it does the map-reveal + god-state + script-parse, then drives the toolbox, overlays, `GOF_Paused` re-assert and per-frame logic. |
| **Editor GUI** — toolbox, slab/creature/object palettes, property/inspector panels, the New/Open/Save/SaveAs dialogs, the Editor menu bar, View menu | Built with `kfx_frontend`'s `frontgui_widgets` wrappers (`FeButton`, `FeBeginListBox`, …) + the shared style. No new GUI toolkit. |
| **Map serializer** — `editor_save_map(lvnum, dir, flags)` + one `write_*` per map file + `verify_map()` | Reads `kfx_sim` state structs directly (it's above `kfx_sim`), writes files itself. Mirrors each `kfx_sim` `load_*`. See [`03`](03-map-serialization.md). |
| **Blank-map builder** — `editor_new_map(w,h,texture,name)` | Populates the `kfx_sim` map/slab arrays for a fresh map, then reuses the normal init path. |
| **Command journal** — undo/redo over editor actions | Records forward + inverse `PckA_*` (and buffer snapshots for brush/fill). Lives here, not in the packet layer. |
| **Editor overlays** — grid, coordinates, ownership tint, thing/light/AP markers, verification flags | Submitted through `RenderOverlayCallbacks` (already the seam `kfx_render` exposes) or drawn as ImGui draw-list content in `editor_frame()`. |

### 5.3 The thin edges the rest of the engine keeps

Kept deliberately minimal — these are the *only* editor-aware lines outside `src/kfx_editor/`:

| Edge | Where | What |
|---|---|---|
| **`EditorCallbacks`** struct | declared in `kfx_config/include/editor_callbacks.h`, implemented in `kfx_editor`, wired in `main.cpp::setup_game()` via `set_editor_callbacks()` | Lets `kfx_frontend`'s main-menu button (which is *below* `kfx_editor`) request `editor_open()` without a reverse include. Follows the established callback pattern (architecture.md §5). |
| **`simulation_suspended` flag** | `kfx_sim_state` (`bool`) | A **neutral engine concept**, not "editor mode". It **forces `GOF_Paused`** — which already gates the whole per-turn loop ([`07`](07-investigation-findings.md) F1) — and `editor_frame()` re-asserts it so nothing can un-pause the editor. No new per-subsystem guards. Blob-safe. |
| **`FeSt_EDITOR` + `FeSt_START_EDITOR`** enum values + ImGui **Tools → Editor** button + browser screen | `kfx_frontend` (`frontend.h`, `frontend.cpp`, `frontgui_screens.cpp`) | Mirrors the `FeSt_LEVEL_SELECT`→`FeSt_START_KPRLEVEL` pair. No `GMnu_*`/classic-menu entry. Browser buttons call `editor_callbacks->…`. No editor *logic* in the frontend. |
| **one `case FeSt_START_EDITOR:`** | `kfx_apploop` `game_session_loop.cpp` post-menu `switch` | Calls `startup_local_game_for_editor()` (a `kfx_game` fn) — a `case` label, not a header dependency (F16). |
| **`startup_local_game_for_editor()`** | new `kfx_game` fn (`main_game.c`) — `init_level` + `setup_zombie_players` + optionally-trimmed `post_init_level` + optional `GOF_Paused` | The one new `kfx_game` function; no editor logic. |
| **`editor_frame()` call** | `main.cpp` ImGui-frame wrapper passed to `RendererSetImGuiFrameCallback` | The one inbound *header* edge. |
| **Loader regen path** | `kfx_sim` `lvl_filesdk1.c` — regenerate `.dat`/`.clm`/`.wib` from `.slb` when absent | Not editor-specific — general robustness. Gated by file-absence, not an editor flag. (Spike S1, phase 03.) |
| **New `PckA_Editor*` verbs** | `kfx_sim`/`kfx_net` packet handlers, beside the existing `PckA_Cheat*` | Generic world mutations (place light, place AP, toggle door lock, flood fill, …). Params travel in the packet, not in a shared struct (F17). |

### 5.4 Editing actions still go through the packet stream

`kfx_editor` mutates the world by calling `set_players_packet_action(player, PckA_…, …)` — the
same mechanism the cheat menu uses. New verbs (place light, place AP + radius, place object at
subtile offset, flood fill, stamp brush, toggle door lock, reinforce perimeter) are **new
`PckA_Editor*` entries handled in `kfx_net`/`kfx_sim`** exactly like the existing `PckA_Cheat*`
— generic world mutations, not "editor code". Their params travel **in the packet**
(`actn_par1/2`, `pos_x/y`, `actn_par3` — ~5 scalars), *not* by extending `CheatSelection`
(which is inside the `kfx_sim_state` save/resync blob — [`07`](07-investigation-findings.md)
F17). `kfx_editor` never pokes sim state to *edit* the world (brush/fill are the one possible
exception, D2); it only *reads* sim state to *serialize* it.

**Guiding principle:** the editor is a *thin shell over existing engine capability*, now also
*physically isolated* in one library. New code outside `src/kfx_editor/` is limited to: a
handful of `PckA_Editor*` verbs (in `kfx_sim`/`kfx_net`, alongside the cheats), the
`simulation_suspended` flag (one `bool` + `GOF_Paused`), the loader regen path,
`startup_local_game_for_editor()` (one `kfx_game` fn), and ~30 lines of menu/enum/`case`/
callback wiring (§5.3).

## 6. Phase roadmap

| Phase | Doc | Deliverable |
|---|---|---|
| 0 | *(this file)* | Shared context, gap analysis, decisions |
| 1 | [`01-entry-and-editor-session.md`](01-entry-and-editor-session.md) | Scaffold `src/kfx_editor/` (CMake + layering rank + `EditorCallbacks`); Tools→Editor menu entry, `FeSt_EDITOR`, `simulation_suspended` flag, load-existing + new-blank-map, suspended-sim session, "exit to menu / playtest / back to edit" |
| 2 | [`02-editing-toolbox.md`](02-editing-toolbox.md) | The tool palette: terrain/room/ownership paint, brush, fill, marking; creatures/heroes/diggers; objects/spells/specials/gold; traps/doors; delete/query. Category pickers driving `cheatselection`. |
| 3 | [`03-map-serialization.md`](03-map-serialization.md) | `editor_save_map()` in **both formats** (KFX-native TOML + classic binary, D5/F20), each writer mirroring a `load_*`; `map_is_legacy_compatible()`; derived-data regen; new-map creation; verification; Save/Open/New dialogs |
| 4 | [`04-views-camera-overlays.md`](04-views-camera-overlays.md) | View menu wiring existing engine features (Plan = parchment zoom box, 1st-Person = spectator camera, Low Walls = `PckA_SetCluedo`, Lights, full map) + grid/coordinate/AP/thing/light overlays. No new renderer code (F9/F21). |
| 5 | [`05-script-and-level-settings.md`](05-script-and-level-settings.md) | Level info (name, players, size, texture set), lights & FX emitters with property panels, action points & hero gates, script text editing + reload, objective/availability helper panels |
| 6 | [`06-milestones-risks-testing.md`](06-milestones-risks-testing.md) | MVP cut line, sequencing, risks, ftest/Catch2 coverage, docs & packaging |
| 7 | [`07-investigation-findings.md`](07-investigation-findings.md) | Three investigation passes: F1–F21 (`src/` evidence) resolving §7's questions + assumptions; D1–D6 design decisions resolved with the user |

## 7. Open questions — **now resolved** (see [`07-investigation-findings.md`](07-investigation-findings.md))

Two code/doc investigation passes ([`07`](07-investigation-findings.md)) settled all of these
with `src/` evidence. Summary; the finding IDs (F1–F21) point at the detail + file:line. The
six **design decisions** (D1–D6) are resolved with the user (listed in `07`).

- **O1 — derived files on load.** **Resolved → regenerate.** `place_single_slab_type_on_map()`
  ([`map_blocks.c:1292`](../../../src/kfx_sim/src/map_blocks.c)) already rebuilds a slab's
  columns from the `slabset` config on every runtime dig/build; `initialise_map_wlb_auto()`
  already auto-generates wibble. New code = **one loader branch** ("regenerate when `.dat`/`.clm`
  absent"). Spike S1 downgraded from feasibility to byte-equivalence validation. (F2)
- **O2 — save target / discoverability.** **Resolved.** `editor_save_map` writes a `.lof`
  (`KIND = SINGLE`/`MULTI`) into `FGrp_CmpgLvls`; `find_and_load_lof_files()`
  ([`lvl_filesdk1.c:638`](../../../src/kfx_sim/src/lvl_filesdk1.c)) auto-discovers it at
  campaign load — **no `levels.txt` edit**, the 1998 editor's worst wart gone for free. Ship an
  "Editor Maps" mappack as the default save bucket. (F4)
- **O3 — one binary.** **Resolved → yes.** In-game ImGui is a *runtime* switch, not a define;
  `kfx_editor` links unconditionally like `kfx_script`; editor gated at runtime by
  `editor_is_active()`. No `Editor.exe`, no editor-only build. (F7)
- **O4 — undo/redo.** **Resolved → feasible, ship in phase 2.** `kfx_editor` (above `kfx_sim`)
  reads slab/owner before dispatching → trivial inverse. "Sim state *is* the map" is safe
  because `simulation_suspended` stops all autonomous spawn/death. Terrain + thing
  place/delete undo in phase 2; brush/fill rect-snapshot undo phase 2-or-6. (F6)
- **O5 — multiplayer / co-op editing.** **Out of scope v1** — editor session is single-player
  `local`. Unchanged.
- **O6 — DK1 (original-format) output.** **Reversed → in scope for v1 (D5/F20).** The editor
  must edit *and save* the classic binary format whenever the map uses no KeeperFX-specific
  feature (`map_is_legacy_compatible()`). Save is **Auto** (legacy if compatible, KFX-native
  TOML otherwise) with a Force override. Bounded work: the classic loaders still exist, so the
  writers are their reverse; `.dat`/`.clm`/`.wib` come from the F2 regeneration.
- **O7 — procedural map gen.** **Resolved → closed.** No generator exists, and the user
  decided one is not desirable for now. New Map is a plain blank. (F8)
- **(phase 4) wall-height / Plan view.** **Resolved → both already exist.** "Low Walls" =
  `settings.video_cluedo_mode` (in-game video menu, `PckA_SetCluedo`, renderer-implemented);
  "Plan" / top-down = the parchment map's `draw_zoom_box` magnifier. Editor just adds toggles.
  Phase 4 has **no renderer dependency**. (F9)
- **(implicit) sim suspension.** **Resolved → `GOF_Paused` already does it.** One flag gates the
  entire per-turn loop ([`game_session_loop.cpp:139`](../../../src/kfx_apploop/src/game_session_loop.cpp))
  while `process_packets` + rendering keep running. `simulation_suspended` = force-and-lock
  that flag, re-asserted each `editor_frame()`. R2 substantially de-risked; the phase 1 §3
  table is now a *description of `GOF_Paused`*, not an edit checklist. (F1)
- **(phase 3 S2) TOML schemas.** **Resolved.** All three (`tngfx`/`lgtfx`/`aptfx`) documented
  from their loaders (F3). Hand-emit, no library. **Correction:** hero gates serialize as
  `tngfx` objects (`HerogateNumber`), *not* `aptfx`.
- **(phase 1 §4) startup reuse.** **Resolved.** Reuse `init_level()` + `setup_zombie_players()`
  + a trimmed `post_init_level`; one sanctioned `kfx_game` function
  `startup_local_game_for_editor`. (F5)
- **(phase 1 §3) enable-cheats step.** **Resolved → none needed.** `PckA_CheatEnter` is a
  no-op; no `cheat_mode` flag exists. The editor just sends `PckA_Cheat*`. (F10)
- **(phase 1 §0/§5) library rank + apploop edit.** **Resolved.** `kfx_editor` ranks *above
  `kfx_apploop`*, below `app_entry`; the per-frame hook wraps the existing
  `RendererSetImGuiFrameCallback` in `main.cpp`. `kfx_apploop` gets one `case` label (F16), no `#include`. (F11)
- **(phase 1 §5 / phase 3) map size.** **Resolved → fully resizable.** `.lof` `MAPSIZE x y` →
  `set_map_size`; all loaders/writers are size-agnostic via `map_tiles_x/y`. New Map at any
  size works; resizing a *populated* map stays deferred. (F12)
- **(O2 refinement) `.lof` scan timing.** `find_and_load_lof_files()` runs only at campaign
  load — the editor re-calls it (via `config_reload_callbacks`) after the first Save so the map
  appears immediately. (F13)
- **(phase 3 §6) Open-dialog thumbnails.** `land_preview_build_minimap()` reads `.slb`/`.own`
  without loading the level — purpose-built for this. (F14)
- **(phase 2/5) custom creatures / objects / texture packs.** **Covered.** Every palette is a
  config-count loop, so mods appear automatically. Texture: base `map%05d.inf` (1 byte) +
  per-slab `map%05d.slx` (byte grid — added to the writer list). Per-map creature-`.cfg`
  *editing* deferred to v2; placing custom creatures already works. (F15)
- **(phase 1) game-start plumbing.** `FeSt_EDITOR` browser + `FeSt_START_EDITOR` transient +
  one `case` in `kfx_apploop`'s post-menu `switch` (mirrors `FeSt_START_KPRLEVEL`). ImGui
  callback confirmed to run in-game. (F16)
- **(phase 2) editor tool params.** Do **not** extend `CheatSelection` (blob); new params live
  in `kfx_editor` + travel in packet params. Brush/fill = burst of primitive packets, or a
  single-player direct-call exception (D2). (F17)
- **(phase 3) verify limits.** Engine caps ≫ classic 255; `verify_map()` has a KeeperFX vs
  classic-compatible **target mode**. (F18)
- **(phase 1) playtest.** No in-memory reset path — Playtest = auto-save to a scratch slot +
  normal startup; Return = `editor_open()` on the scratch slot. (F19, D1)
- **(phase 4) 1st-person view.** **Resolved → wiring (F21).** Reuse the spectator ("ghost")
  camera the engine gives a defeated player (`level_lost_go_first_person` / `PckA_GoSpectator`
  / `CMF_IsSpectator`) — spawn+possess a spectator at the cursor. Not new camera work.
- **Design decisions — all resolved with the user (D1–D6, [`07`](07-investigation-findings.md)):**
  D1 auto-save-then-playtest; D2 brush = burst packets; D3 undo = terrain+things phase 2;
  D4 "Editor Maps" mappack (revisit at creature-tool / campaign work); **D5 classic save is
  in-scope** (see O6); D6 keybindings are **definable** (`Gkey_Editor*` in `settings.kbkeys[]`).

## 8. Non-goals for v1

- Visual node-based script editing (text box + helpers only).
- Column / cube editing (custom columns, graffiti). The engine derives columns from slabs;
  bespoke column editing is an ADiKtEd power-user feature we can defer indefinitely.
- Editing campaign structure or land view. (Per-map `.cfg` overrides — e.g.
  `map*.creature.cfg` — are a v2 add, not v1; D4 will be revisited alongside campaign work.)
- Multiplayer collaborative editing.
- A standalone/headless editor build. (`kfx_editor` is a normal internal library in the one
  binary; it is *isolated*, not *detachable*.)

## 9. References

- `Dungeon Keeper Editor Manual.doc` / `DKEditor QuickCard.doc` (Bullfrog, 1998) — the original
  editor's feature set and workflow (§1).
- `file formats.doc` (`…/GOG/Bullfrog/Dungeon Keeper Extras/Documents/`) — 1998 community
  reverse-engineering of the classic `.slb`/`.own`/`.tng`/`.apt`/`.clm`/`.dat`/`.wib`/`.lgt`/
  `.inf` byte layouts + the "Location" / "Subtile Map" primitives + which files are
  *required* vs *generatable* (SLB/OWN/TNG/DAT required; WIB/TXT/APT/CLM generatable). A useful
  **cross-check** for phase 03, but **`kfx_sim`'s own `load_*` functions are the authoritative
  spec** — KeeperFX's on-disk format has diverged (new TOML `tngfx`/`lgtfx`/`aptfx`, extra slab
  kinds, `.lof`).
- `ADiKtEd` `libadikted/` (`lev_files.c` `write_slb`/`save_mapfile`/`update_slab_owners`,
  `lev_data.c` `level_verify_*`, `obj_slabs.c`/`obj_column*.c`) — canonical algorithms for
  writing classic formats, regenerating derived data, and verifying a map. Spec, not a link
  target.
- `Unearth` (`Class/`, `gdunzip/`) — a modern, maintained reference implementation of the DK1 +
  KeeperFX format read/write path and slab/thing editing UX. Spec, not a link target.
- Internal: [`../ingame-gui/`](../ingame-gui/) (GUI toolkit + seam the editor reuses),
  [`../../Architecture/architecture.md`](../../Architecture/architecture.md) §5 (callback
  pattern), `§8.2` (accepted layering exceptions).
