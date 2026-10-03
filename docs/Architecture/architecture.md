# dAImon Keeper — Architecture

dAImon Keeper is derived from KeeperFX (dkfans/keeperfx) and still merges from
it; it plays KeeperFX campaigns and map packs but is a separate product (§10a).
Internal names keep their KeeperFX-era form (`kfx_*` libraries, `keeperfx_*`
symbols, the `keeperfx` CMake target).

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
├── kfx_content/             { include/ , src/ , CMakeLists.txt }
├── kfx_model/               { include/ , CMakeLists.txt }   (header-only)
├── kfx_pathfinding/         { include/ , src/ , CMakeLists.txt }
├── kfx_sim/                 { include/ , src/ , CMakeLists.txt }
├── kfx_ai/                  { include/ , src/ , CMakeLists.txt }
├── kfx_render/              { include/ , src/ , CMakeLists.txt }
├── kfx_net/                 { include/ , src/ , CMakeLists.txt }
├── kfx_game/                { include/ , src/ , CMakeLists.txt }
├── kfx_frontend/            { include/ , src/ , CMakeLists.txt }
├── kfx_script/              { include/ , src/ , CMakeLists.txt }
├── kfx_apploop/             { include/ , src/ , CMakeLists.txt }
└── kfx_editor/              { include/ , src/ , CMakeLists.txt }
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
            │  kfx_ai        computer-player AI (a client of the sim)    │
            └────────────────────────────────────────────────────────────┘
            ┌────────────────────────────────────────────────────────────┐
            │  kfx_sim       simulation core: map/thing/creature/room/   │
            │                player/dungeon                               │
            └────────────────────────────────────────────────────────────┘
            ┌────────────────────────────────────────────────────────────┐
            │  kfx_pathfinding  Ariadne routing/pathfinding (see §12.1)   │
            └────────────────────────────────────────────────────────────┘
            ┌────────────────────────────────────────────────────────────┐
            │  kfx_model     header-only layouts: struct Thing/Map/SlabMap│
            └────────────────────────────────────────────────────────────┘
            ┌────────────────────────────────────────────────────────────┐
            │  kfx_content   the editors' .cfg content layer (cfgc_*)    │
            └────────────────────────────────────────────────────────────┘
            ┌────────────────────────────────────────────────────────────┐
            │  kfx_config    config loading + the ports (the cross-layer │
            │                "interface" layer, see §5)                  │
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
kfx_platform, kfx_config, kfx_content, kfx_model, kfx_pathfinding, kfx_sim, kfx_ai,
kfx_render, kfx_net, kfx_game, kfx_frontend, kfx_script, kfx_apploop, kfx_editor, app_entry
```

**Special ranks.** `kfx_script`, `kfx_apploop`, and `app_entry` are allowed to
depend on *anything below them*; nothing may depend on *them*.

- `kfx_script` is wide by design — it is the mod-facing scripting API surface.
- `kfx_apploop` holds the top-level `game_loop()` / `update()` session loop.
  It genuinely ties every other layer together each frame, so forcing it through
  per-call port entries (the pattern every other "layer X needs layer Y"
  case uses) would mean adding ~40 new entries for what is the app's
  main loop, not domain logic. It is therefore its own top-ranked library.
- `app_entry` (`main.cpp`) is the composition root: the only place that knows
  about every layer at once, because that is where every provider's port
  table is installed (see §5).

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
**55 sources / 75 headers.**

- `bflib_*` — the legacy Bullfrog engine emulation layer: video
  (`bflib_video`, `bflib_vidraw*`, `bflib_vidsurface`), sprites
  (`bflib_sprite`, `bflib_sprfnt`), text (`bflib_text`), sound
  (`bflib_sound`, `bflib_sndlib`), input (`bflib_inputctrl`, `bflib_keybrd`,
  `bflib_mouse`, `bflib_input_joyst`), file I/O (`bflib_fileio`,
  `bflib_filelst`), math/planar (`bflib_math`, `bflib_planar`), coroutines
  (`bflib_coroutine`), timing (`bflib_datetm`), networking primitives
  (`bflib_enet`, `bflib_netsession`, `bflib_netsp`, `bflib_netconfig`), buttons
  (`bflib_guibtns`), movies (`bflib_fmvids`), CPU/crash (`bflib_cpu`,
  `bflib_crash`), and basics (`bflib_basics` — includes the cross-cutting
  `quit_game` / `exit_keeper` / `FatalError` session-exit signals).
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
- Helpers: `kfx_memory`, `custom_zip`, `cdrom`,
  `steam_api`, `moonphase`, `sound_manager`, `thread.hpp` / `mutex.hpp`,
  `platform.h`, `compiler_compat.h`, `version.h`, `creature_sounds.h`,
  `mod_config_types.h`, `port_check.h` (port-table completeness checks
  and the `KFX_UNWIRED` default-stub marker, §5.2).
- **Its own ports** (`include/ports/`, §5): `FilePathPort`, `SoundHostPort`,
  `InputFocusPort`, `DisplayHostPort` — what the platform code needs from the
  game (file paths, music and sound state, focus predicates, the ImGui
  context and UI scale).

### 2.2 `kfx_config` — config + the interface layer

**Owns:** config-file loading, **and** the port declarations
(`include/ports/*.def`, §5) that let lower/adjacent layers reach state or
behavior owned above them without a direct `#include`. **Depends on:** `kfx_platform`. **44 sources / 48 headers.**

- `config_*` loaders: `config.c` (the master), plus `config_creature`,
  `config_crtrmodel`, `config_crtrstates`, `config_cubes`, `config_effects`,
  `config_keeperfx`, `config_lenses`, `config_magic`, `config_mods`,
  `config_objects`, `config_players`, `config_powerhands`, `config_rules`,
  `config_settings`, `config_slabsets`, `config_sounds`, `config_spritecolors`,
  `config_strings`, `config_terrain`, `config_textures`, `config_translation`,
  `config_trapdoor`, `config_campaigns`, `config_compp`.
- **Block parsers are `NamedField` tables** (`config.c`'s `parse_named_field_block()`
  and friends): one row per key, with a parse and an assign function. The
  creature model files, creature.cfg's blocks and magic.cfg's
  `[spellN]` / `[specialN]` joined the other files in refactor pass 3, S04,
  keeping their hand-written rules through lenient row functions
  (`value_atoi` / `assign_cast`, `value_id_positive`, `value_ids_or`, …;
  `NAMFIELD_KEEP` leaves a field as it is), `NAMFIELD_WHOLE_LINE_UNLIMITED`
  rows for list keys, `CnfLd_ListKnownKeys` for the list-only pass, and
  `parse_named_field_block_lines()` for parsers that walk their numbered
  blocks themselves. rules.cfg's `[research]` and `[sacrifices]` stay
  hand-written: they are lists of records, not keys. The golden test
  (`tests/config_creature_golden_test.cpp`) guards the conversion. The
  editor schema (`kfx_content`, §2.2b) is built from these tables too:
  keys, value kinds and bounds come from the rows, and only rows with a
  parse function of their own have a hand-kept shape
  (`cfgc_schema_creature.cpp`); `cfgc_schema_parser_tables_test.cpp`
  keeps the two, and level scripts' creature model key tables, in step.
- The editors' content layer (`cfgc_*`) used to live here too; it is its own
  library, `kfx_content`, since refactor pass 2 (§2.2b).
- **Port homes** (see §5): `include/ports/<stem>.def` + generated
  `<stem>.h` and `src/<stem>.c` (the unwired defaults) for `UiPort`,
  `ScriptPort`, `SimPort`, `PathfindingWorldPort` (what `kfx_pathfinding`
  needs from `kfx_sim`'s storage and rules, see §2.2a and §12.1), `GamePort`,
  `RenderPort`, `AudioFeedbackPort`, `EditorPort`, `AiPort`,
  `SessionLoopPort`, `NetPort`. `editor_types.h` holds the plain types
  EditorPort's callers share with kfx_editor.
- `kfx_config_state.h/.c` — `struct Configs` (the aggregate of all the
  `*.cfg` sub-structs) plus the config-owned field group migrated out of
  `struct Game`.
- Misc: `highscores`, `value_util`, `instance_info`, `speech_ref`,
  `init_thing`.
- Per-player availability (what a player may build, cast or attract) and
  the config functions that took a `struct Thing` moved to `kfx_sim` in
  refactor pass 2's S05 (§2.3); only model-index lookups stay here.

### 2.2a `kfx_pathfinding` — Ariadne routing

**Owns:** creature pathfinding/routing (the "Ariadne" system: triangulated
navigation mesh, wall-hugging collision-avoidance movement). **Depends on:**
`kfx_model`, `kfx_config`, `kfx_platform`. Extracted out of `kfx_sim` (see
§12.1) via `PathfindingWorldPort` (`kfx_config/include/ports/
pathfinding_world_port.def`, 29 entries) that lets it query map/door/creature
state without an upward `#include`. Since refactor pass 2's S08 it reads
`struct Thing`/`struct Map`/`struct SlabMap` fields directly (the layouts
are in `kfx_model`, §2.2c) and keeps its own copy of the map size in
`kfx_pathfinding_state` (`ariadne_set_map_dimensions()`, called by
`set_map_size()` and `reinit_level_after_load()`).

- `ariadne`, `ariadne_edge`, `ariadne_findcache`, `ariadne_naviheap`,
  `ariadne_navitree`, `ariadne_points`, `ariadne_regions`, `ariadne_tringls`,
  `ariadne_update`, `ariadne_wallhug`.
- `kfx_pathfinding_state.h/.c` — `struct KfxPathfindingState`: the
  navigation map (`navigation_map`, its size, and its dirty flag).
- `ariadne_saved_state.h/.c` — the navigation mesh as one saved block:
  `kfx_pathfinding_state` plus the triangles, points, regions and
  point-location cache, each file listing its own part
  (`ariadne_*_visit_saved_state()`). The mesh is updated incrementally as the
  map changes, so it is not a function of the map: a mesh built again from
  the map is a different one, and paths follow it differently. Saves
  (`SGC_AriadneState`) and resyncs carry the block, and
  `reinit_level_after_load()` no longer calls `init_navigation()` (refactor
  pass 4, P4-F7; before, a loaded or resynced game diverged from the
  uninterrupted one within tens of turns). `init_navigation()` builds the
  mesh at level start.

### 2.2c `kfx_model` — header-only type layouts

**Owns:** the layouts of `struct Thing` (`thing_types.h`, with its flag
enums), `struct Map` (`map_types.h`) and `struct SlabMap`
(`slab_types.h`), and pure `static inline` helpers over them
(`small_around[]`, `small_around_index_in_direction`,
`stl_slab_center_subtile`, `cross_x/y_boundary_first`, and the
subtile-number encoding `kfx_subtile_number`/`kfx_stl_num_decode_x/y`
parameterised by map size). **Depends on:** `kfx_platform` (`globals.h`,
`bflib_basics.h`, `bflib_math.h`). A CMake `INTERFACE` target with no
sources, ranked below `kfx_pathfinding` so Ariadne can read these fields
directly (refactor pass 2, S08). `check_layering.py --strict` rejects any
non-inline function prototype or `extern` data in its headers, so it can
never reference a higher library. The storage (`things_data[]`, `map[]`,
`slabmap[]`, `INVALID_THING`, `thing_get()`) stays in `kfx_sim`, whose
`thing_data.h`/`map_data.h`/`slab_data.h` include these headers.

### 2.2b `kfx_content` — the editors' config content layer

**Owns:** the engine-decoupled read/write layer over `.cfg` files that the
content editors use. **Depends on:** `kfx_config`, `kfx_platform` (it only
includes `config_*.h` headers and `bflib_text.h`). **Used by:** `kfx_editor`
and the (exempt) ftests only. Split out of `kfx_config` in refactor pass 2
(`docs/refactor-pass2/stage-04-content-library.md`); its tests are
`kfx_content_utest`. **16 sources / 13 headers.**

The content layer (`cfgc_*`; plan:
`docs/refactor/editor/fx-plans/03-content-editors-foundation.md`, the
campaign-specific pieces in `08-campaign-editor.md`): a second,
engine-decoupled read/write layer over the same `.cfg` files, built for the
in-game content editors (`kfx_editor`'s `content_*`, see §2.9a) — the
engine's own `config_*` loaders never call into it, and it never
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

### 2.3 `kfx_sim` — the simulation core

**Owns:** the deterministic, network-synced game world. **Depends on:**
`kfx_config`, `kfx_platform`. **86 sources / 88 headers** — the largest library.

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
  engine `roomspace*` (`roomspace`, `roomspace_detection`; the local dig
  prediction, `roomspace_prediction`, is in `kfx_render` since refactor pass
  2's S15).
- **Players:** `player_data`, `player_instances`, `player_utils`,
  `player_computer_state` (the computer players' state accessors; the AI
  itself is `kfx_ai` since refactor pass 2's S14, §2.3a), and
  `player_camera`: the synced cameras (`PlayerInfo.cameras[]`: zoom,
  velocity, first-person tracking, the per-turn update and the packet camera
  handlers), moved from `kfx_render`'s `engine_camera.c` and `kfx_net`'s
  `packets.c` in refactor pass 2's S07. It also holds
  `kfx_sim_view_signals`, per-player counters the sim bumps to ask the local
  camera to re-seed, snap or retarget itself, and (since S10) the per-player
  camera shake (`camera_deviate_quake/_jump`, once `struct Dungeon` fields). They sit outside
  `kfx_sim_state`, so they are never saved, resynced or checksummed.
  `any_player_close_enough_to_see()`/`lightning_is_close_to_player()` may gate
  only unsynced effects (the camera they read advances on one machine for a
  spectated seat).
- **Lights:** `light_registry`, moved from `kfx_render`'s `light_data.c` in
  refactor pass 2's S11. The lights live in `kfx_sim_state.light_registry`
  (saved and resynced), with the shadow-cache slot allocation, since a
  dynamic light can't be created when none is free. A change that makes the
  shading stale records a dirty area in `light_shading_signals` (never
  saved) instead of clearing `kfx_render`'s static light map; `kfx_render`
  drains it before it shades. A load or resync calls
  `light_registry_invalidate_shading()` to have everything rebuilt.
- **Dungeons / powers:** `dungeon_data`, `dungeon_stats`, `power_hand`,
  `power_process`, `power_specials`, `magic_powers`, `actionpt`, `tasks_list`,
  and `player_availability` (room/power/trap/door/creature availability per
  player, moved from `kfx_config`'s `config_*.c` in refactor pass 2's S05).
- `kfx_sim_state.h/.c` — the biggest state struct: map geometry,
  `columns_data[]`, `map[]`, `slabmap[]`, `things_data[]`, `cctrl_data[]`,
  `rooms[]`, `dungeon[]`, `players[]`, `computer_task[]`, `battles[]`, the
  random seeds, timers, and the GUI message / mode-flag fields that kfx_sim is
  the lowest-ranked consumer of.
- `game_lifecycle`, `lvl_filesdk1` (level-file loading), `sim_scratch`.
- **List walks:** `list_walk.h/.c` (refactor pass 3, S01). The things on a
  map block, of a class list and of a player's creature list, the rooms of a
  player or kind, and a room's slabs are linked lists; walk them with
  `FOR_EACH_THING` / `FOR_EACH_ROOM` / `FOR_EACH_ROOM_SLAB` and a
  `thing_walk_*()` / `room_walk_*()` / `room_slab_walk*()` starter, not by
  hand. The iterator reads the next index before the body; slab walks step
  after it, except the `room_slab_walk_ahead*()` ones, which read ahead for a
  body that unlinks or relinks the current slab. It stops on an invalid element, and guards against a cycle with the
  loop's own limit (a class list's live count, `THINGS_COUNT`, …); a
  map-block walk repairs the chain on overflow. A few dozen loops stay
  hand-written because they are not plain walks: wrap-around sweeps from a
  random slab, skip-ahead searches, and `break_mapwho_infinite_chain()`
  itself.

### 2.3a `kfx_ai` — the computer players

**Owns:** the computer-player AI: processes, checks, events, tasks, the
gold lookup. **Depends on:** `kfx_sim` and below. Split out of `kfx_sim` in
refactor pass 2's S14 (docs/refactor-pass2/stage-14-ai-library-spike.md).
**7 sources / 2 headers.**

- `player_computer` (setup, per-turn processing, the process tables),
  `player_comptask` (tasks and the game-action executor), `player_compprocs`,
  `player_compchecks`, `player_compevents`, `player_complookup` (gold
  veins), `player_computer_data` (the default process/check/event tables).
- Its state stays in `kfx_sim_state` (`computer[]`, `computer_task[]`,
  `gold_lookup[]`), typed by `kfx_sim`'s `player_computer_types.h`, so saves
  and resyncs are unchanged. `kfx_sim` keeps the few accessors it needs
  (`player_computer_state.c`: `get_computer_player()`,
  `computer_player_invalid()`, `computer_dungeon()`, the trap-location and
  held-thing records) and reaches the AI's behaviour only through
  `kfx_config`'s `AiPort` (5 entries). Everything above `kfx_ai` calls it
  directly.
- Its own test binary, `kfx_ai_utest`.

### 2.4 `kfx_render` — rendering

**Owns:** the 3D engine, lighting, textures, sprites, video modes. **Depends
on:** `kfx_sim`, `kfx_platform`. **28 sources / 27 headers.**

- Engine: `engine_render` (the big one — bucketed polygon/sprite renderer),
  `engine_arrays`, `engine_camera` (the engine window and zoom scaling; the
  synced cameras moved to `kfx_sim`'s `player_camera` in S07),
  `engine_textures`, `engine_lenses`,
  `engine_redraw` (the engine window, the map-fade blend, screen-to-map, the
  mouse light; composing the frame moved to `kfx_frontend` in refactor pass
  2's S13), `engine_render_data`.
- Views drawn over the engine output: `render_power_hand` (the local
  player's hand and the things it holds) and `render_creature_view` (the
  possession view through the eye lens, and the attack swipe), moved out of
  `kfx_sim` in refactor pass 2's S06.
- Lighting: `light_data` shades the map from `kfx_sim`'s light registry and
  owns the result, `extern struct LightsShadows lish;` (lighting tables,
  shadow caches, static light map, subtile lightness). None of it is saved
  or resynced: it is rebuilt from the registry (`light_drain_shading_signals()`,
  run first thing in `update_light_render_area()`).
- **Lens effect system (C++ class hierarchy):** `LensManager`, `LensEffect`
  (base) with `MistEffect`, `FlyeyeEffect`, `OverlayEffect`,
  `DisplacementEffect`, `PaletteEffect`, `LuaLensEffect`; plus `lens_api`.
- Video: `vidmode` (+ `_data`), `vidfade`, `scrcapt`, `spritesheet`,
  `custom_sprites`, `cursor_tag`, `local_camera` (the interpolated local
  camera; it reads `kfx_sim_view_signals` before it reads its own state, and
  owns the local view-type prediction, `local_view_type_settle()`),
  `roomspace_prediction` (the local dig prediction).
- `local_state.h/.c` — `struct LocalState local_state`, the local machine's
  presentation state (predicted view type, palette fades, the map's UI hold,
  minimap, the thing under the hand). Never saved, synced or checksummed.
  Moved out of `kfx_sim` in refactor pass 2's S15; kfx_sim reaches it only
  through `UiPort.local_view_transition` and `RenderPort.local_view_type_settle`.
- `landview_image` — decodes a land-view background from a PNG (indexed as
  it is, anything else quantised to 256 colours) or the classic `.raw` +
  `.pal` pair; shared by the game's own land-view loader (`kfx_frontend`)
  and the Campaign Editor's Land view page (`kfx_editor`), so both read the
  same image the same way (docs/refactor/editor/fx-plans/08-campaign-editor.md
  §12.6).
- `kfx_render_state.h/.c` — lens/lighting/palette state migrated out of
  `struct Game`.

### 2.5 `kfx_net` — networking

**Owns:** multiplayer networking and the packet exchange. **Depends on:**
`kfx_sim`, `kfx_config`, `kfx_platform`. **16 sources / 16 headers.**

- Transport/session: `net_main`, `net_game`, `net_lobby`, `net_lan`,
  `net_holepunch`, `net_portforward` (UPnP/NAT-PMP), `net_matchmaking`,
  `net_input_lag`.
- Exchange: `net_exchange_common`, `net_exchange_gameplay` (turn sync, chat,
  unpause), `net_resync` (raw-blob resync — see §6.2), `net_checksums`
  (host-vs-client desync detection).
- Packets: `packets` (the turn's exchange, pause, resync gating) and
  `packets_misc` (packet storage, the local pause command). Applying the
  packets and the replay file moved to `kfx_game` in refactor pass 2's
  S12.
- `save_catalogue`, `kfx_net_state.h/.c` (packets array, input-lag turn
  count, active-player count, packet save/load state, desync-debug snapshots +
  checksums).

### 2.6 `kfx_game` — orchestration

**Owns:** game-loop orchestration, level scripting data/CRUD, save/load.
**Depends on:** `kfx_sim`, `kfx_render`, `kfx_net`, `kfx_config`.
**21 sources / 19 headers.**

- `game_legacy` — the (now-near-empty) `struct Game` placeholder and the
  game/frontend resync blobs. (`get_gameturn()` used to be provided from
  here; since refactor pass 2's S10 the turn is `kfx_sim_state.play_gameturn`,
  see §5.2.)
- `game_loop` — dungeon-destruction / level-end logic
  (`process_dungeon_destroy`).
- `main_game` — level startup (`startup_network_game`,
  `faststartup_network_game`, `faststartup_saved_packet_game`), win/lose/resign
  (`lose_level`, `resign_level`, `complete_level`),
  `clear_complete_game`, `init_seeds`.
- Packet application (refactor pass 2, S12, from `kfx_net`):
  `game_commands` (`process_packets()` and the per-user global, dungeon,
  creature and map handlers), `game_commands_input` (dungeon-view clicks),
  `game_commands_cheats` (cheat cursor states and the editor's packet
  actions), and `game_replay` (the `-packetsave`/`-packetload` file and
  `compute_replay_integrity()`). They read the player's modifier keys from
  the packet (`PCtr_Mod*`, set by `kfx_frontend`'s `input()`), never from
  the keyboard, and reach the UI through `UiPort`.
- `game_saves`, `game_merge` (level visibility / next-level), `game_heap`,
  `sounds`, `console_cmd` (debug console — has the one accepted direct call
  into `update()`, see §8).
- Level scripting: `lvl_script`, `lvl_script_commands` (+ `_old`),
  `lvl_script_conditions`, `lvl_script_value`, `lvl_script_lib`.
- `kfx_game_state.h/.c` — level script + timers, campaign name,
  pause/frame-step, music, sound settings.

### 2.7 `kfx_frontend` — the UI

**Owns:** menus, in-game panels, input handling. **Depends on:** `kfx_game`,
`kfx_render`, `kfx_config`, `kfx_platform` (and reads sim state).
**72 sources / 66 headers.**

- Screens: `front_simple` (main menu), `front_network`, `front_landview`
  (+ `_multiplayer`), `front_credits`, `front_easter`, `front_fmvids`,
  `front_highscore`, `front_lvlstats` (+ `_data`), `front_input` (+
  `front_input_roomspace`, the roomspace-cursor keys, moved out of `kfx_sim`
  in refactor pass 2's S06),
  `front_torture` (+ `_data`).
- In-game menus: `frontmenu_ingame_evnt` (+ `_data`), `frontmenu_ingame_map`,
  `frontmenu_ingame_opts` (+ `_data`), `frontmenu_ingame_tabs` (+ `_data`),
  `frontmenu_net` (+ `_data`), `frontmenu_options` (+ `_data`),
  `frontmenu_saves` (+ `_data`), `frontmenu_select` (+ `_data`),
  `frontmenu_specials`.
- GUI widgets: `gui_boxmenu`, `gui_draw`, `gui_frontbtns`, `gui_frontmenu`,
  `gui_msgs`, `gui_parchment`, `gui_soundmsgs`, `gui_tooltips`, `gui_topmsg`,
  `gui_vscroll`, `button_snapping`, `kjm_input`, `frontend.cpp`.
- In-game frame: `frame_compose` (`keeper_screen_redraw()`,
  `redraw_display()`: `kfx_render` draws the world view, then this lays the
  status panel, GUI, compass, messages, boxes, tooltips, captions and debug
  overlays over it, and runs the map fade) and `pointer_graphics` (which
  pointer to show), moved from `kfx_render`'s `engine_redraw` in refactor
  pass 2's S13.
- `local_view` — the local player's view transitions (possession, passenger,
  the parchment map and its UI hold, level start): kfx_sim reports each one
  through `UiPort.local_view_transition` and this sets the palette fades,
  menus and `LocalState` fields (refactor pass 2's S15; the blocks used to
  sit in kfx_sim's player instances).
- The port tables it provides (§5): `ui_port_impl.c`, `audio_port_impl.c`,
  `display_host_port_impl.cpp`.
- `kfx_frontend_state.h/.c` — GUI cheat boxes, flash-button, east-egg
  counters, `save_game_slot`, `time_delta`, land-map start.

### 2.8 `kfx_script` — Lua + HTTP API

**Owns:** Lua scripting bindings and the external HTTP API. **Deliberately wide
access** — it is the mod-facing API surface. **Depends on:** anything below
(rank 7). **19 sources / 13 headers.**

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
(rank 8). `game_session_loop.cpp`, extracted from `src/main.cpp` in stage
12.5; `command_line.cpp` (`process_command_line()`,
`set_default_startup_parameters()`, moved out of `main.cpp` in refactor pass
2's S15); and its `SessionLoopPort` table (`session_loop_port_impl.cpp`).

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
- `network_yield_*` — called **via** `SessionLoopPort` by `kfx_net` while blocked
  on network I/O (kfx_net is lower-ranked than kfx_apploop, so it can't call
  these directly).

### 2.9a `kfx_editor` — the in-game level editor

**Owns:** the level editor, reachable from the main menu (Tools → Editor).
**Depends on:** everything below (rank 9, the highest library; only
`app_entry` ranks above it). Nothing includes it back. Lower libraries reach
it through `EditorPort` (tabled in `editor_port_impl.cpp`, §5): kfx_apploop
opens the session, kfx_frontend asks whether it is active, draws the content
tools and calls `editorport_frame()` (→ `editor_frame()`) at the end of each
ImGui frame, and kfx_game records placements in the undo journal.
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
  on `kfx_content`'s `cfgc_*` layer (see §2.2b), not on the map-editing
  machinery above. `content_tools.cpp` is the host (window management,
  dispatch by `ContentTool`, wired to the frontend through `EditorPort`'s
  `content_tools_*` entries; `ContentTool` is in `kfx_config`'s
  `editor_types.h`);
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
from the exempt `ftests/`): `main.cpp` (about 650 lines) and the small
`native_entry.cpp`.

- `kfxmain()` → `LbBullfrogMain()`: log setup → `wire_ports()` →
  `process_command_line` → `LbTimerInit` → `RendererScreenInitialize` →
  `RendererInit(RENDERER_SOFTWARE)` → `setup_game()` → `steam_api_init` →
  `api_init_server()` → `game_loop()` → `reset_game` → renderer shutdown.
- `wire_ports()` — installs every provider's port table (one
  `set_*_port()` line each) and the function-pointer providers and value
  sources, before anything else runs (see §5.2). The tables themselves and
  their small wrappers live in the providers' `*_port_impl.c(pp)` files
  since refactor pass 2's S15.
- `setup_game()` — loads config and pushes resolved state down into `bflib_*`.
- `init_keeper`, `initial_setup`, `reset_game`. Command-line parsing is
  kfx_apploop's `command_line.cpp`.
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
      ├─ wire_ports()   ← installs every port table            [app_entry]
      ├─ process_command_line / LbTimerInit / Renderer*Init   [kfx_platform]
      ├─ setup_game()   ← config load, bflib_* pushes         [app_entry]
      ├─ api_init_server()                                          [kfx_script]
      └─ game_loop()                                             [kfx_apploop]
         └─ while (!exit_keeper) {
              wait_at_frontend()          ← menu sub-loop         [kfx_apploop]
              └─ keeper_gameplay_loop()
                 ├─ gameplay_loop_logic()
                 │   ├─ poll_inputs() / input()                  [kfx_frontend]
                 │   ├─ exchange_packets()                       [kfx_net]
                 │   ├─ while (process_turn_time < 1.0)
                 │   │    gameplay_loop_draw()                   [kfx_apploop]
                 │   │     └─ keeper_screen_redraw()             [kfx_frontend]
                 │   └─ update()   ← THE per-turn dispatcher     [kfx_apploop]
                 └─ gameplay_loop_network()  (if network active) [kfx_net]
           }
```

`update()` is the heart of the simulation tick. It calls, in order
(`kfx_apploop/src/game_session_loop.cpp`):

```
process_packets()            [kfx_game]
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
   (kfx_sim_state.play_gameturn++)
message_update()             [kfx_frontend]
update_all_players_cameras() [kfx_sim]
update_player_sounds()       [kfx_game]
```

Note the shape: `kfx_apploop` orchestrates the tick and calls straight into the
other layers (it's top-ranked, so it's allowed to). Every *other* cross-layer
call in the game goes through a port instead — that asymmetry is
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
| `kfx_sim_state`      | `kfx_sim`      | `map[]`, `slabmap[]`, `columns_data[]`, `things_data[]`, `cctrl_data[]`, `rooms[]`, `dungeon[]`, `players[]`, `computer_task[]`, `battles[]`, random seeds, timers, mode/operation flags; session values: `play_gameturn`, level numbers, `level_human_player`, `human_players_count`                  |
| `kfx_net_state`      | `kfx_net`      | `packets[]`, `input_lag_turns`, `active_players_count`, desync snapshots + checksums                                                                                                      |
| `kfx_game_state`     | `kfx_game`     | level script + timers, campaign name, pause/frame-step, music, sound settings                                                                                                             |
| `kfx_config_state`   | `kfx_config`   | `struct Configs` (all `*.cfg` aggregates), texture-count constants                                                                                                                       |
| `kfx_render_state`   | `kfx_render`   | active/applied lens, mouse-light position, `delta_time`, lighting state                                                                                                                  |
| `kfx_frontend_state` | `kfx_frontend` | GUI cheat boxes, flash-button, east-egg counters, `save_game_slot`, `time_delta`                                                                                                         |

The replay (packet file) state is deliberately in none of them: `struct ReplayState replay`
(`kfx_sim/include/packet_data.h`, upstream #5376) holds recording/playback, the open file and whether replay mode is
on, so neither a resync (a recorded one played back included) nor a loaded save overwrites it.

`struct Game` itself (in `kfx_game/include/game_legacy.h`) is now a
**near-empty placeholder** holding only `unsigned char _reserved;`. It is kept
non-empty because the save/resync/reset paths still treat it as one chunk in a
fixed serialization chain (see §6.2).

---

## 5. Cross-layer interfaces (ports)

This is the load-bearing mechanism of the whole refactor. When a lower-ranked
layer needs something from a higher one (a state read, a UI action, a sound, a
Lua event), the fix is **never** a raw `#include` of the higher header. It is
one of:

1. **A port entry** — a function-pointer slot in a *port* declared low (in
   `kfx_config` or, for the platform code, `kfx_platform`), implemented and
   tabled by the higher *provider* library, installed once by
   `src/main.cpp::wire_ports()`.
2. **A narrow accessor function** the lower layer owns.
3. **Moving the field/function** to whichever library is actually the
   lowest-ranked real consumer.

Until refactor pass 2's S15 the ports were consumer-grouped "callback
structs" (`SimFeedbackCallbacks`, `GameCallbacks`, …) tabled in `main.cpp`;
the move ledger ([`move-ledger.md`](move-ledger.md),
`scripts/move_ledger.py lookup <entry>`) maps each old entry to its port.

### 5.1 The ports

Each port has one provider library, which builds its table. Entry counts are
from `scripts/callback_inventory.py` (2026-09-28: 15
ports, 325 entries).

| Port | Prefix | Provider | Entries | Lets (lower) call into (higher) |
| --- | --- | --- | ---: | --- |
| `UiPort` | `ui_` | kfx_frontend | 113 | sim/game/net/render/config → UI: menus and panels, on-screen and targeted messages, message boxes, tooltips, event buttons, objectives, cheat menus, high score, frontend-state save/load/resync, the network session screens, the local player's view transitions (`local_view_transition`) |
| `ScriptPort` | `script_` | kfx_script | 38 | sim/game/net → Lua: `lua_on_*` events, `luafunc_*` dispatch, HTTP API events, the Lua resync payload |
| `SimPort` | `simport_` | kfx_sim | 30 | config → sim: push a reloaded config value into live state (door/trap/room stats, research, creature health and speed), the slab set storage, level strings |
| `PathfindingWorldPort` | `world_` | kfx_sim | 29 | pathfinding → sim: map/door/creature storage and rules for routing (§2.2a) |
| `GamePort` | `game_` | kfx_game | 19 | config/sim/net/render → game: level end and bonus levels, campaign progress, game-state resync |
| `RenderPort` | `render_` | kfx_render | 18 | config/sim/ai → render: cursor tagging, engine view and window, palettes, sprite lookups, the local view-type prediction |
| `AudioFeedbackPort` | `audio_` | kfx_frontend | 12 | sim/game → sound messages, speech, thing samples |
| `EditorPort` | `editorport_` | kfx_editor | 10 | apploop/frontend/game → the level editor: open it, is it active, its ImGui frame, the content tools, the undo journal |
| `AiPort` | `ai_` | kfx_ai | 5 | sim → ai: make a player a computer keeper and back, drop a held thing, restart a build process |
| `SessionLoopPort` | `loop_` | kfx_apploop | 5 | net/render → the session loop: network-yield hooks, host-packet timestamp, interpolation time |
| `NetPort` | `netport_` | kfx_net | 4 | config/render → net: packet history (local lag compensation), matchmaking settings |

Platform ports (declared in `kfx_platform/include/ports/`):

| Port | Prefix | Provider | Entries | Purpose |
| --- | --- | --- | ---: | --- |
| `SoundHostPort` | `soundhost_` | kfx_game | 19 | audio → game: music track, frame skip, random seeds, creature sounds, mod sound lists, audio init and mute |
| `DisplayHostPort` | `display_` | kfx_frontend | 11 | renderer/input → frontend: the ImGui context `kfx_frontend` owns (`imgui_*`, see [05-imgui-linkage-consolidation.md](../refactor/renderer/05-imgui-linkage-consolidation.md)), UI scale values, the slab-background fallback draw |
| `InputFocusPort` | `focus_` | kfx_sim | 8 | input → focus-loss, pause and possession predicates |
| `FilePathPort` | `filepath_` | kfx_config | 4 | sound/input/zip → install, mod and level file paths |

### 5.2 Anatomy and wiring

A port is listed once, in `<lib>/include/ports/<stem>.def`, one X-macro line
per entry:

```c
KFX_PORT_VOID(turn_off_menu, (MenuID mnu_idx), (mnu_idx))
KFX_PORT_RET(TbBool, is_active, (void), (), false)          /* unwired: false */
KFX_PORT_RETX(const char *, lua_resync_export, (size_t *len), (len), "", if (len) *len = 0;)
KFX_PORT_RETS(char *, get_music_track, (void), (), static char track = -1; return &track;)
```

`..VOIDX`/`..RETX` add a statement the unwired default runs first;
`KFX_PORT_RETS` is for a default that returns something non-constant. The
`.def` drives:

- `ports/<stem>.h` — `struct <Port>` (+ `KFX_ASSERT_PORT_TABLE`), the
  installed pointer (never NULL), `<stem>_defaults`, `set_<stem>()`, and one
  `static inline` wrapper per entry, `PREFIX##name`, which callers use:
  `ui_turn_off_menu(GMnu_MAIN)`.
