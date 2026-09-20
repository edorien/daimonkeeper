# Phase 6 — MVP cut line, sequencing, risks, testing, packaging

Status: **historical -- the MVP and everything after it is built.** What was actually built,
slice by slice, is in `phase3`-`phase6`; what remains (and what was found later) is tracked in
[`fx-plans/00-audit-and-index.md`](fx-plans/00-audit-and-index.md). The testing strategy below was
largely followed: Catch2 for pure logic (`kfx_sim`/`kfx_editor` tests) and the `editor_*` ftests
(`src/ftests/tests/ftest_editor_*.cpp`) for stock-map round trips, the frozen-sim invariant, strokes,
undo and Preview. Read alongside 00–05.

---

## 1. MVP definition (what "the editor works" means, minimally)

A user can, entirely in-game:
1. Main menu → Tools → Editor → **New Map** (or Open an existing one).
2. Paint terrain and rooms, set ownership, place a Dungeon Heart and Portal.
3. Place creatures, objects, traps, doors.
4. Place action points / hero gates.
5. Set name + basic level settings (gold, gen speed, availability, `WIN_GAME`).
6. **Save**.
7. **Playtest** from the editor, then return to editing.
8. Quit to menu; the saved map is playable from Free Play.

MVP = phases **1, 2 (terrain+rooms+ownership+creatures+objects+traps/doors+query),
3 (save/load/new/verify, both output formats — D5/F20), 5 (§1 settings + §3 APs +
§4.1/4.2 script text+managed region)**.
Phase 4 (views/overlays) — **slab grid + coordinates + thing markers + the full View menu
(Plan / 1st-Person-spectator / Low Walls / Lights — all wiring, F9/F21) are MVP**; the full
overlay set, brush/fill/undo in 2, and light/FX editing in 5 §2 are post-MVP.

## 2. Suggested sequencing

```
Phase 1  ─────────────►  editor opens, map loads frozen, blank map creates
            │
   ┌────────┴────────┐
   ▼                 ▼
Phase 3 spikes    Phase 2 toolbox            Phase 4 (grid+coords+markers)
S1/S2/S3          (terrain→creatures→        can start after 1, lands
   │              objects→traps/doors)       whenever
   ▼                 │
Phase 3 save/verify ─┤
   │                 │
   ▼                 ▼
        Phase 5 settings + APs + script text/managed region
   │
   ▼
   MVP ship  ──►  post-MVP: brush/fill/undo, lights/FX editing, full overlay set,
                  multi-tileset paint, objective/creature-stat helpers,
                  per-map creature.cfg panel
```

Phase 1 is a hard gate. The phase-7 investigation already retired the design-level unknowns
(sim suspension = `GOF_Paused`, derived-data regen mechanism exists, TOML schemas known,
`.lof` auto-discovery, startup reuse). Spike **S1** (regen byte-equivalence on stock maps)
still runs during phase 1/2 but now only *sizes* a fallback, it doesn't gate the design.

## 3. Risks

