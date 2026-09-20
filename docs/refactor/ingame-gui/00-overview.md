# In-game GUI: Dear ImGui migration — investigation and roadmap

Status: **investigation, second pass — scope and approach settled with the user (§8), broken into
per-phase docs (§7), no implementation started.** This file is the shared-context + decisions
index; each phase's code-level investigation lives in its own numbered doc. Scope:
`src/kfx_frontend/`'s in-game HUD/menu code (`frontmenu_ingame_*`, `gui_parchment.c`,
`gui_boxmenu.c`, `gui_topmsg.c`, `gui_tooltips.c`, the in-game arm of `draw_gui()` /
`get_gui_inputs(1)`), plus the `render_overlay` callback seam in
[`src/kfx_config/include/render_overlay.h`](../../../src/kfx_config/include/render_overlay.h) that
`kfx_render` calls the HUD through. Does **not** touch the 3D engine, the frontend menus (already
migrated — see below), or the layering graph.

This is the "separate, longer-term project" that
[`../renderer/04-imgui-gui-foundation.md`](../renderer/04-imgui-gui-foundation.md) §2.3 / §10 and
[`../renderer/gpu-v2/01-phase-b-2d-compositing.md`](../renderer/gpu-v2/01-phase-b-2d-compositing.md)
§B3 both explicitly defer to.
Stage 4 migrated the whole main-menu frontend to real Dear ImGui widgets; stage 4 §2.3 drew the
line there deliberately, calling the in-game GUI *"gameplay-critical, latency-sensitive, over live
3D content, chrome entangled with game state — a port, not a reskin"*. Stage 4 §2.3 originally
said "revisited after the GPU work", but **stage 3's own Phase B3 route 1 already inverts that**
(*"wait for the in-game-GUI-as-ImGui project… B3 is subsumed"* — the recommended default there),
and the user has now confirmed: **this project runs before stage 3's GPU Phase B**. It works
against today's `RendererSoftware` unchanged (the overlay path already composites in-game), so
there is nothing to wait for, and doing it first means stage 3 never builds a throwaway CPU→GPU
HUD compositor. The one shared item — cursor unification (§3.9) — is done here.

---

## 1. What the frontend migration already bought us

Nothing below has to be rebuilt — stage 4 (`../renderer/04-imgui-gui-foundation.md`) and stage 5
(`../renderer/05-imgui-linkage-consolidation.md`) delivered it, and it is all directly reusable:

- **Dear ImGui 1.92.x vendored** at `deps/imgui/`, with `imgui_impl_sdl3` +
  `imgui_impl_sdlrenderer3` backends, compiled into exactly one library (`kfx_frontend`, after
  stage 5) — the same library the in-game menus already live in. No new vendoring, no new linkage
  question.
- **The ImGui context is already live during gameplay.** `RendererImGuiEnabled()` is a
  session-global flag set once from `!use_classic_menu()`, and
  `RendererSoftware::PresentFrame()` new-frames + renders ImGui on *every* present, in-game
  included (`../renderer/gpu-v2/00-overview.md`). The overlay compositing path this project needs
  already runs; today it just submits nothing while a level is loaded.
- **The wrapper layer** — `frontgui_widgets.{h,cpp}`'s `FeBeginPanel` / `FeButton` / `FeSlider` /
  `FeCheckbox` / `FeBeginListBox` / `FeBeginScrollArea` / `FeHeading` / … — exists, is styled
  (`frontgui_style.cpp`: bronze/parchment/blood-red, font metrics-driven sizing), and enforces
  the "screens never call `ImGui::` directly" rule. The in-game GUI is a second consumer of the
  same wrappers, possibly with a few HUD-specific additions (§6).
- **The deferred-action pattern** (`frontgui_screens.cpp`'s `s_pending_*`) for "don't call heavy
  state transitions from inside an active ImGui window" — the in-game menus transition player
  state and send packets from click handlers, so this pattern transfers directly.
- **The `-classicmenu` / `CLASSIC_MENU` toggle + `Clo_ClassicMenu` override.** Rode this flag for
  the whole migration (frontend and in-game HUD together) as the bisect-and-workaround safety net
  above described. **Retired 2026-09-12**, once the frontend menus and the in-game HUD were both
  confirmed working live and had nothing left to fall back to as a *session-wide* switch. The
  frontend's legacy sprite menus are gone outright — every `FeSt_*` `state_is_migrated()` marks is
  ImGui-only now, no toggle at all. The in-game HUD's legacy sprite renderer stayed, on purpose (a
  deliberate live-tested request, not a leftover) — it just moved to a *much* narrower switch:
  `GUI_ICON_PACK`'s reserved `"CLASSIC"` value (alongside `"NONE"`,
  `config_settingschema.c`'s `ensure_gui_icon_pack_enum()`), read by
  `ingame_gui_use_classic_hud()` (`config_keeperfx.h`/`.c`). Every in-game HUD draw call that used
  to check `RendererImGuiEnabled()` (itself retired — `RendererSoftware::PresentFrame()`'s ImGui
  pipeline is unconditional now) checks that instead; nothing outside the in-game HUD reads it.
  Per-`GMnu_*` opt-in (§4.2) is still how the migration itself landed incrementally; this note is
  only about the on/off switch, not the menu-by-menu rollout.
- **Dynamic textures from raw pixel data** — `RendererCreateDynamicTexture()` /
  `UpdateDynamicTexture()` / `DestroyDynamicTexture()` (`RendererManager.h`), used by the land
  preview panel and the ImGui cursor. The minimap (§4.4) needs exactly this.

The single most important structural fact: **`../renderer/04-imgui-gui-foundation.md` §1.4's
"clean pre-existing seam" argument applies here too, one layer in.** `draw_active_menus_buttons()`
(`frontend.cpp:3480`) iterates `active_menus[]` and dispatches per-menu by `GMnu_*` id. Migration
is **per-`GMnu_*`, opt-in**, exactly as the frontend migration was per-`FeSt_*` — never a flag day,
never "replace `draw_gui()`".

---

## 2. The in-game GUI inventory

Everything `draw_gui()` and its neighbours put on screen during a level. Grouped by what kind of
problem each is.