- `src/<stem>.c` — the unwired defaults, each starting with
  `KFX_UNWIRED("<Port>")`.
- `tests/ports_defaults_test.cpp` (kfx_config's and kfx_platform's) — one
  `TEST_CASE` per port calling every default and checking its return.

The provider defines the table in `src/<provider>/src/<stem>_impl.c(pp)`
(with any small wrappers it needs), and `main.cpp` installs it.
`wire_ports()` also installs the function-pointer providers
(`set_emulate_integer_overflow_provider`, …) and the read-only value
sources: `set_gameturn_source(&kfx_sim_state.play_gameturn)` (kfx_platform's
`get_gameturn()`, a `static inline` load used by every log macro) and
`set_config_level_sources()` (the level numbers kfx_config's per-level config
loading needs). A value source is a `const` pointer to a field owned higher
up: reading it is one load, and it can't go stale after a save load or
resync replaces the state wholesale (refactor pass 2, S10):

```c
static TbBool wire_ports(void)
{
    set_file_path_port(&kfx_config_file_path_port);
    set_sound_host_port(&kfx_game_sound_host_port);
    ...
    set_editor_port(&kfx_editor_port);
    set_emulate_integer_overflow_provider(&emulate_integer_overflow);
    set_gameturn_source(&kfx_sim_state.play_gameturn);
    set_config_level_sources(&kfx_sim_state.selected_level_number, &kfx_sim_state.loaded_level_number);
    ...
    return ports_verify_wired();
}
```