| # | Risk | Impact | Mitigation |
|---|---|---|---|
| R1 | **Derived-data (`.dat`/`.clm`/`.wib`) regeneration isn't byte-equivalent** to a normal load → saved maps render/collide wrong | **Low–Med** (was High) | [`07`](07-investigation-findings.md) F2: `place_single_slab_type_on_map` + `initialise_map_wlb_auto` already do this at runtime. S1 now just sizes "how many stock maps keep their `.clm`". Editor-authored maps never have one. Fallback (serialize derived files from the same regen) is one flag. |
| R2 | **Suspending the sim cleanly** — a subsystem advances state or crashes when frozen | **Low–Med** (was High) | [`07`](07-investigation-findings.md) F1: `GOF_Paused` already gates the whole turn loop (`game_session_loop.cpp:139`) and is well-tested (used by every in-game pause). `simulation_suspended` just forces+locks it. Residual risk is only the "Preview motion" un-freeze path and the trimmed `post_init_level`. Keep the 10k-frame zero-delta ftest. |
| R3 | **TOML thing/light/AP schema drift** — writer emits keys the loader half-accepts | **Low** (was Med) | Schemas already read from the loader callbacks ([`07`](07-investigation-findings.md) F3); every `write_*` written against its `load_*` twin; round-trip test S3 in CI on every stock map. |
| R4 | **In-game GUI project churn** — editor panels built on `frontgui_widgets` while that API is still moving | Med | Sequence editor UI work *behind* the in-game GUI project's phase 0–4; `kfx_editor` is "another consumer", not a driver. Track [`../ingame-gui/`](../ingame-gui/). |
| R5 | **Renderer migration churn** — editor overlays touch draw code mid-migration | **Low** (was Med) | [`07`](07-investigation-findings.md) F9: View menu is pure wiring (`PckA_SetCluedo` for low walls, parchment `draw_zoom_box` for Plan, `PckA_ToggleLights`) — **no new renderer code**. Overlays go through the existing `RenderOverlayCallbacks` seam the in-game-GUI project already owns. |
| R6 | **Scope creep toward full ADiKtEd/Unearth parity** (custom columns, graffiti, procedural gen, collaborative editing) | Med | 00-overview §8 non-goals are firm. (Classic binary *export* is now **in** scope — D5/F20 — but bounded: reverse the classic loaders + a feature-detect gate, no new format design.) |
| R7 | **Layering violations** — `kfx_editor` mis-ranked, or an editor-specific hook landing in `kfx_sim`/`kfx_frontend`/`kfx_apploop` | Low–Med | [`07`](07-investigation-findings.md) F11: `kfx_editor` sits right below `app_entry` in `LIBRARY_ORDER`; **the only inbound edge is `main.cpp`** (the ImGui-frame wrapper + `EditorCallbacks` impl). `--strict` is CI-blocking. Editor-aware lines elsewhere are enumerated in 00-overview §5.3. `simulation_suspended` is a neutral flag (forces `GOF_Paused`), not "editor mode". |
| R11 | **`kfx_editor` links the whole engine** — a new top-of-ladder library including headers from 8 libs below it enlarges the build graph / compile time | Low | Compiled twice (std/hvlog) like every `kfx_*` lib but leaf (nothing rebuilds because of it); only `main.cpp` recompiles when its public header changes. Same shape as `kfx_apploop`. |
| R8 | **`levels.txt` / free-play discoverability** — saved maps invisible without hand-editing (the original editor's worst UX) | **Resolved** | [`07`](07-investigation-findings.md) F4: `find_and_load_lof_files()` auto-scans `*.lof` in `FGrp_CmpgLvls` at campaign load and registers them (`KIND = SINGLE`/`MULTI`). Write a `.lof`, done — no `levels.txt`. Ship an "Editor Maps" mappack as the bucket. |
| R9 | **255-creature / 48-IF / `u16`-count classic caps** vs KFX's raised limits ([`07`](07-investigation-findings.md) F18: 1024 / — / 12288) — maps that only run on KFX | Low | `verify_map()` has a **target mode** (level setting): KeeperFX (warn near real caps) vs classic-compatible (ERROR at classic caps). Default KeeperFX. |
| R10 | **Multiplayer packet stream assumptions** — editor packet actions in a `local` game vs the netcode's expectations | Low | Editor session is always single-player `local` (00-overview O5); the cheat packets already run in that mode. |

## 4. Testing strategy

- **Catch2**: a new `kfx_editor` test target (`editor_new_map`, all `write_*` for **both
  formats**, `map_is_legacy_compatible()` fixtures, `verify_*`, palette generation, script
  validation, command-journal inverses); plus `regenerate_derived_map_data()` equivalence in
  `kfx_sim`'s target (S1 as a checked-in fixture diff).
- **ftests** (`src/ftests/`, real running game, `FUNCTESTING`): every phase's §"Tests"
  section. The headline ftest is **`editor_full_roundtrip`**: New map → build a minimal
  playable dungeon via synthesized packets → Save → exit → Open → assert identical → Playtest
  → assert `WIN_GAME` fires → return to editor. This is the acceptance test for MVP.
- **Frozen-sim invariant ftest** (R2): enter editor on 3 different stock maps, run 10000
  frames, assert `game.play_gameturn` unchanged and a checksum of slab/thing/creature state
  unchanged.
- **Stock-map regression** (R1/R3): CI job saves and reloads every campaign map, asserts no
  verification ERROR and stable column tables. Stock maps are legacy-compatible, so this
  exercises the **classic-binary** writers; the diff is against the original files.
- **Classic-writer conformance** (S4): a saved classic-format map opens without warnings in
  ADiKtEd / Unearth (offline check, not CI).
- **Layering** (R7): `check_layering.py --strict` already CI-blocking; `kfx_editor` added to
  its rank table in phase 1.
- **Manual/visual** (per repo rules — ask before live-desktop interaction): grid alignment,
  marker legibility, light preview, low-walls view, overall editor UX on a real map.

## 5. Docs & packaging

- User-facing: a `docs/level_editor.txt` (sits beside `docs/custom_levels_play.txt`,
  `docs/creating_campaigns.txt`) — how to open the editor, the tools, saving, playtesting,
  where maps go, the script helpers, and a "from blank map to playable level" walkthrough
  mirroring the original manual's checklist (§6 of `Dungeon Keeper Editor Manual.doc`).
- `CLAUDE.md` / architecture.md: document the new `src/kfx_editor/` library and its ladder
  position, the `EditorCallbacks` struct, `editor_save_map()`, `simulation_suspended`, and the
  `regenerate_derived_map_data()` loader path, once they land. Add `kfx_editor` to
  architecture.md §5's callback-struct table and the layering diagram.
- No new build *target* — `kfx_editor` is an internal `OBJECT` library linked into the normal
  `keeperfx` / `keeperfx_hvlog` binaries; the editor is reached behind the menu button
  (00-overview O3), not a separate executable.
- `levels/editor/` created on first save; add to the packaging manifest if editor sample maps
  are bundled (optional — a couple of "example" maps like the original's level 200/201 would
  be a nice touch and double as test fixtures).

## 6. Post-MVP backlog (roughly priority order)

1. Brush (grab/stamp) + Fill (flood) + area ops (phase 2 §2.2–2.4).
2. Undo/redo across all tools (phase 2 §4).
3. Light & FX-generator editing with live preview (phase 5 §2).
4. Full overlay set (phase 4). (Plan, 1st-Person-spectator, Low Walls, Lights are all MVP wiring.)
5. Multi-tileset per-slab texture painting (phase 5 §1).
6. Objective/information/creature-stat script helpers (phase 5 §4.4/4.5).
7. Classic binary format export (`.tng`/`.lgt`/`.apt`/`.dat`/`.clm`) for vanilla DK / other
   tools (00-overview O6).
8. Verification "zoom to next issue" polish, map thumbnail regeneration on save.
9. Procedural New Map starting points (00-overview O7).
10. (Speculative) co-op editing over the packet stream (00-overview O5).
