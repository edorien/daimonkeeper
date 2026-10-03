# S13 — Frame composition up: kfx_render draws the world, kfx_frontend composes the frame

**Status:** done 2026-09-28 · **Work items:** W12 · **Depends on:** S06 (the hand
and creature-view drawing are already in kfx_render) · **Risk:** medium
(draw ordering) · **Layout change:** none · **Estimate:** 1.5–2 weeks ·
**Callback entries removed:** ~25 `RenderOverlayCallbacks`, plus
`mouse_is_over_panel_map`

## Goal

The top of `kfx_render/src/engine_redraw.c` builds the whole frame: the 3D
view, then the hand, overlays, GUI, tooltips and messages. That needs about
25 upward calls into kfx_frontend. Invert it: kfx_frontend (or a small
compositor in kfx_apploop) composes the frame, and kfx_render provides "draw
the world view into this window" plus its render-side overlays.

## Moves (from the 2026-09-27 scan of `engine_redraw.c`)

| Function | Upward calls it makes today | New home |
| --- | --- | --- |
| `redraw_display` (164 lines) | `draw_debug_overlays`, `draw_eastegg`, `draw_slab64k`, `redraw_parchment_view`, `set_parchment_loaded`, `bonus_script_or_variable_overlay_active`, `get_unpausing_in_progress`, `set_winfont`, `GetMouseX/Y` | **kfx_frontend** `frame_compose.c` |
| `redraw_isometric_view`, `redraw_frontview` | `draw_gui`, `draw_tooltip`, `draw_whole_status_panel`, `gui_draw_all_boxes`, `message_draw` | `frame_compose.c` (each becomes "render world view, then UI layers") |
| `redraw_creature_view` (+ `draw_creature_view_icons`) | the same UI layers, plus `sync_cheat_box_3_active_option`, `draw_gui_panel_sprite_left`, `get_main_menu_width` | `frame_compose.c` |
| `keeper_screen_redraw` | — | `frame_compose.c` |
| `process_pointer_graphic`, `process_dungeon_top_pointer_graphic`, `get_place_*_pointer_graphics`, `draw_spell_cursor` | `cheat_or_menu_window_active`, `game_is_busy_doing_gui`, `get_battle_creature_over`, `mouse_is_over_panel_map` | kfx_frontend `pointer_graphics.c` (cursor choice is UI) |
| `draw_overlay_compass` | `get_map_diagonal_length`, `set_winfont` | frontend (it is a HUD element) |
| `prepare_map_fade_buffers` | `load_and_redraw_minimal_overhead_view` | stays in render; takes the minimap as a callback argument, or moves with map fade (decide in the stage) |

**Stays in kfx_render:** `setup_engine_window`/`store_engine_window`/
`load_engine_window`, `set_engine_view`, the map fade (`map_fade*`),
`smooth_screen_area`, `engine_point_to_map`, `screen_to_map` (it needs
`point_to_overhead_map`, which is minimap math; move that to render if it's
pure, otherwise pass it in), `players_cursor_is_at_top_of_view`,
`update_local_mouse_light`, `update_mouse_light`.

## Callers to re-point

`keeper_screen_redraw()` is called from kfx_apploop (fine, top-ranked) and
from **kfx_net** while waiting on the network: `packets_misc.c` (which moves
to kfx_game in S12) and `net_exchange_gameplay.c`. kfx_net → kfx_frontend is
upward, so route that call through the existing
`NetCallbacks.network_yield_draw_frontend`-style yield hook, adding a
`network_yield_redraw_gameplay` entry.

## Callback entries this frees (~25)

- `RenderOverlayCallbacks`:
  - UI layers: `draw_gui`, `draw_tooltip`, `draw_whole_status_panel`,
    `gui_draw_all_boxes`, `message_draw`, `sync_cheat_box_3_active_option`;
  - overlays: `draw_debug_overlays`, `draw_eastegg`, `draw_slab64k`;
  - parchment: `redraw_parchment_view`, `set_parchment_loaded`,
    `is_parchment_loaded`, `reload_parchment_file`;
  - state queries: `bonus_script_or_variable_overlay_active`,
    `get_unpausing_in_progress`, `cheat_or_menu_window_active`,
    `game_is_busy_doing_gui`, `get_battle_creature_over`,
    `get_map_diagonal_length`, `get_main_menu_width`;
  - sprites: `draw_gui_panel_sprite_left/centered`,
    `draw_button_sprite_left`;
  - `load_and_redraw_minimal_overhead_view` (if map fade moves).
- `SimFeedbackCallbacks.mouse_is_over_panel_map`.

## Steps

1. Create `frame_compose.c` and move `redraw_display` and the three view
   redraws unchanged, apart from direct calls. The render side gains
   `render_world_view(player, cam)` wrappers for what they called
   internally.
2. Move the pointer graphics and the compass.
3. Re-point `keeper_screen_redraw` callers, and add the net yield entry.
4. Delete the freed entries.

## Verification

