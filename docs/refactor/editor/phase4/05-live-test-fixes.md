# Phase 4 — live-test fixes (post-slice-5)

Status: **done — all three issues fixed and confirmed live.** Issue 1 (the zoom-out render
dropout) took four rounds to actually root-cause: a conservative zoom-constant pullback (didn't
fix it), a `MINMAX_LENGTH`/horizon-scan-clamp fix backed by a diagnostic (the diagnostic proved
this theory wrong, confirmed live), a poly-pool-exhaustion diagnostic (first reading inconclusive
at ~52%), and finally a peak-tracking version of that same diagnostic that confirmed the pool
hitting exactly `16777216/16777216` — full exhaustion — during a live reproduction.
`POLY_POOL_SIZE` raised 4× as the real fix, confirmed working by the user's own re-test. See §1
for the full blow-by-blow; it's a useful record of what didn't work and why, not just the final
answer — the same "measure, don't guess" discipline should apply the next time zoom/view-distance
limits get pushed further.

## 1. Partial 3D-view rendering dropout at large (far) zoom

**Symptom** (screenshot): with the editor's isometric camera zoomed out further than normal
gameplay allows (slice 2's `EDITOR_CAMERA_ZOOM_MIN`), a horizontal band of the screen simply
stopped rendering terrain — solid black, with overlay markers (AP4/AP10 in the report) floating
in that void since the overlay projection itself has no equivalent distance cutoff. Reported again,
still present, after a first attempted fix (pulling `EDITOR_CAMERA_ZOOM_MIN` from 130 back to a
more conservative 350) — the user's own follow-up question ("could it need to be limited by the
map size?") is what pointed at the real mechanism.

**Root cause, traced in `engine_render.c`/`engine_camera.h`**: `compute_cells_away()` derives how
many subtile-scale "cells" away the visible screen edges are from the camera via
`get_floor_pointed_at()`'s own raycast, then hard-clamps the result to `MAX_I_CAN_SEE_OVERHEAD`
(`= (MINMAX_LENGTH/2)-2`, itself sized off `MINMAX_LENGTH`'s fixed 512-entry per-row horizon-scan
array). Past that clamp, `find_gamut()`'s horizon scan never generates terrain columns for the rest
of the screen. The user's instinct was right: how far that raycast travels before hitting anything
depends on how much open, revealed floor extends in the camera's view direction — a large, open
map (like the one in the report) hits the clamp at a much less extreme zoom than a small/enclosed
one would. That's exactly why a fixed `EDITOR_CAMERA_ZOOM_MIN` couldn't reliably dodge this for
every map: the real ceiling being hit is `MAX_I_CAN_SEE_OVERHEAD`, a constant, not the zoom value
itself.

**Fix**: raised `MINMAX_LENGTH` itself, 512 → 2048 (`engine_camera.h`), giving
`MAX_I_CAN_SEE_OVERHEAD` real headroom (254 → 1022) instead of continuing to tune around it.
Checked the actual cost rather than assuming one: `minmaxs[]`/`ecs1[]`/`ecs2[]` are the only arrays
sized off this constant, all global/static (no stack-overflow risk), going from roughly 502KB
combined to roughly 2MB combined — a few MB of memory, trivial on any modern system. Per-frame scan
cost is bounded by however far `compute_cells_away()` actually needs to go, not by the array's max
capacity, so every existing gameplay zoom level (which already stays well under the old 254 limit)
sees no behavior or performance change at all. This is a global engine constant, not
editor-specific — the safety argument above is why that's acceptable here, not an oversight.

First retuning attempt after the `MINMAX_LENGTH` fix set `EDITOR_CAMERA_ZOOM_MIN` to 100 — a
mistake, caught by the user immediately: lower means *more* zoomed out, not more conservative
(`engine_camera.h`'s own "zoom max is zoomed in... zoom min is zoomed out" comment), so 100 was a
more extreme ask than the already-broken 130, not a safer one. Corrected to 450 (close to the
stock floor, 520) to isolate whether the `MINMAX_LENGTH` fix worked at all — **still reported
broken even there**, close enough to the stock zoom floor that guessing another number stopped
being a responsible way to iterate.

**Added instead: a live diagnostic.** `compute_cells_away()` (`engine_render.c`) logs (via
`WARNLOG`, edge-triggered so it fires once on entering/leaving the clamped state rather than every
frame) the *raw*, pre-clamp cell count a live editor session's camera actually wants, alongside the
`MAX_I_CAN_SEE_OVERHEAD` ceiling it got clamped to. Gated on `editor_callbacks->is_active()`
(the same seam slice 2's zoom-floor swap already used) so this can never fire during normal
gameplay.

**Result: this theory is wrong.** The user reproduced the dropout live and confirmed no
`Editor camera horizon-scan` line appeared in `keeperfx.log` at all — the horizon-scan clamp never
engaged, yet the dropout still happened. `MINMAX_LENGTH`/`MAX_I_CAN_SEE_OVERHEAD` is ruled out as
the cause (the raise to 2048 is harmless — see its own memory-cost note above — but doesn't fix
this bug and wasn't reverted, since it's free and might still matter for a different map).

**Second diagnostic added, same file, same session:** `getpoly`/`poly_pool_end` gate dozens of
separate terrain-column/polygon insertion points throughout `engine_render.c` (`get_bucket_item()`
plus many inline `if (getpoly < poly_pool_end)` checks) — if the pool fills up partway through a
frame's terrain generation, every later insertion in iteration order silently no-ops, which could
plausibly produce exactly the "everything past this point just doesn't render" cutoff seen live.

**First result, inconclusive.** The user reproduced the dropout and it fired once:
`Editor poly pool usage 8728656 / 16777216 bytes (over halfway)` — about 52%, nowhere near
exhaustion, with the dropout already visible at that reading. Doesn't rule the theory out outright
(the original diagnostic only logged the first crossing of a 50% threshold — the real peak during
the session, after further zooming, is unknown) but doesn't look like a smoking gun either.

**Both diagnostics converted to peak-trackers.** Rather than a single threshold-edge log, both
`compute_cells_away()`'s and the poly-pool check now log every time they hit a *new* high-water
mark for the session — so the next test's `keeperfx.log` shows the actual growth curve toward
whatever the real ceiling is, for both candidates at once, instead of one ambiguous snapshot.

**Confirmed: this is the real cause.** The user reproduced the dropout again; the poly-pool peak
climbed all the way to `16777216 / 16777216` — full exhaustion, exactly at `POLY_POOL_SIZE`. Once
`getpoly` reaches `poly_pool_end`, every one of the dozens of `if (getpoly < poly_pool_end)`
terrain-column insertion checks throughout `engine_render.c` starts silently no-op'ing for the rest
of that frame — a clean "everything past this point in iteration order just doesn't render"
cutoff, matching the reported symptom exactly.

**Fix**: raised `POLY_POOL_SIZE` (`engine_render.h`) 16777216 → 67108864 (4×), same "measure the
cost, it's cheap, be generous" reasoning already applied to `MINMAX_LENGTH` — a single global/
static byte array, no stack risk, no per-frame cost change for any usage level that already stays
under the old ceiling (which normal, non-editor gameplay's own history says it always has).
Verified with the **full** Catch2 suite across every `kfx_*` library again (same discipline as the
`MINMAX_LENGTH` change, since this is also a global engine constant, not editor-specific) — all
pass, no regressions. **Confirmed fixed** by the user's own live re-test, with the peak-tracker
reporting a new session peak of `22432032 / 67108864` bytes (~33%) — comfortable headroom under the
new ceiling on the same map that used to hit it exactly, not just barely clearing it.

## 2. Overlay markers shown in the wrong place in Map View

**Symptom** (screenshot): switching to View > Map View while an overlay (AP/Hero Gate markers,
in the report) was on showed markers scattered in positions with no visible relationship to the
actual dungeon layout.

**Root cause**: `project_world_position_to_screen()` (slice 3) replicates
`do_map_who_for_thing()`'s exact camera-relative-offset + `rotpers()` recipe — which only holds for
the normal isometric dungeon view. `map_x_pos`/`map_y_pos`/`map_z_pos`/`camera_matrix` are set up
for that camera/lens each frame; the parchment map screen (`PVT_MapScreen`) renders through a
completely different, unrelated 2D path (`draw_map_parchment()`/`draw_2d_map()`) that never touches
those variables, so overlays kept reading stale/meaningless leftovers from the last isometric frame
while in Map View.

**Fix**: `editor_overlay_frame()` (`editor_overlay.cpp`) now checks
`get_my_player()->view_type != PVT_DungeonTop` and returns immediately if so — every overlay in
that file (slab grid, coordinates, ownership tint, all three marker types) assumes the isometric
view, so this is one shared guard rather than a per-overlay fix. Overlays now simply don't draw at
all while in Map View or 1st Person, rather than drawing incorrectly.

## 3. Reconsidered: hide the normal in-game GUI entirely during an editor session

Not a bug — a scope reconsideration: rendering both the gameplay HUD (sidebar, minimap, tab panel)
*and* the editor's own File/Edit/View menu bar + toolbox offers no benefit; an editor session has
no use for the gameplay HUD.

**What already existed**: `ingame_panel_frame()` (`frontgui_ingame_panel.cpp`) already early-returns
without drawing anything (the whole sidebar, "same flag Tab/Ctrl+Tab toggles during normal play")
when `kfx_sim_state.operation_flags & GOF_ShowGui` is clear — this is a session-runtime flag, not
a persisted Options setting.

**Fix**: `editor_open()` now saves the flag's current state (`s_prev_show_gui`) and force-clears
`GOF_ShowGui` for the duration of the session, ignoring whatever the player had it set to;
`editor_deactivate()` (called by both `editor_close()` and `editor_frame()`'s own safety-net path)
restores the saved value, so a later normal play session isn't left with the GUI hidden. Same
save/restore shape already established for `Ft_SkipHeartZoom` in the same function.

**Known scope limit**: only confirmed to hide what `ingame_panel_frame()`'s own early-return
covers (its own comment says "the whole sidebar"). If any other HUD element turns out not to be
covered by this same flag (visible in a follow-up live-test), that's a separate, small follow-up,
not a sign this fix is wrong.

## Tests

- No new Catch2 coverage — the view-type guard and GOF_ShowGui save/restore are both simple flag
  checks with no fixture-constructible pure-function shape (same reasoning as every other slice
  4/5 overlay/session function); the zoom/`MINMAX_LENGTH` constants have no behavior to
  unit-test beyond "these are different numbers now."
- Because `MINMAX_LENGTH` is a global engine constant (not editor-specific), ran the **full**
  Catch2 suite across every `kfx_*` library this time, not just `kfx_editor` — all pass, no
  regressions anywhere (`kfx_platform` 694, `kfx_config` 2176, `kfx_pathfinding` 250, `kfx_sim`
  2103, `kfx_render` 322, `kfx_net` 83, `kfx_game` 137, `kfx_frontend` 134, `kfx_script` 49,
  `kfx_apploop` 12, `kfx_editor` 53 assertions).
- Full ftest sweep (`-ftests -exitonfailedtest -headless`) — clean pass, exit code 0, 22/22 (this
  exercises real gameplay rendering, a meaningful regression check for a change to core terrain
  rendering limits).
- Manual live-test — pending re-confirmation from the user on all three fixes, especially #1 now
  that it targets the actual structural cause rather than a guessed zoom value.

## Verification

- `python3 scripts/check_layering.py --strict` — no new violations.
- `keeperfx`/`keeperfx_hvlog` (native Linux) build clean.
- Every `kfx_*` library's Catch2 suite passes — see Tests above.
- Full ftest sweep — clean pass (22/22).
