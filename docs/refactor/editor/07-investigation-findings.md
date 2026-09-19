# Phase 7 — investigation findings: open questions + assumptions resolved

Status: **complete.** Three `src/` investigation passes (F1–F21) + the design decisions (D1–D6)
settled with the user. This doc records what the code actually shows; the phase docs (0–6) are
updated to match. Read it alongside the phase it feeds.

Everything here is from reading `src/` at the current `refactor-renderer` HEAD. File:line
references are load-bearing — re-verify if the code has moved.

---

## F1 — the frozen sim is *already built*: `GOF_Paused` (resolves O-implicit, de-risks R2)

**The entire per-turn simulation is one `if` away from stopped.**
[`game_session_loop.cpp:139`](../../../src/kfx_apploop/src/game_session_loop.cpp) —
`update()` runs `process_packets()` + camera updates **unconditionally**, then:

```c
if (!flag_is_set(kfx_sim_state.operation_flags, GOF_Paused)) {
    clear_active_dungeons_stats();
    update_creature_pool_state();
    update_things();          // ← all creature AI, effects, shots
    process_rooms();
    process_dungeons();       // ← economy, upkeep
    update_research();
    update_manufacturing();
    event_process_events();
    process_level_script();   // ← IF/WIN_GAME/ADD_CREATURE_TO_LEVEL
    lua_on_game_tick();
    process_computer_players2();
    process_players();
    process_action_points();
    kfx_game_state.play_gameturn++;   // ← the clock
}
```

Consequences for the editor:
- **Setting `GOF_Paused` freezes AI, economy, script, generation, action points and the game
  clock in one flag** — no subsystem-by-subsystem audit needed. The phase 1 §3 table becomes a
  *description of what `GOF_Paused` already does*, not a checklist of edits.
- **`process_packets()` still runs while paused** ([`packets.c:1571`](../../../src/kfx_net/src/packets.c) —
  no pause guard), and cheat/placement actions dispatch through
  `process_user_dungeon_control_packet_action` → `process_players_dungeon_control_cheats_packet_action`
  ([`packets.c:1096`](../../../src/kfx_net/src/packets.c)), also un-gated. **So editor edits
  apply normally in a paused game.**
- **Rendering is independent of `update()`** — a paused game draws the frozen world every
  frame. WYSIWYG for free.
