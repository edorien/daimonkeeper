# Phase 4 — the sidebar frame and the minimap

Status: **landed 2026-09-06** — the sidebar frame, gold, minimap-as-texture, compass, the 5 tab
headers, zoom / map / autopilot buttons and the 13 event markers are all ImGui. Deferred:
`status_panel_width`→`hud_layout` seam swap (blocked on Phase 5), gold-from-cache, map-sized
minimap buffer, and the full cursor fold (§3.9 — visually fine, own stage-3 pass). Depends on
[01-seam-and-toggle.md](01-seam-and-toggle.md); [04](04-messages-tooltips-infobox.md) proved the
text stack. See [00-overview.md](00-overview.md) §2.1, §2.2, §3.1, §3.3, §4.4.

**Cosmetic pass 2026-09-07 (retest):** panel outer border removed (it stuck a floating line out
past the overhanging event-marker column) -- `draw_background` fills the column solid down to the
tab grids. Minimap buffer cleared transparent, not near-black, so the diamond's corners read as
panel not a black square. Nav buttons (M/+/-/cpu) lost their border. Tab spangle drawn in a second
pass (icons first, then ring + corner sparks) so it can't tuck behind an adjacent icon.
`sprite_button_body` lost the blood-red hover ring on the message-box / quit-modal icons -> soft
warm wash. Payday bar shows `dungeon->creatures_total_pay` centred in the track.

**Cosmetic pass 2, 2026-09-07:** the persistent gold line down the sidebar (and around the chat box)
was `style.WindowBorderSize` (1.5px, `ImGuiCol_Border` = gold) on every ImGui window -- fixed by
pushing `ImGuiStyleVar_WindowBorderSize = 0` around the HUD windows in `ingame_imgui_frame()` (the
modal panels below keep it). Minimap redness: `panel_map_draw_slabs`'s background-blend classes
(fog-of-war / wall / gold / gems / tagged / abyss) were blending toward the old near-black-**red**
buffer clear that `setup_background()` sampled; the buffer clear is now transparent (-> sampled as
black), and `render_minimap()` forces one `reset_panel_map_background_cache()` so the tables
re-sample it. Rock / lava / water / rooms / paths were already flat colours matching the parchment
(`setup_panel_colors`).

**Real cause (retest 2, 2026-09-07):** `panel_map_draw_slabs` / `setup_panel_colors` resolve every
slab colour through `RendererGetActivePalette()`, which at ImGui present time (when `render_minimap()`
runs) is **not** the game's engine palette -- the same gotcha the panel sprites hit. So rock / lava
/ water / fog all decoded to the wrong (reddish) hues. Fixed by forcing `engine_palette` around the
`panel_map_draw_slabs` / `_overlay_things` capture, exactly like `FeGuiPanelTexture`. Exact parity
with the parchment would still need the blended classes ported to flat colours (and fog-of-war is
drawn dark on the minimap vs not drawn at all on the parchment) -- a gap by design.

**Cosmetic pass 3, 2026-09-07 (retest):** event markers are now **fully ImGui-drawn** --
`draw_event_markers()` / `event_style()`: a rounded body (right corners) + either a coloured vector
glyph (`?` green / `i` blue-or-green / `!` blue-with-white-top for objectives / info / attacks) or
one of the room/spell/trap/creature tab icons (`GPS_rpanel_rpanel_tab_infoa + 4/8/12/16`) for the
new-room / new-spell / new-trap / new-creature kinds -- **retest: switched to the exact query-panel
sprites** (`get_player_colored_icon_idx(GPS_plyrsym_symbol_room/player_red_std_a)`,
`GPS_room_research_std_s`, `GPS_room_workshop_std_s`), and the marker geometry sits entirely in the ImGui
window's right overhang (the +44px margin past the sidebar edge, ~square, 3px clear of the panel)
so it neither clips nor overlaps panel content. Combat kinds (HeartAttacked / EnemyFight /
FriendlyFight / Breach / RoomUnderAttack / RoomLost / AlarmTriggered) use `GBS_guisymbols_sym_fight`
(the crossed-swords button sprite -- `tab_crtr_fight_std-borderless.png` isn't in the packed atlas;
that would need it added to the `filelist_gui2.txt` files + `make pkg-enginegfx` + a new enum).
Pulsing gold ring while unread
(`!(my_event_button_state & EvBtnS_Read)`), steady gold while it's the open one, `EvF_BtnFalling`
slide-in kept; `event_marker_sprite()` removed.