- There's no image-diff harness, so manual screenshots before and after, in
  the same resolution and scale: main dungeon view, front view, possession
  (with lens), map screen, parchment, pause menu, cheat menu, debug
  overlays, easter egg, network-wait screen, each cursor type (room, trap,
  door, spell, hand over minimap).
- Both renderers: software and gpu-v2 (Vulkan).
- Ftests (the ones that draw frames).
- Inventory: about −25.

## Risks

- **Draw order is subtle:** the hand over the GUI versus under the
  tooltips, the lens after the world but before the GUI. Moving functions
  verbatim keeps the order. Resist reordering in the same commit.
- **The ImGui path (the in-game GUI migration)** already moves frame
  composition responsibilities. Coordinate with
  `docs/refactor/ingame-gui/00-overview.md` so the two efforts don't move
  the same functions in different directions.

## As built (2026-09-28)

Code commit `f473db61d`, one commit rather than the plan's four:
`redraw_display()` calls the pointer code, so moving them separately
would have left an upward call for a commit.

- **kfx_frontend `frame_compose.c`:** `keeper_screen_redraw`,
  `redraw_display`, `redraw_isometric_view`, `redraw_frontview`,
  `redraw_creature_view` (with `draw_creature_view_icons`), and
  `draw_overlay_compass`. Moved whole; each `render_overlay->` call became
  the function it was wired to. Where main.cpp's wrapper held logic (the
  debug overlay list, cheat box 3's sync, the main menu width, the
  bonus/script overlay check, the minimap redraw) it became a static
  helper in frame_compose.c. The view redraws still call kfx_render for
  the world (`engine()`, `draw_frontview_engine()`, `draw_creature_view()`,
  `draw_power_hand()`), so no `render_world_view()` wrapper was needed.
- **The map fade, decided:** `map_fade_in`/`map_fade_out` and
  `prepare_map_fade_buffers` moved with the compositor (they're driven
  only by `redraw_display()` and needed the minimap redraw); the pixel
  blend `map_fade()` stays in kfx_render.
- **kfx_frontend `pointer_graphics.c`:** `process_pointer_graphic`,
  `process_dungeon_top_pointer_graphic`, `draw_spell_cursor` and the
  `get_place_*_pointer_graphics` lookups. `draw_spell_cost` is shared with
  `redraw_display()` through pointer_graphics.h.
- **kfx_net** redraws the frame while an unpause is broadcast
  (net_exchange_gameplay.c, packets_misc.c's `set_packet_pause_toggle`,
  which S12 left in kfx_net). That is now
  `NetCallbacks.redraw_gameplay_frame`.
- **Callback entries:** 18 `RenderOverlayCallbacks` entries out (the plan's
  UI layers, overlays, parchment view, state queries, the minimap redraw,
  and `turn_on_menu`, which had no caller), one `NetCallbacks` entry in:
  374 → 357. Staying, unlike the plan: the panel sprite entries
  (`render_power_hand.c` from S06 uses them), `set_parchment_loaded`,
  `is_parchment_loaded`, `reload_parchment_file` (main_game.c, vidmode.c),
  `game_is_busy_doing_gui` (cursor_tag.c, engine_render.c) and
  `get_status_panel_width` (engine_camera.c, player_data.c; the compass
  still reads it through the port). `SimFeedbackCallbacks.
  mouse_is_over_panel_map` stays too: kfx_render's render_power_hand.c
  still calls it.
- **The ImGui HUD work** (`docs/refactor/ingame-gui/`) points at these
  functions in engine_redraw.c and notes that kfx_render can't call
  ImGui; in kfx_frontend they can.
- **Checks:** Linux and Windows builds, both layering checks, all 12
  Catch2 binaries. Scratch builds of S12's end (`e2bb6aa5a`) and of
  `f473db61d` logged, per turn, the `compute_replay_integrity()` checksum
  and a hash of the framebuffer (1920×1080) after drawing that turn's
  frame. Frames aren't deterministic as the game runs (sprite animation
  advances by wall-clock time per frame drawn, and flicker draws from
  `UNSYNC_RANDOM`, advanced per frame), so the instrumentation drew exactly
  one frame per turn, with the interpolation at 1.0, a fixed
  `delta_time` and the unsync seed set from the turn number; two runs of
  the same binary then give identical hashes. Across the 70-test sweep:
  the checksum traces (14,340 turns) are identical, and on every turn both
  builds drew (9,765 frames) the frame hashes are identical. The frame
  limiter still skips a turn's frame now and then, differently per run (25
  and 24 turns out of about 9,800). `config_content_tool_smoke` fails on
  both instrumented builds (it drives the UI across frames) and passes on
  an uninstrumented build of `f473db61d`. Not checked: the software-only
  palette path versus the gpu-v2 Vulkan renderer, and the manual
  screenshots in the plan.

## Upstream-merge notes

`engine_redraw.c` changes upstream with UI features. After this stage,
hunks in the moved functions go to `frame_compose.c` and
`pointer_graphics.c` (the ledger has them).