`LbBullfrogMain()` calls it right after opening the log file, before
`process_command_line()`, so nothing that runs at startup can reach an
unwired default (wiring late used to drop keeperfx.cfg's INGAME_RES and
MATCHMAKING_SERVER silently). Value pushes that need loaded config
(`bf_sprfnt_set_language_lwrstr()`, …) stay in `setup_game()`.

Safety nets (`kfx_platform/include/port_check.h`):

- Provider tables are **designated initializers** (`.entry = &fn`), so a slot
  can't be silently paired with the wrong member.
- `ports_verify_wired()` checks that no slot of an installed table is NULL
  (`KFX_TABLE_COMPLETE`) and logs each one; a FUNCTESTING build fails its
  `-ftests` run on an incomplete table. Catch2 checks the defaults the same
  way (`kfx_config/tests/port_tables_test.cpp`,
  `kfx_platform/tests/port_check_test.cpp`).
- At the Debug log level a call that reaches an unwired default is logged
  once per entry.
- Tests override entries with `ScopedPortOverride<T>`
  (`kfx_config/tests/scoped_port_override.h`), which restores the previous
  table on scope exit.

There is no event dispatcher. Where several things react to one change, the
change is one named entry and its provider does all of it (for example
`UiPort.local_view_transition`, whose kfx_frontend implementation also sets
kfx_render's palette fades).

**Design consequence:** a lower layer can be compiled, reasoned about, and
tested without ever seeing a higher layer's types. Port signatures use only
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
blobs — in two places:

- **Network resync** — `kfx_net/src/net_resync.cpp`
- **Save games** (and continue-replays) — `kfx_game/src/game_saves.c`

At both the per-library state structs, Ariadne's navigation mesh
(`ariadne_saved_state.h`, §2), and the saved part of `kfx_config_state`
(`KFX_CONFIG_STATE_SAVED_*`: from `texture_animation` to `pay_day_progress`,
what upstream's `struct Game` held -- the level's configuration as its script
changed it, the payday progress, the slabs' texture packs; the
daimonkeeper.cfg settings after it stay out; refactor pass 4, P4-F16) are
synced/saved *alongside one another* as a single fixed chain. A load reads the
configuration files again (the info chunk) and then the saved part over them. `main_game.c::clear_complete_game()` zeroes them all once,
at start-up; between levels `clear_game()` clears chosen parts, so a field it
doesn't clear carries into the next level (refactor pass 4, S07: its
`sim_state_continuity_restart` ftest plays a level twice in one process to
catch that). State the simulation keeps anywhere else — a global, a file or
function `static` — is carried by none of these; pass 4's S07 classified every
such variable in kfx_sim, kfx_pathfinding and kfx_ai
(docs/refactor-pass4/stage-07-sim-state-audit.md), and its
`sim_state_continuity` ftest checks that a game carries on the same after a
save and load and after a resync. `net_resync.cpp` used to
reach this by `#include`-ing `kfx_frontend_state.h`/`kfx_game_state.h`/
`game_legacy.h` directly (a `kfx_net -> kfx_game`/`kfx_frontend` layering
violation, previously an accepted residual — §8.2) — fixed by exporting/
importing those three structs as opaque blobs through ports (`GamePort`,
`UiPort`; `NetCallbacks` until refactor pass 2's S15) instead (same pattern
the file already used for the Lua resync payload), so the raw-blob wire
format itself is unchanged but `net_resync.cpp` no longer needs the higher
layers' headers to produce it.

