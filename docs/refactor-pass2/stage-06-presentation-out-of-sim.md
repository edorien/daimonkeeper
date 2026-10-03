# S06 — Presentation code out of kfx_sim

**Status:** done 2026-09-28 · **Work items:** W4 (the rest; input primitives were
S03) · **Depends on:** S03 (input primitives in kfx_platform) · **Risk:**
low–medium · **Layout change:** none · **Estimate:** 1 week · **Callback
entries removed:** 16 (planned ~13) · **Timing:** right after an upstream merge

## Goal

kfx_sim is the deterministic core, but it holds drawing code and
local-input handling whose only callers are kfx_render and kfx_frontend.
Move them to where they're called from. This removes the callbacks that
exist purely so the sim can draw or read the keyboard, and shrinks the
places where local, machine-specific input is read inside the simulation
library.

## Moves (all verified: every caller of each function is outside kfx_sim)

| Group | Functions (lines as of `216be51d5`) | From | To | Only caller(s) |
| --- | --- | --- | --- | --- |
| Power hand drawing | `draw_power_hand` (L501–691), `draw_mini_things_in_hand` (L1079–1220) | `kfx_sim/src/power_hand.c` | new `kfx_render/src/render_power_hand.c` | `engine_redraw.c` ×2 |
| Possession view drawing | `draw_creature_view` (L4366–4435), `draw_swipe_graphic` + static `draw_swipe_graphic_impl` | `kfx_sim/src/thing_creature.c` | new `kfx_render/src/render_creature_view.c` | `engine_redraw.c` (`redraw_creature_view`) |
| Roomspace input | `numpad_to_value`, `get_roomspace_size_input` (static), `process_box_roomspace_inputs` (static), `process_build_roomspace_inputs`, `process_sell_roomspace_inputs`, `process_highlight_roomspace_inputs` (L496–544, L1422–1529) | `kfx_sim/src/roomspace.c` | new `kfx_frontend/src/front_input_roomspace.c` | `front_input.c` ×3 |

**Stays in kfx_sim**, despite being swipe or lightning related:

- `load_swipe_graphic_for_creature`, `free_swipe_graphic` and
  `randomise_swipe_graphic_direction`: sim-side state changes, called from
  `player_instances.c` and packet handling.
- `draw_god_lightning`: it spawns unsynced effect elements and draws
  nothing; S07 switches it to the synced camera.

The packet path already carries the roomspace size
(`get_packet_roomspace_size`), so the sim half of `roomspace.c` needs
nothing from the moved input half.

## Callback entries this frees (~13)

- `SimFeedbackCallbacks.engine`, `process_keeper_sprite`,
  `draw_lens_effect`, `lens_is_ready`, `lens_get_render_target`,
  `lens_get_render_target_width`, `lens_get_render_target_height`: used only
  inside `draw_creature_view` / `draw_power_hand`, which become
  kfx_render-internal calls.
- `SimFeedbackCallbacks.is_best_roomspace_key_pressed`,
  `is_square_roomspace_key_pressed`, `is_roomspace_incsize_key_pressed`,
  `is_roomspace_decsize_key_pressed`, `is_sell_trap_on_subtile_key_pressed`,
  `is_left_button_held`: used only inside the moved input functions, which
  call the frontend's own functions directly.

**Not freed yet.** Three calls change caller from kfx_sim to kfx_render but
remain upward calls into kfx_frontend, so they stay until S13 moves frame
composition up:

- `RenderOverlayCallbacks.draw_gui_panel_sprite_left`,
  `draw_gui_panel_sprite_centered`, `draw_button_sprite_left`;
- `SimFeedbackCallbacks.mouse_is_over_panel_map`.

## `struct LocalState` is out of scope, with its remaining sim uses recorded

`LocalState` is the local player's UI state (`player_data.h`, kfx_sim). All
libraries use it. After this stage, kfx_sim still uses it in about 30 places:

