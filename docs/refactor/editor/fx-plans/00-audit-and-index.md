# FX plans — audit of the original editor plans, and index

Status: **initial pass** (2026-09-19). Written after phase 6 closed the classic-format editor. The
audit walked every doc in `docs/refactor/editor/` (00–10 and `phase3`–`phase6`) against the code
and lists what was promised, deferred or found later and never picked up. "Verified" means checked
against the source today, not just against the docs.

## Scope decisions (from the user)

| Topic | Decision | Plan |
|---|---|---|
| Sidecar files dropped by Save As | Warn and confirm on Save As. **Done** (see 01). Playtest has the same problem and is planned. | [01](01-sidecar-files.md) |
| Lua level scripts | **Built** (L1–L7: round trip, Lua tab, Validate, Lua API page, banners, New Map option/snippets, pack-module editing). Live tests pending. | [02](02-lua-scripts.md) |
| Per-level rules and content (creature, trap/door, spell/ability, room editors) | Needs a set of tools beside the map editor: **plans written** (foundation [03](03-content-editors-foundation.md), [04 creature](04-creature-editor.md), [05 trap/door](05-trap-door-editor.md), [06 spell/ability](06-spell-ability-editor.md), [07 room](07-room-editor.md), [09 text](09-text-strings-editor.md); shared read/write layer: [10](10-config-content-model-and-writers.md)). | [03](03-content-editors-foundation.md) |
| Per-campaign rules and campaigns themselves | **Now in scope** (reverses the earlier "out of scope"): every content editor works at Level *or* Campaign scope, and a Campaign / mappack editor is planned: [08](08-campaign-editor.md). | [08](08-campaign-editor.md) |

## Progress on the classic-format gaps

Status after the first working session (commits `acada21de` .. HEAD). "Done" means built, unit- or
ftest-covered, but not yet live-tested by the user.