**Layout versions (refactor pass 2, S09).** Every raw-serialized struct —
`struct Game`, `kfx_sim_state`, `kfx_net_state`, `kfx_game_state`,
`kfx_frontend_state`, `intralvl`, the Ariadne block and `kfx_config_state`'s
saved part — has a version and an expected size in
`kfx_config/include/state_versions.h`. The state headers check their struct's
size in every file that includes them, not only in the struct's own `.c`: a
`#pragma pack` leaking into a header's includes once gave `kfx_config_state`
another layout in 45 files (refactor pass 4, P4-F17), so keep `#include`s
outside a header's `pack(1)` region.
- Saves stamp the version into each chunk header. Loading first walks every
  chunk (`validate_save_chunks()`), checking version, size and that the chunk
  is complete. Only if all pass does it apply anything, so a save of another
  layout is refused with the game untouched: the in-game load shows an error
  box, and the save lists show such saves greyed out as "(other version)"
  (`CEF_OtherVersion`).
- Continue-replays go through the same check.
- **Product chunk.** Every save and replay also carries a `PROD` chunk
  (`struct ProductChunk`: `PRODUCT_MAGIC` 'DMKR' + slug), right after `INFO` --
  `INFO` stays first so the save list still reads names from KeeperFX saves.
  `validate_save_chunks()` refuses a file without it (KeeperFX's, or from
  before 1.0.0) or with another product's magic, by name, before any layout
  check (§10a).