### 2.1 The sidebar panel (`frontmenu_ingame_tabs*`, `GMnu_MAIN` + the tab menus)

The always-on right-hand panel. One `GuiMenu` (`main_menu`, `GMnu_MAIN`, 140×400 virtual px) plus
the tab-content menus that swap into the lower two-thirds:

| Menu(s) | `GMnu_*` | Content |
| --- | --- | --- |
| `main_menu` | `GMnu_MAIN` | panel background (`status_panel` `TiledSprite`), gold total, minimap, compass overlay, 5 tab headers, zoom in/out/fullscreen-map buttons, autopilot button, 13 event-notification buttons down the right edge, the money tooltip hotspot |
| `query_menu` | `GMnu_QUERY` | "information" tab — payday/research/workshop progress bars, per-player creature/room counts, ally toggles, tend-to-imprison / tend-to-flee |
| `room_menu` (`room_menu2` dropped) | `GMnu_ROOM` (`GMnu_ROOM2` dropped) | room-build icon grid → **scrollable** (the 2nd page was a fit-more hack, [06](06-tab-content-panels.md) §2.1), "big" hovered-room info area, sell-area button |
| `spell_menu` (`spell_menu2` dropped) / `spell_lost_menu` | `GMnu_SPELL` (`GMnu_SPELL2` dropped) / `GMnu_SPELL_LOST` | keeper-power icon grid → scrollable, big power info |
| `trap_menu` (`trap_menu2` dropped) | `GMnu_TRAP` (`GMnu_TRAP2` dropped) | manufacture (traps + doors) icon grid → scrollable, big trap/door info, buildable-count info, sell |
| `creature_menu` | `GMnu_CREATURE` | creatures tab — scrollable 4×N table of creature-model rows with per-activity (idle/working/fighting) counts, click-to-pick, anger colour bars |
| `creature_query_menu1..4` | `GMnu_CREATURE_QUERY1..4` | 4-page possessed/queried-creature detail panels (health/anger/xp bars, instances, ~30 stat readouts) |

Shared drawing: `gui_area_new_normal_button` / `_vertical_button` / `_null_button` /
`_no_anim_button`, `gui_area_room_button` / `_spell_button` / `_trap_button` (icon + count +
flash), `gui_draw_tab`, the `gui_area_progress_bar_*` family, `draw_gold_total` (renders digits
from `button_sprite[]`, not a font).

### 2.2 The minimap (`frontmenu_ingame_map.c`)

Not a menu — drawn by `draw_whole_status_panel()` inside the `GMnu_MAIN` background. It is a
**per-pixel software raster**: `panel_map_draw_pixel()` pokes single `TbPixel`s, `PanelMap[]` is a
`MAX_SUBTILES_X*MAX_SUBTILES_Y` scratch buffer, overlay things (creatures, traps, spells,
call-to-arms circles, the view cone) are plotted pixel by pixel every frame. `PanelColours[]` is a
precomputed shade table. `gui_parchment.c` is the separate full-screen "parchment" zoomed map
(`redraw_parchment_view` / `load_and_redraw_minimal_overhead_view`, reached via the
fullscreen-map button and the map key).

### 2.3 The pause/options menus (`frontmenu_ingame_opts*`)