| Item | State |
|---|---|
| A1 Playtest sidecars | **Done.** The level's sidecar files are copied to the scratch level before a playtest (`editor_sidecars`, plan 01 §3). |
| A2 Return to editor | **Done.** Playtest end (win, lose or quit) re-opens the scratch level as the real level, marked unsaved (`editor_playtest_begin`, `editor_playtest_running`). Unit test in `editor_relaunch_test`. |
| A3 Script Validate / Reload | **Done for classic scripts.** Validate button, clickable problem list, Reload; problems also appear in Verify Map. Unknown commands, unclosed/stray IF, missing parenthesis are errors; argument counts are warnings because 143 of 452 shipped scripts break a stricter rule while being fine (the engine tokenizer is looser than the argument strings). Swept over all 451 shipped scripts: 8 flagged, all genuine (typos such as `REMENDIF`, `SET_CREATURE_FEAR`). |
| A4 Lua | **Done** ([02](02-lua-scripts.md), slices L1–L7). Also: win/lose conditions in Level Settings > Script Setup, duplicate-rule warning. |
| A5 Author | **Done.** `LevelInformation::author`, `.lof` `AUTHOR` read and written, Level Settings field, plus a read-only line listing which keepers have a Dungeon Heart. |
| A6 Reinforce | **Done** as the Area > Reinforce tool (button, journaled, hotkey N). Room floors and doors count as the owner's ground (first version missed rooms). |
| A7 Undo gaps | **Done.** Free-hand paint, Fill, Paint Texture, Stamp (slabs, things, points) and Reinforce are undoable as one step per stroke; deleting things is undoable and restores model, owner, position, level, gold amount, creature health and a door's lock. **Property edits** are journaled too: Apply Position / Apply Value on an object (`EJK_ThingEdit`), a door's lock toggle (`record_door_lock` callback), and every Points inspector edit (`EJK_PointEdit`, via `editor_points_replace`). A locked door's keyhole object is not tracked separately (the lock recreates it). Not verified in the headless tree: deleting and restoring a door (its slab kinds are missing there, so the ftest skips that part); worth a live check.
| A8 Trap/door rules | **Done:** one trap per *subtile* (shipped maps such as keeporig level 9 put several traps in one slab, so traps now go on the clicked subtile whatever the trap's PlaceOnSubtile setting) and one door per square. |
| A9 Stamp points | **Done.** Lights, action points (renumbered) and effect generators are captured and re-placed; each point is its own undo step. |
| A10 RMB delete | **Done** in Creature and Thing (plain right-click; right-drag stays camera / Stamp capture). |
| A11 Hotkeys | **Done, with a default set:** F fill, B stamp, L points, C creature, O thing, N reinforce (plus the existing R, T, Q, E). All rebindable. Localised labels remain English (B7). |
| A12 verify scope | Script checks added. **Decision:** `Auto` is documented as "KeeperFX-shape compatible"; a vanilla-ID-range check was not built because no reliable base-roster boundary exists in the config (unchanged finding from phase 3). |
| A13 Classic output | **Done in part.** A classic save now also writes `.dat`, `.clm`, `.wib` from the live session; for stock levels 1-3 all three are byte-identical to the shipped files, for the others `.dat`/`.wib`/`.clm` differ where the engine's regeneration differs from the shipped file. Opening the result in ADiKtEd / Unearth (S4) was not possible here. **Bug found and fixed on the way:** a classic save over a level previously saved in KeeperFX format left `.tngfx/.lgtfx/.aptfx` behind and the loader prefers them, so stale things replaced the saved ones (and the reverse for `.dat/.clm/.wib`). Each writer now removes the other format's files. |
| A14 Level Settings leftovers | **Done.** **Resize:** Level Settings has New width / New height / "Keep the old map centred" and a Resize... button; a confirm lists what would be lost (things, lights, action points outside the new bounds). It works on a snapshot (`editor_resize`, unit-tested), writes the resized copy to the scratch level and reopens the editor on it as the same level, unsaved. New ground is solid rock; 8 to 170 slabs. **Paint Texture** has Brush, Rectangle and Fill modes (`editor_texture_paint`). **tmap discovery:** the texture-set dropdowns list the built-in 0-14 plus every higher id the engine's own pack search finds (level folder, campaign config, standard data, mods). |
| A15 Ambient light | Was closed as per-campaign; with campaigns now in scope it can be revisited as a campaign-editor / Rules-editor field ([08](08-campaign-editor.md), [03](03-content-editors-foundation.md) §9). |
| A16 Bookmarks | **Dropped** by decision. |
| A17 Layout redesign | Postponed by decision. |
| A18 Thumbnails | Doors no longer duplicated in Terrain > Other; the heart's pedestal and walls show the Dungeon Heart picture. Two slabs and six objects still fall back to text. |
| B1 `editor_full_roundtrip` | Not written as one test; covered in pieces: `editor_save_reload` (edit, save, reload), `editor_session` (stock maps in both formats), `editor_strokes` (undo, reinforce, points, validator). Playtest itself cannot be run headlessly. |
| B2 Frozen-sim invariant | **Done** (`editor_session`: 400 frames, game turn and a world checksum unchanged). |
| B3 blank map / views / overlay toggles | **Blank map done** (`editor_session`: create_blank_map, size, all neutral rock, no things, save and reload in both formats). Views and overlay toggles are ImGui-drawn and not headless-testable. |
| B4 Stock-map regression | **Done** for the 14 shipped keeporig maps in both formats (`editor_session`); other campaigns were not swept. |
| B5 User guide | **Done** (`docs/level_editor.txt`). |
| B6 Architecture / CLAUDE.md | **Done** (architecture.md §2.9a, one line in CLAUDE.md). |
| B7 Language strings | **Not done:** about 300 English labels; translating them needs new string IDs and a translation pass, which is a project decision rather than an editor bug. |
| B8 "Editor Maps" mappack | **Done.** A map that was never saved defaults to `levels/editormaps/` (next free level number) when you save; the first save writes `levels/editormaps.cfg` (mappack "Editor Maps") and rescans the pack list, so it can be picked in Free Play or the Open dialog's Browse. Ctrl+S on an untitled map opens Save As. |
| B9 Fill / Brush ftests | **Done.** Brush/Stamp capture and stamp now live in `editor_brush`, right-click delete in `editor_things`, and Query in `editor_query` (no UI in them); `ftest editor_brush` covers capture, stamp (two subtile traps, slabs, a light), undo, query and delete + undo. Fill: (`editor_flood_fill_terrain` is now exported as a test seam; `editor_strokes` checks it fills exactly the enclosed pocket). Brush/Stamp capture lives inside the toolbox's UI handler and would need extracting into its own module first. |
| Dead handlers | **Removed** (`PckA_EditorEyedropperTerrain/Thing` enum values and handlers; the eyedropper writes the selection directly). |

## A. Real gaps in the core (classic-format) editor

Ordered by how much a mapmaker would notice. Verified against the code unless marked otherwise.

| # | Item | Origin | State today |
|---|---|---|---|
| A1 | **Playtest ignores sidecars.** Playtest saves to a scratch level number in the same folder; the level's `.lua`, `.rules.cfg`, `.sounds.cfg` etc. are keyed by level number, so the scratch level runs without them. A Lua-driven or rules-modified level plays differently in Playtest than for real. | found in this audit | Bug. Plan: [01](01-sidecar-files.md) §3. |
| A2 | **Return to editor after Playtest.** | `01` §6, `06` MVP step 7, `phase3/04` | `editor_notify_playtest_end()` is still an empty function. After a playtest the player lands wherever a normal game would. The MVP list in `06` says "then return to editing". |
| A3 | **Script Reload / Validate.** Balanced `IF`/`ENDIF`, name resolution against config, argument types. | `05` §4.1, §5 item 6, `phase5/08` | Not built. Colouring and the command browser exist; nothing checks a script. `verify_map` has no script checks either. |
| A4 | **Lua scripts.** | this audit / user | See [02](02-lua-scripts.md). |
| A5 | **Author** in Level Settings (`.lof` `AUTHOR`); **keeper count** and **which players own a heart** shown in Level Settings / New Map. | `05` §1, `phase3/02`, `phase5/01` | Not built (`LevelInformation` has no author field). |
| A6 | **`r` — reinforce a player's perimeter.** | `02` §2.1, original manual | No code. Every earth slab next to a dungeon has to be reinforced by hand. |
| A7 | **Undo gaps.** No undo for free-hand terrain paint, Fill, Delete Things, Stamp (slabs and things), Paint Texture, or property edits (object position, gold, point properties). | `02` §4, `09` §1, phase5 | Documented as accepted at the time; taken together it is a real hole (a mis-click Fill cannot be undone). Candidate: journal at stroke granularity (mouse-down to mouse-up) with a slab-snapshot record, the same shape rect ops already use. |
| A8 | **Trap and door placement rules** (not on an occupied square; door quality checks). | `02` §2.7 | Deferred "to a later pass", not done. |
| A9 | **Stamp does not capture lights, action points or effect generators.** It was deferred "to phase 5", and phase 5 built the Points tool without it. | `09` §1 | Open. |
| A10 | **RMB-delete** while another placement tool is active. | `02` §2.10, `09` §2 | Only the Erase tool deletes. |
| A11 | **Hotkeys**: 4 of the original tools have one; the new tools (Points, Paint Tex, Fill, the three area tools, Creature/Thing) have none. The classic (non-ImGui) Define Keys menu still has the contiguity problem. Localised labels. | `10` §3 | Open, and a product decision (which tools, which keys). |
| A12 | **`verify_map` scope.** No room checks, no portal check (deliberately, `phase3/06`); no script checks (see A3); the classic vanilla-ID-range check is still missing so `Auto` means "KeeperFX-shape compatible", not "vanilla can open it". | `03` §5, `phase3/01` | Decisions on record; A3 covers the script half. |
| A13 | **Classic output is not vanilla-openable.** `.dat`/`.clm`/`.wib` are not written; the S4 conformance check (open a saved map in ADiKtEd / Unearth) never ran. | `03`, `phase3/01`, `06` §4 | KeeperFX reloads such maps by regenerating; other tools cannot. Only matters if that promise is kept. |
| A14 | **Level Settings leftovers.** Resize a populated map; custom `tmap` pack discovery; rectangle/fill mode for Paint Texture. | `05` §1, `phase5/03` | Open, low. |
| A15 | **Ambient light.** | `phase5/03` §3 | **Closed**: per-campaign, out of scope (this doc). |
| A16 | **Camera bookmarks** (numbered save/recall); cursor-based 1st-person spawn. | `04` §4, `phase4/00` | Not built; bookmarks were never in the 1998 editor. |
| A17 | **`08-gui-layout.md`** (icon tool rail, status bar, splitter inspector). | `08`, `09` §3 | Never built. The toolbox is now four levels of tabs; `09` already said "revisit `08` rather than adding more tabs". Worth a decision once the live test of phase 6 is in. |
| A18 | **Thumbnails.** Two slabs and six objects still fall back to text tiles. | `phase6/01` | Small. |

## B. Tests and documentation promised but missing

| # | Item | Origin |
|---|---|---|
| B1 | The **acceptance ftest `editor_full_roundtrip`** (new map, build a dungeon, save, reopen, playtest, `WIN_GAME`). Not written. | `06` §4 |
| B2 | **Frozen-sim invariant** ftest (`editor_session_enter`: 10 000 frames, `play_gameturn` and state checksum unchanged on three stock maps). Not written. | `01` §8, `06` §4 (risk R2) |
| B3 | `editor_blank_map`, `editor_views`, `editor_overlay_toggles` ftests. Not written. Only 8 editor ftests exist (place_creature, paint_terrain, undo, save_reload, points, script_commands, palette, thumbs). | `01` §8, `04` §7 |
| B4 | **Stock-map save/reload regression** across every campaign map. | `06` §4 (R1/R3) |
| B5 | **User guide** `docs/level_editor.txt`. Does not exist. | `06` §5 |
| B6 | **`docs/Architecture/architecture.md` has no editor section** (zero mentions), nor does `CLAUDE.md`. `kfx_editor` is in `check_layering.py` only. | `06` §5 |
| B7 | **Language strings** for editor labels (`pkg-languages`); everything is English literals. | `01` §1, `10` §3 |
| B8 | "Editor Maps" mappack as the default save bucket; sample maps. | `00` O2, `06` §5 |
| B9 | `editor_fill` / `editor_brush` ftests were excluded as untestable through the packet layer; a direct-call test seam would close it. | `09` §2 |

## C. Housekeeping

- **Live tests pending** (built and verified headlessly only): phase5 slices 5–9 (availability, points,
  message helper, command browser, syntax colouring), phase 6 (icon grids, thumbnails, owner symbols,
  CLASSIC-HUD override, nested tabs, walls tab, closable toolbox, the round-3 selection fixes).
- **Dead code:** removed (the two eyedropper packet actions).
- **Stale docs:** fixed (`00-overview.md`, `06`, `05` §5a now say built / historical and point here).

## D. Suggested order

1. A1 (playtest sidecars) — correctness bug; small.
2. A2 (return to editor) — the missing half of the MVP flow.
3. A3 + [02](02-lua-scripts.md) — script validation, then Lua.
4. A7 (stroke-level undo) and A6 (`r`) — the two items a mapmaker will hit daily.
5. B1–B4 tests and B5–B6 docs — before this is called finished.
6. Content editors — [10](10-config-content-model-and-writers.md) (engine-decoupled content layer and writers, first), [03](03-content-editors-foundation.md) (host, Rules editor), then 05 → 07 → 06 → 04; [08](08-campaign-editor.md) and [09](09-text-strings-editor.md) in parallel; F5 (Playtest campaign context) after spike S6. Rename to *Map Editor* and the Tools menu come first (F0).
7. Everything else in A as demand appears.

## Follow-ups from live testing

- **Room repainted over a room.** `place_slab_type_replacing_room()` (kfx_sim) deletes the old room and puts plain earth under a new room slab; used by Brush, Rectangle, Fill, Set Owner, Stamp and undo/redo. The purple centre could not be reproduced headlessly (slab kinds and columns were already identical to a fresh room), so this follows your suggested strategy; please confirm it in the live UI.
- **Preview Motion** used to leave creatures wherever they had walked. Turning it on now remembers every slab and every creature, object, trap and door; turning it off puts them back (positions, health, what exists; spawned things are removed). Editing is paused while it runs (the toolbox says so), and saving turns it off first. If a 1st Person view is still controlling a creature the restore waits until you leave it. Covered by `editor_session` (200 game turns run, world identical afterwards).
- **Bug found:** maps not 85 x 85 were saved without `MAPSIZE` in the `.lof` and reloaded as 85 x 85. The writer now always writes it (a New Map of another size would have been broken on reload).