- Network resync sends the versions in a header and rejects a mismatch before
  parsing anything else.
- There are no migrations: saves from before a version bump can't be loaded.
  S09's own version 1 already refuses every earlier save.

**Implication for contributors:** you can move fields between the state
structs, but every such move changes a layout. A `_Static_assert` next to
each struct fails the build until you bump its `*_VER` and update its
`*_SIZE` in `state_versions.h`. Then add a line to the save-compatibility
list below, so the release notes say old saves won't load. The sizes are the
same on every target (explicit `int64_t` fields, packed layouts).

**Save-compatibility breaks** (one line per version bump, for release notes):

- Refactor pass 2, S09: every state chunk versioned; older saves can't be loaded.
- Refactor pass 2, S10: sim, net and game state version 2; older saves can't be loaded.
- Refactor pass 2, S11: sim and game state version 3; older saves can't be loaded.
- Refactor pass 4, S07: game state version 4 (P4-F8), sim state versions 4 and 5 (P4-F10, P4-F11), and the
  navigation mesh saved as a new chunk, version 1 (P4-F7); older saves can't be loaded.
- Refactor pass 5, S04: sim state version 9 (the drawing and local-camera fields out of `Thing`, `CreatureControl`
  and `kfx_sim_state`); older saves can't be loaded.
