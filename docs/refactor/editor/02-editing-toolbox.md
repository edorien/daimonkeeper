# Phase 2 — the editing toolbox

See [`09-toolbox-remainder.md`](09-toolbox-remainder.md) for the current consolidated punch
list (scoped-down first slices + not-started items), rather than reconstructing it from the
blow-by-blow status notes below -- every tool works, but several things called for in this doc
are narrower than originally scoped or not started at all.

Status: **first slice implemented** (`src/kfx_editor/src/editor_toolbox.cpp` +
`editor_toolbox.h`), covering §5 items 1-4 only: the toolbox panel itself
(tool strip + picker + bottom bar, shown whenever `editor_is_active()`),
terrain/room paint with a palette generated from `kfx_config_state.conf.slab_conf`,
the player (P0-3/Neutral) + experience (1-10) bottom bar, and creature/hero/digger
placement with a model-grid picker from `crtr_conf`. Confirms the doc's own §1 claim
in practice -- every tool here is *only* `set_players_packet_action()` calls onto
already-live `PckA_SetPlyrState`/`PckA_CheatSwitchTerrain`/`PckA_CheatSwitchPlayer`/
`PckA_CheatSwitchCreature`/`PckA_CheatSwitchHero`/`PckA_CheatSwitchExperience` handlers
(`packets_cheats.c`) -- all of which turned out to be **direct setters** keyed off
`pckt->actn_par1`, not the cycle-only inputs §1 assumed; the classic cheat menu just
happens to only ever call them with a precomputed next/prev value. No new packet verbs,
no new `kfx_sim`/`kfx_net` code needed for this slice.