**Cosmetic pass 4, 2026-09-07 (retest):** the markers had a big vertical gap between them --
each was drawn centred in its own legacy per-slot rect, and those rects scale 30 virtual px up to
~55-80 screen px, far taller than the ~square markers. `draw_event_markers()` now anchors the
column to slot 0's rect (`b0->scr_pos_y + b0->height`) and stacks the markers *upward* at a fixed
`mh + 1` (= 41px) pitch keyed to the slot index -- an empty slot still leaves a gap (matching the
legacy fixed-slot layout), but a run of populated slots reads as a contiguous stack. Marker size
40×40. `mx = edge + 3` unchanged (entirely in the +44px right overhang). Query-mode button in `query_panel` is now a red `?`
glyph (`glyph_toggle`), not the pixelated `tab_crtr_wandr_std` sprite. Possession header
re-proportioned ~80:20 -- a big `CGI_QuerySymbol` portrait with two slim vertical anger/xp bars in
the right column.

**Landed so far — the layout module (§0):** `frontgui_hud_layout.{h,cpp}` — `enum HudPanelLayout`
(`VerticalRight` wired; `HorizontalBottom` / `Minimal` stubbed), `enum HudRegion`
(`Minimap`/`TabStrip`/`TabContent`/`Gold`/`Events`), `struct HudLayout` (per-region `HudRect` +
`viewport_inset`). `hud_layout_frame(w,h)` rebuilds on display-size change; `hud_region_rect(r)` /
`hud_layout_viewport_inset()` (C-callable, for `render_overlay->get_status_panel_width()`). No
consumers yet — pure geometry, wired up piece by piece below.

