# KeeperFX — Architecture

**Status:** steady-state. The multi-stage refactor described in
[`docs/refactor/`](../refactor/) (stages 0–13) is **complete**; this document
describes the code as it is *now*. The `docs/refactor/` directory is kept as the
historical record of *why* each boundary is where it is — read it for design
rationale, but don't expect it to track the code going forward.

**Scope:** the C/C++ game engine under `src/`. It does not cover `deps/`
(third-party, already separate), `tools/`, or the asset trees (`config/`,
`campgns/`, `levels/`, `lang/`) except where they explain how a library works.

---

## 1. The big picture

KeeperFX is a free reimplementation of Bullfrog's *Dungeon Keeper*. After the
refactor, `src/` is **not** a flat pile of files. It is a set of internal CMake
`OBJECT` libraries with a **strict, one-directional, acyclic dependency graph**,
plus a thin application entry point.

```
src/
├── main.cpp                 ← app entry point (the only free-standing file)
├── ftests/                  ← functional-test scaffolding (exempt tier)
├── kfx_platform/            { include/ , src/ , CMakeLists.txt }
├── kfx_config/              { include/ , src/ , CMakeLists.txt }
├── kfx_sim/                 { include/ , src/ , CMakeLists.txt }
├── kfx_render/              { include/ , src/ , CMakeLists.txt }
├── kfx_net/                 { include/ , src/ , CMakeLists.txt }
├── kfx_game/                { include/ , src/ , CMakeLists.txt }
├── kfx_frontend/            { include/ , src/ , CMakeLists.txt }
├── kfx_script/              { include/ , src/ , CMakeLists.txt }
└── kfx_apploop/             { include/ , src/ , CMakeLists.txt }
```

Every `src/kfx_<name>/` directory is a real CMake `OBJECT` library target whose
source set is exactly the files in that directory (each `CMakeLists.txt` globs
its own `src/*.c|*.cpp`). The physical layout **is** the build target
membership — there is no separate hand-maintained file list to keep in sync.

### The dependency ladder