- `GKind_NonInteractiveState` ([`game_session_loop.cpp:132`](../../../src/kfx_apploop/src/game_session_loop.cpp))
  is an even harder stop (returns before `update_things`) used for menu-backdrop demos — too
  hard for the editor (no thing updates at all, and it's a `game_kind` not a flag).

**Revised design for `simulation_suspended`:** it is a thin wrapper that (a) sets `GOF_Paused`,
(b) **re-asserts it every `editor_frame()`** so the player's pause key / menu can't lift it,
(c) exposes an explicit "Preview motion" toggle that clears `GOF_Paused` for as long as it's
held. It does **not** need its own guards scattered through `kfx_sim` — one flag, already
plumbed. Keep the neutral name (it's "force-and-lock the existing pause", reusable).

Caveat: with pure `GOF_Paused`, effect generators, static-light updates and animated textures
also freeze (they're inside `update_things` / the paused block). Acceptable for v1 (static
preview); the "Preview motion" toggle covers the "does my lava look right" case. If we want
lights/particles live while otherwise frozen, `editor_frame()` can call
`update_global_lighting()` + `update_animating_texture_maps()` itself — small, additive, phase 5.

Un-pause plumbing to be aware of: `set_packet_pause_toggle()`
([`packets_misc.c:347`](../../../src/kfx_net/src/packets_misc.c)) has a cooldown and broadcasts;
`PckA_TogglePause` / `PckA_UpdatePause` handlers at
[`packets.c:662`/`732`](../../../src/kfx_net/src/packets.c). Editor should own pause state
directly (set the flag), not round-trip through the toggle packet.

---

## F2 — derived `.dat`/`.clm`/`.wib` are regenerable (resolves O1, de-risks R1)

**Update (phase 3 slice 1, implemented and empirically tested against a real stock level — see
[`phase3/00-slice1-native-save.md`](phase3/00-slice1-native-save.md)):** two corrections below.
Line number drifted (1292 → 1315, function unchanged). More importantly: **reinforced corners
are *not* covered by this function** — `fill_in_reinforced_corners()` is reachable only via the
digger-specific `place_and_process_pretty_wall_slab()` ("prettify this wall after a real dig"),
never from `place_single_slab_type_on_map()` itself. This was confirmed as a real, non-fatal gap
in practice: reloading a `regenerate_derived_map_data()`-saved map logs ~250+
`update_slabset_column_indices: column:N referenced in slabset.toml but not present in
columnset.toml` errors — some slab-style column variants a real map's shipped `.clm` provides
pre-baked aren't reproduced by the runtime regen path alone. Non-fatal (falls back to something,
doesn't crash), but exactly the kind of stock-map divergence the S1 spike below was meant to
size — now a named, confirmed-real target for it, not a hypothetical.

**The runtime already rebuilds a slab's columns from config on every dig/build.**
`place_single_slab_type_on_map()`
([`map_blocks.c:1315`](../../../src/kfx_sim/src/map_blocks.c)) takes `(slbkind, slb_x, slb_y,
plyr_idx)` and produces the full 3×3 column set for that slab from
`kfx_sim_state.slabset[]` (the game-wide slab→column template table, built from
`slabset.toml` / `columnset.toml` by `load_slab_datclm_files()`
[`lvl_filesdk1.c:1029`](../../../src/kfx_sim/src/lvl_filesdk1.c)), applying:
neighbour-aware fortified-wall variants, `place_single_slab_modify_column_near_liquid`,
torches (`slab_kind_has_torches`), room prettiness, style set. **Not** reinforced corners — see
the update note above. `place_slab_columns()` → `find_column`/`create_column`
dedupes into `kfx_sim_state.columns_data[]` and `update_map_collide()` sets collision.

`.wib` (wibble) has an explicit auto-generator already: `initialise_map_wlb_auto()`
([`lvl_filesdk1.c:1105`](../../../src/kfx_sim/src/lvl_filesdk1.c)) is the fallback when `.wlb`
is absent (`initialise_extra_slab_info` [`:1187`](../../../src/kfx_sim/src/lvl_filesdk1.c)).

**Corroboration:** `file formats.doc` lists CLM/WIB/APT as engine-generatable ("Currently
generates blank entries" / "easy"), and ADiKtEd's note "*.clm and .dat files are now
auto-generated, nearly perfectly*". The classic tools regenerate this because it *is*
derivable; KeeperFX derives it better because it's the engine.

**Current blocker — corrected:** `load_level_file()` calls `load_map_data_file` (`.dat`) +
`load_column_file` (`.clm`), but **discards both return values** — it does not "treat absence as
failure"; it silently ignores it, leaving an empty column table rather than erroring. There was
**no "regenerate on missing" path today** either way — that's the one piece of new `kfx_sim`
code.

**Implemented** ([`phase3/00-slice1-native-save.md`](phase3/00-slice1-native-save.md)):
`regenerate_derived_map_data()` in `kfx_sim`/`lvl_filesdk1.c`. Must run *after*
`load_map_slab_file()` sets real slab kinds (the earliest point they exist), not "after `.slb` +
`.own` + `.inf` load" as originally designed here — `load_map_slab_file()` runs much later in
`load_level_file()`'s own sequence. Body: `for each slab: place_single_slab_type_on_map(slb->kind,
x, y, slabmap_owner(slb))`, with a bounds check that fails loudly on a genuinely out-of-range
slab kind rather than silently clamping. Doesn't separately call `init_columns()`/
`initialise_map_wlb_auto()` — both already run unconditionally elsewhere in `load_level_file()`'s
own sequence regardless of this path. `load_level_file()` now propagates `false` if regen fails,
closing the silent-corruption gap. `editor_save_map` writes only `.slb`/`.own`; the derived files
are never written.

**Spike S1 still required** to prove *byte/graphics equivalence* on a stock map (regen vs
loaded `.clm`), because the stock `.clm` files may encode hand-tweaks the slabset table
doesn't reproduce. But the *mechanism* is proven — this is a validation spike, not a
feasibility one. If a handful of stock maps differ, they keep their `.clm` (loader tries the
file first, regenerates only when absent); editor-authored maps never have one.

---

## F3 — TOML schemas for `tngfx` / `lgtfx` / `aptfx` are known (resolves S2)

Container (`load_kfx_toml_file` [`lvl_filesdk1.c:789`](../../../src/kfx_sim/src/lvl_filesdk1.c)):
`[common]` table with a count field, then **either** an array `<name> = [ {…}, {…} ]`
**or** `[<name>0]`, `[<name>1]` … sections. Writer picks one — the array form is cleanest.
All values are flat scalars/strings — hand-emitting TOML is trivial, no emitter library
needed (`tomlc99` is parse-only).

### `tngfx` — `[common] ThingsCount=N`, then `thing` array
Keys, from `thing_create_thing_adv()`
([`thing_factory.c:215`](../../../src/kfx_sim/src/thing_factory.c)):

| Key | Meaning | Applies to |
|---|---|---|
| `ThingType` | class name (`value_parse_class`) | all |
| `Subtype` (int) **or** `SubtypeStringID` (string) | model | all |
| `Ownership` | owner player | all |
| `SubtileX` / `SubtileY` / `SubtileZ` | position (`value_read_stl_coord`, fixed-point) | all |
| `ParentTile` | attached slab index (`y*w+x`), `-1` = none | object/trap/effectgen |
| `Orientation` | `move_angle_xy` | object/creature/trap |
| `CreatureLevel` | 1–10 (stored −1) | creature |
| `CreatureGold` | gold carried | creature |
| `CreatureInitialHealth` | % of max | creature |
| `CreatureName` | custom name | creature |
| `GoldValue` | pile value | gold objects |
| `CustomBox` | custom special-box kind | special box |
| `HerogateNumber` | **hero-gate id** — hero gates are objects here, *not* in `aptfx` | hero gate object |
| `EffectRange` | emitter radius | effect generator |
| `DoorOrientation` / `DoorLocked` | 0/1 | door |

### `lgtfx` — `[common] LightsCount=N`, then `light` array
Keys, from `light_create_light_adv()`
([`light_data.c:195`](../../../src/kfx_render/src/light_data.c)):
`Dynamic` (bool), `SubtileX/Y/Z`, `LightRange` (radius), `LightIntensity` (uint),
`ParentTile` (attached slab). Flags beyond dynamic/static are `TODO: not implemented yet` in
the loader — writer emits the four fields + `Dynamic`, nothing more.

### `aptfx` — `[common] ActionPointsCount=N`, then `actionpoint` array
Keys, from `actnpoint_create_actnpoint_adv()`
([`actionpt.c:81`](../../../src/kfx_sim/src/actionpt.c)):
`PointNumber`, `SubtileX`, `SubtileY`, `PointRange`. **Action points only** — no Z, no owner
(player-independent, matches the manual). Hero gates go in `tngfx` (see above).

**Correction to earlier drafts:** phase 3 §1 and phase 5 §3 said hero gates serialize to
`aptfx`. They don't — a hero gate is a `TCls_Object` with `HerogateNumber` in `tngfx`.
Phase 5's "Add Hero Gate" tool creates an object, not an action point.

---

## F4 — no `levels.txt` step needed: `.lof` auto-discovery (resolves O2)

`find_and_load_lof_files()` ([`lvl_filesdk1.c:638`](../../../src/kfx_sim/src/lvl_filesdk1.c))
globs `*.lof` in `FGrp_CmpgLvls` (the active campaign's levels dir) at campaign load and
`level_lof_file_parse()` ([`:350`](../../../src/kfx_sim/src/lvl_filesdk1.c)) registers each
into the campaign's level lists. A `.lof` with `KIND = SINGLE` calls
`add_single_level_to_campaign`; `KIND = MULTI` → `add_multi_level_to_campaign`; there's also
`add_freeplay_level_to_campaign` / `add_bonus_level_to_campaign` / `…extra…`.

`.lof` commands (`cmpgn_map_commands`): `NAME_TEXT`, `NAME_ID`, `ENSIGN_POS`, `ENSIGN_ZOOM`,
`PLAYERS`, `ENSIGN`, `SPEECH`, `LAND_VIEW`, `KIND`. `.lif` is the minimal legacy form
(`num, Name`), scanned by `find_and_load_lif_files()` ([`:314`](../../../src/kfx_sim/src/lvl_filesdk1.c)).

`get_level_fgroup()` always returns `FGrp_CmpgLvls`
([`config.c:1691`](../../../src/kfx_config/src/config.c)); it resolves per-campaign
(`campaign.levels_location`), defaulting to `levels/` for the standard campaign. There is a
separate `FGrp_VarLevels` = `<install>/levels` (original DK maps).

**Decision:** `editor_save_map` writes a `.lof` with `NAME_TEXT` + `KIND` (SINGLE or MULTI
from the level-settings player count) alongside the map files, into `FGrp_CmpgLvls`. The map
then appears in Free Play / Skirmish with **no `levels.txt` edit** — the single worst wart of
the 1998 editor is gone for free. For a clean "my editor maps" bucket, ship a bundled
mappack/campaign whose `levels_location` is the editor's default save dir (O2 sub-decision:
do this, name it "Editor Maps"). Playtest from the editor doesn't need any of this — it resets
the loaded level in place.

---

## F5 — session bootstrap: reuse `startup_network_game`, add one trim (resolves phase 1 §4)

[`main_game.c`](../../../src/kfx_game/src/main_game.c):
- `startup_network_game(context, local=true)` ([`:591`](../../../src/kfx_game/src/main_game.c))
  → `init_level()` ([`:377`, `static`](../../../src/kfx_game/src/main_game.c)) does the whole
  load: `clear_game`, heap, seeds, `open_lua_script`, `load_stats_files`, `init_map_size`,
  **`load_map_file(level)`**, `init_navigation`, `init_player_start` per roaming player,
  `event_initialise_all`, `battle_initialise`, `ambient_sound_prepare`. Sets
  `play_gameturn = 0`.
- → `startup_network_game_tail` (coroutine): `setup_computer_players()` **or**
  `setup_zombie_players()` (chosen by `is_fe_computer_players_active() || ShouldAssignCpuKeepers`)
  → `post_init_level()` ([`:501`](../../../src/kfx_game/src/main_game.c)):
  `setup_computer_players2`, **`load_script`**, **`lua_on_game_start`**,
  `init_dungeons_research`, `create_transferred_creatures_on_level` (skipped for map packs),
  `update_dungeon_generation_speeds`, `init_traps`, `init_all_creature_states`,
  `init_keepers_map_exploration`.

For the editor:
- Force the **`setup_zombie_players()`** branch (no CPU keepers) — editor sets
  `is_fe_computer_players_active()` false and `AssignCpuKeepers`/`campaign.assignCpuKeepers` off.
- `init_level()` is reused **as-is** (we want the full map + config load).
- `post_init_level()` needs a **trim**: `init_traps` + `init_all_creature_states` +
  `init_keepers_map_exploration` are wanted (correct rendering); `lua_on_game_start` +
  `create_transferred_creatures_on_level` + generation-speed setup are gameplay side effects
  we don't want firing (though `GOF_Paused` makes most inert anyway — `lua_on_game_start` is
  the real risk: arbitrary user Lua).

**Decision (matches phase 1's fallback):** add **one** `static`-lifting or new function in
`main_game.c` — `startup_local_game_for_editor(context, lvnum)` — = `init_level()` +
`setup_zombie_players()` + `post_init_level_editor()` (the trimmed variant) + set
`GOF_Paused`. `init_level()` is currently `static`; either expose it or wrap the whole thing.
This is the single sanctioned `kfx_game` addition (00-overview §5.3). Everything else —
the god-state setup, reveal, the toolbox — is `kfx_editor` calling down.

`init_level()` already sets `FTF_LevelLoaded` under `FUNCTESTING` — the editor ftests get a
ready signal for free.

Entry is coroutine-staged (`CoroutineLoop *context`), like `faststartup_network_game`
([`:664`](../../../src/kfx_game/src/main_game.c)) — `editor_open()` enqueues steps the same
way, triggered on entering a new top-level game state (sibling of `FeSt_START_KPRLEVEL`).

---

## F6 — undo/redo is feasible; "sim state *is* the map" (resolves O4)

The cheat handlers ([`packets_cheats.c:945`+](../../../src/kfx_net/src/packets_cheats.c))
place/create but **capture no prior state** — no built-in inverse. But:

- `kfx_editor` is above `kfx_sim`, so before dispatching a terrain edit it reads
  `get_slabmap_block(x,y)->kind` + `slabmap_owner(x,y)` directly → inverse = "place that kind
  with that owner". Read-before-write, client-side, in `editor_frame()`.
- Because **`simulation_suspended` stops all autonomous spawning/death** (F1), the set of
  things/slabs/lights/APs in the sim after `editor_open()` + edits *is* the map. No need for a
  parallel authoritative editor model — **serialization walks live sim state** (skipping
  transient thing classes: shots, effects, effect-elems, dead bodies — none of which exist in
  a suspended sim anyway). Undo of a thing-place = locate + delete the just-created thing
  (either "highest index" or a dedicated `PckA_EditorPlaceThing` that stashes the new index
  in `player->` for read-back).
- Brush / fill / area-op undo = snapshot the whole affected slab rect + thing list before,
  restore after. Bounded (≤85×85), cheap.

**Decision:** command journal in `kfx_editor` from phase 2 day 1; ship undo for terrain +
thing place/delete in phase 2 (inverse capture is trivial there); brush/fill/area-op undo in
phase 2 if the rect-snapshot lands cleanly, else phase 6.

---

## F7 — one binary, no new define (resolves O3)

The in-game ImGui GUI is a **runtime** switch (`RendererImGuiEnabled()` ← `!use_classic_menu()`),
not a compile define. `FUNCTESTING` is the only feature `#ifdef` near this code and it's
test-only. `kfx_editor` is a normal internal `OBJECT` library (like `kfx_script`) linked into
`keeperfx` / `keeperfx_hvlog` unconditionally; the editor is reached behind the menu button,
gated at runtime by `editor_is_active()`. No `Editor.exe`, no `-editor`-only build. **O3
confirmed: single binary.** (A `-noeditor` *runtime* flag to hide the menu entry is trivial
if ever wanted; not needed for v1.)

---

## F8 — no procedural map generator, and none wanted (resolves O7)

`grep -ri mapgen|procedural|random_map|generate.*map src/` → nothing. ADiKtEd's `-r`
random-map has no KeeperFX equivalent. **User decision: a random map generator is not
desirable for now** — O7 is closed, not "speculative future". New Map is a plain blank
(earth + rock border); nothing to fold in.

---

## F9 — wall-height *and* top-down view both already exist (corrects earlier draft, resolves phase 4 §2)

**Earlier draft was wrong.** Both original-editor "View" items the plan flagged as new
renderer work already ship:

- **Wall Height** = **`settings.video_cluedo_mode`** (DK's "cluedo mode" / low-walls). It's a
  persisted video option, toggled from the **in-game video options menu**
  (`gui_video_cluedo_mode` [`frontmenu_options.c:259`](../../../src/kfx_frontend/src/frontmenu_options.c),
  `PckA_SetCluedo` [`packets.c:665`](../../../src/kfx_net/src/packets.c)). The renderer fully
  implements it: wall column height `5 → 2` ([`engine_render.c:2491`](../../../src/kfx_render/src/engine_render.c)),
  `fill_in_points_cluedo` / `do_a_plane_of_engine_columns_cluedo`, `temp_cluedo_mode` gating
  throughout `engine_render.c`. **The editor just puts a toggle for this existing setting in
  its View menu** (send `PckA_SetCluedo`). Zero new renderer work.
- **Plan / true top-down** = the **parchment map's zoom box** (`draw_zoom_box`
  [`gui_parchment.c:861`](../../../src/kfx_frontend/src/gui_parchment.c)) — a magnifier over
  the overhead map, `minimap_zoom` 128–2048, that renders terrain *and* things
  (`draw_zoom_box_terrain` / `draw_zoom_box_things`). It's the existing "Plan view"
  equivalent. The editor's "Plan" toggle opens the parchment map with the magnifier; the
  overhead map is already a genuine top-down projection. No new ortho camera.

So **phase 4 has no new renderer dependency at all** — it's all menu wiring + overlays via the
existing `RenderOverlayCallbacks` path. R5 (renderer churn) drops to Low for the editor.

---

## F10 — no cheat-mode gate exists; the editor needs no "enable cheats" step

`PckA_CheatEnter`'s handler is a **no-op** — the only line is commented out
(`// game.???[my_player_number].cheat_mode = 1;`,
[`packets_cheats.c:774`](../../../src/kfx_net/src/packets_cheats.c)). Every `gui_boxmenu.c`
cheat handler carries `// if (player->cheat_mode == 0) return false; -- there's no cheat_mode
flag yet`. So **`PckA_Cheat*` packet actions execute unconditionally on receipt** — there is no
handshake to perform.

Consequences:
- `editor_open()` does **not** send `PckA_CheatEnter` or set any flag before using
  `PckA_CheatPlaceTerrain` / `PckA_CheatMakeCreature` / … — just send them.
- These packets are reachable today only through the debug cheat menu (itself dev/keybind
  gated) or the console; the editor is a third, deliberate entry point. No new "is this
  allowed" check is needed for the editor's own use.
- Phase 1 §3 should drop its `PckA_CheatEnter` mention.

---

## F11 — layering: `kfx_editor` ranks *above `kfx_apploop`*, only `main.cpp` calls in

The in-game ImGui submission is already a **single registered callback**:
`main.cpp:1301` — `RendererSetImGuiFrameCallback(&FrontendImGuiFrame)`. The renderer invokes it
once per frame (frontend and in-game alike); `FrontendImGuiFrame()` (kfx_frontend) internally
calls `ingame_imgui_frame()`.

**So the editor's per-frame hook is trivial and needs no layer edits below it:** `main.cpp`
registers a wrapper — `static void app_imgui_frame(){ FrontendImGuiFrame(); editor_frame(); }`
— and passes *that* to `RendererSetImGuiFrameCallback`. `editor_frame()` no-ops unless
`editor_is_active()`. `kfx_apploop` (the game loop) needs **no editor `#include`** (one `case`
label, F16) — the
`GOF_Paused` re-assert, tool handling, cursor and packet dispatch all live in `editor_frame()`,
which runs every rendered frame.

Layering (`scripts/check_layering.py` `LIBRARY_ORDER`
[line 62](../../../scripts/check_layering.py)):
```
… kfx_frontend, kfx_script, kfx_apploop, **kfx_editor**, app_entry
```
Insert `"kfx_editor"` immediately before `"app_entry"`. It becomes the highest-ranked internal
library: may `#include` anything below (`kfx_config` … `kfx_apploop`); **nothing below may
include it**; the only inbound edge is `main.cpp` (app_entry), which already includes every
layer. `LIB_DIR_NAMES` derives from the list, so `src/kfx_editor/` is picked up automatically.

The **only** cross-layer plumbing (00-overview §5.3): `EditorCallbacks` in
`kfx_config/include/` — `request_open(lvnum, is_new)` + browser helpers — so the
`FeSt_EDITOR` menu button (in kfx_frontend, *below* kfx_editor) can request entry without a
reverse include. Wired in `main.cpp::setup_game()` like every other `*Callbacks`.

Corrects phase 1 §0 ("above kfx_script, below kfx_apploop" → **above kfx_apploop**), and drops
the phase 1 §0 "wire `editor_frame()` into `kfx_apploop`" line — it's wired into the
renderer's ImGui callback from `main.cpp` instead.

---

## F12 — maps are fully resizable; the writer is size-agnostic (informs phase 1 §5 / phase 3)

`set_map_size(x, y)` ([`map_data.c:833`](../../../src/kfx_sim/src/map_data.c)) sets
`kfx_sim_state.map_tiles_x = x`, `map_subtiles_x = x * STL_PER_SLB`, and rebuilds the
`around_slab` neighbour tables. `init_map_size(lvnum)` reads `lvinfo->mapsize_x/y`
(`DEFAULT_MAP_SIZE = 85` fallback). Map size is a **`.lof` command** — `MAPSIZE x y`, case 13
in `level_lof_file_parse()` ([`lvl_filesdk1.c:596`](../../../src/kfx_sim/src/lvl_filesdk1.c))
(also a campaign-file command).

Every map-file loader sizes its buffer from `map_tiles_x/y` / `map_subtiles_x/y`
(e.g. `load_map_slab_file` reads `2 * map_tiles_y * map_tiles_x`), so **`write_slb` / `write_own`
are automatically size-correct if they iterate `kfx_sim_state.map_tiles_x/y`.**

- **New Map** at an arbitrary size: `editor_new_map(w,h,…)` calls `set_map_size(w,h)` then fills
  the arrays; the `.lof` gets `MAPSIZE w h`. Works.
- **Resizing a populated map** stays deferred (phase 5 §1) — things/APs/lights outside the new
  bounds, `around_slab` table churn.

---

## F13 — `.lof` discovery runs at campaign load, not continuously (refines F4)

`find_and_load_lof_files()` is called from `config_campaigns.c:1219` / `:1499` — during
**campaign load / change**, via `config_reload_callbacks->find_and_load_lof_files()`. It is
**not** re-run when the menus are opened. So a map the editor *just saved* will not appear in
Free Play until the campaign is (re)loaded.

Fix: after a successful first Save of a new map, `kfx_editor` calls
`config_reload_callbacks->find_and_load_lof_files()` + `…lif_files()` to re-scan.
`level_lof_file_parse` → `add_single_level_to_campaign` guards on
`if ((lvinfo->level_type & LvKind_IsSingle) == 0)` before adding, so a re-scan is close to
idempotent (level-info fields are overwritten, not duplicated; campaign list entries not
double-added). Confirm no duplicate on re-scan in the phase-3 spike.

---

## F14 — land-preview thumbnails read `.slb` without loading the level (confirms phase 3 §6)

`land_preview_build_minimap(lvnum)`
([`frontmenu_landpreview.c:310`](../../../src/kfx_frontend/src/frontmenu_landpreview.c))
reads `map%05u.slb` + `.own` straight through `load_single_map_file_to_buffer` and builds a
slab-colour minimap — *"Deliberately does NOT call load_map_slab_file()/load_map_ownership_file()"*
(its own comment). Exactly what the **Open Map** dialog's thumbnail needs, for any level number
with a `.slb`. Keyed by `LevelInformation` (so editor maps need their `.lof` scanned first —
F13).

---

## F15 — custom creatures / objects / texture packs are covered by config-count-driven palettes

Every domain palette the editor builds is a loop over a **config count**, so modded/custom
content appears **automatically** with no editor changes:

| Palette | Source | Count field |
|---|---|---|
| Creatures / heroes | `kfx_config_state.conf.crtr_conf.model` | `.model_count` (`CREATURE_TYPES_MAX = 128`) |
| Objects / spellbooks / specials | `object_conf` | its count |
| Traps / doors | `trapdoor_conf` | its counts |
| Rooms | `room_conf` | its count → each room's `assigned_slab` |
| Slab kinds | `slab_conf` | `slab_types_count` |
| Texture packs | `texture_pack_desc` ([`lvl_script_commands.c:428`](../../../src/kfx_game/src/lvl_script_commands.c)) + any custom `tmap?%03d.dat` present in `FGrp_CmpgConfig` | 32 slots (`TEXTURE_VARIATIONS_COUNT`) |

Names for the palette labels come from the existing `*_code_name(model)` helpers /
`texture_pack_desc`. Icons via `SpriteLookupCallbacks`. A campaign/mappack that ships extra
creature `.cfg` entries or its own `tmap*.dat` texture pack is picked up the moment its configs
load — the editor sees exactly what the running game sees. **This is the core reason for
building the editor in-engine (00-overview §2).**

### Texture sets — the file picture

- **Base texture set**: `map%05d.inf` — a **single byte** = `texture_id`
  (`load_and_setup_map_info` [`lvl_filesdk1.c:1330`](../../../src/kfx_sim/src/lvl_filesdk1.c)).
  `write_inf` = one byte.
- **Per-slab texture override** (multi-tileset maps): `map%05d.slx` — `map_tiles_x * map_tiles_y`
  bytes, one texture-pack id per slab, into `kfx_config_state.slab_ext_data` (`load_ext_slabs`
  [`lvl_filesdk1.c:1344`](../../../src/kfx_sim/src/lvl_filesdk1.c)). `write_slx` = a byte grid,
  same shape as `write_own`. Only written when the map actually uses >1 pack.
- Textures load `FGrp_CmpgConfig` first (custom), then `FGrp_StdData` (built-in) —
  `tmap{a|b}%03d.dat` ([`engine_textures.c:141`/`195`](../../../src/kfx_render/src/engine_textures.c)).
- **Correction to phase 3:** the writer list must include `.inf` **and `.slx`** (`.slx` was
  missing).

### Custom creatures — per-map stat overrides

`map%05lu.creature.cfg` / `map%05lu.<name>.cfg` in `FGrp_CmpgLvls`
([`config_crtrmodel.c:2841`/`2925`](../../../src/kfx_config/src/config_crtrmodel.c)) are a
per-map creature-config layer — the modern form of the 1998 manual's "edit creature.txt".
v1 defers editing these (phase 5 §4.5 emits `SET_CREATURE_*` script commands instead); a
per-map creature-cfg panel is a clean v2 addition. Either way, **placing** a custom creature
already works (it's just a model id in `crtr_conf`).

---

## F16 — game-start dispatch: one `case` in `kfx_apploop`; ImGui callback fires in-game (refines F11)

**How a game actually starts** ([`game_session_loop.cpp`](../../../src/kfx_apploop/src/game_session_loop.cpp)):
`frontend_update()` sets `*finish_menu = 1` for the transient states
`FeSt_START_KPRLEVEL` / `START_MPLEVEL` / `LOAD_GAME` / `PACKET_DEMO`
([`frontend.cpp:3927`](../../../src/kfx_frontend/src/frontend.cpp)); the menu loop exits and a
`switch (prev_state)` at [`game_session_loop.cpp:864`](../../../src/kfx_apploop/src/game_session_loop.cpp)
dispatches to `startup_network_game` / `load_game` / `startup_saved_packet_game`.

So the editor needs **two frontend states** and **one `kfx_apploop` case**:
- `FeSt_EDITOR` — the browser (its own `frontgui_editorbrowser_frame()`, does not set
  `finish_menu`).
- `FeSt_START_EDITOR` — transient: add to the `*finish_menu = 1` group in `frontend_update`,
  and add `case FeSt_START_EDITOR: startup_local_game_for_editor(&loop, target);` to the
  post-loop `switch` in `game_session_loop.cpp`. The browser's Open/New button stashes the
  target (level number + `is_new`) and `request_frontend_state(FeSt_START_EDITOR)`.

**Correction to F11:** `kfx_apploop` gets **one `case` label** calling one `kfx_game` function
— not literally zero, but not integration either. It does *not* `#include kfx_editor`
(`startup_local_game_for_editor` is in `kfx_game`); `simulation_suspended` is a `kfx_sim` flag
it can already set. `kfx_editor`'s rank (above `kfx_apploop`) is unaffected.

**ImGui submission is confirmed to run in-game:** `RendererSoftware::PresentFrame()`
([`RendererSoftware.cpp:185`](../../../src/kfx_platform/src/renderer/RendererSoftware.cpp))
calls `RendererRunImGuiFrameCallback()` on **every present**, gated only on
`RendererImGuiEnabled()` (session-global). So `main.cpp`'s wrapper — `app_imgui_frame(){
FrontendImGuiFrame(); editor_frame(); }` — runs the editor's frame during gameplay, and
`editor_frame()` no-ops via `editor_is_active()` when not editing.

---

## F17 — editor tool params must NOT extend `CheatSelection` (blob); they travel in packet params

`struct CheatSelection` (`chosen_terrain_kind`, `chosen_player`, `chosen_creature_kind`,
`chosen_hero_kind`, `chosen_experience_level`) is a member of `struct PlayerInfo`, which is
`struct PlayerInfo players[PLAYERS_COUNT]` **inside `kfx_sim_state`**
([`kfx_sim_state.h:235`](../../../src/kfx_sim/include/kfx_sim_state.h)) — i.e. inside the raw
blob that save-games / network resync / level-reset `memcpy` wholesale (architecture.md §6.2).
**Adding fields to `CheatSelection` is an on-disk/on-wire layout change** needing a migration
story.

So phase 2 must **not** "add fields to `cheatselection`":
- The **existing** `CheatSelection` fields are reused as-is to drive the **existing** `PSt_*`
  cheat modes (terrain, creature, hero, digger — set via `PckA_SetPlyrState` +
  `PckA_CheatSwitch*` today).
- Every **new** editor tool param (object model, trap/door kind, light intensity/radius/height,
  AP radius, fill target, eyedropper result) lives in **`kfx_editor`'s own state** and travels
  to its new `PckA_Editor*` handler in the **packet params**: `actn_par1` (i32), `actn_par2`
  (i32), `pos_x` (i32), `pos_y` (i32), `actn_par3` (i16) — `struct Packet`
  [`packet_data.h:272`](../../../src/kfx_sim/include/packet_data.h). ~5 scalar slots per action.

**Brush stamp / large fill don't fit 5 scalars.** Options (phase 2 decision D2):
(a) emit a **burst of primitive `PckA_Editor*` packets** — one per slab/thing — queued over
frames; correct, resync-clean, simple, slow for a big brush;
(b) a **documented editor-only exception**: `kfx_editor` calls the sim mutation functions
directly for brush/fill (legit because the editor session is always single-player `local` —
no resync — and `simulation_suspended`). Faster, breaks the "all mutations via packets" purity.
Lean (a); fall back to (b) only if a big-brush stamp is visibly janky.

---

## F18 — engine limits are far above classic 255 (informs `verify_map`)

`THINGS_COUNT = 12288` (8192 synced + 4096 unsynced,
[`thing_list.h:34`](../../../src/kfx_sim/include/thing_list.h)),
`CREATURES_COUNT = 1024`, `ACTN_POINTS_COUNT = 256`. The 1998 "255 creatures / 48 IFs / `u16`
thing count" ceilings are **classic-DK limits**, not KFX limits.

`verify_map()` therefore has a **target mode** (phase 3 §5, R9): "KeeperFX" (warn only near the
real engine caps) vs "classic-compatible" (ERROR at 255 creatures, 48 script `IF`s, `u16`
thing/AP counts, 8×8 columns, …). Default: KeeperFX. The thing counter in the toolbox shows
the relevant ceiling for the active target.

---

## F19 — playtest needs no snapshot mechanism: auto-save to a scratch slot

There is **no lightweight "reload current level from memory"** path — `clear_game()`
([`main_game.c:177`](../../../src/kfx_game/src/main_game.c)) only runs inside `init_level()`,
which reloads from disk.

So **Playtest = `editor_save_map()` to a scratch level number → normal
`startup_local_game_for_editor(..., /*suspend=*/false, /*full_post_init=*/true)` on that
slot** (i.e. the same startup function, minus the `GOF_Paused` + minus the `post_init_level`
trim, so the script's setup commands and `lua_on_game_start` *do* run). **"Return to editor" =
`editor_open()` on the same scratch slot** (which reloads the pre-playtest saved state).
No packet-save, no bespoke blob copy. The only cost: playtest always tests the *saved* state,
so an unsaved edit is auto-committed to the scratch slot first — which is what the user wants
anyway. (D1: confirm "auto-save-then-playtest" is acceptable vs a "save first" prompt.)

---

## Design decisions — resolved with the user

- **D1 — Playtest source. DECIDED: auto-save to a scratch slot, then playtest that** (F19). No
  "save first" prompt.
- **D2 — Brush/fill delivery. DECIDED: burst of primitive `PckA_Editor*` packets** (F17
  option a); fall back to the single-player direct-call exception only if a big stamp is janky.
- **D3 — Undo scope. DECIDED: terrain + thing place/delete in phase 2** (F6); brush/fill/area-op
  undo phase 2-if-cheap else phase 6.
- **D4 — Save bucket. DECIDED (provisional): a dedicated "Editor Maps" mappack** as the default
  save target (F4). **Revisit** when the creature-tool / per-map-config work lands and again
  with campaign-authoring support — the bucket may become "the campaign you're editing".
- **D5 — Legacy format. DECIDED: the editor must edit *and save* the classic binary format
  when the map uses no KFX-specific features.** So the classic-binary writers are a **phase-3
  deliverable, not deferred** (was O6 "indefinitely deferred"). See F20.
- **D6 — Editor keybindings. DECIDED: definable, through the existing key-config system** —
  add `Gkey_Editor*` entries to `settings.kbkeys[]`
  ([`config_settings.c:50`](../../../src/kfx_config/src/config_settings.c) — a
  `struct GameKey[GAME_KEYS_COUNT]` table: name, GUI-string, default key+mod, gamepad, visibility;
  indexed by a `Gkey_*` enum; editable in the Define Keys menu, classic + ImGui
  `frontgui_definekeys_frame`). Bump `GAME_KEYS_COUNT`; the editor reads
  `settings.kbkeys[Gkey_Editor…]` via the normal `is_key_pressed` path. Defaults follow the
  1998 manual (`F1`–`F9` tiles, `0`–`5` players, `f`/`b`/`z`, `t`, `l`, …). Not sim-blob state
  (settings file).

---

## F20 — legacy (classic binary) save is a real deliverable (implements D5)

The classic on-disk loaders **still exist** and define the byte layouts:
`load_thing_file` / `load_static_light_file` / `load_action_point_file`
([`lvl_filesdk1.c:745`/`1254`/`877`](../../../src/kfx_sim/src/lvl_filesdk1.c)) with
`struct LegacyInitThing` (0x15), `LegacyInitLight` (0x14), `LegacyInitActionPoint` (8)
([`lvl_filesdk1.c:96`](../../../src/kfx_sim/src/lvl_filesdk1.c)). Every classic writer is the
reverse of one of these:

| File | Reverse of | Notes |
|---|---|---|
| `.slb` / `.own` / `.inf` | `load_map_slab_file` / `load_map_ownership_file` / `load_and_setup_map_info` | **identical** to the KFX-native writers — these formats never changed |
| `.tng` | `load_thing_file` (`LegacyInitThing[]`, `u16` count) | classic thing params only |
| `.lgt` | `load_static_light_file` (`LegacyInitLight[]`, `u32` count) | |
| `.apt` | `load_action_point_file` (`LegacyInitActionPoint[]`, `u32` count) | + hero gates as things in `.tng` |
| `.dat` / `.clm` / `.wib` | dump the arrays `regenerate_derived_map_data()` (F2) already builds in memory | the regeneration exists for the loader path; the classic path just persists its output |
| `.lif` | `level_lif_file_parse` (`num, Name`) | classic name file (not `.lof`) |
| `.txt` | — | the script buffer verbatim (must be DK-script, not Lua) |

**`map_is_legacy_compatible()`** (part of `verify_map()`, "detect" mode) — the map saves as
classic iff **none** of these KFX-only markers are present:
- a slab kind outside the classic 0–53 set (i.e. any modded slab);
- a creature / object / trap / door model outside the classic ID ranges (any modded model);
- any thing carrying a TOML-only field (`CreatureName`, `CreatureInitialHealth`, `CustomBox`,
  effect-generator `EffectRange`, per-thing `Orientation` where classic had none);
- a `.lua` script, or a `.txt` using any KeeperFX-only script command, or >48 `IF`s, or
  flag/timer/param values outside classic ranges;
- map size ≠ 85×85; a `.slx` (multi-tileset); a texture id outside the classic range;
- a static light with a KFX-only flag; >255 creatures; thing/AP counts exceeding `u16`.

The exact marker list is a phase-3 sub-task (cross-check each against the classic loaders /
the DK1 file-format doc). Default Save = **Auto**: legacy binary if compatible, KFX-native TOML
otherwise. Save dialog has **Auto / Force KeeperFX / Force Classic** (Force Classic lists the
blocking markers if incompatible).

---

## F21 — free-fly first-person already exists: the spectator ("ghost") camera (corrects earlier note)

`level_lost_go_first_person(plyr_idx)`
([`player_instances.c:1430`](../../../src/kfx_sim/src/player_instances.c)) — the camera a
player gets when their heart is destroyed — spawns a **spectator creature**
(`get_players_spectator_model()` → `crtr_conf.spectator_breed`, model flag `CMF_IsSpectator`)
and possesses it via `create_and_control_creature_as_controller()`. The spectator flies free
in first person and is excluded from AI, traps, combat, the creature list and win/lose
(the `!flag_is_set(…, CMF_IsSpectator)` guards throughout `kfx_sim`). Triggered by
`PckA_GoSpectator` ([`packets.c:864`](../../../src/kfx_net/src/packets.c)).

**Editor "1st Person" = spawn + possess a spectator at the cursor** (a small
`PckA_EditorGoSpectator` that takes a position, instead of `level_lost_go_first_person`'s
"random owned creature's position"). Exit = the normal possession-exit (`PckA_DirectCtrlExit`).
**Back to MVP-tier wiring — not new camera work.** Needs `crtr_conf.spectator_breed` to be set
(it's a standard KFX config entry).

---

## Not-yet-checked, low-risk (flagged for the implementing phase, not blockers)

- **`GOF_Paused` halves sound/music volume** ([`packets.c:283`](../../../src/kfx_net/src/packets.c)) —
  cosmetically odd for a long editor session. `editor_frame()` can restore full volume after
  the pause is set. Trivial.
- **Atomic save** needs a file-rename primitive in `kfx_platform` (only `LbFileSaveAt` /
  `LbFileLoadAt` were confirmed). If none exists, write-in-place with a `.bak` copy first.
- **TOML string escaping** for level/creature names containing `"` or newlines — a 10-line
  concern in `write_tngfx` / `write_lof`.
- **Roomspace box size cap** — `create_box_roomspace` is built for room placement; confirm it
  accepts arbitrary large rectangles for the Mark tool, or the Mark tool tracks its own rect.

---

## Summary: 00-overview §7 status

| Q | Was | Now |
|---|---|---|
| O1 derived files | "leaning regenerate" | **Resolved (F2): regenerate. `place_single_slab_type_on_map` + `initialise_map_wlb_auto` already exist. New code = one loader branch. Spike S1 downgraded to validation.** |
| O2 save target / discoverability | open | **Resolved (F4): write `.lof` (KIND=SINGLE/MULTI) into `FGrp_CmpgLvls`; auto-discovered; no `levels.txt`. Ship an "Editor Maps" mappack as the default bucket.** |
| O3 one binary | "assumed yes" | **Resolved (F7): yes. Runtime-gated internal library, no define, no separate exe.** |
| O4 undo/redo | open | **Resolved (F6): feasible via client-side before-snapshots; "sim state is the map" (safe under `simulation_suspended`). Ship terrain+thing undo in phase 2.** |
| O5 MP co-op | out of scope | unchanged — out of scope v1 |
| O6 classic export | "KFX-native only v1" | unchanged — KFX-native only v1; classic `.tng`/`.lgt`/`.apt`/`.dat`/`.clm` export is a post-MVP "Export" item |
| O7 procedural gen | open | **Resolved (F8): no generator exists and none wanted (user decision). Closed.** |
| (phase 4) wall height / Plan view | "new renderer work, post-MVP" | **Resolved (F9): both already exist — `video_cluedo_mode` (in-game video menu) for low walls, parchment `draw_zoom_box` for top-down. Editor just wires toggles. No renderer dependency.** |
| (implicit) sim suspension | "thorough audit needed" | **Resolved (F1): `GOF_Paused` already gates the whole turn loop and still runs packets + rendering. `simulation_suspended` = force-and-lock that flag. R2 substantially de-risked.** |
| (phase 1 §4) startup reuse | "if it can't be re-entered…" | **Resolved (F5): reuse `init_level()` + `setup_zombie_players()` + a trimmed `post_init_level`; one sanctioned `kfx_game` function `startup_local_game_for_editor`.** |
| (phase 3 S2) TOML schema | spike | **Resolved (F3): all three schemas documented from the loaders. Hand-emit, no library. Hero gates are `tngfx` objects, not `aptfx`.** |
| (phase 1 §3) cheat-enable step | assumed `PckA_CheatEnter` | **Resolved (F10): no cheat-mode gate exists at all. Editor just sends `PckA_Cheat*`, no handshake.** |
| (phase 1 §0/§5) `kfx_editor` rank + apploop edit | "above kfx_script, wire into kfx_apploop" | **Resolved (F11): rank *above kfx_apploop*, only `main.cpp` calls in. Per-frame hook = `main.cpp` wraps the existing `RendererSetImGuiFrameCallback`. `kfx_apploop` untouched.** |
| (phase 1 §5 / phase 3) map size | "default 85×85" | **Resolved (F12): fully resizable via `.lof` `MAPSIZE`; loaders + writers are size-agnostic if they use `map_tiles_x/y`. Resizing a *populated* map still deferred.** |
| (O2 / F4) `.lof` scan timing | "auto-discovered" | **Refined (F13): scan runs at campaign load only. Editor re-calls `find_and_load_lof_files()` after first Save.** |
| (phase 3 §6) Open-dialog thumbnail | assumed | **Resolved (F14): `land_preview_build_minimap()` reads `.slb`/`.own` without a full level load — purpose-built for this.** |
| custom creatures / objects / texture packs | "palette from config" | **Resolved (F15): all palettes are config-count loops → mods appear automatically. Texture: base `.inf` (1 byte) + per-slab `.slx` (byte grid — was missing from the writer list). Per-map creature `.cfg` editing deferred to v2.** |
| (phase 1) game-start plumbing | "coroutine-staged like faststartup" | **Resolved (F16): `FeSt_EDITOR` browser + `FeSt_START_EDITOR` transient; one `case` in `game_session_loop.cpp`'s post-menu `switch`. ImGui callback confirmed to fire in-game.** |
| (phase 2) editor tool params location | "populate `cheatselection`" | **Resolved (F17): `CheatSelection` is in the `kfx_sim_state` blob — DON'T extend it. New tool params live in `kfx_editor`, travel in packet params. Brush/fill = burst packets (D2).** |
| (phase 3 §5) verify limits | "≤255 / ≤48" | **Resolved (F18): engine caps are 12288 things / 1024 creatures / 256 APs. Classic ceilings apply only in "classic-compatible" target mode.** |
| (phase 1 §6) playtest reset | "in-memory snapshot the reset path consumes" | **Resolved (F19): no such path — Playtest = auto-save to scratch slot + normal startup; Return = `editor_open()` on the scratch slot.** |
| (O6 / D5) legacy binary save | "deferred indefinitely" | **Reversed → phase-3 deliverable (F20): classic `.tng`/`.lgt`/`.apt`/`.dat`/`.clm`/`.wib` writers (reverse of the still-present classic loaders) + `map_is_legacy_compatible()`. Save = Auto (legacy if no KFX-only features).** |
| (phase 4) 1st-person camera | "new camera work, post-MVP" | **Resolved (F21): reuse the spectator/"ghost" camera (`level_lost_go_first_person` / `PckA_GoSpectator` / `CMF_IsSpectator`). Editor variant spawns it at the cursor. MVP-tier wiring.** |