**Sub-chunk order (each checkpointed):**
1. layout module ✅
2. **frame v1 ✅** — `frontgui_ingame_panel.{h,cpp}`, `GMnu_MAIN` added to `menu_is_migrated()`
   (so `draw_active_menus_buttons` + the `get_gui_inputs` sweep skip it), `ingame_panel_frame()`
   from `ingame_imgui_frame()`. `draw_whole_status_panel()` skips the bg tiled sprite + gold +
   placefiller under `RendererImGuiEnabled()` — **keeps the minimap raster** (still draws to the
   real framebuffer; shows through a transparent hole; geometry both derive from the scaled
   `GMnu_MAIN` menu rect). ImGui draws: procedural bronze chrome (rails + the strip between minimap
   and tab content, transparent over minimap + tab-content holes), gold as heading-font text, the
   5 tab icons (`gui_set_menu_mode`), zoom in / out (`gui_zoom_in` / `_out`), and the 13 event
   markers (base sprite only — `event_button_info[kind].bttn_sprite` / `event->icon_idx`; no blink
   / falling / read-state yet). Buttons drawn at the real legacy `get_gui_button(BID_*)` screen
   rects; clicks go through a captured-value deferred trampoline (a stack `GuiButton` carrying just
   `btype_value` / `content.lval`). Window sized to the panel (+44px right margin for the
   overhanging event column) so `io.WantCaptureMouse` → `busy_doing_gui` only fires over the bar.
   `status_panel_width` **left legacy** (not swapped to `hud_layout_viewport_inset()`) so the 3D
   inset + legacy tab content stay put — the seam swap moves to a later chunk once the panel owns
   its own geometry.
   **Not yet:** fullscreen-map + autopilot buttons (handlers aren't in a public header),
   event-marker blink/falling/read-state, tab spangle, the gold from the per-turn cache.
3. **minimap + compass + FS-map/autopilot + event polish ✅** — `panel_map_draw_slabs` /
   `_overlay_things` guarded under `imgui_hud`, re-run by `render_minimap()` into an off-screen
   buffer (`RendererSwapFramebufferTarget`), uploaded as a dynamic texture and `ImGui::Image`d at
   the same `PanelMapX/Y` the legacy raster used (from `player->minimap_pos_*` × `mm_upp`) — so
   `front_input.c`'s `mouse_is_over_panel_map()` hit-test is untouched. Buffer covers
   `[0..px+diag]²`, clamped ≤1024; the `ImGui::Image` samples the `(px,py)..(+diag,+diag)`
   sub-rect. `draw_overlay_compass()` (`engine_redraw.c`) early-returns under `RendererImGuiEnabled()`;
   the ImGui compass draws N/S/E/W over the texture, rotated by `cam->rotation_angle_x`.
   `gui_go_to_map` / `gui_turn_on_autopilot` exported and wired (`BID_MAP_ZOOM_FS` / `BID_ASSIST`).
   Event markers now use the full `gui_area_event_button` sprite-index logic
   (`event_marker_sprite()` — custom-icon / blink / read-state offsets) + the `EvF_BtnFalling`
   `interpolate_synced()` slide-in.
   **Not yet:** tab spangle/flash, gold from the per-turn cache, minimap map-sized buffer (still
   viewport-sized).
   **Tuning (user 2026-09-06):** the zoom / autopilot / big-map legacy sprites bake in a chunk of
   the old panel background, so they show as text stand-ins (`+` `-` `M` `cpu`, `slot_text_button`,
   auto-widened) until dedicated ImGui art. Minimap buffer clear changed to opaque near-black and
   the buffer given fixed 512px slack (a stale `MapDiagonalLength` made it too small -> half-drawn
   scanlines).
4. **tab spangle ✅ + nav-button rework ✅** — nav buttons (`+` `-` `M` `cpu`) now positioned by
   this module around the minimap rect (not the legacy tall-thin slots), compact auto-sized
   `slot_text_button`, `cpu` stays lit while `dungeon->computer_enabled & 0x01`. Tab spangle: a
   pulsing yellow border on the tab `button_designation_to_tab_designation(flash_button_index)`
   maps to, while that tab isn't active (mirrors `draw_menu_spangle`).
   **Not yet:** gold from the per-turn cache (still a direct `total_money_owned` read -- cheap,
   deferred), minimap map-sized buffer.
5. **`status_panel_width` → `hud_layout` seam swap — blocked on Phase 5.** The legacy tab-content
   grids (`room_menu` etc.) still draw relative to the legacy `GMnu_MAIN` menu rect, so the panel
   geometry and `status_panel_width` must keep matching it until tab content is ImGui too. Deferred
   to the start of [06](06-tab-content-panels.md).
6. **cursor unification (§3.9) — assessed, deferred as focused stage-3 work.** In-game the game
   cursor (`bflib_mspointer`, engine palette, follows spell/dig pointers) is baked into the
   software frame before ImGui composites; over an ImGui window it's covered and
   `ImGuiContext.cpp` draws a stand-in (`FeStyleGetCursorImage` → `GFS_cursor_horny`, frontend
   palette, `DisplaySize.y/32` scale). The two **visually agree for the gameplay gauntlet** (every
   Phase 0–4 screenshot), so it isn't a blocking regression. The real fold — one draw of the
   game's *current* pointer sprite over everything at present time, matching scale + palette
   across the boundary, and spell-pointer shapes over the panel edge — is stage 3's §B-cursor item
   and wants its own pass, not to be rushed inside this phase.

Goal: the always-on sidebar — panel chrome, gold counter, compass, the 5 tab headers, the
zoom/map/autopilot buttons, the minimap as a composited texture, and the 13 event-notification
buttons. **First pass ships the current vertical-right layout** ([00-overview.md](00-overview.md)
§8) — a re-skin plus the font/text upgrade, not the frontend's responsive rework — but the layout
is expressed through the descriptor in §0 so the **horizontal (DK2-style) and minimal variants
aren't walled out**.

Tab *content* (`room_menu` etc.) stays legacy-drawn inside the ImGui-composited frame until
[06-tab-content-panels.md](06-tab-content-panels.md); this phase is the frame + minimap + the
`GMnu_MAIN` buttons only.

---

## 0. Panel layout — the parameterisation (decided, user 2026-09-06)

The sidebar is **composed of named regions**, and a layout descriptor assigns each one a rect. The
regions are:

| Region | Content |
|---|---|
| `minimap` | the minimap texture + compass |
| `tabstrip` | the 5 tab headers (info / room / spell / manufacture / creature) + zoom / map / autopilot buttons |
| `tabcontent` | the active tab's body (Phase 5) |
| `gold` | the gold counter |
| `events` | the event-notification markers |

Two layouts, both built from that region set (the API must express both from the start; only
the vertical one is *wired up* in this phase):

- **`PanelLayout_VerticalRight`** (default, current KeeperFX / DK1) — regions stacked top-to-bottom
  in a fixed-width right column: `gold` (or a hotspot) near the top, `minimap` below it,
  `tabstrip` as a horizontal row of tab buttons, `tabcontent` filling the rest, `events` down the
  right edge.
- **`PanelLayout_HorizontalBottom`** (DK2-style, the user's reference screenshot) — regions laid
  out **left-to-right along a bottom strip**: `minimap` at the far left, then the room / spell /
  trap / creature tab groups **as separate blocks side by side, each an N×2 icon table** (not the
  vertical layout's one 4×N table), with `gold` (and the heart/mana readouts) as a **top bar**
  above the strip. `events` fold into the top bar or the strip's edge. Not built here — this phase
  just must not make it impossible.

**Concrete API shape (decided, user 2026-09-06):** a new **`frontgui_hud_layout.{h,cpp}` module**
(separation of concerns — `frontgui_widgets` stays "reusable ImGui primitives"). It holds a
`struct HudLayout` = the chosen `PanelLayout_*` enum + the computed `ImRect` per region, rebuilt
on window resize / layout switch. Region code calls `HudRegionRect(HudRegion_minimap)` and lays
out *within* that rect — never against a `140`-relative constant or a hard-coded screen edge.
`get_status_panel_width()` (§1, §4) becomes "the extent the 3D viewport must inset by, on the
active layout's axis", derived from the layout.

The minimal variant is then just a third layout that omits `tabcontent` / shrinks `tabstrip` to
icons — no new mechanism.

**Icon-grid orientation:** the `FeIconGrid` wrapper ([06](06-tab-content-panels.md) §6) takes an
orientation — **4-wide × N-tall** for the vertical layout, **N-wide × 2-tall** for the
horizontal — so the same grid code serves both.

---

## 1. Legacy composition

`draw_whole_status_panel()` (`frontmenu_ingame_tabs.c:2586`), called directly from
`engine_redraw.c` (`:553` / `:611` / `:635`), **outside** `draw_active_menus_buttons()`:

1. `LbTiledSpriteDraw(0, 0, fs_units_per_px, &status_panel, get_panel_sprite)` — the panel
   background as a 2×4 `TiledSprite` (`status_panel`, `frontmenu_ingame_tabs_data.cpp:475`), scaled
   by `fs_units_per_px` derived from `gmnu->height`.
2. `draw_gold_total(...)` — the gold number, rendered from `button_sprite[]` digit glyphs (not a
   font), centred in the panel.
3. `panel_map_draw_slabs(minimap_pos_x, minimap_pos_y, mm_units_per_px, mmzoom)` — the minimap
   terrain.
4. `panel_map_draw_overlay_things(mm_units_per_px, mmzoom, basic_zoom)` — creatures / traps /
   spells / CTA circles / view cone, plotted pixel by pixel.
5. `draw_placefiller(...)` — fills the panel below the menu if the screen is taller than the menu.

Then `draw_gui()` → `draw_active_menus_buttons()` draws `GMnu_MAIN`'s buttons over that: the 5 tab
headers (`gui_draw_tab`), zoom in/out (`gui_area_new_vertical_button`), fullscreen-map, autopilot
(`gui_area_autopilot_button`), and the 13 event buttons (`gui_area_event_button`).

Then `engine_redraw.c` calls `draw_overlay_compass()` itself (it lives in `kfx_render`).

**The 3D viewport is inset by the panel.** `set_gui_visible()` (`frontend.cpp:2638`) calls
`setup_engine_window(status_panel_width, 0, MyScreenWidth, MyScreenHeight)` when the panel shows —
so the panel width feeds `engine_render.c`'s draw rect. If the ImGui panel's width differs from
`status_panel_width`, that value must be updated in lockstep or the 3D view and the panel overlap
or gap. `render_overlay->get_status_panel_width()` already exists as the seam.

---

## 2. The minimap (`frontmenu_ingame_map.c`)

`panel_map_draw_pixel(x, y, col)` (`:123`) writes one pixel straight into
`RendererGetFramebuffer()[(PanelMapY+y)*GraphicsScreenWidth + (PanelMapX+x)]`, clipped to the
diamond shape by `MapShapeStart[y]` / `MapShapeEnd[y]` scanline arrays (length `MapDiagonalLength`,
via `render_overlay->get_map_diagonal_length()`). `PanelMap[MAX_SUBTILES_X*MAX_SUBTILES_Y]` is the
subtile scratch buffer; `PanelColours[]` is a precomputed shade table (`setup_background`).
`MapBackground` / `MapShapeStart` / `MapShapeEnd` are lazily built and cached, rebuilt on
`PrevPixelSize` change.

**Decided ([00-overview.md](00-overview.md) §4.4): off-screen RGBA render → dynamic texture →
`ImGui::Image`**, same as `frontmenu_landpreview.c`. The moving zoom/pan viewport
(`player->minimap_zoom`, `minimap_pos_x/y`, Alt-drag recentre, `grabbed_small_map`) is the part
the static land preview didn't have.

**Decided (user 2026-09-06): map-sized buffer + UV sub-rect.** Because the minimap zooms in and
out, cropping a whole-map raster with `ImGui::Image` UVs is more reliable than re-deriving the
visible window's rasterisation at every zoom level. Rasterise the whole revealed map into the
buffer (per-subtile, sized to `MAX_SUBTILES_X*MAX_SUBTILES_Y` or the level's used extent),
invalidate on dig / build / reveal / door change, and let the `ImGui::Image` UV rect track
`minimap_zoom` + `minimap_pos_x/y`. `panel_map_*`'s overlay-things pass (creatures, CTA circles,
view cone) draws into the same buffer after the terrain, or as ImGui draw-list marks over the
image using the same UV→screen mapping.

**Interaction rebases onto the image rect** (cached from last frame's ImGui layout, the
land-preview one-frame-stale-rect trick): `mouse_is_over_panel_map()` (carved out of button
hit-testing in `get_gui_inputs`, `front_input.c:2980`), click-to-recentre, CTA target placement,
`map_to_minimap` / `thing_minimap_position`. `draw_overlay_compass` composites over the same
texture or as a sibling ImGui draw.

**`panel_map_draw_pixel` writes are on the sim-adjacent render path** but read only camera / thing
positions — safe to keep, just redirect the destination.

---

## 3. The pieces

- **Panel background — procedural, not the sprite chrome.** Decided (user 2026-09-06): the panel
  art (`FXGraphics-main/menufx/gui2-256/rpanel_256/`) is a **monolithic fixed-shape image** —
  `rpanel_full.png` is a single 560×1600 picture of the whole DK1 vertical sidebar, and
  `panel_divided/` just splits that same fixed shape into region pieces (`rpanel_btm`,
  `rpanel_farea`, `rpanel_mapbk`, `rpanel_tabsbk`). It is **not a 9-slice** and can't reflow to a
  different width, a horizontal bar, or a minimal HUD. So `FeBeginPanel` for the HUD draws chrome
  **procedurally** — translucent bronze/parchment fills + borders, the `frontgui_style.cpp`
  treatment — not `AddImage` of panel sprites. (The individual *button/icon* sprites —
  `rpanel_btn_*`, `rpanel_tab_*`, `frame_portrt_*`, `bar_*`, `room_ensign_filled`, and the
  room/spell/trap/creature icons from the `room`/`trapdoor`/`gui1` sets — **do** transfer.)
- **Gold counter** — `FeValue` compact-tabular text style ([00-overview.md](00-overview.md) §6),
  fed `dungeon->total_money_owned` from the per-game-turn cache. Retire `draw_gold_total`'s digit
  sprites.
- **Compass — migrate in this phase** (decided, user 2026-09-06 — not deferred). `draw_overlay_compass`
  is in `kfx_render`; route through `render_overlay` to a rotating ImGui image drawn over the
  minimap texture (`rpanel_mapdrir_{n,s,e,w}` sprites, or procedural), using the same
  camera-angle input.
- **Tab headers** — 5 `FeTab`-style toggle buttons (`gui_draw_tab`, `gui_set_menu_mode` →
  `set_menu_mode` / `turn_on_menu`). The flash/spangle behaviour (`spangle_button`,
  `kfx_frontend_state.flash_button_index`, `gui_set_button_flashing`) — a tab spangles when it has
  an unseen new item — becomes a wrapper affordance.
- **Zoom in / out / fullscreen-map / autopilot buttons** — `FeIconButton`. Zoom sends
  `PCtr_ViewZoomIn/Out` packets (`gui_zoom_in/out`); autopilot toggles
  (`gui_turn_on_autopilot`, `maintain_turn_on_autopilot` gates on whether assist is available).
- **13 event buttons** (`gui_area_event_button`, `frontmenu_ingame_tabs.c:343`) — the
  "semi-circular markers descending the sidebar edge" from the brief. Per event: icon by
  `event->kind` / `event->icon_idx`, blink at `kfx_config_state.gui_blink_rate`, a **falling
  animation** on spawn (`EvF_BtnFalling` → `interpolate_synced(y - h, y)`), read/unread sprite
  swap, click = open (`gui_open_event` → `activate_event_box` → `text_info_menu`), right-click =
  dismiss (`gui_kill_event`). Needs a `FeEventMarker` wrapper carrying the blink + slide-in +
  state-sprite logic. Event list is `kfx_sim_state.event[]` (sim state — read only).

## 4. Entanglements / risks

- **`status_panel_width` ↔ `setup_engine_window`** (§1) — the hard one. The 3D view geometry
  depends on the panel width; changing it needs `get_status_panel_width()` to return the ImGui
  panel's actual width and `set_gui_visible` / `setup_engine_window` to re-run when it changes
  (resolution change, panel show/hide, and — if a future variant — width change).
- **`GNFldD_StatusPanelDisplay` / `GOF_ShowGui`** — the panel can be hidden (`toggle_gui`, Tab
  key). The ImGui submission must respect `GOF_ShowGui` and the view-mode switches in
  `set_gui_visible` (`PVT_DungeonTop` vs `PVT_MapScreen` vs `PVT_CreatureContrl`).
- **`get_gui_inputs`'s minimap carve-out** — the `mouse_is_over_panel_map` early-`continue`
  becomes "pointer over the ImGui minimap image rect".
- **Per-game-turn cache cadence** — gold, counts, minimap things update on the turn boundary;
  the falling/blink animations and hover are per-frame. The cache/submission split must allow both.
- **Compass + minimap draw from `kfx_render`** — keep `kfx_render` ImGui-free; everything crosses
  the `render_overlay` seam.
- **Screenshot / capture** — the minimap texture is composited at present time; stage 3's B1
  "capture includes the overlay" work needs to be done or the minimap is missing from screenshots
  (`../renderer/gpu-v2/01-phase-b-2d-compositing.md` §B1). Coordinate.

## 5. Checklist

- [ ] `FeBeginPanel` sidebar frame, fixed width, layout from one panel rect.
- [x] `frontgui_hud_layout.{h,cpp}` module: `HudLayout` (enum + per-region `HudRect`),
      `hud_region_rect()`, rebuilt on resize; `HudLayout_VerticalRight` wired (2026-09-06).
- [ ] `get_status_panel_width()` returns the layout-axis inset; `setup_engine_window` re-runs on change.
- [ ] Panel frame procedural (`frontgui_style.cpp` fills + borders), not `rpanel_256` chrome;
      button/icon sprites reused.
- [ ] Gold counter as `FeValue`, from per-game-turn cache.
- [ ] 5 tab headers as toggle buttons; spangle/flash affordance in the wrapper.
- [ ] Zoom / map / autopilot `FeIconButton`s; zoom packets unchanged; autopilot maintain gate.
- [ ] `FeEventMarker` wrapper: blink + `EvF_BtnFalling` slide-in + read/unread sprite; 13 markers
      from `kfx_sim_state.event[]`; click/right-click → open/kill event.
- [ ] Minimap: **map-sized** off-screen buffer, invalidate on dig/build/reveal/door,
      `UpdateDynamicTexture`, `ImGui::Image` with a `minimap_zoom`/`_pos`-tracking UV rect;
      interaction rebased on the image rect + UV mapping.
- [ ] Compass migrated: rotating ImGui image over the minimap texture, via `render_overlay`.
- [ ] `GOF_ShowGui` / view-mode switches respected.
- [ ] Screenshot/capture includes the panel + minimap (coordinate with stage 3 §B1).
- [ ] Both `-classicmenu` states; layering; both builds; Catch2; frame-time vs. current build.

## 6. Open questions

**All resolved (user 2026-09-06):**

- **Minimap buffer: map-sized + UV crop** — see §2. The minimap zooms, so cropping a whole-map
  raster is more reliable than re-rasterising per zoom level.
- **`PanelLayout`: new `frontgui_hud_layout.{h,cpp}` module** — separation of concerns; see §0.
- **Horizontal layout: separate room/spell/trap/creature tab blocks, each an N×2 icon table**
  (not the vertical layout's one 4×N table). `FeIconGrid` takes an orientation. See §0.
- **Panel background: procedural, not sprite chrome** — `rpanel_256/` is a monolithic fixed-shape
  image, not a 9-slice, unusable for a reflowing / horizontal / minimal panel. Icons and
  button sprites transfer; the frame is drawn with `frontgui_style.cpp` fills + borders. See §3.
- **Compass: migrate in this phase** (not deferred) — rotating ImGui image over the minimap. See §3.

**Resolved by code investigation (2026-09-06):**

- **`status_panel_width` has exactly one writer:** `toggle_status_menu()` (`frontend.cpp:2455`),
  set to `get_active_menu(GMnu_MAIN)->width` — the `create_menu`-scaled pixel width of the 140
  virtual-px `GMnu_MAIN`. The global (`frontend.cpp:431`, default 140) is read only via
  `render_overlay->get_status_panel_width()`, consumed in `setup_engine_window`
  (`engine_redraw.c:190`) and `engine_camera.c:693`. For the ImGui panel,
  `get_status_panel_width()` returns the measured panel extent on the layout axis.
- **`setup_engine_window(x,y,w,h)` insets from the left only, via one clamp:**
  `if (GOF_ShowGui && x < status_panel_width) x = status_panel_width` (`engine_redraw.c:196-201`),
  plus one explicit `setup_engine_window(status_panel_width, 0, ...)` in `set_gui_visible`
  (`frontend.cpp:2662`). For `PanelLayout_HorizontalBottom` this becomes "inset `height` from the
  bottom by panel height" — a layout-axis parameter on ~2 functions. `engine_camera.c`'s calls
  pass the stored `player->engine_window_*` results, so they round-trip regardless. Contained.
- Event-marker geometry in a fixed-width panel — same right-edge column as today, or restyled?