- Refactor pass 5, S11: sim state version 10 (the light registry's drawing state out of `struct Light`); older saves
  can't be loaded.
- Upstream merge 2026-10-03 (#5376): sim state version 11 and net state version 3 (the replay state out of both, into
  `struct ReplayState replay`), then sim state version 12 (`system_flags` split: `run_after_victory` stays,
  the machine's own flags became `local_system_flags`; each player's start settings in `PlayerInfo`); older saves
  can't be loaded.

---

## 7. Build system

**CMake is the only build definition that compiles the game**, for every
target: native Linux, Windows via MSVC/clang-cl (vcpkg), and Windows via
mingw-w64 cross-compile (`build/cmake/toolchains/mingw32.cmake`). It is what
CI runs.

| Build file                                    | Role                                                                                     | How it lists sources                                       |
| --------------------------------------------- | ---------------------------------------------------------------------------------------- | ---------------------------------------------------------- |
| `CMakeLists.txt` + `src/kfx_*/CMakeLists.txt` | compiles `keeperfx` and the Catch2 test binaries, every platform                          | `file(GLOB ...)` per library — **auto-follows file moves** |
| `Makefile` (+ `build/make/*.mk`)              | asset/data pipeline only (`pkg-languages`, `pkg-gfx`, `pkg-enginegfx`, `pkg-assemble`)    | its `OBJS` list is unmaintained; don't use it to compile   |

`linux.mk` and `scripts/setup-linux-thirdparty.sh` are gone. A new or moved
source file needs no build-file edit: re-run the CMake configure step
(`cmake <build-dir>`) so the glob sees it.

### 7.1 CMake target shape

- Each `src/kfx_*/CMakeLists.txt` globs its own `src/*.c|*.cpp` into a
  `STATIC` library, compiled once, with the logging settings of the INTERFACE
  target `kfx_log_opts`.
- The root `CMakeLists.txt` links every library (`KFX_OBJECT_LIBS`, reversed
  into link order) into **one executable**, `keeperfx`. `KFX_SOURCES_REMAINING`
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
blobs via ports (`GamePort`/`UiPort`) instead of `#include`-ing their headers directly
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

- **`config/daimonkeeper.cfg`** — top-level user settings (install path, language,
  resolutions, display, VSYNC, focus/pause behavior, …), shipped as
  `daimonkeeper.cfg` next to the executable. On first run a missing one is
  seeded from an existing `keeperfx.cfg` (never written). Mods still override
  settings with `mods/<mod>/keeperfx.cfg` (content compatibility, §10a).
- **`config/fxdata/daimonkeeper/`** — the 32-bit PNG start-up splash and legal
  screens (4:3 and wide), generated with the icon and README banner from
  `res/branding/`; they live apart from `data/*.raw` so installing over a
  KeeperFX folder doesn't replace its screens.
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

## 10a. Product identity and KeeperFX compatibility

The tree ships as **dAImon Keeper**. Two goals pull in opposite directions,
and each piece below serves one of them: never be mistaken for KeeperFX (by the
network, save files, the file system or players), and keep playing KeeperFX
content -- saying clearly when a map needs something this build doesn't have,
instead of crashing.

**Names and versions -- one source.** `build/make/version.mk` holds
`PRODUCT_SLUG` (`daimonkeeper`), the product version `VER_*` (1.0.0) and
`KFX_COMPAT_MAJOR/MINOR` (1.4: the KeeperFX *release* whose content this plays).
CMake (`OUTPUT_NAME`, map/debug file names, CPack package
`daimonkeeper-<ver>.<build>-kfx<compat>`), the Makefile and the code read it;
the code through `ver_defs.h` (generated into each build tree, so a tree's
`-DBUILD_NUMBER` is its own) and `kfx_platform/include/version.h`
(`PRODUCT_NAME`, `PRODUCT_SLUG`, `PRODUCT_EXE_NAME`, `PRODUCT_MAGIC`,
`PRODUCT_VERSION_LABEL` "dAImon Keeper 1.0.0 — KFX 1.4" -- UTF-8, with an ASCII
variant for the bitmap fonts). The label shows on the main menu, in the log
header, the `ver` command and PE metadata; the TCP API's `get_kfx_info` reports
the compat level as `kfx_version` plus `product`/`product_version`.

**Kept apart from KeeperFX.**
- Files: executable `daimonkeeper`, base config `daimonkeeper.cfg`
  (`import_kfx_base_config()`), log `daimonkeeper.log`. `fxdata/`, `save/`
  and `replays/` keep KeeperFX's names (an own-folder scheme --
  `fxdata-daimonkeeper/`, `save/daimonkeeper/` -- was tried and reverted):
  the two games need separate install folders, sharing data by symlink.