| File | Fields | What it is |
| --- | --- | --- |
| `player_instances.c` (~15) | `palette_fade_step_{possession,map}`, `status_menu_{restore,hidden_for_map}`, `tooltips_{restore,hidden_for_map}` | local UI side effects of the synced "enter map / enter possession" player instances |
| `player_utils.c` (6) | `minimap_pos_*`, `minimap_zoom`, `roomspace_size`, `main_palette` | resetting local view state at level start |
| `player_data.c` (4) | `view_type` | local view type bookkeeping |
| `thing_creature.c` (2) | `swipe_sprite_drawLR`, `palette_fade_step_possession` | |
| `roomspace_prediction.c` (1) | `local_thing_under_hand` | |

These are "sim event → local UI reaction". S15's event mechanism (or S07's
per-player counter pattern) removes them. After that, `LocalState` can move
to kfx_render, its lowest remaining user.

## Steps

1. Power hand: `git mv`-style move of the two functions into
   `render_power_hand.c`, with the header declaration moved to a new
   `render_power_hand.h`. `sim_feedback->process_keeper_sprite` becomes a
   direct call.
2. Creature view: the same, into `render_creature_view.c`. The `engine`,
   `lens_*` and `draw_lens_effect` callbacks become direct calls.
3. Roomspace input: into `front_input_roomspace.c`. The `sim_feedback`
   key-query entries become direct calls to `front_input.c`'s own
   functions.
4. Delete the 13 freed entries.

## Verification

- Both builds and both layering checks pass.
- Manual, since there are no automated rendering tests:
  - the power hand in every state (empty, holding creatures and gold,
    grabbing, slapping, over the minimap) at 1× and scaled `HAND_SIZE`;
  - possession view with and without lens effects (for example a creature
    with a fisheye lens), plus the swipe animation;
  - roomspace: numpad sizes, +/- size keys, square and "best" room, drag,
    sell mode, highlight (dig) mode.
- Ftests: the roomspace and possession-related ftests, and
  `bug_invisible_units_cant_select`.
- Inventory: −13.

## Risks

- **Static helpers the moved functions use that aren't listed here.** When
  moving each function, check for static functions or file-scope variables
  in the source file it uses. If also used by the staying code, expose them
  in the sim header instead of duplicating them.

## As built (2026-09-28)

One code commit, `54559c604`.

- **Moves as planned**, into `kfx_render/src/render_power_hand.c`,
  `kfx_render/src/render_creature_view.c` and
  `kfx_frontend/src/front_input_roomspace.c`, each with its own header.
  The moved code calls kfx_render's and kfx_frontend's own functions
  directly: `process_keeper_sprite`, `engine`, the lens functions,
  `sync_render_globals`, `get_panel_sprite`/`get_button_sprite`,
  `get_video_scale_values`, `is_game_key_pressed(Gkey_*)` and
  `left_button_held`. `power_hand.c`'s literal copies of four sprite IDs
  became the real `sprites.h` names.
- **Two extra moves the plan didn't list.**
  - `randomise_swipe_graphic_direction` came along as a static. The plan
    kept it in kfx_sim, expecting callers in `player_instances.c` and packet
    handling, but the swipe drawing is its only caller.
  - `roomspace.c`'s `reset_roomspace` flag is used only by the moved input
    code, so it came along too, also as a static.
- **Kept in kfx_sim:** `draw_square[]`, which `frontmenu_ingame_map.c` and
  `gui_parchment.c` also use.
- **Callback entries:** 16 removed, 474 → 458. They are the 13 listed above,
  plus three that lost their only caller along the way:
  `RenderOverlayCallbacks.sync_render_globals` and
  `SpriteLookupCallbacks.get_panel_sprite`/`get_button_sprite`.
  `mouse_is_over_panel_map` and the three panel-sprite draw entries stay
  until S13, as planned; their only callers are now in kfx_render.
- **Checks:** Linux and Windows builds, both layering checks, all Catch2
  suites (2041 cases) and the 70-test ftest sweep. That sweep includes
  `bug_invisible_units_cant_select` and the possession and room ftests.
  The manual rendering and roomspace checks listed below are still to do.

## Upstream-merge notes

- **`power_hand.c` and `thing_creature.c` are busy upstream.** The hand
  drawing code changes rarely; `draw_creature_view` sometimes changes (lens
  work). Ledger rows cover them.
- **`roomspace.c` changes upstream with roomspace features.** A change to
  the input half now belongs in `front_input_roomspace.c`.