Modal centred windows over the paused game, `gui_pretty_background` (the brown-with-lighter-border
chrome — same family the user's screenshot annotation refers to):

| Menu | `GMnu_*` | Notes |
| --- | --- | --- |
| `options_menu` | `GMnu_OPTIONS` | 6 icon buttons → load / save / video / sound / computer-assist / quit sub-menus |
| `video_menu` | `GMnu_VIDEO` | shadows, view distance, rotate mode, wall height (cluedo), gamma — **cycle buttons bound to live `settings.*` / `video_*` globals** |
| `sound_menu` | `GMnu_SOUND` | 3 volume sliders |
| `autopilot_menu` | `GMnu_AUTOPILOT` | 4 computer-assist radio buttons, bound to `kfx_net_state.comp_player_*` |
| `quit_menu` | `GMnu_QUIT` | yes/no |
| `load_menu` / `save_menu` | `GMnu_LOAD` / `GMnu_SAVE` | save-slot lists (`frontmenu_saves.c`, shared with frontend) |
| `error_box` / `message_box` | `GMnu_ERROR_BOX` / `GMnu_MSG_BOX` | modal text + OK |
| `dungeon_special_menu`, `resurrect_creature_menu`, `transfer_creature_menu`, `armageddon_menu`, `hold_audience_menu` | `GMnu_DUNGEON_SPECIAL` etc. | dungeon-special activation dialogs |
| `instance_menu` | `GMnu_INSTANCE` | (empty button list today) |
| `pause_buttons` | — | the "Paused" caption from the screenshot; not a `GuiMenu` in `menu_list[]` |

**This group is the natural first phase** — it is the smallest self-contained menu set, it reuses
the frontend Options window almost wholesale, and it closes the *"two visually unrelated settings
UIs in one game"* wart `../renderer/04-imgui-gui-foundation.md` §2.3 names.

**Correction (user, 2026-09-06): opening the pause/options menu does *not* pause the game.** Pause
is a separate toggle (`set_packet_pause_toggle()`, the Pause key), and in multiplayer it is
constrained further (`front_input.c:1242` — non-`GKind_LocalGame` pause is limited). Pressing Esc
just `turn_on_menu(GMnu_OPTIONS)` (`front_input.c:409`); the 3D world keeps rendering and, in MP,
keeps simulating underneath it. `active_menu_functions_while_paused()` (`front_input.c:2771`) is
the list of menus that keep taking input while the game *is* also paused. So **this phase still
composites over live 3D** and the "no compositing risk" framing is wrong — it is only *lower* risk
than the sidebar because it is centred, occasional, and not latency-critical to the frame.

**Decided — don't rebuild the settings UI, reuse the frontend one.** The in-game options menu
becomes a **4-button launcher: Save / Load / Options / Quit** (reusing the existing option-menu
icons and the `gui_pretty_background` corner ornaments as ImGui-composited chrome). "Options"
opens the *same* `frontgui_options_frame()` window stage 4 Phase G built for `FeSt_FEOPTIONS` —
one settings screen, one schema (`config_settingschema.{h,c}`), reached from both the frontend and
mid-level. What this needs:
- `frontgui_options_frame()` is already a self-contained `##FeOptions` ImGui window; the only
  `FrontendMenuState` coupling is in the dispatcher (`frontgui_screens.cpp:1818`) and input
  gating, not the body. Make it callable as an in-game modal overlay independent of
  `frontend_menu_state`.
- **A context dimension on the schema** — the user's "disable any that can't be changed in-game,
  and vice-versa". The schema already has `is_enabled` (control-gates-control) and `apply_class`
  (`SApply_Live` / `SApply_NeedsRestart`); add an "available in this context" gate so
  restart-only / frontend-only rows render `ImGui::BeginDisabled()` when opened mid-level, and any
  in-game-only control is disabled in the frontend. Extra `keeperfx.cfg` options §6.2 of the
  frontend doc lists get added to the schema here (that work was already partly scoped there).
- The "Define Keys" button routes to the in-game key-rebind path (or is disabled mid-level for a
  first pass).

`video_menu` / `sound_menu` / `autopilot_menu` as separate sprite sub-menus **go away** — their
controls are schema rows in the unified Options window. **Decided (§8): the autopilot /
computer-assist controls become schema rows on the *Game* tab behind a collapsed
`CollapsingHeader`** — currently just the four aggressive/defensive/construct/creatures-only
toggles, but the section is expected to grow as the AI improves, so it gets its own header rather
than being four loose rows.

### 2.4 Overlays not in `active_menus[]`

- **Messages** — `gui_topmsg.c` (`message_draw`, top-of-screen scrolling strings) and
  `gui_msgs.c`.
- **Box menus** — `gui_boxmenu.c` (`gui_draw_all_boxes`): the draggable cheat / service menus
  (`GuiBox` / `GuiBoxOption`, a separate widget system from `GuiButton`).
- **Tooltips** — `gui_tooltips.c` (`draw_tooltip`), context tooltips over both the panel and the
  world.
- **The event/battle info box** — `text_info_menu` (`GMnu_TEXT_INFO`, `gui_round_glass_background`,
  the dark rounded panel), `battle_menu` (`GMnu_BATTLE`). Bottom-centre.
- **Debug overlays** — `frontmenu_ingame_evnt.c`'s `draw_bonus_timer` / `draw_timer` /
  `draw_gameturn_timer` / `draw_script_timer` / `draw_script_variable` / `draw_frametime` /
  `draw_network_stats` / `draw_consolelog`. Already conditional, low-stakes, good early ImGui
  practice (a debug HUD is what ImGui is *for*).
- **Compass** — `draw_overlay_compass()` (lives in `engine_redraw.c` itself, `kfx_render`).
- **Power hand** — `draw_power_hand()` (`kfx_render`) — the cursor-follow held-creature sprite.
  Arguably not GUI; leave it.

### 2.5 The dispatch / input path

- **Draw**: `redraw_display()` (`engine_redraw.c:928`) → `render_overlay->draw_whole_status_panel()`
  → `render_overlay->draw_gui()` → `->message_draw()` → `->gui_draw_all_boxes()` →
  `->draw_tooltip()`. Three near-identical copies of this block for the three view types
  (`engine_redraw.c:553`, `:611`, `:635`).
- **Input**: `get_gui_inputs(1)` (`front_input.c:2936`), called from `get_players_input()` when
  not paused (or when a menu is active while paused). Sweeps `active_buttons[]`, runs
  `maintain_call` / `ptover_event` / click handlers. Sets `busy_doing_gui`; when
  `game_is_busy_doing_gui()` the packet gets `PCtr_Gui` so `kfx_sim` / `kfx_net` suppress world
  interaction that frame.

---

## 3. Why this is materially harder than the frontend migration

The frontend doc's §1–§2 argument ("palette objection gone, chrome doesn't scale, bitmap fonts
don't scale, clean seam") all still holds. These are the *additional* problems the in-game GUI has
that the menus did not:

1. **It composites over live, moving 3D content at frame rate, not over a static backdrop.** The
   frontend migration kept `frontend_copy_background()` on the software path and let ImGui draw on
   top (`../renderer/04-imgui-gui-foundation.md` §3.4). Here the "backdrop" is the 3D dungeon
   view, re-rendered every frame by
   `engine_render.c` into `lbDrawSurface`, and ImGui already composites on top of that at present
   time. That part is *fine* — the infra exists. What is not free: the HUD must not add perceptible
   latency or cost, and the 3D view underneath it must still be pixel-correct (stage 3 R-series
   risks about palette churn during lighting apply here).

   **§3.1 — Latency mitigation the user flagged: decouple GUI content updates from frame rate.**
   The original engine locked 20 game-turns/sec to 20 fps; frame rate and turn rate are now
   independent. Panel *content* (gold total, counts, progress bars, activity rows, timers) only
   changes on a game-turn boundary, so it can be computed once per game-turn into a cached
   view-model and the per-frame ImGui submission just lays out the cache — not re-walking `Dungeon`
   / `kfx_sim_state` every frame. This bounds the per-frame HUD cost to layout + draw and takes the
   heavy state reads off the render path. Interactive elements (hover, click, tab switch, minimap
   drag) still respond per-frame.

2. **`draw_gui()` and `get_gui_inputs(1)` are load-bearing for gameplay input.** `busy_doing_gui`
   / `PCtr_Gui` / `first_monopoly_menu()` / `point_is_over_gui_menu()` gate whether a click digs a
   tile, drops a creature, or casts a spell. ImGui's `WantCaptureMouse` has to become the
   authority for migrated menus *and* stay consistent with `PCtr_Gui` so the sim layer agrees —
   and this is **multiplayer-deterministic**: GUI clicks that change shared state
   (`gui_toggle_ally`, `gui_set_tend_to`, room/trap selection, `gui_quit_game`) go through the
   packet system. A migrated menu must produce the same packets on the same turns.

3. **§3.3 — The sidebar is a fixed 140×400 virtual canvas scaled by `units_per_pixel`.** Every button in
   `main_menu_buttons[]` is a hand-placed rect in that space (`scr_pos_x/y`, `pos_x/y` in the
   ~640×480-relative grid). **Decided (§8): the first pass keeps a fixed-width sidebar in similar
   position** — a re-skin and a font/text upgrade, not the frontend's full responsive rework. The
   one constraint that costs nothing now and buys a lot later: drive the layout from a single
   panel rect + content reflow, so a future **horizontal bar** or **minimal HUD** variant isn't
   walled out by a second hard-coded 140px grid. Those variants are explicitly not built here.

4. **The minimap is a per-pixel raster, not sprites.** `panel_map_draw_pixel()` × thousands per
   frame into `lbDrawSurface`. In an ImGui-composited panel it has to become: render to an
   off-screen RGBA buffer (or keep poking a scratch buffer) → upload as a dynamic texture → draw
   with `ImGui::Image` at whatever rect the layout gives it. The land-preview panel already proved
   this exact technique; the minimap is a bigger, every-frame version of it. Interaction
   (click-to-recentre, `mouse_is_over_panel_map`, CTA placement) moves onto the texture's rect.

5. **Panel content is driven by deep game state, and a lot of it.** Creature-activity rows
   (`update_creatr_model_activities_list`), buildable counts, research/workshop/payday progress,
   per-player room/creature tallies, anger levels, battle participant lists, dungeon-special
   dialogs. The frontend screens read campaign/save/settings state; these read the whole live
   `Dungeon` / `kfx_sim_state`. Extraction (the `_by_index` / `static`-dropped pattern from stage
   4 Phases D–F) will be more invasive.

6. **`gui_boxmenu.c` is a second, independent widget system** (`GuiBox` / `GuiBoxOption`, dragging,
   `optn_list`) — not `GuiButton` at all. Migrating it is a small separate exercise (it is
   basically a debug/cheat console → an ImGui window is a strict upgrade).

7. **`draw_gold_total` and friends render text from sprite sheets, not fonts** — `button_sprite[]`
   digit glyphs, `winfont`. Moving to ImGui text is the same win as the frontend's font work but
   touches the HUD's most latency-visible element (the gold counter updates constantly).

8. **§3.8 — The panel *frame* art is a dead end; the *icon* art transfers.** `gui2-256/rpanel_256/`
   goes to `-256` (unlike `frontend-64/`), but `rpanel_full.png` is one monolithic 560×1600
   picture of the fixed DK1 vertical sidebar and `panel_divided/` just cuts that same shape into
   region pieces — **not a 9-slice, can't reflow**. So the HUD frame is drawn **procedurally**
   (`frontgui_style.cpp` fills + borders), not from panel sprites. The individual button / tab /
   bar / portrait sprites (`rpanel_btn_*`, `rpanel_tab_*`, `frame_portrt_*`, `bar_*`) and the
   room/spell/trap/creature icons **do** transfer. ([05](05-sidebar-frame-and-minimap.md) §3.)

9. **§3.9 — CRITICAL: one cursor, identical in the world and over the GUI.** Same sprite, same hotspot,
   same position, same colour, whether the pointer is over the 3D view or over a panel. The
   frontend migration's single longest bug tail (`../renderer/04-imgui-gui-foundation.md`, the
   cursor saga: warp-relative-motion divergence, one-shot capture latching a blank/dark palette,
   the `LbSpriteDrawImmediate` memcpy bug, the framebuffer-stride restore bug) was *entirely*
   about making the ImGui-drawn cursor match the game's. In-game the stakes are higher — a
   half-pixel or one-frame cursor offset makes tile-precise digging and creature-dropping feel
   wrong. This must be treated as a hard acceptance gate on every phase, and is the natural place
   to finish stage 3's "fold the legacy `bflib_mspointer.cpp` path into one GPU-composited cursor
   draw at the game's tracked position" item
   (`../renderer/gpu-v2/01-phase-b-2d-compositing.md` §B-cursor) rather
   than carrying two cursor code paths.

   **Landed 2026-09-07:** in-game, `FeStyleGetCursorImage` now mirrors the game's *current* pointer
   sprite (`LbMouseGetSprite()` — the sprite last set via `LbMouseChangeSpriteAndHotspot`) instead
   of the frontend `GFS_cursor_horny` gauntlet: arrow / pickaxe / power hand / per-spell pointers /
   deny mark all show correctly over an ImGui panel and the parchment map. In-game `pointer_sprites`
   decode against the ambient engine palette (no override needed), and the sprite is pre-scaled to
   `scale_ui_value_lofi()` — the same size `LbI_PointerHandler` draws it at over the 3D view — and
   flagged `native_size` so `ImGuiContext` draws it 1:1 (no UI-scale rescale). `ImGuiCursorImage`
   gained `serial` + `native_size`; the context polls the callback every frame and re-uploads a
   `STREAMING` texture on change. `MousePG_Invisible` → callback returns false → no cursor drawn.
   Still two code paths (legacy `bflib_mspointer` for the 3D view, ImGui for over-panel); the full
   single-draw fold remains a stage-3 §B-cursor item.