Arrows point "depends on". A library may only `#include` headers from itself or
a library **below** it. Enforced in CI by
[`scripts/check_layering.py --strict`](#8-enforcement).

```
            ┌────────────────────────────────────────────────────────────┐
            │  app_entry  =  src/main.cpp   (composition root)           │
            └────────────────────────────────────────────────────────────┘
            ┌────────────────────────────────────────────────────────────┐
            │  kfx_apploop   top-level per-frame session loop            │
            └────────────────────────────────────────────────────────────┘
            ┌────────────────────────────────────────────────────────────┐
            │  kfx_script    Lua bindings + HTTP API  (deliberately wide)│
            └────────────────────────────────────────────────────────────┘
            ┌────────────────────────────────────────────────────────────┐
            │  kfx_frontend  UI: menus, in-game panels, input            │
            └────────────────────────────────────────────────────────────┘
            ┌────────────────────────────────────────────────────────────┐
            │  kfx_game      game-loop orchestration, level scripting,   │
            │                save/load                                   │
            └────────────────────────────────────────────────────────────┘
            ┌────────────────────────────────────────────────────────────┐
            │  kfx_net       multiplayer networking, packets             │
            └────────────────────────────────────────────────────────────┘
            ┌────────────────────────────────────────────────────────────┐
            │  kfx_render    3D engine, lighting, textures, video        │
            └────────────────────────────────────────────────────────────┘
            ┌────────────────────────────────────────────────────────────┐
            │  kfx_sim       simulation core: map/thing/creature/room/   │
            │                player/dungeon                               │
            └────────────────────────────────────────────────────────────┘
            ┌────────────────────────────────────────────────────────────┐
            │  kfx_pathfinding  Ariadne routing/pathfinding (see §12.1)   │
            └────────────────────────────────────────────────────────────┘
            ┌────────────────────────────────────────────────────────────┐
            │  kfx_config    config loading + the callback "interface"   │
            │                structs (see §5)                            │
            └────────────────────────────────────────────────────────────┘
            ┌────────────────────────────────────────────────────────────┐
            │  kfx_platform  OS/SDL integration (bflib_*), globals.h,    │
            │                platform/renderer seam, memory/zip/version  │
            └────────────────────────────────────────────────────────────┘
```

The authoritative rank list lives in
[`scripts/check_layering.py`](../../scripts/check_layering.py) as
`LIBRARY_ORDER` (lowest → highest):

```
kfx_platform, kfx_config, kfx_pathfinding, kfx_sim, kfx_render, kfx_net,
kfx_game, kfx_frontend, kfx_script, kfx_apploop, app_entry
```

**Special ranks.** `kfx_script`, `kfx_apploop`, and `app_entry` are allowed to
depend on *anything below them*; nothing may depend on *them*.

- `kfx_script` is wide by design — it is the mod-facing scripting API surface.
- `kfx_apploop` holds the top-level `game_loop()` / `update()` session loop.
  It genuinely ties every other layer together each frame, so forcing it through
  per-call callback injection (the pattern every other "layer X needs layer Y"
  case uses) would mean adding ~40 new callback entries for what is the app's
  main loop, not domain logic. It is therefore its own top-ranked library.
- `app_entry` (`main.cpp`) is the composition root: the only place that knows
  about every layer at once, because that is where the callback tables are
  implemented and wired up (see §5).

### Why this shape

The pre-refactor `src/` was one flat directory of 266 files built by a single
`file(GLOB_RECURSE)` into two executables, with no enforced boundary: any file
could `#include` any other's header, and a ~176-field `struct Game` god-object
let every subsystem reach into every other. That made incremental builds slow,
subsystems untestable in isolation, onboarding hard, and reuse of
self-contained pieces (platform layer, pathfinder) impossible. The refactor's
goal was a small number of internal libraries with a strict acyclic graph,
reached through many small always-buildable stages — never a one-shot rewrite.

---

## 2. Library-by-library

For each library: what it owns, its key headers, and what it is allowed to
depend on. File counts are sources / headers.

### 2.1 `kfx_platform` — the foundation

**Owns:** OS/SDL integration; the shared low-level vocabulary header; memory /
zip / version helpers. **Depends on:** external libs only (SDL3, enet, zlib, …).
**46 sources / 59 headers.**

- `bflib_*` — the legacy Bullfrog engine emulation layer: video
  (`bflib_video`, `bflib_vidraw*`, `bflib_vidsurface`), sprites
  (`bflib_sprite`, `bflib_sprfnt`), text (`bflib_text`), sound
  (`bflib_sound`, `bflib_sndlib`), input (`bflib_inputctrl`, `bflib_keybrd`,
  `bflib_mouse`, `bflib_input_joyst`), file I/O (`bflib_fileio`,
  `bflib_filelst`), math/planar (`bflib_math`, `bflib_planar`), coroutines
  (`bflib_coroutine`), timing (`bflib_datetm`), networking primitives
  (`bflib_enet`, `bflib_netsession`, `bflib_netsp`, `bflib_netconfig`), buttons
  (`bflib_guibtns`), movies (`bflib_fmvids`), CPU/crash (`bflib_cpu`,
  `bflib_crash`), basics (`bflib_basics` — includes the cross-cutting
  `quit_game` / `exit_keeper` / `FatalError` session-exit signals), and the
  engine entry (`bflib_main`).
- `globals.h` — the **shared vocabulary header**: coordinate structs
  (`Coord3d`, `Coord2d`, …) and ~55 domain-ID typedefs (`PlayerNumber`,
  `ThingIndex`, `RoomKind`, …) used identically by every other library. This is
  the legitimate foundation header that stays below all nine libraries.
- The **C++ platform/renderer seam** under `src/kfx_platform/{include,src}/platform/` and
  `src/kfx_platform/{include,src}/renderer/`: `PlatformManager` (C-callable facade delegating to
  `PlatformWindows` / `PlatformLinux` + `WindowSystemSDL`), and `RendererManager`
  (C-callable facade over `IRenderer` / `RendererSoftware`). This is the
  "complete refactor of the platform seam" — the C engine talks to a swappable
  backend through these `extern "C"` entry points.
- Helpers: `kfx_memory`, `custom_zip` (+ `MapZipCallbacks`), `cdrom`,
  `steam_api`, `moonphase`, `sound_manager`, `thread.hpp` / `mutex.hpp`,
  `platform.h`, `compiler_compat.h`, `version.h`, `creature_sounds.h`,
  `mod_config_types.h`.

### 2.2 `kfx_config` — config + the interface layer

**Owns:** config-file loading, **and** the callback-struct declarations that let
lower/adjacent layers reach state or behavior owned above them without a direct
`#include`. **Depends on:** `kfx_platform`. **60 sources / 61 headers.**

- `config_*` loaders: `config.c` (the master), plus `config_creature`,
  `config_crtrmodel`, `config_crtrstates`, `config_cubes`, `config_effects`,
  `config_keeperfx`, `config_lenses`, `config_magic`, `config_mods`,
  `config_objects`, `config_players`, `config_powerhands`, `config_rules`,
  `config_settings`, `config_slabsets`, `config_sounds`, `config_spritecolors`,
  `config_strings`, `config_terrain`, `config_textures`, `config_translation`,
  `config_trapdoor`, `config_campaigns`, `config_compp`.
- **Content layer** (`cfgc_*`; plan:
  `docs/refactor/editor/fx-plans/03-content-editors-foundation.md`, the
  campaign-specific pieces in `08-campaign-editor.md`): a second,
  engine-decoupled read/write layer over the same `.cfg` files, built for the
  in-game content editors (`kfx_editor`'s `content_*`, see §2.9a) — the
  engine's own `config_*` loaders above never call into it, and it never
  calls into them. `cfgc_document` (lossless line-level parse/edit of a
  `.cfg` file), `cfgc_content`/`cfgc_stack` (typed field access, merging the
  base/campaign/level layers), `cfgc_schema`/`cfgc_schema_engine` (+
  `_creature`, `_campaign`, `_shapes`: reflection off the engine's own field
  tables plus curated value shapes) and `cfgc_validate` (per-file
  diagnostics: what the loader would clamp, ignore or skip), `cfgc_writer`/
  `cfgc_writebatch` (patch-in-place writes that leave an untouched file byte
  for byte; staged, atomic multi-file commits), `cfgc_help` (help text mined
  from the base files' own comments) and `cfgc_strings` (the byte-preserving
  language string files). The campaign/mappack family:
  `cfgc_campaign_check` (the validator: level lists, shared folders, land
  view, `[strings]`/`[speech]`), `cfgc_campaign_levels` (the level-list
  model: add/move/remove, number allocation, default entries) and
  `cfgc_campaign_edit` (new-campaign/pack text, giving a campaign its own
  configuration folders, copying/moving a level's files).
- **Callback-struct homes** (see §5): `config.h` (`ConfigReloadCallbacks`),
  `sim_feedback.h`, `game_callbacks.h`, `net_callbacks.h`, `render_overlay.h`,
  `script_hooks.h`, `sprite_lookup.h`, `dungeon_availability.h`,
  `pathfinding_world.h` (`PathfindingWorldCallbacks`, 51 entries — the
  largest, since it covers `kfx_pathfinding`'s entire map/door/creature/
  `struct Thing` query surface, see §12.1). Each has a matching
  `set_*_callbacks()` and a no-op default implementation in its `.c`.
- `kfx_config_state.h/.c` — `struct Configs` (the aggregate of all the
  `*.cfg` sub-structs) plus the config-owned field group migrated out of
  `struct Game`.
- Misc: `highscores`, `value_util`, `instance_info`, `speech_ref`,
  `init_thing`, `dungeon_availability`.

### 2.2a `kfx_pathfinding` — Ariadne routing

**Owns:** creature pathfinding/routing (the "Ariadne" system: triangulated
navigation mesh, wall-hugging collision-avoidance movement). **Depends on:**
`kfx_config`, `kfx_platform`. Extracted out of `kfx_sim` (see §12.1) via a
`PathfindingWorldCallbacks` interface (`kfx_config/include/
pathfinding_world.h`, 51 entries) that lets it query map/door/creature/
`struct Thing` state without an upward `#include`.

- `ariadne`, `ariadne_edge`, `ariadne_findcache`, `ariadne_naviheap`,
  `ariadne_navitree`, `ariadne_points`, `ariadne_regions`, `ariadne_tringls`,
  `ariadne_update`, `ariadne_wallhug`.
- `kfx_pathfinding_state.h/.c` — `struct KfxPathfindingState`: the
  navigation-map cache (`navigation_map`, its size, and its dirty flag).
  Deliberately **not** part of the save-game/network-resync raw-blob
  serialization (see §6.2) — `reinit_level_after_load()` unconditionally
  calls `init_navigation()` on both paths, which fully recomputes this
  cache before anything reads it, so serializing it would be redundant.
  Still `memset` in `clear_complete_game()` to match every sibling state
  struct's clear-on-level-reset invariant.

### 2.3 `kfx_sim` — the simulation core

**Owns:** the deterministic, network-synced game world. **Depends on:**
`kfx_config`, `kfx_platform`. **83 sources / 80 headers** — the largest library.

- **Map/terrain:** `map_data`, `map_blocks`, `map_columns`, `map_ceiling`,
  `map_events`, `map_locations`, `map_utils`, `slab_data`.
- **Things** (everything in the world is a `struct Thing`): `thing_data`,
  `thing_list`, `thing_factory`, `thing_stats`, `thing_creature`,
  `thing_objects`, `thing_shots`, `thing_effects`, `thing_traps`,
  `thing_doors`, `thing_corpses`, `thing_physics`, `thing_navigate`.
- **Creatures** (a thing + `struct CreatureControl`): `creature_control`,
  `creature_instances`, `creature_graphics`, `creature_groups`, `creature_jobs`,
  `creature_senses`, `creature_battle`, and the state machine
  `creature_states*` (`_barck`, `_combt`, `_gardn`, `_guard`, `_hero`, `_lair`,
  `_mood`, `_pray`, `_prisn`, `_rsrch`, `_scavn`, `_spdig`, `_tortr`, `_train`,
  `_tresr`, `_wrshp`).
- **Rooms:** `room_data`, `room_util`, `room_list`, `room_entrance`,
  `room_garden`, `room_graveyard`, `room_jobs`, `room_lair`, `room_library`,
  `room_scavenge`, `room_treasure`, `room_workshop`, and the room-placement
  engine `roomspace*` (`roomspace`, `roomspace_detection`,
  `roomspace_prediction`).
- **Players & computer AI:** `player_data`, `player_instances`, `player_utils`,
  `player_computer*` (`player_computer`, `_comptask`, `_compevents`,
  `_compchecks`, `_compprocs`, `_complookup`, `_computer_data`).
- **Dungeons / powers:** `dungeon_data`, `dungeon_stats`, `power_hand`,
  `power_process`, `power_specials`, `magic_powers`, `actionpt`, `tasks_list`.
- `kfx_sim_state.h/.c` — the biggest state struct: map geometry,
  `columns_data[]`, `map[]`, `slabmap[]`, `things_data[]`, `cctrl_data[]`,
  `rooms[]`, `dungeon[]`, `players[]`, `computer_task[]`, `battles[]`, the
  random seeds, timers, and the GUI message / mode-flag fields that kfx_sim is
  the lowest-ranked consumer of.
- `game_lifecycle`, `lvl_filesdk1` (level-file loading), `sim_scratch`.

### 2.4 `kfx_render` — rendering

**Owns:** the 3D engine, lighting, textures, sprites, video modes. **Depends
on:** `kfx_sim`, `kfx_platform`. **26 sources / 25 headers.**

- Engine: `engine_render` (the big one — bucketed polygon/sprite renderer),
  `engine_arrays`, `engine_camera`, `engine_textures`, `engine_lenses`,
  `engine_redraw`, `engine_render_data`.
- Lighting: `light_data` (owns `extern struct LightsShadows lish;`).
- **Lens effect system (C++ class hierarchy):** `LensManager`, `LensEffect`
  (base) with `MistEffect`, `FlyeyeEffect`, `OverlayEffect`,
  `DisplacementEffect`, `PaletteEffect`, `LuaLensEffect`; plus `lens_api`.
- Video: `vidmode` (+ `_data`), `vidfade`, `scrcapt`, `spritesheet`,
  `custom_sprites`, `cursor_tag`, `local_camera`.
- `landview_image` — decodes a land-view background from a PNG (indexed as
  it is, anything else quantised to 256 colours) or the classic `.raw` +
  `.pal` pair; shared by the game's own land-view loader (`kfx_frontend`)
  and the Campaign Editor's Land view page (`kfx_editor`), so both read the
  same image the same way (docs/refactor/editor/fx-plans/08-campaign-editor.md
  §12.6).
- `kfx_render_state.h/.c` — lens/lighting/palette state migrated out of
  `struct Game`.

### 2.5 `kfx_net` — networking

**Owns:** multiplayer networking and packet handling. **Depends on:**
`kfx_sim`, `kfx_config`, `kfx_platform`. **17 sources / 15 headers.**

- Transport/session: `net_main`, `net_game`, `net_lobby`, `net_lan`,
  `net_holepunch`, `net_portforward` (UPnP/NAT-PMP), `net_matchmaking`,
  `net_input_lag`.
- Exchange: `net_exchange_common`, `net_exchange_gameplay` (turn sync, chat,
  unpause), `net_resync` (raw-blob resync — see §6.2), `net_checksums`
  (host-vs-client desync detection).
- Packets: `packets`, `packets_input`, `packets_cheats`, `packets_misc`.
- `save_catalogue`, `kfx_net_state.h/.c` (packets array, input-lag turn
  count, active-player count, packet save/load state, desync-debug snapshots +
  checksums).

### 2.6 `kfx_game` — orchestration

**Owns:** game-loop orchestration, level scripting data/CRUD, save/load.
**Depends on:** `kfx_sim`, `kfx_render`, `kfx_net`, `kfx_config`.
**15 sources / 15 headers.**

- `game_legacy` — `get_gameturn()` (a top hotspot, fan-in 329) and the
  (now-near-empty) `struct Game` placeholder.
- `game_loop` — dungeon-destruction / level-end logic
  (`process_dungeon_destroy`).
- `main_game` — level startup (`startup_network_game`,
  `faststartup_network_game`, `faststartup_saved_packet_game`), win/lose/resign
  (`winning_player_quitting`, `lose_level`, `resign_level`, `complete_level`),
  `clear_complete_game`, `init_seeds`.
- `game_saves`, `game_merge` (level visibility / next-level), `game_heap`,
  `sounds`, `console_cmd` (debug console — has the one accepted direct call
  into `update()`, see §8).
- Level scripting: `lvl_script`, `lvl_script_commands` (+ `_old`),
  `lvl_script_conditions`, `lvl_script_value`, `lvl_script_lib`.
- `kfx_game_state.h/.c` — level script + timers, campaign name,
  `play_gameturn`, pause/frame-step, music, sound settings.

### 2.7 `kfx_frontend` — the UI

**Owns:** menus, in-game panels, input handling. **Depends on:** `kfx_game`,
`kfx_render`, `kfx_config`, `kfx_platform` (and reads sim state).
**43 sources / 33 headers** — the largest file count.

- Screens: `front_simple` (main menu), `front_network`, `front_landview`
  (+ `_multiplayer`), `front_credits`, `front_easter`, `front_fmvids`,
  `front_highscore`, `front_lvlstats` (+ `_data`), `front_input`,
  `front_torture` (+ `_data`).
- In-game menus: `frontmenu_ingame_evnt` (+ `_data`), `frontmenu_ingame_map`,
  `frontmenu_ingame_opts` (+ `_data`), `frontmenu_ingame_tabs` (+ `_data`),
  `frontmenu_net` (+ `_data`), `frontmenu_options` (+ `_data`),
  `frontmenu_saves` (+ `_data`), `frontmenu_select` (+ `_data`),
  `frontmenu_specials`.
- GUI widgets: `gui_boxmenu`, `gui_draw`, `gui_frontbtns`, `gui_frontmenu`,
  `gui_msgs`, `gui_parchment`, `gui_soundmsgs`, `gui_tooltips`, `gui_topmsg`,
  `gui_vscroll`, `button_snapping`, `kjm_input`, `frontend.cpp`.
- `kfx_frontend_state.h/.c` — GUI cheat boxes, flash-button, east-egg
  counters, `save_game_slot`, `time_delta`, land-map start.

### 2.8 `kfx_script` — Lua + HTTP API

**Owns:** Lua scripting bindings and the external HTTP API. **Deliberately wide
access** — it is the mod-facing API surface. **Depends on:** anything below
(rank 7). **15 sources / 9 headers.**

- Lua core: `lua_base`, `lua_params`, `lua_utils`, `lua_triggers` (event
  dispatch), `lua_cfg_funcs` (Lua-registered function dispatch).
- Lua API modules: `lua_api`, `lua_api_camera`, `lua_api_lens`, `lua_api_map`,
  `lua_api_player`, `lua_api_room`, `lua_api_slabs`, `lua_api_sound`,
  `lua_api_things`.
- `api.c` — a non-blocking TCP/HTTP server
  (`api_init_server` / `api_update_server` / `api_close_server` / `api_event`)
  for external tooling; uses a native Winsock/socket layer (SDL3_net is not
  reliably packaged).
- The Lua-side scripts live in `config/fxdata/lua/` (see §11): `bindings/`,
  `classes/`, `managers/`, `triggers/`, `gamelogic/`, `config-api/`, `core/`,
  `utils/`.

### 2.9 `kfx_apploop` — the session loop

**Owns:** the top-level per-frame session loop. **Depends on:** anything below
(rank 8). **1 source / 1 header** — a single file, `game_session_loop.cpp`,
extracted from `src/main.cpp` in stage 12.5.

- `game_loop()` — the outer `while(!exit_keeper)` loop:
  `wait_at_frontend()` → per-level (heart-zoom setup → `keeper_gameplay_loop()`
  → teardown: stop sounds, free level strings, `delete_all_structures`, reset
  lenses, close packet file).
- `keeper_gameplay_loop()` — drives `gameplay_loop_logic` +
  `gameplay_loop_network`.
- `gameplay_loop_logic()` — delta-time pacing, the functional-test hook,
  `poll_inputs` / `input` / `exchange_packets`, the
  `while(process_turn_time < 1.0) gameplay_loop_draw()` inner loop, then the
  per-turn `update()`.
- `update()` — the **per-turn dispatcher** (see §7).
- Frame pacing: `find_frame_rate`, `keeper_wait_for_next_turn`,
  `keeper_screen_swap`, `display_should_be_updated_this_turn`,
  `update_gameplay_delta_time`.
- `network_yield_*` — called **via** `NetCallbacks` by `kfx_net` while blocked
  on network I/O (kfx_net is lower-ranked than kfx_apploop, so it can't call
  these directly).

### 2.9a `kfx_editor` — the in-game level editor

**Owns:** the level editor, reachable from the main menu (Tools → Editor).
**Depends on:** everything below (rank 9, the highest library; only
`app_entry` ranks above it). Nothing includes it back; the one inbound edge is
`main.cpp`, which calls `editor_frame()` from the ImGui frame callback and
wires the `EditorCallbacks` / `EditorJournalCallbacks` implementations.
Full plan and history: `docs/refactor/editor/` (start with `00-overview.md`;
open items in `fx-plans/00-audit-and-index.md`). User guide: `docs/map_editor.txt`.

- **Session** (`editor_session.cpp`): `editor_open()` / `editor_close()`,
  dirty flag, current level number/folder/name/script text. The simulation is
  frozen with `kfx_sim_state.simulation_suspended` (a neutral flag that stops
  the per-turn update while packets and rendering keep running).
- **Toolbox** (`editor_toolbox.cpp`, `editor_icon_grid.cpp`,
  `editor_palette.cpp`, `editor_thumbs.cpp`): nested tab bars of tools; icon
  palettes built from the live config, so modded slabs, rooms, creatures and
  objects appear automatically. Editing goes through the normal packet stream
  (`PckA_Editor*` verbs beside the cheat verbs) or, for area/stroke tools,
  direct calls into `kfx_sim` (single-player-local).
- **Undo/redo** (`editor_journal.cpp`): placement entries, rect-terrain
  snapshots, stroke-level slab diffs and point (light / action point / effect
  generator) entries.
- **Save/load** (`editor_mapsave.cpp` + `kfx_sim`'s `map_content_*`): a
  `MapContent` snapshot written by `KfxNativeMapContentWriter` or
  `ClassicMapContentWriter` (Auto picks by `map_is_legacy_compatible()`);
  `verify_map_content()` reports problems. Files the editor does not save
  (`.lua`, per-level `*.cfg`, ...) are listed by `editor_sidecars.cpp` so Save
  As can warn and Playtest can carry them.
- **Scripts** (`editor_script*.cpp`): text editor with syntax colouring, the
  managed setup region, availability grid, objective/message helper, command
  browser and a validator that reads the engine's own `command_desc[]`.
- **Playtest**: saves to a scratch level number, launches it as a normal game,
  and returns to the editor when the game ends
  (`editor_playtest_running`, `frontend.cpp`).
- **Content editors** (`content_*.cpp`; plan:
  `docs/refactor/editor/fx-plans/03-content-editors-foundation.md`, the
  Campaign Editor in `08-campaign-editor.md`; user guide:
  `docs/map_editor.txt`'s "Content editors" chapter): a second family of
  tools, reached from the main menu's own Tools list as well as the Map
  Editor's Tools menu, that edit configuration rather than the map — built
  on `kfx_config`'s `cfgc_*` layer (see §2.2), not on the map-editing
  machinery above. `content_tools.cpp` is the host (window management,
  dispatch by `ContentTool`, wired to the frontend through
  `ContentToolsCallbacks`, `content_tools_callbacks.h`, `kfx_config`);
  `content_picker.cpp` (target picker: campaign/pack, level, layer),
  `content_target.cpp` (`ConfigTarget`/`ContentCampaign`, the game's
  campaign lists turned into resolved directories), `content_struct.cpp` /
  `content_form.cpp` / `content_entity.cpp` (a session over a
  `StructuredSession`, form rows and popups, and the shared entity-editor
  window that several editors below configure rather than reimplement) are
  the shared plumbing. Editors: `content_raw`/`content_tools` (raw Config
  Files editor), `content_rules`, `content_trapdoor`, `content_spells`,
  `content_creature`, `content_text` (+ `content_strings`,
  `content_names`), `content_rooms`, and `content_campaign` (+
  `content_campaign_ops` — the campaign/pack file operations: create,
  register a saved level, menu order, copy/move a level — kept apart from
  the drawing code so ftests can drive them directly).

### 2.10 `app_entry` — `src/main.cpp` + `src/native_entry.cpp`

**Owns:** the composition root. Two free-standing files under `src/` (aside
from the exempt `ftests/`): `main.cpp` (**1,821 lines**) and the small
`native_entry.cpp`.

- `kfxmain()` → `LbBullfrogMain()`: log setup → `process_command_line` →
  `LbTimerInit` → `RendererScreenInitialize` → `RendererInit(RENDERER_SOFTWARE)`
  → `setup_game()` → `steam_api_init` → `api_init_server()` → `game_loop()` →
  `reset_game` → renderer shutdown.
- `setup_game()` — loads config, pushes resolved state down into `bflib_*`, and
  wires **all** the callback tables (see §5).
- `init_keeper`, `initial_setup`, and a large set of `static` callback-wrapper
  functions that implement the `*Callbacks` tables (e.g.
  `net_callbacks_*`, `game_callbacks_*`, `config_reload_*`, `sim_feedback_*`).
- `main.cpp` is the one translation unit that `#include`s headers from every
  layer — which is exactly the point: the cross-layer knowledge is isolated in
  a single file instead of being smeared across the codebase via `#include`.
- `native_entry.cpp` is the OS-native process entry point: `main()` on Linux,
  `WinMain()` (plus the vectored-exception-handler crash parachute) on
  Windows, both just handing off to `kfxmain()`. Used to live in
  `kfx_platform` (`PlatformLinux.cpp`/`PlatformWindows.cpp`) — the one
  documented case of a lower-ranked library calling up into `app_entry`; moved
  here since only `app_entry` is allowed to depend on every layer. See
  `docs/refactor/todo/remove-kfxmain-symbol-residual.md`.

---

## 3. How a frame runs (runtime flow)

The call chain from process start to a single game turn:

```
main()  (OS)                                  [app_entry / native_entry.cpp]
└─ kfxmain()                                  [app_entry / main.cpp]
   └─ LbBullfrogMain()                        [app_entry / main.cpp]
      ├─ process_command_line / LbTimerInit / Renderer*Init   [kfx_platform]
      ├─ setup_game()   ← wires every *Callbacks table        [app_entry]
      ├─ api_init_server()                                          [kfx_script]
      └─ game_loop()                                             [kfx_apploop]
         └─ while (!exit_keeper) {
              wait_at_frontend()          ← menu sub-loop         [kfx_apploop]
              └─ keeper_gameplay_loop()
                 ├─ gameplay_loop_logic()
                 │   ├─ poll_inputs() / input()                  [kfx_frontend]
                 │   ├─ exchange_packets()                       [kfx_net]
                 │   ├─ while (process_turn_time < 1.0)
                 │   │    gameplay_loop_draw()                   [kfx_render]
                 │   └─ update()   ← THE per-turn dispatcher     [kfx_apploop]
                 └─ gameplay_loop_network()  (if network active) [kfx_net]
           }
```

`update()` is the heart of the simulation tick. It calls, in order
(`kfx_apploop/src/game_session_loop.cpp`):

```
process_packets()            [kfx_net]
update_local_cameras()       [kfx_render]
api_update_server()          [kfx_script]
   … if not paused …
update_things()              [kfx_sim]
process_rooms()              [kfx_sim]
process_dungeons()           [kfx_sim]
update_research()            [kfx_sim]
update_manufacturing()       [kfx_sim]
event_process_events()       [kfx_sim]
update_all_events()          [kfx_sim]
process_level_script()       [kfx_game]
process_fx_lines()           [kfx_sim]
lua_on_game_tick()           [kfx_script]
process_computer_players2()  [kfx_sim]   (if computer-player processing on)
process_players()            [kfx_sim]
process_action_points()      [kfx_sim]
update_footsteps_nearest_camera() [kfx_sim]
PaletteFadePlayer()          [kfx_render]
process_armageddon()         [kfx_sim]
update_global_lighting()     [kfx_render]
   (kfx_game_state.play_gameturn++)
message_update()             [kfx_frontend]
update_all_players_cameras() [kfx_render]
update_player_sounds()       [kfx_game]
```

Note the shape: `kfx_apploop` orchestrates the tick and calls straight into the
other layers (it's top-ranked, so it's allowed to). Every *other* cross-layer
call in the game goes through a callback struct instead — that asymmetry is
deliberate (see the `kfx_apploop` note in §1).

---

## 4. Core domain model

The world is a hierarchical grid; everything that exists in it is a **thing**.
(Details in [`docs/data_structure.md`](../data_structure.md).)

### 4.1 Map hierarchy

```
Slab  (one of a map; carries room type + ownership; AI pathfinding is slab-based)
 └─ Subtile / STL  (minimal 2D part of a map; vision is STL-based)
     └─ Column  (a stack of cubes for each subtile)
         └─ Cube  (one textured cube; mapped to textures in cubes.cfg)
```

- **Slabs** have a `Slb_ID` used for "altering walls" so slabs with the same ID
  aren't altered (0 = solid environment, 2 = dungeon/claimed, 3 = lava,
  4 = water, 5 = entrance, …). Some slab settings are hardcoded, others live in
  `terrain.cfg`. Doors are implemented as replacing slabs with a delay. Each
  subtile has a mapping of columns/objects to place when it's placed, depending
  on neighboring tiles (torches on walls, chandeliers on treasure rooms, …).
- **Cubes** map to textures in `cubes.cfg`; there are animated textures (magic
  door = temple center) and static ones (simple walls).

### 4.2 Things

Every entity is a `struct Thing` (in `kfx_sim`) with `class_id`, `owner`, and
`model`. Thing classes: Empty, Object, Shot, EffectElem, DeadCreature,
Creature, Effect, EffectGen, Trap, Door, AmbientSnd, CaveIn (plus two unused
slots). **Creatures** carry the additional `struct CreatureControl`
(`cctrl_data[]` in `kfx_sim_state`).

### 4.3 Where state lives

The big world arrays no longer live in a single `struct Game`. They are
distributed into per-library state structs, each a single `extern` global owned
by its library:

| State struct         | Owner          | Key contents                                                                                                                                                                             |
| -------------------- | -------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `kfx_sim_state`      | `kfx_sim`      | `map[]`, `slabmap[]`, `columns_data[]`, `things_data[]`, `cctrl_data[]`, `rooms[]`, `dungeon[]`, `players[]`, `computer_task[]`, `battles[]`, random seeds, timers, mode/operation flags |
| `kfx_net_state`      | `kfx_net`      | `packets[]`, `input_lag_turns`, `active_players_count`, packet save/load state, desync snapshots + checksums                                                                             |
| `kfx_game_state`     | `kfx_game`     | level script + timers, campaign name, `play_gameturn`, pause/frame-step, music, sound settings                                                                                           |
| `kfx_config_state`   | `kfx_config`   | `struct Configs` (all `*.cfg` aggregates), texture-count constants                                                                                                                       |
| `kfx_render_state`   | `kfx_render`   | active/applied lens, mouse-light position, `delta_time`, lighting state                                                                                                                  |
| `kfx_frontend_state` | `kfx_frontend` | GUI cheat boxes, flash-button, east-egg counters, `save_game_slot`, `time_delta`                                                                                                         |

`struct Game` itself (in `kfx_game/include/game_legacy.h`) is now a
**near-empty placeholder** holding only `unsigned char _reserved;`. It is kept
non-empty because the save/resync/reset paths still treat it as one chunk in a
fixed serialization chain (see §6.2).

---

## 5. Cross-layer interfaces (the callback pattern)

This is the load-bearing mechanism of the whole refactor. When a lower-ranked
layer needs something from a higher one (a state read, a UI action, a sound, a
Lua event), the fix is **never** a raw `#include` of the higher header. It is
one of:

1. **A callback struct** — declared in the lower layer (in practice almost
   always in `kfx_config`), implemented by the higher layer, wired up once in
   `src/main.cpp` via a `set_*_callbacks()` call.
2. **A narrow accessor function** the lower layer owns.
3. **Moving the field/function** to whichever library is actually the
   lowest-ranked real consumer.

### 5.1 The callback structs

Game-facing callback structs live in `kfx_config/include/` (the "interface
layer"). Each has a `set_*_callbacks()` and a no-op default in its `.c`:

| Struct                         | Header                   | Lets (lower) call into (higher)                                                                                            |
| ------------------------------ | ------------------------ | -------------------------------------------------------------------------------------------------------------------------- |
| `ConfigReloadCallbacks`        | `config.h`               | `kfx_config` → sim/game (re-derive thing models, map dims, room kinds on config reload)                                    |
| `SimFeedbackCallbacks`         | `sim_feedback.h`         | `kfx_sim` → frontend/game (error stats, on-screen msgs, sound/speech, roomspace key queries, roomspace-prediction queries) |
| `GameCallbacks`                | `game_callbacks.h`       | `kfx_game` → frontend (cheat menus, debug overlays, GUI boxes, high-score, menu visibility, frontend-state save/load)      |
| `NetCallbacks`                 | `net_callbacks.h`        | `kfx_net` → frontend/game/script (menu state, join/session UI, chat, Lua resync payloads)                                  |
| `RenderOverlayCallbacks`       | `render_overlay.h`       | `kfx_render` → frontend/net (draw GUI/debug overlays, parchment, panel sprites)                                            |
| `ScriptHookCallbacks`          | `script_hooks.h`         | `kfx_sim`/`kfx_game` → script/app (Lua `lua_on_*` events, `luafunc_*` dispatch, HTTP API events)                           |
| `SpriteLookupCallbacks`        | `sprite_lookup.h`        | `kfx_config`/`kfx_sim` → render (resolve sprite indices)                                                                   |
| `DungeonAvailabilityCallbacks` | `dungeon_availability.h` | `kfx_config` → sim (creature-availability queries)                                                                         |

Platform-level callback structs (declared in `kfx_platform`, also wired in
`main.cpp`):

| Struct                 | Header               | Purpose                                                                                |
| ---------------------- | -------------------- | -------------------------------------------------------------------------------------- |
| `SoundStateCallbacks`  | `bflib_sndlib.h`     | audio → game (music track, frame skip, random seeds, creature sounds, mod sound lists) |
| `MapZipCallbacks`      | `custom_zip.h`       | zip I/O → game (resolve map-zip paths)                                                 |
| `ReceiveCallbacks`     | `bflib_netsession.h` | enet session → net (receive path)                                                      |
| `InputFocusPredicates` | `bflib_inputctrl.h`  | input → game (focus-loss / pause / possession predicates)                              |
| `RendererImGuiCallbacks` | `renderer/RendererManager.h` | renderer → frontend (ImGui context lifecycle/per-frame submission/input feed; `kfx_frontend`'s `gui/FrontendImGui.cpp` owns the real ImGui context — see [05-imgui-linkage-consolidation.md](../refactor/renderer/05-imgui-linkage-consolidation.md)) |

### 5.2 Wiring (composition root)

`src/main.cpp::setup_game()` assembles every table from static wrapper
functions defined in `main.cpp` and registers them:

```c
set_input_focus_predicates(&input_focus_predicates);
set_renderer_imgui_callbacks(&renderer_imgui_callbacks_impl);
set_sound_state_callbacks(&sound_state_callback_table);
set_map_zip_callbacks(&map_zip_callback_table);
set_power_grant_revoke_callbacks(add_power_to_player, remove_power_from_player);
set_config_reload_callbacks(&config_reload_callbacks_impl);
set_script_hook_callbacks(&script_hooks_impl);
set_sim_feedback_callbacks(&sim_feedback_impl);
set_sprite_lookup_callbacks(&sprite_lookup_impl);
set_render_overlay_callbacks(&render_overlay_impl);
set_dungeon_availability_callbacks(&dungeon_availability_impl);
set_net_callbacks(&net_callbacks_impl);
set_game_callbacks(&game_callbacks_impl);
```

**Design consequence:** a lower layer can be compiled, reasoned about, and
tested without ever seeing a higher layer's types. Callback signatures use only
`globals.h` vocabulary plus opaque forward declarations (`struct Thing;`,
`struct PlayerInfo;`, `struct Camera;`) — never the higher layer's full header.
This is what makes each library independently buildable and testable.

---

## 6. State ownership & serialization invariants

### 6.1 Per-library state structs

See the table in §4.3. Each state struct is a single `extern` global owned by
its library (e.g. `extern struct KfxSimState kfx_sim_state;`). The refactor
migrated `struct Game`'s ~176 fields into these structs **one field-group at a
time**, timed to land alongside each library's physical extraction (stages
6–10), and continued field-by-field through stage 13.

### 6.2 Raw-blob serialization (a deliberate invariant)

The state structs are **raw-serialized wholesale** — `memcpy`'d as opaque
blobs — in three places:

- **Network resync** — `kfx_net/src/net_resync.cpp`
- **Save games** — `kfx_game/src/game_saves.c`
- **Level reset** — `kfx_game/src/main_game.c::clear_complete_game()`

At all three call sites the per-library state structs are synced/saved/reset
*alongside one another* as a single fixed chain. `net_resync.cpp` used to
reach this by `#include`-ing `kfx_frontend_state.h`/`kfx_game_state.h`/
`game_legacy.h` directly (a `kfx_net -> kfx_game`/`kfx_frontend` layering
violation, previously an accepted residual — §8.2) — fixed by exporting/
importing those three structs as opaque blobs through `NetCallbacks`
instead (same pattern the file already used for the Lua resync payload),
so the raw-blob wire format itself is unchanged but `net_resync.cpp` no
longer needs the higher layers' headers to produce it.

Save-game loading tolerates a chunk-size mismatch
(`hdr.len != sizeof(struct Game)` → WARNLOG + skip), so an old save loses only
its (regenerable) lighting/shadow-cache snapshot, not player progress.

**Implication for contributors:** you can move *fields between* the state
structs freely (that's how the refactor proceeded), but you cannot reorder or
change the *on-disk / on-wire layout* of a struct without a save/migration
concern.

---

## 7. Build system

There are **three independent build definitions** — a file move or add must be
reflected in all of them, or a build path silently stops compiling it
(discovered in stage 0; see `docs/refactor/00-overview.md` §8).

| Build file                                    | Target                                                                                              | How it lists sources                                       |
| --------------------------------------------- | --------------------------------------------------------------------------------------------------- | ---------------------------------------------------------- |
| `CMakeLists.txt` + `src/kfx_*/CMakeLists.txt` | Windows (MSVC/clang-cl via vcpkg) **and** native Linux                                              | `file(GLOB ...)` per library — **auto-follows file moves** |
| `Makefile`                                    | Windows, mingw-w64 cross-compile (what CI's `build-prototype.yml` + release workflows actually use) | hand-maintained `OBJS = \` flat list                       |
| `linux.mk`                                    | native Linux, via `scripts/setup-linux-thirdparty.sh` (git-ignored `third_party/` staging, no sudo) | hand-maintained `KFX_SOURCES = \` flat list                |

Only the CMake path auto-follows a `git mv`. **Every physical file add/move
needs a matching edit to `Makefile`'s `OBJS` and `linux.mk`'s `KFX_SOURCES`**,
or those two build paths silently drop the file.

### 7.1 CMake target shape

- Each `src/kfx_*/CMakeLists.txt` globs its own `src/*.c|*.cpp` into an
  `OBJECT` library, and emits **two variants** — a `std` and an `_hvlog` one —
  differing only in the `BFDEBUG_LEVEL` compile definition (0 vs 10), carried
  by the INTERFACE targets `kfx_bfdebug_std` / `kfx_bfdebug_hvlog`.
- The root `CMakeLists.txt` links the matching variant of every OBJECT library
  into **two executables**: `keeperfx` and `keeperfx_hvlog`. `KFX_SOURCES_REMAINING`
  (what's left after the libraries carve themselves out) is exactly
  `main.cpp` + `src/ftests/*`.
- `kfx_common_opts` (INTERFACE) carries the shared include paths for all nine
  library `include/` dirs + `src/` + the repo root, plus SDL3. It is
  deliberately **bidirectional** include-wise: `kfx_platform` still has a
  handful of acknowledged residual upward includes, and sibling OBJECT
  libraries only link together at the final executable.
- Link order matters: the default linker (`ld.bfd`) resolves static-library
  symbols in a single left-to-right pass, so the OBJECT libraries must come
  before the static/shared libs that provide their symbols.
  `kfx_link_dependencies()` (from `build/cmake/modules/Dependencies.cmake`)
  handles the rest.

### 7.2 External dependencies

Resolved in `build/cmake/modules/Dependencies.cmake`. Windows/MinGW uses
prebuilt `kfx-deps` static libs + SDL3 dev tarballs; Linux uses system
pkg-config libs with a FetchContent-from-source fallback.

- **SDL3** (+ SDL3_mixer, SDL3_image) — windowing/audio/graphics (dynamic).
- **enet6** — reliable UDP for multiplayer.
- **zlib / minizip** — compression / zip.
- **spng** — PNG. **astronomy** — moon phase. **centijson / centitoml** —
  JSON/TOML parsing (centitoml is an in-repo OBJECT lib under `deps/centitoml`).
- **ffmpeg** (avcodec/avformat/avutil/swresample) — movie decoding.
- **OpenAL** — audio. **LuaJIT** — scripting VM.
- **miniupnpc / libnatpmp** — NAT traversal. **libcurl** — HTTP.

---

## 8. Enforcement

### 8.1 The layering check

`scripts/check_layering.py` is the merge-blocking gate. It:

- **Classifies** each file by the `src/<name>/` directory it physically lives
  under (that directory *is* the CMake OBJECT library's source set). No
  hand-maintained per-file table. `src/ftests/` is an exempt tier (test code may
  depend on anything); anything else directly under `src/` is `app_entry`.
- **Resolves** each `#include "foo.h"` to the file it maps to via a stem→path
  index built from `git ls-files` (not a filesystem glob, to skip gitignored
  build artifacts like `src/ver_defs.h`), so two libraries with same-stem
  headers don't collide.
- **Flags** an edge `A → B` as a violation when `rank(B) > rank(A)`.
- `--strict` exits 1 on any violation **not** in the `ACCEPTED_VIOLATIONS`
  allowlist — so a genuinely new back-edge fails CI, while the documented
  irreducible residuals don't.

CI runs it as a standalone job in `.github/workflows/build-prototype.yml`:

```yaml
check-layering:
  ...
  - name: Run layering check
    run: python3 scripts/check_layering.py --strict
```

The stage-13 rewrite (from a stem table to physical-directory classification)
surfaced 15 real violations the old table had silently never checked — all
fixed.

### 8.2 Accepted (irreducible) residuals

Tracked in `check_layering.py::ACCEPTED_VIOLATIONS`. Each was investigated and
found to have no viable fix without a deeper redesign out of scope for the
refactor. **Don't let this list grow to paper over new violations** — remove an
entry when a future change actually resolves it.

**Currently empty** — the last two entries (`kfx_net/src/net_resync.cpp`'s
raw-blob resync includes and `kfx_platform/src/bflib_enet.cpp`'s `net_main.h`
include) were fixed once real functional-test coverage of both files existed
to verify the fix against (`docs/refactor/todo/ftest-fake-multiplayer.md`,
`docs/refactor/todo/remove-remaining-layering-violations.md`): `net_resync.cpp`
now exports/imports `game`/`kfx_game_state`/`kfx_frontend_state` as opaque
blobs via `NetCallbacks` instead of `#include`-ing their headers directly
(same pattern already used there for the Lua resync payload), and
`bflib_enet.cpp` now includes a new `kfx_platform/include/bflib_netsp.h`
(holding `struct NetSP` and its supporting types) instead of reaching up into
`kfx_net/include/net_main.h`. A third entry, `kfx_game/src/console_cmd.c` →
`game_session_loop.h`, was found to be dead code and removed earlier
(`docs/refactor/todo/two-remaining-layering-violations.md`).

---

## 9. Testing

- **`src/ftests/`** — a functional-test framework (CUnit-based scaffolding:
  `ftest.c`, `ftest_list.c`, `ftest_util.c`) for replicating bugs / testing new
  behavior. Tests are registered in `ftest_list.c` with a level file, level
  number, and `frame_skip` (default 8 for speed; they skip the trademark /
  cutscene for fast launch). Examples: `bug_imp_tp_attack_door__{claim,
  prisoner,deadbody}`, `bug_imp_goldseam_dig`, `bug_pathing_stair_treasury`,
  `bug_invisible_units_cant_select`, and a long-running `bug_ai_bridge`
  (repeat 100×, seed 1). Enabled via the `FUNCTESTING` define; the
  `gameplay_loop_logic()` hook calls `ftest_update()` each turn. `ftests/` is an
  **exempt tier** in the layering check (test code may depend on anything).
- **`tests/`** — standalone CUnit test programs: `tst_main`, `tst_enet_client`,
  `tst_enet_server`, `001_test`.
- **The `KFX_BUILD_TESTS` unit-test harness** — one Catch2 binary per
  `src/kfx_*/` library (`kfx_sim_utest`, `kfx_config_utest`, …), opt-in,
  native Linux only, with an opt-in `gcov`/`lcov` coverage report layered
  on top. Full description: [`testing-harness.md`](testing-harness.md).

---

## 10. Data / asset layout

Game content is data, not code, and is loaded by `kfx_config` / `kfx_sim`:

- **`config/keeperfx.cfg`** — top-level user settings (install path, language,
  resolutions, display, VSYNC, focus/pause behavior, …).
- **`config/fxdata/`** — the core balance/rules files: `creature.cfg`,
  `crstates.cfg`, `cubes.cfg`, `effects.toml`, `keepcompp.cfg`, `lenses.cfg`,
  `magic.cfg`, `objects.cfg`, `playerstates.toml`, `powerhands.toml`,
  `rules.cfg`, `slabset.toml`, `sounds.cfg`, `spritecolors.toml`,
  `terrain.cfg`, `textureanim.toml`, `translation.toml`, `trapdoor.cfg`,
  `columnset.toml`.
- **`config/fxdata/lua/`** — the Lua scripting layer (see §2.8): `init.lua`,
  `aliases.lua`, and `bindings/`, `classes/`, `managers/`, `triggers/`,
  `gamelogic/`, `config-api/`, `core/`, `utils/`, `examples/`.
- **`config/creatrs/`** — per-creature definition files (one `.cfg` per
  creature model).
- **`config/mods/`** — mod loading (`_load_order.cfg` + per-mod dirs).
- **`campgns/`** — campaigns (each a `.cfg` + a `_crtr/` dir of creature
  overrides): `keeporig`, `ancntkpr`, `dzjr06lv`, `origplus`, `lqizgood`,
  `revlord`, `twinkprs`, `burdnimp`, `jdkmaps8`, `pstunded`, `undedkpr`,
  `postanck`, `ami2019`.
- **`levels/`** — level sets (`classic`, `legacy`, `standard`, `lostlvls`,
  `deepdngn`, `personal`), each with `_cfgs/` and `_crtr/` overrides.
- **`multiplayer/`** — multiplayer rule presets (`classic`, `modern`,
  `original`).
- **`lang/`** — translations (gettext `.po`/`.pot`), organized per campaign and
  per level set, plus global `gtext_*` and `speech_*`.

---

## 11. Graph-analytics snapshot

From the code knowledge graph (43,097 nodes / 134,663 edges). Useful as an
orientation map, not a spec.

### 11.1 Heaviest inter-library call edges (boundaries)

| From → To                       | Calls |
| ------------------------------- | -----:|
| `kfx_sim` → `kfx_config`        | 1296  |
| `kfx_frontend` → `kfx_platform` | 711   |
| `kfx_game` → `kfx_sim`          | 482   |
| `kfx_sim` → `kfx_platform`      | 386   |
| `kfx_frontend` → `kfx_sim`      | 358   |
| `kfx_frontend` → `kfx_config`   | 290   |
| `kfx_game` → `kfx_config`       | 287   |
| `kfx_net` → `kfx_sim`           | 285   |
| `kfx_render` → `kfx_platform`   | 274   |
| `kfx_render` → `kfx_sim`        | 253   |

(`kfx_sim → kfx_config` dominating is expected: sim resolves creature/object/
room/slab stats from config on hot paths.)

### 11.2 Top hotspots by fan-in (most-called functions)

| Function                          | Library      | Fan-in |
| --------------------------------- | ------------ | ------:|
| `creature_control_get_from_thing` | `kfx_sim`    | 707    |
| `thing_is_invalid`                | `kfx_sim`    | 536    |
| `thing_model_name`                | `kfx_sim`    | 368    |
| `get_gameturn`                    | `kfx_game`   | 329    |
| `room_is_invalid`                 | `kfx_sim`    | 226    |
| `thing_exists`                    | `kfx_sim`    | 184    |
| `get_map_block_at`                | `kfx_sim`    | 180    |
| `creature_stats_get_from_thing`   | `kfx_config` | 171    |
| `thing_is_creature`               | `kfx_sim`    | 151    |
| `dungeon_invalid`                 | `kfx_sim`    | 141    |

### 11.3 Layer classification (fan-in / fan-out)

- **core** (high fan-in, ~0 out): `kfx_config` (1873 in), `kfx_platform`
  (1371 in).
- **internal** (both directions): `kfx_sim` (1378 in / 1682 out) — the hub.
- **entry** (only outbound calls): `kfx_frontend`, `kfx_game`, `kfx_net`,
  `kfx_render`.

---

## 12. Known residuals & open items

### 12.1 Ariadne pathfinding — now `kfx_pathfinding`; not yet playtested

Ariadne (`ariadne*`, 9 `.c` + 9 `.h` files, see §2.2a) is a **standalone
`kfx_pathfinding` library**, ranked between `kfx_config` and `kfx_sim`.
Every world-query dependency it has on `kfx_sim` goes through
`PathfindingWorldCallbacks` (`kfx_config/include/pathfinding_world.h`, 51
entries): map/door queries, the `CreatureControl`-embedded
`struct Navigation`/`struct Ariadne` slot, direct `struct Thing` field
access (creature position/move-angle, read and written throughout the
wall-hugging/A* collision code — ~450 call sites, the piece originally
flagged as an order-of-magnitude bigger and much higher-risk decision than
the rest combined), and a further 16 entries the physical library move
itself surfaced (state-reading functions and two shared globals that
`ariadne.c`/`ariadne_update.c` had been reaching only *transitively*
through `kfx_sim_state.h`, invisibly until that transitive path was cut).
Ariadne's own `navigation_map` cache now lives in its own state struct,
`struct KfxPathfindingState` (§2.2a) — deliberately excluded from the
save-game/network-resync raw-blob serialization (§6.2), since
`reinit_level_after_load()`'s unconditional `init_navigation()` call
already rebuilds it on both paths. All of this is build-verified (native
Linux + mingw, both `BFDEBUG_LEVEL` variants, `check_layering.py --strict`
clean, zero new `ACCEPTED_VIOLATIONS`) but **not yet playtested** — no real
game-data install was available to exercise the converted movement code in
motion, only to confirm it compiles and links correctly.
The full breakdown, the as-built interface design, and the recommendation
to playtest before merging are in
[`docs/refactor/stage-06a-ariadne-pathfinding-interface.md`](../refactor/stage-06a-ariadne-pathfinding-interface.md).

### 12.2 `kfx_script` boundary is deliberately wide

The Lua layer reaches into ~23 distinct `kfx_sim` headers and ~13 `kfx_config`
headers. Narrowing that to a stable public binding surface is a large, separate
effort (only a pragmatic pass was done in stage 11). Revisit if the scripting
API needs versioning for mod compatibility.

### 

### 12.3 Two executables differ only in `BFDEBUG_LEVEL`

`keeperfx` (BFDEBUG_LEVEL=0) and `keeperfx_hvlog` (BFDEBUG_LEVEL=10) are the
same code with a different debug verbosity. That's why every library emits a
`std` and an `_hvlog` OBJECT variant. If a library ever reads `BFDEBUG_LEVEL`
in a way that changes behavior (not just log volume), it needs two build
variants — which the current setup already supports.

---

## 13. Conventions for contributors

1. **Never break the layering.** If you find yourself wanting a lower library to
   `#include` a higher one, stop. Add a callback struct (in `kfx_config`), a
   narrow accessor, or move the field/function to the lowest-ranked real
   consumer. `check_layering.py --strict` will reject a raw back-edge.
2. **A new file must be added to all three build definitions.** Drop it in the
   right `src/kfx_<name>/src/` (CMake auto-globs it), then add it to
   `Makefile`'s `OBJS` and `linux.mk`'s `KFX_SOURCES`. Forgetting the latter two
   silently drops the file from the Windows and native-Linux builds.
3. **World state belongs in the owning library's state struct**, not in
   `struct Game` (which is a serialization placeholder). Respect the raw-blob
   serialization invariant (§6.2) — moving fields between structs is fine;
   changing on-disk/on-wire layout is not, without a migration story.
4. **Cross-layer knowledge lives in `main.cpp`.** If a new callback is needed,
   declare the struct in the lower layer (usually `kfx_config`), implement the
   wrapper in `main.cpp`, and register it in `setup_game()`.
5. **`kfx_apploop` and `kfx_script` are top-ranked by design.** Put genuinely
   cross-cutting per-frame orchestration in `kfx_apploop`; put mod-facing API
   surface in `kfx_script`. Don't add new top-ranked libraries casually.
6. **Functional tests go in `src/ftests/`** (exempt tier). Register them in
   `ftest_list.c` with a level file / level / `frame_skip`.

---

## 14. Document map

| Question                                          | Where to look                                                                             |
| ------------------------------------------------- | ----------------------------------------------------------------------------------------- |
| Which library owns what, dependency order         | this doc (§1–2) + [`docs/data_structure.md`](../data_structure.md) "Library architecture" |
| Why a boundary is where it is                     | [`docs/refactor/`](../refactor/) stage docs (historical)                                  |
| How the layering check works / accepted residuals | this doc (§8) + [`scripts/check_layering.py`](../../scripts/check_layering.py)            |
| Map / thing / slab data structures                | [`docs/data_structure.md`](../data_structure.md)                                          |
| How to build (all three build paths)              | [`docs/build_instructions.txt`](../build_instructions.txt) §7                             |
| How to write a functional test                    | [`src/ftests/README.md`](../../src/ftests/README.md)                                      |
| How the `KFX_BUILD_TESTS` unit-test/coverage harness works | [`testing-harness.md`](testing-harness.md)                                       |
| How to merge upstream (dkfans/keeperfx) into this fork | [`upstream-merge-workflow.md`](upstream-merge-workflow.md)                          |
|                                                   |                                                                                           |