**Selection feedback added after live testing**: none of the palette/bottom-bar picks had any
visual "selected" indication, which made a working click indistinguishable from a no-op --
found live when the user reported terrain placement "not working" via the toolbox. Root-caused
via `packets_cheats.c`: painting is genuinely a single click-driven `PckA_CheatPlaceTerrain`,
sent from `front_input.c` on `PCtr_LBtnRelease`, keyed off `ustate->cheatselection.
chosen_terrain_kind` -- exactly what `PckA_CheatSwitchTerrain` (the toolbox's palette click) sets.
No bug found in that path; the far more likely explanation is that a New Map canvas starts 100%
`SlbT_ROCK`, and painting *more* rock over rock (the engine default `chosen_terrain_kind` before
ever picking something else) is correct behaviour that's simply invisible. Added: a local shadow
of the current terrain/creature/hero/owner/level selection in `editor_toolbox.cpp`, used to
highlight the active pick (`FeListRow`'s own `selected` param for the palettes;
bracketed labels, e.g. `[P0]`, for the bottom bar's plain `FeButton`s, which have no built-in
highlight state). Note for testing: a `PckA_SetPlyrState` (tool switch) and a
`PckA_CheatSwitchTerrain`/`_Creature`/`_Hero` (palette pick) can't be sent in the same click --
only one action fits in the per-turn packet slot, so sending both back-to-back would silently
clobber the first with the second. This mirrors the classic cheat menu's own one-action-per-click
shape (`gf_change_player_state`), not a workaround.

**Confirmed live**: single-slab terrain placement works (the invisible-rock-on-rock theory was
it). **Drag placement of terrain does not** -- expected, not a bug: `PSt_PlaceTerrain`'s per-
frame cursor code (`packets_cheats.c`) always calls `create_box_roomspace(player->render_roomspace,
1, 1, slb_x, slb_y)` -- a fixed 1x1 box, not one that grows with a held drag -- and the actual
paint only fires once, on `PCtr_LBtnRelease`. Multi-tile drag painting is exactly what §2.2
("Marking (rectangular select) + area ops") is for -- reusing the roomspace drag machinery this
single-tile tool doesn't -- and hasn't been built yet. User's own read on this
("likely not enabled for terrain, as was originally done for room placement") matches: room
placement's drag support is the existing machinery §2.2 plans to extend to terrain/marking
generally, not something `PSt_PlaceTerrain` itself already has.

**Drag/multi-tile terrain painting explicitly tracked as the next step (user's ask)**: §2.2
("Marking") is the right place for it -- reusing room placement's existing roomspace-drag
machinery for terrain too. Sequenced after creature placement is fixed (below), still ahead of
objects/traps/doors in this doc's own §5 ordering, since the user asked for it specifically.

**Creature placement: narrowed down, not yet root-caused.** The `keeperfx.log` from a real
attempt (BARBARIAN/BUG models, indices 19-22 and 24) shows `create_creature()` succeeding
cleanly -- `set_start_state_f` transitions each straight to `CreatureDoingNothing`, no
`ERRORLOG`/`ERRORDBG` anywhere near those lines. So the whole dispatch chain (`tag_cursor_
blocks_place_thing()` -> `PckA_CheatMakeCreature` -> `create_creature()`) genuinely works --
**the creature exists** (confirmed independently: visible on the minimap/overview immediately)
**but never renders in the 3D view.** Existing creatures already on the level (e.g. the level's
own starting Imps) render fine in the same session, so this isn't a blanket "nothing renders"
problem -- it's specific to things created *after* the editor session (and its
`simulation_suspended` freeze) is already active.

Leading theory: this is the same shape of bug as the `PI_HeartZoom` intro from
01-entry-and-editor-session.md -- something about a newly-created thing needs at least one real
`update()` pass to be fully wired up for rendering (the pre-existing Imps got that pass during
normal level startup, before the freeze ever engaged; anything created afterward never does).
Not confirmed -- `keeperfx.log` at the current `SYNCDBG` level doesn't have per-thing rendering
trace to pin this down further from logs alone.

**Added the "Preview Motion" affordance** 01-entry-and-editor-session.md §3 already planned (a
checkbox in the Esc/F10 editor menu, `editor_session.cpp`) -- while checked,
`simulation_suspended` is left off instead of being re-forced every frame, so a real turn
actually runs. **Confirmed live: toggling it on made the invisible creature appear.** But the
user immediately (and correctly) flagged this as unusable as the *only* fix: Preview Motion
unfreezes everything, including creature AI -- placed creatures belonging to different players
would start fighting each other the moment you toggle it, which is exactly the kind of thing an
editor session exists to prevent. Preview Motion stays as a real, deliberately-opt-in feature
(checking idles/lava/FX motion) -- it is not the placement fix.

**Root-caused and fixed properly**: `update_thing_interpolation()` (`thing_list.c`, called once
per thing per real game turn from inside `update_things_in_list()`) is what primes
`thing->previous_mappos` from its zeroed post-allocation state to the thing's actual position.
On a frozen sim, no turn ever runs, so anything created after the freeze engaged keeps
`previous_mappos == (0,0,0)` forever -- and the renderer, interpolating from that degenerate
baseline, never draws it, even though the thing exists in every other respect (immediately on
the minimap, no creation errors). Fix (`thing_creature.c`): `create_creature()` now primes
`previous_mappos` (and clears `TF1_Teleported`) itself, right after `mappos` is finalized --
exactly what `update_thing_interpolation()` would do on the thing's first real turn regardless
of whether the sim is frozen, so this is correct in normal (non-frozen) play too, not an
editor-only hack; it just closes a one-frame gap that's imperceptible outside a frozen session.
`create_owned_special_digger()` (which calls `create_creature()` internally, then re-corrects
`mappos`'s Z height afterward without re-syncing `previous_mappos` to match) got the same
one-line re-sync after its own correction.

**Confirmed live: Digger now places visibly; raw Creature/Hero placement (`PckA_CheatMakeCreature`)
still doesn't.** Found the actual difference between the two routes: `PckA_CheatMakeCreature`'s
handler (`packets_cheats.c`) never sets `pos.z.val` at all before calling `create_creature()` --
only `pos.x.val`/`pos.y.val`. `pos` is a shared local reused across many `case`s of the same
switch, so `z.val` came out as whatever an earlier case happened to leave on that stack slot --
undefined, and evidently not "close enough to the real floor height" to render, unlike
`create_owned_special_digger()`, which explicitly sets `z.val = 0` before its own
`create_creature()` call and then corrects it afterward via `get_thing_height_at()`. This is a
pre-existing engine bug (the classic cheat menu's "Make Creature" goes through the exact same
handler), not something the editor introduced -- just newly exposed by more deliberate/repeated
testing. **Fix**: `PckA_CheatMakeCreature`'s handler now follows the same two-step pattern
Digger already used -- explicit `pos.z.val = 0` before creation, then
`thing->mappos.z.val = get_thing_height_at(thing, &thing->mappos)` (and the matching
`previous_mappos` re-sync) right after. **Confirmed live -- both Creature and Hero placement
now work.**

**Drag/multi-tile terrain painting, first slice (§2.2)**: `PSt_PlaceTerrain`'s placement gate
in `packets_cheats.c` fired only on `PCtr_LBtnRelease` (one tile per click); now also fires on
`PCtr_LBtnHeld`, so holding the button and dragging paints every tile the cursor crosses, like
a normal paint-tool brush. Deliberately the simpler half of what §2.2 eventually wants (a
`PCtr_MapCoordsValid`-gated stream of individual placements as the cursor moves, not yet the
fuller "mark a rectangle, apply one value to the whole thing in a single batched/undoable
action" design that reuses room placement's roomspace-drag machinery) -- room placement's own
gold-cost/roomspace-cost tracking has no equivalent for raw terrain and wasn't pulled in.

**Confirmed live -- works, but user correctly distinguished it from room placement's own drag
behaviour** in the main game: room placement drags out a rectangular N×N region and commits the
whole region as one action on release (via the roomspace/`keeper_build_roomspace` machinery
above); what's implemented here paints each tile individually as the cursor crosses it during
the hold, which is a different feel (continuous brush vs. a defined rectangle). **Both are
wanted** -- tracked as a toggle to add in the toolbox once work reaches the Brush tool (§2.4)
near the end of this phase, not blocking anything now: a "Rectangle" mode reusing the roomspace-
drag rectangle math (without room placement's gold-cost logic) alongside the current continuous
"Brush" mode already built.

**Also observed**: Query (`PSt_QueryAll`) opens a classic (non-ImGui) popup. Expected, not a
new bug -- the creature-query screen hasn't been migrated to ImGui by the separate ingame-gui
project yet; the editor's Query tool just reaches an existing, still-legacy screen.

**Not started**: items 5-11 (objects/spellbooks/gold, traps/doors, marking+area-ops,
flood fill, brush grab-and-stamp, query inspector/eraser/eyedropper as real tools rather
than a bare work-state switch, the command journal/undo-redo) and the definable-keybinding
work in §3. No ftest/Catch2 coverage (§6) either. Depends on phase 1 (editor session +
`editor_is_active()`), which is done.

Goal: a proper editor UI over the player-work-state machine — a tool palette, category pickers,
a "what am I placing" cursor preview, and the marking / brush / fill mechanics — so building a
map is a paint-program experience, not a cheat-menu radio list.

---

## 1. The model: tools = player work-states, driven by a better UI

Every tool below already has a `PSt_*` work-state and a `PckA_Cheat*` packet handler in
[`packets_cheats.c`](../../../src/kfx_net/src/packets_cheats.c). Phase 2 does **not** add new
world-mutation logic for the common cases — it adds:

1. A **tool palette** (ImGui panel, `frontgui_widgets` wrappers) that sets
   `player->work_state` via `PckA_SetPlyrState` (exactly what `gf_change_player_state` in
   `gui_boxmenu.c` does) and shows the active tool.
2. **Category pickers.** The **existing** `player->cheatselection` fields
   ([`player_data.h:138`](../../../src/kfx_sim/include/player_data.h) — `chosen_terrain_kind`,
   `chosen_player`, `chosen_creature_kind`, `chosen_hero_kind`, `chosen_experience_level`) are
   set as-is to drive the existing `PSt_*` cheat modes — the pickers just replace the
   `PckA_CheatSwitch*` cycle-only inputs with grids/lists. **New** tool params (object model,
   trap/door kind, light intensity/radius/height, AP radius, fill target, brush buffer) live
   in `kfx_editor`'s own state and travel in the packet params of new `PckA_Editor*` verbs —
   **not** as new `CheatSelection` fields ([`07`](07-investigation-findings.md) F17:
   `CheatSelection` is inside the `kfx_sim_state` save/resync blob).
3. A **shared bottom bar**: player selector (0–3 / Hero / Neutral) and experience level (1–10),
   mirroring the original editor's always-visible bottom icons. These feed `cheatselection`
   for whichever tool is active.
4. **Cursor feedback**: reuse `tag_cursor_blocks_place_terrain()` / roomspace highlight; add a
   ghost sprite / slab preview under the cursor for the current tool.

## 2. Tool list

### 2.1 Terrain / Room / Ownership paint
- **Tool:** `PSt_PlaceTerrain` (already paints any `SlbT_*` with an owner and deletes rooms on
  overwrite).
- **Picker:** a slab-kind palette grouped as the original panels were — *Tiles* (earth, rock,
  impenetrable, gold, dirt path, claimed floor, pretty path/wall, lava, water, gems) and
  *Rooms* (every `RoomKind` → its `assigned_slab`, incl. atomic 3×3 Portal & Heart). Source of
  truth: `kfx_config_state.conf.slab_conf` and `room_conf` — the palette is generated, so new
  modded slab/room kinds appear automatically.
- **RMB** = paint earth (already the handler's behaviour).
- **Atomic rooms:** Portal / Heart place as 3×3; delete only (RMB with a room/tile tool),
  confirm dialog — same rules as the original. `place.slab` already special-cases
  `RoK_DUNGHEART`.
- **`r` — reinforce perimeter:** for the selected player, convert their dungeon's earth
  perimeter to reinforced wall. New small helper in `kfx_sim` (walk owned slabs, fortify
  bordering earth) exposed as `PckA_CheatReinforcePerimeter`, or a console-style loop. Original
  shortcut `r`.

### 2.2 Marking (rectangular select) + area ops
- Reuse the roomspace drag machinery (`PckA_SetRoomspaceDrag` / `create_box_roomspace` /
  `RoomspaceHighlightToggle`). `Ctrl+drag` (or a "Mark" toggle) draws a rectangle; then a
  single click of a terrain/room/ownership value applies it to the whole rectangle — one packet
  carrying the rect + value, handled like a batched `PckA_CheatPlaceTerrain`.
- Area ops on a marked rect: **set owner**, **fill with slab**, **clear to earth**, **delete
  things inside**.

**"Fill with slab" and "Clear to Earth" implemented; "set owner" and "delete things inside"
deferred.** Didn't reuse the roomspace-drag machinery above after all -- see §2.4's own status
note (Terrain Rectangle mode, which *is* "fill with slab" on a marked rect) for why: that
system is tightly coupled to digging/gold-cost/highlight-mode concerns a raw area op has no use
for. "Clear to Earth" reuses the exact same drag-tracking the Rectangle mode built (factored out
into a shared `editor_rect_drag_update()` helper in `packets_cheats.c` once there were two
consumers) via a new `PSt_EditorRectClearEarth` work state + `PckA_EditorRectClearEarth` verb --
fixed target (`SlbT_EARTH`/neutral) instead of the picker's current selection, applied via a
second shared helper, `editor_apply_slab_rect()` (also now used by Rectangle mode's own
handler). Toolbox: the Brush/Rectangle toggle became a 3-way Terrain mode row (Brush/Rectangle/
Clear Earth); the palette hides itself in Clear Earth mode since there's no kind to pick.

"Set owner" deferred: re-applying a slab's *own* kind with a new owner via the same
`place_slab_type_on_map()` primitive Paint/Clear Earth use is fine for plain terrain, but a
slab that's currently part of a *room* needs a real room-ownership transfer, not a raw
kind-rewrite -- rooms carry their own dungeon-tracking state beyond the slab's `owner` byte, and
blindly re-deriving via `delete_room_slab()` + `place_slab_type_on_map()` (Paint's own approach
for a *kind change*) would destroy and rebuild the room just to reassign it, which is likely
correct-by-accident at best. Needs a dedicated per-room-aware path, not investigated yet.

**"Delete Things Inside" implemented.** Needed the genuinely different mechanism flagged above:
`editor_delete_things_in_rect()` (`packets_cheats.c`) walks every subtile in the box's own
mapwho linked list -- the same `get_mapwho_thing_index()`/`next_on_mapblk` traversal
`find_base_thing_on_mapwho()` already does for one subtile, generalized to sweep the whole area
-- and deletes whatever it finds via the eraser tool's own per-class dispatch
(`destroy_door()`/`destroy_effect_thing()`/`destroy_object()`, same split `PSt_DestroyThing`
already uses). One deliberate exception: skips the Dungeon Heart specifically -- losing it via
a big careless box-drag would be a much harder mistake to recover from than any other thing this
op might catch. New `PSt_EditorRectDeleteThings` work state + `PckA_EditorRectDeleteThings`
verb reuse the exact same corner-carrying packet shape and `editor_rect_drag_update()` helper as
Clear Earth. Toolbox mode row is now 4-way (Brush/Rectangle/Clear Earth/Delete Things); palette
hidden for both Clear Earth and Delete Things, since neither has a kind to pick.

**"Set owner" implemented, including real room-ownership transfer.** All four area ops from
this section's own list are now done. First pass skipped room slabs entirely, treating room
ownership transfer as unsolved; user pushed back and supplied the fix directly: `delete_room_slab()`
first (the exact same call Paint/Clear Earth already make for a *kind* change -- it properly
tears the room slab down to ground *and* updates the old owner's room-area accounting/slab
list, not just the raw slab), then `place_slab_type_on_map()` with the *same* kind but the
*new* owner, which rebuilds it as a room the new owner actually owns. `editor_set_owner_rect()`
(`packets_cheats.c`) now does exactly this: reads the slab's own current kind (`get_slabmap_block()`),
calls `delete_room_slab(sx, sy, true)` first only when the slab is currently a room
(`subtile_is_room()`), then `place_slab_type_on_map(kind, ..., new_owner, 0)` unconditionally.
Ownerless kinds (rock, gems, lava, ...) are still skipped, same check `PckA_CheatSwitchTerrain`
uses elsewhere. Owner comes from the bottom bar's existing `chosen_player` selector -- no new
picker needed, which is also why the terrain palette itself is hidden in Set Owner mode (kind
picker would be misleading; owner selector stays visible since the bottom bar renders
unconditionally). New `PSt_EditorRectSetOwner` work state + `PckA_EditorRectSetOwner` verb,
same shape as the other three area ops. Toolbox mode row is now 5-way (Brush/Rectangle/Clear
Earth/Delete Things/Set Owner).

Not yet live-confirmed.

### 2.3 Fill (flood)
- New `PSt_EditorFill` + `PckA_EditorFloodFill` carrying `pos_x/pos_y` (seed) + `actn_par1`
  (slab kind) + `actn_par2` (owner) — all fits one packet. The **handler** does the 4-connected
  flood from the seed across contiguous same-kind, same-unclaimed-ness slabs, bounded by the
  map and by rooms (original refuses to flood rooms). Area-capped (whole map max).

**Implemented.** `PSt_EditorFill` appended to `enum PlayerStates` (`config_players.h`) and
`PckA_EditorFloodFill` to `enum TbPacketAction` (`packet_data.h`) -- both appended after the
last existing entry, not inserted among the numbered ones, since these values are what
savegames/`.pck` replay files actually store. `packets_cheats.c`: the `PSt_EditorFill`
work-state case reuses the Terrain tool's own `chosen_terrain_kind`/`chosen_player` selection
(no new selection state needed, release-only so one fill per click) and sends
`PckA_EditorFloodFill`; its handler runs an iterative BFS (`editor_flood_fill_terrain()`, a
static helper -- not recursive, uses map-sized static queue/visited buffers so a large flood
can't stack-overflow) that floods 4-connected slabs of the *seed's own* original kind,
refusing rooms as a boundary, calling `place_slab_type_on_map()` per flooded tile (same
mutation path Terrain itself uses). Toolbox: a new "Fill" entry in the tool strip
(`editor_toolbox.cpp`) reuses `draw_terrain_picker()` verbatim. Not yet live-confirmed.

### 2.4 Brush (grab region → stamp)
- Right-drag a rectangle to **capture** into an in-editor clipboard: slab kinds + ownership +
  things + lights + APs within the box (client-side buffer in `kfx_editor`, not world
  state).
- LMB **stamps** the buffer at the cursor as a **burst of primitive `PckA_Editor*` packets**
  (one per slab / thing — the buffer can't fit in one packet's ~5 scalar params,
  [`07`](07-investigation-findings.md) F17). Queued over frames if large. Fallback if a big
  stamp is visibly janky: `kfx_editor` calls the sim mutations directly (single-player-local
  exception, D2). The undo journal records one "stamp" entry regardless.
- Original restrictions to keep: can't stamp over a Heart or Portal; Gems / Guard Post /
  Bridge don't survive a grab (engine-derived slabs).
- **Terrain rectangle-region mode (user ask, phase 2 first-slice follow-up):** the Terrain
  tool's current LMB-drag paints continuously, tile-by-tile, as the cursor crosses each one
  (`packets_cheats.c`'s `PSt_PlaceTerrain`, `PCtr_LBtnHeld`-gated) -- distinct from, and wanted
  alongside, room placement's own drag behaviour of marking out a rectangular N×N region and
  committing the whole region as one action on release. Add a toolbox toggle here between
  "Brush" (current, continuous) and "Rectangle" (drags out a box via the same roomspace-drag
  rectangle math room placement uses, applies the chosen slab kind to the whole box on
  release) -- reuse the geometry, not room placement's gold-cost logic, which doesn't apply to
  raw terrain.

**Rectangle mode implemented** (the general Brush capture/stamp/clipboard feature above it is
still future work -- this is only the specific follow-up the user asked for). Ended up *not*
reusing room placement's roomspace-drag machinery after all: that system (`packets_input.c`'s
`PSt_BuildRoom` path -- `get_dungeon_highlight_user_roomspace`, `check_roomspace_for_diggable_slabs`,
`apply_roomspace_dig_tag_selection`, `drag_mode`, etc.) is tightly coupled to
digging/gold-cost/highlight-mode concerns that don't apply to raw terrain, and untangling "just
the geometry" from it risked exactly the kind of subtle bug the original note above was already
worried about. Built a small self-contained mechanism instead:

- New `PSt_EditorPlaceTerrainRect` work state (separate from `PSt_PlaceTerrain`, since Brush and
  Rectangle need genuinely different `PCtr_LBtn*` handling shapes -- paint-every-Held-frame vs.
  track-then-commit-once-on-Release) and `PckA_EditorPlaceTerrainRect` packet verb, both
  appended per the usual never-renumber rule.
- Toolbox: a "Brush"/"Rectangle" toggle above the Terrain picker only (not shown for Fill, which
  reuses the same picker but has no rectangle mode); switching modes re-sends
  `set_work_state()` immediately, and the Terrain tool-strip button itself now sends whichever
  of the two states the toggle currently selects.
- `packets_cheats.c`'s new `PSt_EditorPlaceTerrainRect` case tracks the drag: records the
  start subtile in a file-scope static (`s_rect_drag_stl_x/y` -- transient per-drag bookkeeping,
  not a persisted selection, so a `UserState` field felt like the wrong home for it) on
  `PCtr_LBtnClick`, draws a live preview box via `draw_map_volume_box()`/`floor_height_for_volume_box()`
  (the same primitive `tag_cursor_blocks_place_trap`/`_door` call internally -- no need for the
  full roomspace apparatus just to draw a box) on every `Held` frame, and on `Release` sends
  `PckA_EditorPlaceTerrainRect` with the drag-start corner in `actn_par1`/`actn_par2` (int32,
  same "needs full range" reasoning as `PckA_EditorPlaceObject`) and slab kind/owner in
  `actn_par3`/`actn_par4` -- the release corner itself travels as the packet's own `pos_x`/`pos_y`,
  which `set_packet_action()` leaves untouched, so all 6 of the packet's scalar slots together
  carry both corners plus kind and owner in one packet.
- The handler applies the chosen kind to every slab in the resulting box with the same
  per-tile mutation `PckA_CheatPlaceTerrain` already uses for one tile (`place_slab_type_on_map`/
  `place_animating_slab_type_on_map` + `do_slab_efficiency_alteration`), deleting any room slab
  first exactly like `PSt_PlaceTerrain`'s own single-tile path does.

Not yet live-confirmed.

**Brush (grab region → stamp), first slice implemented — terrain slabs only, no things/lights/
APs capture yet.** New "Stamp" tool in the tool strip (named "Stamp" in the UI to avoid
colliding with the Terrain tool's own "Brush" *mode* above, a completely different thing --
doc references here still say "Brush" per this section's own numbering). Unlike every other
tool this phase, capture and stamp both happen **entirely client-side in `kfx_editor`**
(`handle_brush_capture_and_stamp()`, `editor_toolbox.cpp`), calling sim mutation primitives
(`place_slab_type_on_map()` etc., `kfx_sim`) **directly** rather than through a packet -- this
is the doc's own sanctioned fallback above (D2, single-player-local exception) for when the
buffer can't fit in one packet's params. Went straight to the direct-call fallback rather than
attempting the "burst of packets, queued over frames" design first: `set_players_packet_action()`
overwrites the single per-turn packet slot (documented repeatedly elsewhere in this codebase),
so safely queuing a multi-item burst needs a real turn-boundary signal kfx_editor doesn't have
-- dispatching one queued packet per *render* frame risks the same same-turn clobber Eyedropper's
own bug already demonstrated, since multiple render frames can land inside one logic turn.
Calling the mutations directly sidesteps that entirely (same "read/mutate world truth directly
from kfx_editor" precedent `screen_to_map()`/`handle_eyedropper_click()` already established).

- **Capture**: RMB-drag draws a live preview box (same `draw_map_volume_box()`/
  `floor_height_for_volume_box()` primitives the Rectangle-family tools use), and on release
  reads every slab in the box directly (`get_slabmap_block()`/`slabmap_owner()`) into a local
  `std::vector<BrushSlabEntry>` (`{dx, dy, kind, owner}`, relative to the box's own top-left).
  Skips `SlbT_BRIDGE`/`SlbT_GEMS`/`SlbT_GUARDPOST` per the doc's own "Gems/Guard Post/Bridge
  don't survive a grab" restriction (engine-derived slabs, not meaningfully re-stampable).
- **Stamp**: LMB click re-applies the captured buffer anchored at the cursor's current slab,
  looping the exact same per-tile mutation `editor_apply_slab_rect()` uses (delete any room
  slab first, animated-kind check, `place_slab_type_on_map`/`place_animating_slab_type_on_map`,
  `do_slab_efficiency_alteration`) -- except skipping any target tile that's currently
  `SlbT_DUNGHEART`/`_WALL` or `SlbT_ENTRANCE`/`_WALL`, per the doc's "can't stamp over a Heart
  or Portal" restriction.
- A new minimal `PSt_EditorStamp` work state exists only so a stray click doesn't fall through
  to whatever tool was active before (same reason `PSt_EditorPlaceObject`/`PSt_EditorEyedropper`
  exist) -- no dispatch of its own, since nothing here goes through a packet.

**Deliberately deferred** (first-slice scope, matching every other tool this phase): capturing
things/lights/APs alongside slabs; the undo journal's own "records one stamp entry regardless"
design (this tool doesn't call `editor_journal->record_placement()` at all yet, since it never
creates a *thing* the existing journal knows how to journal -- a batch/compound journal entry
type would be needed for a true one-undo-per-stamp experience); a stamp-preview ghost at the
cursor before committing (only the capture-drag itself previews).

Not yet live-confirmed.

### 2.5 Creatures / Heroes / Diggers
- **Tools:** `PSt_MkBadCreatr` / `PSt_MkGoodCreatr` / `PSt_MkDigger` (all live).
- **Picker:** creature-model grid — loop `[1, crtr_conf.model_count)` (`CREATURE_TYPES_MAX =
  128`), split evil / hero, icons via `SpriteLookupCallbacks`, labels from
  `creature_code_name(model)`. Sets `cheatselection.chosen_creature_kind` / `chosen_hero_kind`.
  **Custom / campaign-modded creatures appear automatically** — they're just extra `crtr_conf`
  entries ([`07`](07-investigation-findings.md) F15). Same principle for every palette in this
  phase (objects, traps, doors, rooms, slabs).
- Bottom bar's player + experience feed owner and level.
- **255-thing cap** enforced with a visible counter (original's warning).
- RMB delete (via `PSt_DestroyThing` fallthrough or tool-specific).

### 2.6 Objects / Spellbooks / Specials / Gold / Decor
- **Tool:** `PSt_MkGoldPot` for gold; a new `PSt_EditorPlaceObject` for the rest (thin wrapper
  around `create_thing` / `PckA_CheatMakeObject` — `console_cmd.c cmd_create_object` shows the
  call).
- **Picker:** object-model grid from `object_conf` (spellbooks, dungeon specials, gold pots,
  hearts-as-object, food, decor, traps-as-boxes). `s`/`x` value tweak for gold amount,
  spellbook power, special kind — property panel, not just cycle keys.
- **Sub-tile precision:** original places one object per 3×3 sub-square at the cursor's
  sub-tile. Packet already carries `stl_x/stl_y`; keep sub-tile resolution for objects (unlike
  slabs which snap to the 3×3 slab).
- Rules: one object per sub-square; not on a square occupied by creature/trap/spell/secret;
  no gold bags in a Treasure Room (original constraints — port the checks).

**First slice implemented** -- a generic model-grid picker + click-to-place, no property panel
(gold amount/spellbook power tweak, sub-square occupancy rules, treasure-room restriction) yet.
`PSt_EditorPlaceObject`/`PckA_EditorPlaceObject` appended to their enums (same "never renumber,
only append" rule as `PSt_EditorFill`/`PckA_EditorFloodFill`). **This tool's interaction shape
is genuinely different from every tool before it**, and is the template §2.7 (Traps/Doors) and
any later new-selection-state tool should follow:

- No `CheatSelection` field exists for "chosen object model" (F17), so unlike Terrain/Creature
  there's no server-side selection for `packets_cheats.c`'s per-work-state switch to read back
  when a click arrives. `PSt_EditorPlaceObject`'s case there does *nothing but* the usual
  cursor-highlight (`tag_cursor_blocks_place_thing`) -- it exists only so a stray click doesn't
  fall through to whatever tool ran before it.
- The picker (`editor_toolbox.cpp`) only ever updates a *local* `s_selected_object_model` --
  no packet sent on selection at all.
- Placement is triggered by **kfx_editor watching for the click itself**
  (`handle_object_placement_click()`, called once per frame from `editor_toolbox_frame()`
  whenever the Object tool is active): `ImGui::IsMouseClicked` gated on `!io.WantCaptureMouse`
  (so clicking the picker list itself doesn't also place an object), then sending
  `PckA_EditorPlaceObject` directly. kfx_editor can do this because it's the highest-ranked
  library and already includes `packet_data.h`/`player_data.h`; no new callback or cross-layer
  plumbing required.
- `PckA_EditorPlaceObject`'s handler (`packets_cheats.c`) calls `create_object()`
  (`thing_objects.c`) and applies the same `previous_mappos`-priming fix `create_creature()`
  already needed -- `create_object()` itself now primes it too (never did before), closing the
  same latent "invisible until a real turn runs" gap for objects.

**Confirmed live: nothing appeared** -- correctly suspected as a repeat of the creature Z-height
bug. It partly was: the handler set `pos.z.val = 0` as a placeholder before `create_object()`
(matching `create_owned_special_digger()`'s own first step) but never corrected it afterward via
`get_thing_height_at()`. Fixed. **Retested live: still nothing appeared.** Added unconditional
`JUSTMSG` diagnostics to both `handle_object_placement_click()` and the packet handler; the log
showed every click getting past the `!io.WantCaptureMouse` guard but then `MapCoordsValid=0
pos=(0,0)` on every single attempt, and the packet handler's own log line never appeared at all
-- the packet was never sent.

**Root cause:** the original design's assumption above -- that `get_local_packet()`'s
`pos_x`/`pos_y`/`PCtr_MapCoordsValid` are "already kept current every frame ... regardless of
which work state is active" -- was wrong for *when kfx_editor reads them*. Those fields are
per-turn scratch state: `get_dungeon_control_nonaction_inputs()` populates them once during
`input()`, but `input()` runs once per logic turn (gated by `use_delta_time()`/
`process_turn_time` in `game_session_loop()`), and `exchange_packets()` (called immediately
after `input()`) resets the local packet for the next turn. `handle_object_placement_click()`
runs later in the same frame, from the ImGui render phase (`editor_toolbox_frame()`, invoked
from `gameplay_loop_draw()`) -- by then `get_local_packet()` is already the *next* turn's blank
packet. Terrain/Fill/Creature never hit this because their dispatch lives *inside*
`packets_cheats.c`'s per-work-state switch, which runs from within `input()` itself, before the
packet is reset.

**Fixed:** `handle_object_placement_click()` no longer reads the packet's position fields at
all. It recomputes the world position itself, at click time, with the same `screen_to_map()`
(`engine_redraw.h`, `kfx_render`) the input path uses internally, against the current mouse
position (`GetMouseX()`/`GetMouseY()`) and the local active camera (`get_local_active_camera()`).
The resolved position now travels explicitly in the packet: `PckA_EditorPlaceObject`'s params
were reordered to `actn_par1`/`actn_par2` (`int32_t`, x/y) + `actn_par3`/`actn_par4` (`int16_t`,
model/owner) -- the position needs the full 32-bit range (a max-size map's subtile position can
exceed `int16_t`), which the model/owner values never will. Not yet live-confirmed.

### 2.7 Traps / Doors
- **Tool:** new `PSt_EditorPlaceTrap` / `PSt_EditorPlaceDoor` (wrappers over trap/door thing
  creation; `thing_traps.c` / `thing_doors.c` have the constructors; `give.trap`/`give.door`
  show config lookup).
- **Picker:** trap-kind / door-kind grid from `trapdoor_conf`.
- Door rules: only on the selected player's claimed floor, between two walls (port the
  original's adjacency check). `create_door(pos, model, orient, plyr, is_locked)`
  ([`thing_doors.c:102`](../../../src/kfx_sim/src/thing_doors.c)) takes the lock state at
  creation; **Ctrl+LMB toggles it live** via `lock_door()` / `unlock_door()`
  ([`thing_doors.c:219`/`230`](../../../src/kfx_sim/src/thing_doors.c)) — needs a small
  `PckA_EditorToggleDoorLock` verb. Persisted as `DoorLocked` in `tngfx`.
- Traps: any land incl. unclaimed; not on an occupied square.

**First slice implemented** -- unlike Objects, this turned out to be the *simple* pattern
(same shape as Terrain/Creature/Fill), not the "kfx_editor watches for its own click" one:
`struct UserState` already has `chosen_trap_kind`/`chosen_door_kind` fields (used by the classic
workshop `PSt_PlaceTrap`/`PSt_PlaceDoor`, set via `set_player_state()`), so there's no F17 gap
here at all -- the picker can set them directly via new dedicated verbs
(`PckA_CheatSwitchTrap`/`PckA_CheatSwitchDoor`, same shape as `PckA_CheatSwitchTerrain`), and
`PSt_EditorPlaceTrap`/`PSt_EditorPlaceDoor`'s own `packets_cheats.c` dispatch (running from
within `input()`, same as every other tool except Objects) reads the packet's `pos_x`/`pos_y`
directly and sends `PckA_EditorPlaceTrap`/`PckA_EditorPlaceDoor` on release.

New work states rather than reusing `PSt_PlaceTrap`/`PSt_PlaceDoor` themselves, because those
work states' *dispatch* (`packets_input.c`) is wired to the resource-checked placement path
(`player_place_trap_at`/`player_place_door_at`, which refuse when the player has no
manufactured stock -- always true on a blank editor map). The editor verbs instead call
`player_place_trap_without_check_at()`/`player_place_door_without_check_at()` with `free=true`
directly, bypassing the workshop-inventory check entirely (same "free placement" shape
`PckA_EditorFloodFill` already established for terrain).

Owner is `ustate->cheatselection.chosen_player` (the bottom bar selector), not the packet's
literal `plyr_idx` -- consistent with every other editor tool letting you place on behalf of
any player regardless of who's actually driving the session.

Doors needed one genuine validity gate before this session's other "first slice, refinements
deferred" precedent could apply: `create_door()` indexes `doorst->slbkind[orient]` with
whatever `find_door_angle()` returns, and that's `-1` (an out-of-bounds read, not just a visual
glitch) unless the target slab is `SlbT_CLAIMED` and owned by the chosen owner. `PSt_EditorPlaceDoor`
checks `find_door_angle(stl_x, stl_y, chosen_player) != -1` itself before dispatching, rather
than reusing `tag_cursor_blocks_place_door()`'s own gate, which is keyed to the packet's actual
`plyr_idx` and also drags in fog-of-war/`is_my_player_number` visual-only gating that doesn't
fit an editor placing on behalf of an arbitrary owner. Traps have no equivalent crash risk, so
(matching Objects' own "bare click-to-place, occupancy rules deferred" first slice) no
placement-time validity check was added for them yet.

Also fixed `create_trap()` (`thing_traps.c`) and `create_door()` (`thing_doors.c`) to prime
`previous_mappos` on creation -- the same latent "invisible until a real turn runs" gap
`create_creature()`/`create_object()` already needed fixing for editor-created things.
`player_place_trap_without_check_at()`'s own z-height correction re-syncs it a second time
after, same two-step pattern as `create_owned_special_digger()`; doors need no second sync since
their z is a fixed constant, never corrected afterward.

**Ctrl+LMB door-lock toggle implemented.** `PSt_EditorPlaceDoor`'s dispatch now checks, before
its usual placement branch, whether Ctrl is held (`net_callbacks->is_key_pressed(KC_LCONTROL/
KC_RCONTROL, KMod_DONTCARE)`) and a door already exists at the clicked square
(`find_base_thing_on_mapwho(TCls_Door, 0, stl_x, stl_y)`) -- if so, sends
`PckA_EditorToggleDoorLock` (carrying the door thing's own index, not a position, since the
dispatch already resolved which door) instead of a placement action; its handler flips
`lock_door()`/`unlock_door()` (`thing_doors.c`) based on `door.is_locked`. Checked first so a
Ctrl-click on an occupied square toggles the lock rather than attempting (and likely failing
anyway) to place a second door on top.

**Deferred to a later pass**: trap occupancy validation ("not on an occupied square"), door
occupancy/wall-adjacency-quality checks beyond the bare orientation gate. Not yet live-confirmed.

### 2.8 Lights & effect emitters
- Covered in [`05-script-and-level-settings.md`](05-script-and-level-settings.md) §2 (they need
  a property panel — intensity/size/height for lights, radius for effect generators — so they
  live with the other "properties" work). The palette entry + place/delete tool stubs are
  created here so the toolbox layout is complete.

### 2.9 Action points & hero gates
- Covered in [`05`](05-script-and-level-settings.md) §3 (numbering, radius handles). Palette
  stubs here.

### 2.10 Query / Delete / utility
- **Query** (`PSt_QueryAll`): click anything → property panel for that slab/thing/light/AP.
  This is the inspector the property panels (phase 5) render into.
- **Delete** (`PSt_DestroyThing` + slab→earth for terrain): RMB in most tools; also an
  explicit eraser tool.
- **Eyedropper:** click a slab/thing → set the active tool + picker to match it (quality-of-
  life, not in the original; cheap given `cheatselection`).

**Query finalized -- no longer reaches the classic message box.** `PSt_QueryAll`'s own dispatch
(`packets_cheats.c`) calls `query_thing()`/`query_room()` for anything that isn't a creature,
and both call `create_message_box()` -- a classic (unmigrated) `GMnu_MSG_BOX` popup, jarring
inside an otherwise all-ImGui editor session. Found live: "please finalise query tool first, as
it uses the legacy gui somehow." Creature queries themselves are actually fine as-is --
`GMnu_CREATURE_QUERY1`-`4` *are* ImGui-migrated already (`frontgui_ingame_creature.cpp`'s
`creature_query_panel()`, part of the separate in-game-GUI migration project, further along
than this doc's own earlier "hasn't been migrated yet" note assumed) -- so the only real gap was
non-creature queries (objects/traps/doors/rooms/slabs).

Fix: new `PSt_EditorQuery` work state (same "kfx_editor handles the click itself, no
`packets_cheats.c` dispatch" shape Object/Eyedropper/Stamp established) replaces `PSt_QueryAll`
for the editor's Query tool. `handle_query_click()` (`editor_toolbox.cpp`) reads the
creature-or-thing-or-room at the clicked position directly (`get_creature_near()`/
`get_nearest_thing_at_position()`/`subtile_room_get()`, same precedence `PSt_QueryAll`'s own
dispatch uses) -- for a creature, calls `query_creature()` directly (a plain function, no
world mutation, so safe to call outside a packet per the same "single-player-local exception"
reasoning Brush/Stamp already used; correctly opens the already-migrated ImGui panel); for
anything else, populates a small `QueryResult` struct with the same fields `query_thing()`/
`query_room()` themselves compute (title, name, owner, health, one or two extra class-specific
lines) and renders it in a plain ImGui panel (`draw_query_inspector()`) instead of ever calling
`query_thing()`/`query_room()` at all. Not yet live-confirmed.

**Eyedropper (terrain only) implemented.** Same "kfx_editor watches for its own click, no
`packets_cheats.c` dispatch" shape Objects established -- new `PSt_EditorEyedropper` work state
(cursor-highlight only, reusing the Terrain tool's own `tag_cursor_blocks_place_terrain`) and a
`handle_eyedropper_click()` in `editor_toolbox.cpp`. Unlike Object placement, this one *reads*
rather than writes: samples the slab under the cursor directly via `get_slabmap_block()`/
`slabmap_owner()` (`kfx_sim` -- fair game from `kfx_editor`, same "read world truth directly"
precedent `screen_to_map()` already established for cursor position), updates the toolbox's own
local shadow (`s_selected_terrain_kind`/`s_selected_owner`) so the Terrain picker's highlight
jumps to match immediately, and sends the sampled kind+owner to the server in one new verb
(`PckA_EditorEyedropperTerrain`, applying both atomically to `ustate->cheatselection`) rather
than the two existing `PckA_CheatSwitchTerrain`/`PckA_CheatSwitchPlayer` verbs -- only one
action fits the per-turn packet slot per click, so picking both values needs one verb, not two.

**Confirmed live: first version broken -- "clicking with eyedropper selected defaults to
placing last selected terrain".** Root cause: that version also auto-switched back to the
Terrain work state in the *same* click, via a second `set_players_packet_action()` call
(`PckA_SetPlyrState`) right after the sample's own call. Both write through the single per-turn
packet slot documented everywhere else in this file ("only one action fits per click") -- the
second call silently clobbered the first before either was processed, so the sample never went
out at all, `work_state` flipped back to `PSt_PlaceTerrain` immediately, and the very click that
was supposed to sample instead painted with the stale previous selection. **Fixed** by dropping
the auto-switch entirely: `handle_eyedropper_click()` now only sends the one sample packet, and
the toolbox stays on the Eyedropper tool after a pick -- one extra click (Terrain/Brush/
Rectangle) to resume painting, trading a little polish for correctness under the one-action-
per-click constraint. Thing eyedropping (sample a placed creature/object/trap/door's model) not
implemented -- terrain slabs only for this first pass. Not yet re-confirmed live.

## 3. Toolbox UI layout

An ImGui dock/panel set shown only when `editor_is_active()`:

- **Left:** vertical tool strip (Terrain, Rooms, Creatures, Objects, Traps/Doors, Lights, FX,
  Action Points, Query, Brush, Fill, Mark).
- **Left, below:** the active tool's **picker** (slab grid / creature grid / …).
- **Bottom:** player selector + experience selector + thing counter + coordinate readout.
- **Right:** the **inspector** (Query results / selected-object properties).
- **Top:** the Editor menu bar (File: New/Open/Save/Save As; Edit: Undo/Redo/Clear Map;
  View: → phase 4; Playtest; Help).

Build with `FeBeginPanel` / `FeButton` / `FeBeginListBox` / `FeBeginScrollArea` etc. Icons via
the sprite-lookup callback (`SpriteLookupCallbacks`) so slab/room/creature/object art matches
the game. Follow the in-game GUI project's **deferred-action** rule for anything that transitions
player state from a click.

**Keybindings (D6).** Every tool shortcut is a **definable key** — add `Gkey_Editor*` entries
to `settings.kbkeys[]` ([`config_settings.c:50`](../../../src/kfx_config/src/config_settings.c),
`struct GameKey[GAME_KEYS_COUNT]`), each with a GUI-string label and a default from the 1998
manual (`F1`–`F9` tiles, `0`–`5` players, `f`/`b`/`z` fill/brush/paint, `t` texture,
`l` lights, `Ctrl+Z`/`Ctrl+Y` undo/redo, `Delete`, `Tab` mode, `p`/`i`/`o` views, …). They
appear in the Define Keys menu automatically (classic + ImGui `frontgui_definekeys_frame`);
the editor reads them via the normal `is_key_pressed` path. Bump `GAME_KEYS_COUNT`; these live
in the settings file, not the sim blob ([`07`](07-investigation-findings.md) D6).

**Editor menu moved off its F10 placeholder, sidestepping D6 for this one shortcut.** The menu
(Save/Save As/Playtest/Preview Motion/Exit) was reachable only via a hardcoded F10 keypress,
explicitly flagged from the start as a temporary stand-in for D6's real definable-keybinding
work (F10 was picked purely because the base game has no default binding there, to dodge a
conflict with the normal in-game pause menu's own raw-key Escape handling — not a considered
default). Per user ask, replaced with a **"Menu" button in the toolbox header** instead of
waiting on D6: `editor_open_menu()` (`kfx_editor.h`, implemented in `editor_session.cpp`) sets
the same `s_show_editor_menu` flag the F10 handler used to, called from a new button next to the
"Toolbox" heading (`editor_toolbox.cpp`). A button has no Escape-key conflict to sidestep in the
first place, so this closes the placeholder gap for this specific shortcut without needing D6's
full `Gkey_Editor*`/settings-file machinery — D6 itself (every *other* tool shortcut becoming a
definable key) remains undone.

The menu itself also became a real modal in the same pass (`FeOpenModal`/`FeBeginModal`/
`FeEndModal` — the same wrapper `frontgui_screens.cpp`'s own confirm/define-key popups use)
instead of a plain `ImGui::Begin()` window, so it now blocks interaction with the toolbox behind
it while open, matching how a menu should behave.

**Collapsed to a single "Back" button, dropping the menu's own "Exit to Main Menu".** The modal
originally had two closing buttons -- "Resume Editing" (close the modal) and "Exit to Main
Menu" (`editor_close()`, quit the session). Per user ask: exiting the editor already has its own
route -- the normal in-game pause menu's own Exit to Main Menu (Esc), which already works
correctly for an editor session (`editor_frame()`'s own safety-net comment covers that path) --
so a second quit button inside this menu was a redundant second route to the same place, not a
second capability. Both buttons collapsed into one "Back" that just closes the modal; quitting
the editor now happens only via the classic pause menu. Not yet live-confirmed.

## 4. Command journal (undo/redo)

Design the toolbox around a journal from the start (O4):
- Every editor packet action also appends a record to `kfx_editor`'s command journal:
  `{ action, args, inverse_args }`. For terrain: inverse = prior slab kind + owner (read before
  write). For thing place: inverse = delete that thing. For delete: inverse = recreate from a
  captured snapshot.
- **Undo** replays the inverse as a normal packet action; **Redo** replays the forward action.
- Cap the journal (e.g. 200 entries) or snapshot-and-truncate.
- Ship undo/redo in this phase if the inverse capture is straightforward for terrain + thing
  place/delete (the 90% case); defer brush/fill/area-op undo to phase 6 if fiddly.

**First slice implemented — Undo only, thing placement only.** Investigated the full design
above and concluded most of it wasn't "straightforward" enough to ship alongside everything
else this phase, so scoped down deliberately rather than skip it entirely:

- **Terrain (paint/fill/rectangle) undo deferred.** All three dispatch entirely server-side
  from `packets_cheats.c`'s per-work-state switch, running once per `input()` call regardless of
  whether kfx_editor did anything that frame -- there's no client-visible "this is one deliberate
  stroke" boundary to journal against (a held drag fires the exact same per-tile dispatch every
  turn the button stays down). Journaling every individual tile-write would make one Undo press
  revert one tile of a many-tile stroke, which is arguably worse than no undo at all for this
  case. Matches the doc's own "defer brush/fill/area-op undo... if fiddly" allowance -- this
  turned out to be exactly that fiddly case.
- **What *is* implemented**: creature/hero/digger, object, trap, and door placement are each a
  one-shot "create exactly one thing" action with a clean, class-correct inverse (delete that
  thing), so these get real Undo (Ctrl+Z, edge-triggered via `ImGui::IsKeyPressed(ImGuiKey_Z,
  false)` -- same convention `editor_frame()`'s own F10 toggle already uses -- checked in a new
  `editor_journal_frame()`, called from `editor_frame()`).
- **Cross-layer plumbing**: the journal itself lives in `kfx_editor` (new `editor_journal.cpp`/
  `.h`) as the doc specifies, but the actual placements happen in `packets_cheats.c` (`kfx_net`,
  a lower layer) -- so a new callback struct, `EditorJournalCallbacks`
  (`kfx_config/include/editor_journal_callbacks.h`), lets it report a placement upward without
  an `#include` violation, exactly the same shape `EditorCallbacks` already established for
  `editor_open()`. Wired once in `main.cpp`'s `setup_game()`. The callback is always non-NULL
  (a no-op default until wired, same convention); `editor_journal_record_placement()`'s own
  implementation checks `editor_is_active()` internally, since two of the five instrumented
  verbs (`PckA_CheatMakeCreature`/`_MakeDigger`) are also the classic (non-editor) cheat menu's
  own verbs and must stay no-ops there.
- **Journal**: a capped (200, per the doc) plain array of entries in `editor_journal.cpp`,
  reset on `editor_open()` (a previous session's indices mean nothing, or worse something else
  entirely once slots are reused, in a new one). Undo pops the most recent entry and sends new
  verb `PckA_EditorUndo` (carrying the thing index); its handler reuses the eraser tool's own
  per-class deletion split (`destroy_door()` for `TCls_Door`, `destroy_object()` -- which itself
  falls through to `delete_thing_structure()` -- for everything else this journal ever records).
- **`player_place_trap_without_check_at()`/`player_place_door_without_check_at()` return only a
  `TbBool`**, not the created thing, so their two call sites re-find it by the position+model
  just placed via `find_base_thing_on_mapwho()` -- the same lookup `thing_doors.c`'s own
  spinning-key management already uses for doors -- before journaling it.

**Redo implemented afterward** (originally deferred here as "distinctly bigger than the rest of
this slice" -- came back to it once Undo/the rest of phase 2 was solid). The blocker really was
what the original note said: `PckA_CheatMakeCreature`/`_MakeDigger`/`PckA_EditorPlaceTrap`/
`_PlaceDoor` all read position from the packet's own *ambient* `pos_x`/`pos_y` rather than a
param (only `PckA_EditorPlaceObject` carries position in its own `actn_par1`/`actn_par2`), so
blindly replaying just `par1`-`4` later would place at wherever the cursor happens to be *then*.
`record_placement()` now takes the *entire* creating packet's shape (`pcktype`, `par1`-`4`,
`pos_x`, `pos_y`), not just the thing index, so the journal entry has everything needed to
resend the identical action.

**First fix attempt confirmed broken live**: "placing a creature, then undo, then redo causes
the creature to appear at the current position of the cursor, rather than the original
location". That attempt wrote the recorded `pos_x`/`pos_y` directly onto the local packet
(`get_local_packet()`) before calling `set_players_packet_action()` for the *original* creation
verb, reasoning that `set_players_packet_action()` never touches `pos_x`/`pos_y` so the two
writes would compose safely. True as far as it went, but incomplete: that write happens from
this render-phase callback (`editor_journal_frame()`, called from `editor_frame()`), and
`get_dungeon_control_nonaction_inputs()` -- called from `input()` for the *next* real turn,
which runs again before this render-phase-originated packet is actually processed --
unconditionally overwrites `pos_x`/`pos_y` with whatever's under the mouse *then*. Same "packet
field written from the wrong phase gets clobbered before it's read" bug class as the original
`PckA_EditorPlaceObject` issue, just one layer removed (that one was about *reading* a stale
field; this one is a *write* silently overwritten before the read it was meant for).

**Fixed properly**: four new dedicated Redo verbs (`PckA_EditorRedoCreature`/`_RedoDigger`/
`_RedoTrap`/`_RedoDoor`) that carry position explicitly in `actn_par1`/`actn_par2` instead --
same shape `PckA_EditorPlaceObject` already uses, and the only shape that survives the
multi-frame gap between kfx_editor deciding the value and the packet actually being processed.
Couldn't just add an explicit-position param to the *existing* verbs
(`PckA_CheatMakeCreature`/`_MakeDigger` in particular), since those are also the classic
(non-editor) cheat menu's own verbs and changing their param layout would touch that unrelated
path too. Redo now switches on the journaled `entry.pcktype`: `PckA_EditorPlaceObject` resends
verbatim (no ambient-position problem to begin with); the other four map to their dedicated
Redo counterpart, repacking `entry.par1`/`par2` (the original model/kind + owner) into the new
verb's `actn_par3`/`actn_par4` and `entry.pos_x`/`pos_y` into `actn_par1`/`actn_par2`. Each new
verb's handler mirrors its non-redo counterpart's logic exactly (including re-journaling on
success via `record_placement()`, using the *original* verb name so future undo/redo chains
stay consistent).

Ctrl+Y triggers Redo (same edge-triggered `ImGui::IsKeyPressed` pattern as Undo). Undo pushes
the popped entry onto a separate redo stack; **undo-of-a-redo (and redo-of-a-redo) both fall
out for free** -- Redo's resent action flows through the exact same `packets_cheats.c` handler
any fresh placement does, which already calls `record_placement()` on success, so the newly
(re)created thing gets journaled again automatically with no special-case bookkeeping. One
accepted gap: a genuinely new placement doesn't clear the redo stack (most undo/redo systems
do) -- detecting "this call is a fresh click, not Redo replaying an old entry" would need a
flag surviving across the turn boundary between Redo sending the packet and the server actually
processing it, not attempted here.

Not yet re-confirmed live.

## 5. What must exist after phase 2

1. Toolbox panel set, shown on `editor_is_active()`, hidden otherwise.
2. Terrain/room/ownership paint with a generated slab+room palette and cursor preview.
3. Player + experience bottom bar feeding `cheatselection`.
4. Creature / hero / digger placement with a model-grid picker + thing counter.
5. Object / spellbook / special / gold placement with a model grid + value property.
6. Trap / door placement with kind grid + door rules + lock toggle.
7. Marking rectangle + area ops (set owner / fill / clear / delete-things).
8. Fill (flood) tool.
9. Brush grab-and-stamp with an editor clipboard.
10. Query inspector + eraser + eyedropper.
11. Command journal with undo/redo for terrain + thing place/delete.

## 6. Tests

- ftest `editor_paint_terrain`: select rock, paint a 5×5 block via synthesized packets, assert
  slab kinds + ownership + that columns/collision updated (walkability query).
- ftest `editor_place_creature`: place 3 imps for player 0 at level 5, assert count, owner,
  experience; delete one, assert count.

**First slice implemented** (`src/ftests/tests/ftest_editor_place_creature.{h,c}`, registered in
`ftest_list.c`) -- one creature rather than three, and undo rather than delete, but exercises
the real path end to end: `editor_open()` called directly (safe on an already-loaded level --
its `lvnum` param is only used for a log line), a creature placed via `PckA_EditorRedoCreature`,
asserted (model/owner/`exp_level`), then removed via `PckA_EditorUndo` and asserted gone.

Two things worth recording for whoever writes the next editor ftest:
- **Deliberately dispatches via `PckA_EditorRedoCreature`, not `PckA_CheatMakeCreature`** (the
  verb a real toolbox click actually sends). `PckA_CheatMakeCreature` reads its target position
  from the packet's own *ambient* `pos_x`/`pos_y`, which `get_dungeon_control_nonaction_inputs()`
  (called from `input()`, which the test framework's own loop runs every real turn regardless of
  what a test action wants) unconditionally overwrites from whatever the headless cursor position
  happens to be -- the exact bug class this session's own Redo work found and fixed. A test
  action setting the packet directly has the identical timing exposure, so it needs the
  identical fix: `PckA_EditorRedoCreature` carries position explicitly in `actn_par1`/
  `actn_par2` instead, immune to the overwrite, while still exercising real production code (the
  same handler Ctrl+Y uses, journaling included).
- **`editor_open()` freezes `get_gameturn()`.** It sets `kfx_sim_state.simulation_suspended =
  true`, and `get_gameturn()` (`game_legacy_get_gameturn()` → `kfx_game_state.play_gameturn`)
  only increments inside `game_session_loop.cpp`'s own `!GOF_Paused && !simulation_suspended`
  gate -- so the moment an editor session opens, the turn counter stops advancing for good.
  `ftest.c`'s own action scheduler gates moving to the *next* queued action on `get_gameturn() >=
  intended_start_at_game_turn` (computed once, at init time, as each action's own `turn_delay`
  accumulated on top of the previous one) -- so any action appended with `turn_delay > 0` *after*
  the point where `editor_open()` runs would stall the test forever waiting for a turn that will
  never come (the same failure shape `ftest_list.c` already documents for
  `bug_invisible_units_cant_select`, just a different root cause). Fix used here: every action
  after opening the editor uses `turn_delay=0` and polls via `FTRs_Repeat_Current_Action`
  instead -- safe because `process_packets()` (which is what actually dispatches
  `PckA_EditorRedoCreature`/`PckA_EditorUndo`) runs unconditionally at the very top of `update()`,
  *before* the `simulation_suspended` check, so it keeps running every real loop iteration
  regardless of the frozen turn counter. Repeating the *current* action isn't gated by
  `get_gameturn()` reaching anything new, only by the condition that was already true once.

**Confirmed passing** against a real KeeperFX install (`-DKFX_FUNCTESTING=ON` build,
`./keeperfx_hvlog -ftests editor_place_creature -exitonfailedtest -headless`): all 5 actions
executed at the same (frozen, per the `simulation_suspended` note above) game turn 20, exit code
0, `keeperfx.log` shows `FTest: [20] ftest_update: Test editor_place_creature passed!`.
- ftest `editor_fill`: enclosed earth pocket, flood with path, assert bounded by walls.
- ftest `editor_brush`: grab a 3×3 room+creatures, stamp elsewhere, assert deep-equal.
- ftest `editor_undo`: paint → undo → assert original slab; place thing → undo → assert gone.
- Catch2: palette generation from a synthetic slab/room/creature config (counts, grouping);
  journal inverse-arg capture.