---

## 4. Architecture

### 4.1 Layering — nothing new needed

The seam already exists. `kfx_render`'s `engine_redraw.c` calls the HUD exclusively through
`const struct RenderOverlayCallbacks *render_overlay`
([`render_overlay.h`](../../../src/kfx_config/include/render_overlay.h)), registered from
`main.cpp::setup_game()`. ImGui lives in `kfx_frontend` (stage 5). `kfx_frontend` already owns
every `frontmenu_ingame_*` file. So the migrated menus are authored in the library that already
owns them, calling wrappers that already exist, behind a callback seam that already exists. The
frontend migration's §3.1 split (`kfx_platform` owns context, `kfx_frontend` owns screens) is
already collapsed into `kfx_frontend` by stage 5.

`check_layering.py --strict` should stay green throughout — this project adds no cross-`kfx_*`
include. Verify after every phase regardless.

### 4.2 The per-menu opt-in seam

`draw_active_menus_buttons()` (`frontend.cpp:3480`) is the discriminator. Proposed shape, mirroring
`frontend_imgui_screen_active(state)`:

```c
// true == "ImGui HUD enabled AND this GMnu_* has been migrated"
TbBool ingame_imgui_menu_active(MenuID menu_id);
```

- In `draw_active_menus_buttons()`: for a migrated, active menu, skip the legacy
  `callback(gmnu)` + `draw_menu_buttons(gmnu)` and instead let the per-frame ImGui submission
  (a new `render_overlay` entry, or folded into the existing `FrontendImGuiFrame` dispatch) draw
  it.