- Saves/replays: the `PROD` chunk (§6.2).
- Network: LAN discovery uses `DAIMONKEEPER_DISCOVER`/`DAIMONKEEPER_HOST:`;
  matchmaking is off by default, refuses KeeperFX's server, and announces
  `daimonkeeper-<version>`. (The multiplayer handshake itself doesn't carry the
  product magic yet.)

**Content compatibility -- what must not change.** Script command names and
arguments, `LEVEL_VERSION` semantics, config file names/keys/named values, Lua
API names, folder layout, `mods/<mod>/keeperfx.cfg`, string indices, map file
formats. New features are added, never repurposed.

**Checking content at load (the compat report).** `kfx_config/compat_report.{h,c}`
collects, during a level load, what the content uses that this build doesn't
know:
- unknown level-script commands and creature/room/slab names (`lvl_script.c`);
  a command of the *other* `LEVEL_VERSION`'s table isn't counted -- it fails the
  same in KeeperFX, so it's the level's own mistake;
- unknown config keys (legacy `recognize_conf_command()` and NamedField
  `assign_conf_command_field()`, not list-only passes) and unknown named
  values/flags -- values are held as *pending* with their name table and only
  reported if they still don't resolve once every config has loaded
  (`compat_report_resolve_pending()`), since a name can come from a file loaded
  later;