- In `get_gui_inputs(1)`: skip the `active_buttons[]` sweep for buttons whose `gmenu_idx` is a
  migrated menu — the ImGui widgets own that input now — but **keep** setting `busy_doing_gui` /
  `PCtr_Gui` when `io.WantCaptureMouse` (or the pointer is over a migrated panel's rect), so the
  sim layer still suppresses world clicks.
- `turn_on_menu()` / `turn_off_menu()` / `menu_is_active()` stay the source of truth for *which*
  menus are up — the ImGui side reads them, it does not replace the menu-stack machinery
  (`../renderer/04-imgui-gui-foundation.md` §3.5's "un-migrated states keep their path" applied to
  menus).

### 4.3 Submission entry point

Two options, decide in Phase 1:

- **Reuse `FrontendImGuiFrame()`** (`frontgui_screens.cpp`, the current `submit` callback) and have
  it also dispatch in-game panels when a level is loaded. One ImGui submission site, simplest.
- **A dedicated `render_overlay->submit_ingame_gui()`** called from `redraw_display()` alongside
  the existing `draw_gui()` slot. Keeps the frontend and in-game submission code separate; matches
  the existing `render_overlay` granularity.

**Decided (§8): fold into `FrontendImGuiFrame()`** — one ImGui submission path, matching stage 5's
"one library talks to ImGui" direction. It gains an in-game dispatch arm alongside the frontend
`FeSt_*` one.

### 4.4 The minimap texture

Same off-screen-render → dynamic-texture → `ImGui::Image` + hit-rect mechanism the land-preview
panel uses (`RendererCreateDynamicTexture` once, `UpdateDynamicTexture` from the buffer,
`ImGui::Image` in the panel layout). `panel_map_*` keeps its raster (or is rewritten to fill an
RGBA buffer directly now that `TbPixel` is true-colour). `map_to_minimap` /
`mouse_is_over_panel_map` / CTA-placement hit-testing rebase onto the image rect, cached from last
frame's layout (the land-preview one-frame-stale-rect trick). `draw_overlay_compass` composites
over the same texture or as a sibling draw.

**Where this is harder than the land preview (§8):** the minimap is a *moving viewport* —
`player->minimap_zoom` + `minimap_pos_x/y` + Alt-drag recentre pan a window over a map much larger
than the panel. The static campaign preview had no zoom/pan. Two candidate shapes, decide in
Phase 4:
- **Map-sized buffer, sample a sub-rect** — render the whole known map once (invalidate on
  reveal / room change), let `ImGui::Image` UVs pick the zoomed window. Cheaper per frame, more
  memory, needs an invalidation story.
- **Viewport-sized buffer** — `panel_map_*` keeps doing the zoom/pan math and fills a
  panel-sized buffer each turn. Closer to today's code, re-rasterises every update.

---

## 5. Scope boundaries (proposed)

**In scope:** everything in §2.1–§2.4, **plus the first-person / possession HUD** (see below).

- **First-person / possession HUD — IN scope.** The user's call: the interface has to be
  stylistically consistent, so the possession view's own GUI can't stay classic while the
  top-down HUD is ImGui. It is the same building blocks as the top-down sidebar: minimap +
  creature portrait + life bar + the 2×5 icon-and-timer instance table (`creature_menu` /
  `creature_query_menu*` territory), plus the page-down button and the numerical values + icons
  for the query panels. Migrating the top-down sidebar wrappers (§6) gets most of this for free;
  the remaining work is the possession-specific layout and its entanglement with
  `PVT_CreatureContrl` view code. Sequence it right after the top-down tab panels, not last.

- **`gui_parchment.c` parchment map — in scope, and should be one of the easier pieces.** It is
  essentially a *live* version of the land-preview panel the free-play / skirmish frontend menu
  already renders (`frontmenu_landpreview.c`, embedded as an ImGui-composited dynamic texture in
  stage 4 Phase E). Same off-screen-render → dynamic-texture → `ImGui::Image` + hit-rect
  mechanism, pointed at the live overhead map instead of a static campaign preview. Reuse that
  code path rather than treating the parchment as a bespoke full-screen screen.

**Out of scope — world objects, not GUI:**

- **`draw_power_hand()`** — cursor-attached held-creature sprite. Leave on the current path (but
  see §3.9 — the *cursor itself* is unified).
- **Creature health-flower and level pips** — these are world-space objects rendered in the 3D
  scene, not GUI chrome. Out of scope entirely.
- **Floating damage numbers / other in-world `bflib_sprfnt` text** — world-space, out of scope.

**Text boundary:** in-engine text that appears *through a message/info box* (`gui_topmsg.c`,
`text_info_menu`, `battle_menu`, tooltips) moves to the ImGui font engine — it is GUI. Creature
names and health bars belong to the first-person GUI and come along with that phase. Health-flower
and level pips (above) stay world objects.

**Decisions the brief already makes:** "not an exact duplicate, ideally close" — so §3.3's panel
re-layout is allowed to modernise geometry, and the type/chrome follows `frontgui_style.cpp` as
the frontend menus do, not a pixel-match of the 1997 panel. Reuse the existing option-menu icons
and panel corner ornaments where they fit.

---

## 6. Wrapper additions likely needed

The frontend wrapper set (`frontgui_widgets.h`) covers panels, lists, buttons, sliders,
checkboxes, tabs, text roles, modals. The HUD probably also wants:

- **`FeIconGrid` / `FeIconButton` with count + flash + disabled states** — the room/spell/trap
  build grids are 4×N icon buttons with a live count badge and the "flash to draw attention"
  behaviour (`gui_set_button_flashing` / `spangle_button`). `FeIconButton` exists; the grid +
  badge + flash overlay is the addition.
- **`FeProgressBar`** — payday / research / workshop / anger / xp bars (`gui_area_progress_bar_*`).
- **`FeStatReadout`** — icon + label + value row, used ~40× across the creature-query pages.
- **A compact `FeValue` text style** for the gold counter and timers (tabular figures, the digit
  sprites' role).
- **`FeMinimap`** — wraps the dynamic-texture + hit-rect dance from §4.4 so panel code just places
  it.

All procedural / font-metric-driven, same contract as the existing wrappers. Build them in the
phase that first needs them, not up front.

---

## 7. Phasing — index

Each phase independently shippable; un-migrated menus keep the sprite path. `-classicmenu` is
retired (§1) -- `GUI_ICON_PACK=CLASSIC`/`ingame_gui_use_classic_hud()` forces the in-game HUD back
to the sprite path now; nothing forces the frontend back, since it has no sprite path left. Same
phase discipline as `../renderer/04-imgui-gui-foundation.md` §7. **Each phase has
its own doc** with the code-level investigation, entanglements, checklist and open questions —
written up front for the near phases, scaffolded (goal + what to investigate on arrival) for the
later ones whose shape depends on earlier outcomes.

| # | Doc | Scope | Depends on |
|---|-----|-------|------------|
| 0 | [01-seam-and-toggle.md](01-seam-and-toggle.md) — **landed 2026-09-06** | `frontgui_ingame.{cpp,h}`: `ingame_imgui_menu_active()` / `_modal_active()`, submission arm in `FrontendImGuiFrame`, `get_gui_inputs` / `PCtr_Gui` gating, per-game-turn cache scaffold. Proof menu: `quit_menu` as an ImGui modal. Cursor unification deferred to Phase 4 (needs a non-modal panel). | — |
| 1 | [02-pause-menu-and-options.md](02-pause-menu-and-options.md) — **landed 2026-09-06** | 1a: 4-button launcher (`GMnu_OPTIONS`), `frontgui_options_frame(in_game)` reused, topmost-monopoly stack rule, sprite-icon buttons (`frontgui_sprite_tex`). 1b: `GMnu_LOAD`/`GMnu_SAVE` as ImGui slot lists; video (rotate/wall-height/gamma) + autopilot folded into the Options window (`in_game`-gated). Sprite sub-menus stay for `-classicmenu` only. | 0 |
| 2 | [03-debug-overlays-and-box-menus.md](03-debug-overlays-and-box-menus.md) — **landed 2026-09-06** | `frontmenu_ingame_evnt.c` timer/frametime/network/consolelog → `frontgui_ingame_debug.cpp`; `gui_boxmenu.c` cheat/service boxes → `frontgui_ingame_boxmenu.cpp` (legacy `GuiBox` machinery kept, only draw+hit-test swapped). Both legacy paths early-return under `RendererImGuiEnabled()`. | 0 |
| 3 | [04-messages-tooltips-infobox.md](04-messages-tooltips-infobox.md) — **landed 2026-09-06** | `gui_topmsg.c` banner, `gui_msgs.c` queue, `gui_tooltips.c`, `text_info_menu`, `battle_menu`, Paused caption + MP chat line (`engine_redraw.c`) → ImGui. New modules `frontgui_ingame_text.cpp`, `frontgui_ingame_battle.cpp`; `frontgui_sprite_tex` gained panel-sprite textures/buttons + engine-palette forcing. Only the spell-cost cursor number deferred. | 0, 2 |
| 4 | [05-sidebar-frame-and-minimap.md](05-sidebar-frame-and-minimap.md) — **landed 2026-09-06** | `frontgui_hud_layout.{h,cpp}` + `frontgui_ingame_panel.{h,cpp}`: `GMnu_MAIN` migrated — procedural panel chrome, gold text, minimap as an off-screen-raster texture + ImGui compass, 5 tab icons (spangle-aware), zoom/map/autopilot as text stand-ins, 13 event markers (blink + falling). Minimap + tab content still legacy-drawn through transparent holes; `status_panel_width` left legacy (seam swap → Phase 5); cursor fold deferred to stage 3. | 0, 3 |
| 5 | [06-tab-content-panels.md](06-tab-content-panels.md) — **all five tab bodies landed 2026-09-07** — `GMnu_ROOM` / `SPELL` / `TRAP` / `CREATURE` / `QUERY` in `frontgui_ingame_tabcontent.{h,cpp}`, drawn inside the sidebar frame's own window (no z-order seam), config-driven grids (no array mutation, no 2nd page), ImGui-drawn cells + vector-font `?`/`$`. Remaining: scroll-on-overflow, per-turn cache, packet-parity ftest rows, retiring `update_*_tab_to_config` + `GMnu_ROOM2`/`SPELL2`/`TRAP2`. `GMnu_SPELL_LOST` → Phase 6. | 4 |
| 6 | [07-first-person-hud.md](07-first-person-hud.md) — **in progress 2026-09-07** — shared `creature_query` panel (ABILITIES/STATS, top-down + possession) + `GMnu_SPELL_LOST` landed in `frontgui_ingame_tabcontent.cpp`; legacy 4-page nav neutered; possession needs no extra frame work (`GMnu_MAIN` stays on). Remaining: instance-click-to-cast, passenger-vs-control diff, interactive verification. | 5 |
| 7 | [08-parchment-map.md](08-parchment-map.md) — **functionally complete 2026-09-07** — `frontgui_ingame_parchment.{h,cpp}`: the parchment view (paper + overhead map + zoom box + level name) captured to a dynamic texture and composited full-screen by ImGui; sidebar hidden, cursor via the screen-owned path, dead fade branch removed. Remaining (low priority): ImGui-native paper/frame, zoom box as its own element. | 5 |
| 8 | [09-relief-and-emboss-pass.md](09-relief-and-emboss-pass.md) — **landed 2026-09-08** — procedural relief skin for the ImGui sidebar (`frontgui_ingame_relief.{h,cpp}`: `relief::` bevel / plateau / face / well / well_circle / well_tri / boss / groove / groove_h / ring / edge_frame + domain-warped-sine marble, one shared `Tones` @ rgb 60,44,12). Applied across every surface + the main-menu list bg. Also: full-screen 3D under the HUD (no `status_panel_width` inset), Tab/Ctrl+Tab hides the HUD, new "GUI" options tab + live `UI_FONT` picker (`fxdata/font/` scan). | 4, 5, 6 |
| 9 | [10-maintainability-refactors.md](10-maintainability-refactors.md) — **§2–§8 landed 2026-09-08, §6/§7 landed 2026-09-11** — post-first-pass structural cleanup, no behaviour change: one `FeDeferredQueue` (§3), named tab-content layout constants (§5), shared `fe_noise.h` (§2), one HUD colour source (§4), scoped offscreen-capture RAII (§8), one interactive-cell + bar primitive (§6), `tabcontent.cpp` split into cells/grids/creature (§7). Same commit added the first behaviour change since Phase 8: `GUI_POSITION` (Left/Right, GUI options tab) — the sidebar mirrors onto the right screen edge, recomputed live in `frontgui_ingame_panel.cpp` rather than baked into legacy menu creation. | 0–8 |
| 10 | [11-horizontal-layout.md](11-horizontal-layout.md) — **planning, 2026-09-11** — the third `GUI_POSITION` value (`Bottom`), wiring up `frontgui_hud_layout.h`'s long-stubbed `HudLayout_HorizontalBottom`: minimap+nav / tab-headers+2×8-grid / message-queue+horizontal-event-markers as three regions along a bottom strip, a special cropped-portrait creature cell, and a possession-view proposal. Structural prerequisite identified: region-scoped rects instead of one whole-panel rect. Several open questions flagged, not yet answered. | 9 |
| 11 | [12-png-icon-overrides.md](12-png-icon-overrides.md) — **first landing, 2026-09-11** — user PNG icon packs (`fxdata/gui/<pack>/`) overriding room/power/trap/creature/ability icons, the static job/tendency/bar/battle/confirm/launcher icons, and the procedural marble panel background, selected live from a new `GUI_ICON_PACK` GUI-tab setting mirroring `UI_FONT`'s `fxdata/font/` scan pattern verbatim. New `frontgui_ingame_icon_overrides.{h,cpp}` module (plain RGBA8 via `spng`); every call site wired with an override-or-legacy fallback. Deferred (documented, not dropped): colorized tab icons, the ~20 Stats-page icons. Build/layering/ftest-verified + smoke-tested with a throwaway pack; not yet visually confirmed in a live session. | 9 |
| 12 | [13-minimal-layout.md](13-minimal-layout.md) — **first landing, 2026-09-12** — the third `HudPanelLayout` (`Minimal`, `GUI_POSITION` value 4, `hud_position_type[]`): no persistent panel, just a floating corner minimap+gold+event cluster (`MINIMAP_CORNER`, upper-left/upper-right, default upper-left) and a free-floating Room/Spell/Trap/Creature/Query button cluster (diagonally opposite corner, icons only, no plinth chrome) that toggles a pop-up panel — click again or a different button to close/swap it, world clicks never dismiss it (multi-click panel↔3D workflows). Pop-up reuses the vertical layout's own size and the message-box's plain `WindowBg` (no procedural marble); content reuses the vertical layout's grid/panel functions unchanged, including `creature_query_panel()` for possession (flagged as needing its own follow-up pass). Build/layering/ftest-verified across both corners; not yet visually confirmed in a live session. | 9 |

**Later, separately — retire the toggle**, once every phase has shipped and stabilised. Deliberate,
not automatic. **Done 2026-09-12** -- with a change from the original plan: the frontend's legacy
sprite menus were deleted outright, but the in-game HUD's `frontmenu_ingame_*` sprite draw paths
were kept live, live-tested request, moved onto `GUI_ICON_PACK=CLASSIC` instead of deleted. §8's
"Toggle" decision has the detail.

---

## 8. Decisions on record

- **Toggle: `-classicmenu`, one flag for frontend + HUD together.** No `-classicgui` sibling. §1.
  **Retired 2026-09-12**: the frontend's legacy sprite menus are gone outright (no toggle needed --
  `frontend_imgui_screen_active()` no longer has a global switch to AND against, only its per-`FeSt_*`
  `state_is_migrated()` table). The in-game HUD kept its legacy sprite renderer available, by
  request, but as a per-session *style* choice on the existing `GUI_ICON_PACK` setting (reserved
  `"CLASSIC"` value) rather than a command-line switch -- `ingame_gui_use_classic_hud()`
  (`config_keeperfx.h`/`.c`) is what every in-game HUD draw call now checks.
- **Pause menu: 4-button launcher (Save/Load/Options/Quit), Options reuses the frontend
  `frontgui_options_frame()` window** with a schema context-availability gate. Video/sound
  sub-menus retire into schema rows. §2.3.
- **First-person / possession HUD is in scope**, sequenced right after the top-down tab panels
  (Phase 6). §5.
- **Parchment map is in scope**, built by reusing `frontmenu_landpreview.c`'s dynamic-texture
  panel (Phase 7). §5.
- **Out of scope: creature health-flower, level pips, floating world text, the power-hand
  sprite** — world objects, not GUI. §5.
- **Panel content updates on a game-turn boundary, not per frame** — cached view-model, per-frame
  submission just lays out the cache. §3.1.
- **One cursor, world and GUI identical** — hard acceptance gate every phase; finish stage 3's
  cursor unification here. §3.9.
- **Panel geometry: ship the current vertical-right sidebar first**, as a re-skin, not a
  responsive rework. But express it through a **`PanelLayout` descriptor** ([05](05-sidebar-frame-and-minimap.md)
  §0) that also expresses the **horizontal DK2-style layout** — regions
  (`minimap`, `tabstrip`, `tabcontent`, `gold`, `events`) each get a rect from the layout;
  the horizontal one puts minimap far-left, the room/spell/trap/creature tabs beside it, gold as a
  top bar. Region code lays out *within* its assigned rect, never against a 140-relative constant
  or a screen edge. `get_status_panel_width()` becomes "inset the 3D viewport by this much on the
  layout's axis". The horizontal/minimal layouts are future work — this project only keeps them
  possible. §3.3.
- **Minimap: off-screen RGBA render, same as the land-preview panel.** Caveat: the minimap's
  live zoom/pan window (`player->minimap_zoom`, `minimap_pos_x/y`, Alt-drag recentre) is a
  moving viewport into a larger map, which the static land preview didn't have — expect extra
  work in the render-to-buffer step and the hit-rect ↔ map-coord mapping. §4.4.
- **HUD submission folds into the frontend site** (`FrontendImGuiFrame` / `frontgui_screens.cpp`),
  not a separate `render_overlay` slot — one ImGui submission path, matching stage 5's direction.
  §4.3.
- **Autopilot / computer-assist controls: schema rows on the Settings *Game* tab, behind a
  collapsing header** (`ImGui::CollapsingHeader`, collapsed by default). Room to expand this
  section as the AI improves without it dominating the tab. §2.3.
- **Room / power / trap grids scroll instead of paging.** The 2nd page
  (`GMnu_ROOM2`/`SPELL2`/`TRAP2` + the next-page button) was a hack to fit >16 items in a fixed
  grid; the ImGui grid is one scrollable `FeIconGrid` and the second `GMnu_*`, `gui_set_page`,
  and the `panel_tab_idx` 1..32 slot scheme are removed. [06](06-tab-content-panels.md) §2.1.
- **Do this project before stage 3's GPU Phase B.** Confirmed with the user — it is stage 3 §B3
  route 1 ("wait for the in-game-GUI-as-ImGui project… B3 is subsumed"). This project runs
  against today's `RendererSoftware` unchanged (the overlay path already composites in-game); the
  cursor-unification item (§3.9) is the one piece of shared work with stage 3 §B-cursor and is
  done here.

## Open questions — all resolved (2026-09-06)

**Every phase doc's "Open questions" section now has:** a **code-investigation pass** resolving the
factual ones (packet-routing of every handler, the `status_panel_width` / `setup_engine_window`
seam, `game_kind` as the level predicate, dead code paths — parchment fade, `screen<=320` —
tooltip side-effect analysis, no in-game key-rebind menu, etc.) **plus user decisions** on the
genuine choices:

- **Minimap / parchment: map-sized off-screen buffer + `ImGui::Image` UV crop** for the zoom
  window; invalidate on dig/build/reveal/door. The panel minimap, the overhead map and the zoom
  box can share one rasterised buffer. ([05](05-sidebar-frame-and-minimap.md) §2,
  [08](08-parchment-map.md) §6.)
- **Creature-detail / first-person panel: collapse the 4 query pages to 2** — "Abilities" +
  scrollable "Stats". ([07](07-first-person-hud.md) §2.)
- **In-game Options: `SApply_NeedsRestart` options are disabled in-game** (blanket rule, no new
  schema field). ([02](02-pause-menu-and-options.md) §3.)
- **`HudLayout` lives in a new `frontgui_hud_layout.{h,cpp}` module.** ([05](05-sidebar-frame-and-minimap.md) §0.)
- **Horizontal (DK2) layout: separate room/spell/trap/creature blocks, each an N×2 icon grid**
  (vs. the vertical layout's one 4×N grid). `FeIconGrid` takes an orientation.
  ([05](05-sidebar-frame-and-minimap.md) §0, [06](06-tab-content-panels.md) §2.1.)
- **Panel chrome is procedural**, not sprite art — `rpanel_256/` is a monolithic fixed-shape
  image, not a 9-slice, unusable for a reflowing panel. Button/icon sprites still transfer.
  ([05](05-sidebar-frame-and-minimap.md) §3.)
- **Compass rose migrates in Phase 4** (not deferred). ([05](05-sidebar-frame-and-minimap.md) §3.)

**Standing task (not a question):** the `ftest_packet_capture` / `ftest_gui_packet_parity` harness
grows a golden-trace row + ImGui-path assertion per migrated menu. ([06](06-tab-content-panels.md) §4.)

---

## 9. Verification (every phase)

- `KFX_OS=linux ./build-cmake.sh` + mingw cross-compile, both variants.
- `python3 scripts/check_layering.py --strict` — must stay green; this project adds no
  cross-library include.
- Full Catch2 suite green.
- **Behaviour parity, not screenshot-diff** (appearance deliberately changes) — every panel
  interaction, every setting written and re-read, every tab, both toggle states.
- **Single- and multiplayer** for any phase touching a menu that sends packets (Phase 0 onward);
  a `ftest_gui_packet_parity` golden-trace row asserting identical packets on identical turns for
  each migrated menu (harness built — [06](06-tab-content-panels.md) §4).
- Un-migrated menus **pixel-identical** after each phase (that diff *is* valid, and guards the
  two systems staying independent).
- **Cursor**: world ↔ GUI cursor identical in shape, hotspot, position and colour — a screenshot
  with the pointer straddling a panel edge, every phase (§3.9).
- Frame-time comparison against the current build for the sidebar phases — the HUD is the
  latency-sensitive part the frontend menus were not.
- Legibility at 640×480 / 1080p / 4K, Latin + Cyrillic + CJK, both display faces (Exocet /
  Cinzel).

---

## 10. Cross-references to update once this lands

- [`../renderer/04-imgui-gui-foundation.md`](../renderer/04-imgui-gui-foundation.md) §2.3 / §10 —
  the "in-game GUI is a separate project, revisited **after** GPU work" deferral is superseded:
  this project runs **before** stage 3 Phase B. Record that, that it picked the project up, and
  how the two settings UIs converged (one `frontgui_options_frame()` window, schema context gate).
- [`../renderer/gpu-v2/01-phase-b-2d-compositing.md`](../renderer/gpu-v2/01-phase-b-2d-compositing.md)
  §B3 — route 1 ("wait for the in-game-GUI-as-ImGui project") is the confirmed sequencing; B3 is
  subsumed; §B-cursor unification moves into this project.
- [`../renderer/00-overview.md`](../renderer/00-overview.md) roadmap table — add this initiative,
  sequenced before stage 3 Phase B.
- [`docs/Architecture/architecture.md`](../../Architecture/architecture.md) §5 — if any new
  `RenderOverlayCallbacks` entry is added for the HUD submission path.
- [`docs/refactor/gui/00-overview.md`](../gui/00-overview.md) — the in-game HUD was listed as an
  out-of-scope consumer of the `GuiButtonInit` arrays; note the migration.