- Lua calls to missing functions (`lua_report_missing_function()`, from
  `CheckLua()`'s error text, LuaJIT and Lua 5.4 wording);
- limit overflows (config blocks beyond a table's size, script VALUE slots).

Issues are deduplicated, carry the file (every config load names it via
`compat_report_set_source()`) and line, and are written as `COMPAT:` log lines.
`init_level()` clears the report; after `load_script()`,
`game_compat_review.c` logs it and, for a local human-attended game (not
multiplayer, a replay, ftests, the editor, or with the TCP API enabled), flags it
for the in-game warning (`frontgui_ingame.cpp`): paused, "Back to menu" / "Play
anyway", modal for input.

**Checking content before play (list markers).** `script_preflight_*()`
(`lvl_script.c`) checks a level script without loading it, with the parser's own
tokenizer, comment, line and `LEVEL_VERSION` rules -- commands only, since names
depend on which configs are loaded. `kfx_frontend/frontgui_compat_badges.cpp`
caches results per file (re-checked on change, 16 scans per frame) and marks
level, map-pack and campaign rows `[!]` with a tooltip;
`prepare_campaign_levels_path()` reaches any campaign's level files, not just
the loaded one's.

**The editor's "Force KeeperFX" save** guarantees a map that loads in the
compat release: `kfx_editor/editor_kfx_compat.cpp` refuses (writing nothing)
fork-only script commands, Lua functions and base-game kinds missing at that
model number, listing them in Save As. The list is
`kfx_compat_reference.inc`, generated by `scripts/gen_kfx_compat_reference.py`
against the `v<KFX_COMPAT>.0` tag. Every file type and key the native writer
emits is read by that release, so the check is on content, not format. A plain
Save reuses the format the map was last saved with.

**Keeping it true.** `scripts/kfx_parity.py` diffs the content tables (script
commands/names, config keys/values, Lua registrations; the player's own
settings files separately) between an upstream ref and this tree. Tests pin the
rest: every key and named value in `config/fxdata` is read (loaded in the game's
own order), every shipped level script preflights clean, and the generated
reference matches the shipped data. When to re-run each generator, and the
`KFX_COMPAT_*` bump rule, are in [`upstream-merge-workflow.md`](upstream-merge-workflow.md)
§6a.

**Attribution.** `NOTICE` (origin, modification statement, upstream base,
licences), `THIRD_PARTY_NOTICES.txt` (generated by
`scripts/gen_third_party_notices.py`), `LICENSE` and the graphics licence all
ship in the package; the Credits screen starts with a "Based on KeeperFX" block.

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
| `get_gameturn`                    | `kfx_platform` (inline) | 329    |
| `room_is_invalid`                 | `kfx_sim`    | 226    |
| `thing_exists`                    | `kfx_sim`    | 184    |
| `get_map_block_at`                | `kfx_sim`    | 180    |
| `creature_stats_get_from_thing`   | `kfx_sim`    | 171    |
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
`PathfindingWorldPort` (`kfx_config/include/ports/pathfinding_world_port.def`;
`PathfindingWorldCallbacks` until refactor pass 2's S15; then 51 entries, 29
since refactor pass 2's S08 moved the `struct Thing`/`Map`/
`SlabMap` layouts into `kfx_model` and the plain field accessors went):
map/door queries, the `CreatureControl`-embedded
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

### 12.3 One executable; logging is a runtime option

There used to be two executables, `keeperfx` and `keeperfx_hvlog`, differing
only in `BFDEBUG_LEVEL` (0 or 10), with every library compiled twice. Since
refactor pass 2's stage S02 (`docs/refactor-pass2/stage-02-logging-option.md`)
there is one, and how much it logs is the `LOG_LEVEL` option
(Off/Normal/Debug/Debug max, applied live):

- `*DBG(lv, …)` lines print when `lv` is below the runtime threshold
  (Normal 0, Debug 10, Debug max 20) and below the compile-time
  `KFX_DEBUG_CEILING` (globals.h, 20). Former `#if (BFDEBUG_LEVEL > N)` blocks
  are `if (KFX_DEBUG_ON(N))`. **Debug-only code must never write simulation
  state**, so the log level can't desync a multiplayer game.
- Off writes nothing but crash reports. Lines written before `daimonkeeper.cfg`
  is read are buffered until the level is known. The log is `daimonkeeper.log`. Debug and above buffer their
  writes and flush once per frame and on errors.
- GPU validation is its own restart-only option, `GPU_DEBUG`.
- `BFDEBUG_LEVEL` stays defined (0) only so upstream code compiles; merged
  `#if (BFDEBUG_LEVEL > N)` blocks are dead until converted.
- Functional-test runs pin Normal, whatever daimonkeeper.cfg says. There is no
  `keeperfx_hvlog` any more, not even as a copy (the external launcher's
  heavy-log option is obsolete).

---

## 13. Conventions for contributors

1. **Never break the layering.** If you find yourself wanting a lower library to
   `#include` a higher one, stop. Add a port entry (§5), a narrow accessor,
   or move the field/function to the lowest-ranked real consumer. `check_layering.py --strict` will reject a raw back-edge.
2. **A new file just goes in the right `src/kfx_<name>/src/`.** CMake globs
   it (re-run the configure step so the glob sees it); no other build file
   lists sources (§7).
3. **World state belongs in the owning library's state struct**, not in
   `struct Game` (which is a serialization placeholder). Respect the raw-blob
   serialization invariant (§6.2) — moving fields between structs is fine;
   changing on-disk/on-wire layout is not, without a migration story.
4. **A new cross-layer call is a port entry.** Add one line to the
   provider's port `.def` (the unwired default comes with it), and the real
   function to the provider's table in its `*_port_impl.c(pp)`; callers use
   the `PREFIX_name()` wrapper. A new port also needs its generated header
   and defaults file (copy an existing one) and one `set_*_port()` line plus
   a `KFX_TABLE_COMPLETE` line in `main.cpp`. §5.2.
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
| How to build                                      | [`docs/build_instructions.txt`](../build_instructions.txt) (source layout: §7)            |
| How to write a functional test                    | [`src/ftests/README.md`](../../src/ftests/README.md)                                      |
| How the `KFX_BUILD_TESTS` unit-test/coverage harness works | [`testing-harness.md`](testing-harness.md)                                       |
| How to merge upstream (dkfans/keeperfx) into this fork | [`upstream-merge-workflow.md`](upstream-merge-workflow.md)                          |
| How this tree stays apart from, and compatible with, KeeperFX | this doc (§10a) + [`upstream-merge-workflow.md`](upstream-merge-workflow.md) §6a |
|                                                   |                                                                                           |
