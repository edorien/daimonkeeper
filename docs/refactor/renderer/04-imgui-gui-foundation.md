# Stage 4 — Dear ImGui for the main-menu frontend

Status: **Phases A–F landed** (Phase B's chrome-PNG import and CJK/Cyrillic fallback fonts still
open; see below); **Phase G in progress** — the §6.4 resolution collapse, the §6.2 `keeperfx.cfg`
writer, the §6.2 finding 2 config keys, and the generic renderer + four settings tabs (wired into
`FeSt_FEOPTIONS`, confirmed working live) are done; **every option in §6.2's list is now
schema-backed, including the resolution picker** (35 rows, each with help text). `INGAME_RES`
(step 12) needed a new `kfx_platform` display-mode-enumeration capability and a schema mechanism
for a table built at runtime rather than compile time — see the step's own paragraph below; not
yet confirmed working live (compiles and passes its tests, not yet exercised in the running game).
Depends on stage 2 (landed — `TbPixel` is true-colour now). Does **not** depend on [03-gpu-renderer.md](03-gpu-renderer.md): the integration
point below works against today's `RendererSoftware` unchanged. §10 records every decision this
plan rests on; none are outstanding. See [00-overview.md](00-overview.md) for context.

Phase A (§7): ImGui v1.92.7 vendored at `deps/imgui/` (upstream `ocornut/imgui`, not
`imgui_bundle`, per §3.2); `kfx_platform`'s `gui/ImGuiContext.{h,cpp}` owns context/backend
lifecycle; event feed wired into `LbPollInputs()`; render hook wired into
`RendererSoftware::PresentFrame()` between the backdrop blit and `SDL_RenderPresent`;
`-classicmenu`/`-noimgui` + `CLASSIC_MENU` config key + `Clo_ClassicMenu` override landed
(`CMDLINE_OVERRIDES` bumped to 5); `-imguidemo` debug flag shows `imgui_demo.cpp` as the Phase A
proof. Builds clean (`-Werror`-free for the vendored code, matching `centitoml`'s precedent) on
native Linux (std + hvlog) and mingw-w64 Windows cross-compile (std + hvlog); `check_layering.py
--strict` passes; `kfx_platform_utest` covers the `RendererImGuiEnabled`/`RendererSetImGuiEnabled`
toggle. Verified interactively against real game data: the demo window composites correctly over
the live sprite-drawn main menu with the default (ImGui-on) path. **Not yet verified:** the
Windows binary hasn't been run (cross-compiled only, no Windows/Wine runtime available here), and
`-classicmenu`'s suppression of the overlay is confirmed by code (the single
`RendererImGuiEnabled()` gate in `PresentFrame`) and by unit test, but not by a second interactive
screenshot — an attempt was abandoned when the screenshot tool grabbed the whole desktop instead
of the game window. Multi-resolution legibility (§9) is deferred to Phase B, where it's an actual
exit criterion (today's proof is demo-only, not a real screen).

Phase B (§7), first pass: typography and the wrapper layer are up, chrome-PNG import is not.
`kfx_frontend/frontgui_style.{h,cpp}` loads Exocet from `fxdata/` when the installer copied it in
(`EXH_____.TTF`/`EXL_____.TTF`, checked as a pair) or falls back to the bundled Cinzel static
instances (`Cinzel-Black.ttf`/`Cinzel-Regular.ttf` as the heavy/light stand-ins, §4.1) — both
verified live against real files (`tmp/EX{H,L}_____.TTF`, `config/fxdata/Cinzel/`), each rendering
correctly. Font size derives from `ImGui::GetIO().DisplaySize.y` every `FeStylePushFont` call
(§4.3), not a baked atlas, so it re-derives on resize for free — 1.92's dynamic font system makes
this free. A KeeperFX `ImGuiStyle` (bronze/parchment/blood-red, hand-picked rather than sampled
from `front.pal` — a placeholder per §5.1's own "procedural is fine through Phase E") is applied
once. `kfx_frontend/frontgui_widgets.{h,cpp}` implements the full §5.2 wrapper list —
`FeBeginPanel`/`FeBeginListBox`/`FeBeginScrollArea`/`FeButton`/`FeIconButton`/`FeNavButton`/
`FeSlider`/`FeCheckbox`/`FeCombo`/`FeTextInput`/`FeKeybindRow`/`FeHeading`/`FeSubheading`/
`FeBodyText`/`FeCaption`/`FeSeparator`/`FeBeginTabBar`/`FeTab`/`FeBeginModal` and their `End`
counterparts — procedurally rendered (translucent fills, bronze borders, no chrome PNGs yet),
click feedback wired through the existing `do_sound_menu_click()` (no per-wrapper hover sound: the
legacy system this mirrors has none either), keyboard nav enabled once via
`ImGuiConfigFlags_NavEnableKeyboard`. A `-imguistyle` debug flag shows
`frontgui_stylesheet_test.cpp`, a proving-ground screen exercising every wrapper plus a handful of
hardcoded non-Latin script samples (Cyrillic/CJK/Arabic — real shipped-translation sampling would
need a mid-session language reload, out of scope here); verified live with both Exocet and Cinzel.
`RendererManager` gained a `RendererImGuiFrameFn` callback slot (mirroring `RendererDrawCallbacks`)
so kfx_platform's `PresentFrame` can invoke kfx_frontend's submission without an upward include.
The `.ttf` packaging rule landed in `package.mk` (verified with a real `make` invocation staging
`Cinzel/` into `pkg/fxdata/`); `Packaging.cmake` needed no change on inspection — its `gamedata`
install step already copies `pkg/` recursively with no extension filter, so the original plan
text's "and correspondingly in Packaging.cmake" turned out not to apply. Every `*_utest` target
needed the same `imgui` link fix `kfx_platform_utest` picked up in Phase A once `kfx_frontend`
started referencing ImGui symbols too — full suite (1486 tests) passes. **Not done, remaining
Phase B work:** the §4.2 CJK/Cyrillic fallback merge (Unifont/WenQuanYi — `tools/fxfontmaker/` only
has `.hex`/`.bdf`, not the TTF/OTF releases ImGui needs) and the §5.1 chrome-PNG-into-texture-atlas
import (`FXGraphics-main/menufx/frontend-64/`) are both still open; every wrapper is procedural
only. Multi-resolution legibility (640×480 through 4K) hasn't been screenshot-verified, only
argued from `FeStylePushFont`'s formula.

Phase C (§7): `FeSt_STORY_POEM`, `FeSt_STORY_BIRTHDAY`, `FeSt_CREDITS` and `FeSt_FEOPTIONS` migrated,
per-screen, behind the existing `-classicmenu`/`-noimgui` toggle — every other state is untouched.
New dispatcher `kfx_frontend/frontgui_screens.{h,cpp}`: `frontend_imgui_screen_active(state)`
("ImGui enabled AND this state migrated", §3.5) is called from both `frontend.cpp`'s draw switch
(skip the legacy widget-draw call, `frontend_copy_background()` still runs — §3.4) and its input
switch (skip `get_gui_inputs(0)` for `FeSt_FEOPTIONS` entirely rather than only
`WantCaptureMouse`-gating it, per §8's "a given `FrontendMenuState` belongs entirely to one
system"); `FrontendImGuiFrame()` is the new `RendererImGuiFrameFn` registration, dispatching to the
active screen's submission and still running the Phase B style-sheet overlay. `FeSt_FEOPTIONS`
binds directly to the same `FrontendSliderCtrl`/`FrontendCheckboxCtrl` instances
(`sound_volume_ctrl`/`music_volume_ctrl`/`mentor_volume_ctrl`/`mouse_sensitivity_ctrl`/
`mouse_invert_ctrl`, exposed from `frontmenu_options.c` — `static` dropped, single source of truth
with the legacy widgets) rather than duplicating the settings read/write; the "Define Keys" button
transitions state and falls onto the still-classic `FeSt_FEDEFINE_KEYS` on its own. `FeSt_CREDITS`
uses real-time (delta-time) auto-scroll rather than porting the legacy tick-based
`units_per_pixel`-scaled advance — an approximation of "the same rate" (§7's exit wording), not a
byte-exact port; still drives the shared `credits_offset`/`credits_end` globals so
`front_continue_pressed(credits_end)` (`frontend.cpp`, unchanged) still auto-advances once scrolling
finishes. Fixed a real bug surfaced during interactive testing, general to every phase not just C:
`io.MouseDrawCursor` was hardcoded false (§3.3's "the game draws its own cursor sprite"), but that
sprite is baked into the software backdrop *before* ImGui composites on top of it, so any opaque
ImGui window (a `FeSt_FEOPTIONS` panel, the style-sheet overlay, `imgui_demo`) hid the cursor
underneath itself; `ImGuiContext.cpp`'s `ImGuiContextNewFrame()` now sets
`io.MouseDrawCursor = io.WantCaptureMouse` every frame, so ImGui draws its own cursor for exactly as
long as it's the topmost thing under the pointer. Verified live (real user, real interaction, not a
screenshot): `FeSt_FEOPTIONS`'s three volume sliders confirmed rendering and working correctly
end-to-end, including after the cursor fix. `check_layering.py --strict` passes; full suite (1486
tests, unchanged) passes; all four build configs (native Linux std/hvlog, mingw-w64 Windows
std/hvlog) build clean. **Not verified:** `FeSt_STORY_POEM`/`FeSt_STORY_BIRTHDAY`/`FeSt_CREDITS`
were not reached interactively this pass (no menu-navigation automation available in this
environment); their correctness rests on code review and the same wrapper/font stack `FeSt_FEOPTIONS`
already proved. Keyboard-only nav (§5.2) is enabled globally but untested end-to-end on these
screens specifically.

Phase D (§7): `FeSt_FEDEFINE_KEYS`, `FeSt_HIGH_SCORES` and `FeSt_FELOAD_GAME` migrated, extending
`frontgui_screens.cpp`'s dispatcher and `state_is_migrated()`. `FeSt_FEDEFINE_KEYS` confirms §6.1's
own prediction: `FeBeginListBox`'s native scrolling genuinely replaces the twelve hand-declared row
buttons plus their `_maintain`/`_up`/`_down`/`_scroll` callbacks and
`kfx_frontend_state.define_key_scroll_offset` outright — the migrated screen is just `for (key_id in
0..num_definable_keys())`. Key *capture* itself (`define_key_input()`, `defining_a_key`/
`defining_a_key_id`/`lbInkey`) is pure global state with no `GuiButton` involved, so it's reused
completely unchanged by both draw paths; the row click only sets the same three globals
`frontend_define_key()` already did. `frontend_format_key_binding()` was split out of
`frontend_draw_define_key()` (`frontmenu_options.c`, `static` dropped) so the mods/mouse-button/
key-name label text has one source for both paths, and `draw_defining_a_key_box()`'s "press a key"
prompt is now an `FeBeginModal`.

`FeSt_HIGH_SCORES` uses `ImGui::InputText` (via `FeTextInput`) for new-entry name capture rather
than porting `frontend_high_score_table_input()`'s manual UTF-8 cursor-splice logic — exactly what
§7's own framing anticipated ("ImGui's SDL3 backend text input is enough; no IME composition
work"). `finalize_high_score_entry()` (`front_highscore.c`, `static` dropped) is reused unchanged
for both the Enter-to-confirm and Escape-to-cancel-with-default-name paths.
`frontend_high_score_table_input()` is now gated off entirely while migrated (not just
`WantCaptureMouse`-gated), since two systems editing the same `high_score_entry` buffer
concurrently would corrupt it, not just double-claim a click.

`FeSt_FELOAD_GAME` drops `frontend_load_game_button_to_index()`'s "walk N in-use catalogue entries
from a windowed on-screen row" indexing scheme entirely — `FeBeginListBox`'s native scroll means the
migrated screen just iterates `save_game_catalogue[]` directly, filtering `CEF_InUse`, and calls
`load_game()` with the real catalogue index (no row/slot indirection to invert). Confirmed by
inspection (not by touching the draw switch) that `turn_on_menu(GMnu_FELOAD/GMnu_FEDEFINE_KEYS/
GMnu_FEHIGH_SCORE_TABLE)` and their state-entry setup (`load_game_save_catalogue()`,
`frontstats_save_high_score()`) all run from `frontend.cpp`'s unconditional state-entry switch, not
from `draw_gui()` itself — so skipping `draw_gui()` when migrated doesn't skip the data population
those screens depend on.

All four build configs pass; `check_layering.py --strict` passes; full suite (1486 tests, unchanged)
passes. **Not verified interactively:** none of Phase D's three screens were reached by hand this
pass (same automation gap as the Phase C text screens) — correctness rests on code review and the
same wrapper/font/list-box mechanics `FeSt_FEOPTIONS` already proved live in Phase C.

**Post-Phase-D bug fixes**, from real interactive testing (`FeSt_FEOPTIONS`/`FeSt_FELOAD_GAME`/
`FeSt_HIGH_SCORES`, this time actually clicked through, not just code-reviewed): four bugs reported,
three fixed with a confirmed root cause, one hardened without a confirmed repro.

1. **Dual/jumping cursor, general to every ImGui screen, not screen-specific.** Root cause: the
   game's own mouse handling (`bflib_inputctrl.cpp`, "warp-based relative motion") grab-warps the OS
   cursor back toward the window centre whenever it nears an edge, tracking its real logical
   position via accumulated deltas instead — a raw SDL motion event's absolute x/y reflects that
   warping, not the actual position. ImGui's SDL3 backend reads exactly that absolute position, so
   its own cursor (drawn since the Phase C `io.MouseDrawCursor` fix) snapped to centre whenever the
   pointer neared any edge, diverging visibly from the game's own correctly-tracked cursor sprite
   underneath — explaining both "cursor jumps to screen centre" (bottom-row buttons like Define Keys
   sit near an edge) and "dual mouse points." Fixed with a new `ImGuiMousePositionFn` callback
   (`gui/ImGuiContext.h`, `RendererManager.h` façade): `LbPollInputs()` now skips forwarding
   `SDL_EVENT_MOUSE_MOTION` to ImGui entirely, and `ImGuiContextNewFrame()` instead feeds it the
   game's own tracked position (`GetMouseX()`/`GetMouseY()`, registered from `main.cpp`) once per
   frame via `AddMousePosEvent()`, before `ImGui::NewFrame()` drains the queue.
2. **Large empty gap between panels on the Options screen.** Root cause: `FeBeginPanel`'s `size.y ==
   0` was meant as "size to content" but is `BeginChild`'s own default for "fill remaining space in
   the parent" without `ImGuiChildFlags_AutoResizeY` -- a panel with only a few sliders in it was
   stretching to cover most of the window. Fixed: `FeBeginPanel` now adds
   `ImGuiChildFlags_AutoResizeX`/`AutoResizeY` per axis whenever that axis's `size` is `0`.
3. **High score "Return" crash.** Not reproduced from the attached log (it shows no `FeSt_HIGH_SCORES`
   visit at all, ending in a clean quit — likely from a different run than the one that crashed), so
   this is hardened rather than confirmed-fixed: `frontgui_highscores_frame()` now NULL-checks
   `campaign.hiscore_table` before `count_high_scores()` (matching the legacy `draw_call`'s own
   guard, which `count_high_scores()` itself doesn't make), and a confusing/tautological
   `SetKeyboardFocusHere()` condition (always false in context) was replaced with a real
   previously-editing-index tracker. Bug 1's fix may also have been the actual cause here: an
   erratic cursor could plausibly land a click somewhere unintended and trigger a different,
   unrelated crash path. Ask for a fresh log (or better, a stack trace) if this recurs.

Build/test verification repeated after these fixes: all four configs, `check_layering.py --strict`,
and the full 1486-test suite still pass.

**A second interactive-testing round found the real bug behind the "Return" crash above** (a
genuine stack trace this time, `Fault address: 0xd1` inside `ImGui::SameLine()`, called from
`FrontendImGuiFrame` right after `frontend_set_state(FeSt_FEDEFINE_KEYS)` on the FEOPTIONS
"Define Keys" button) — **and a cosmetic mismatch**, both fixed:

- **Root cause of the crash: calling `frontend_set_state()` (and `load_game()`, at least as
  heavy) synchronously from inside an active ImGui window's own widget-submission code corrupts
  ImGui's window stack.** `frontend_set_state()`'s side effects (`turn_on_menu`/`turn_off_menu`,
  `fade_out()`, per-target-state setup) are arbitrarily heavy and completely unrelated to ImGui,
  but calling it mid-`Begin()`/`End()` left `g.CurrentWindow` null/dangling by the time the very
  next `ImGui::` call ran — matching the crash exactly (`SameLine()` dereferences
  `g.CurrentWindow` at its very first line). Fixed by deferring: every `frontend_set_state()`/
  `load_game()` call site in `frontgui_screens.cpp` now sets a pending-action variable instead of
  calling directly; `FrontendImGuiFrame()` applies whatever was requested at the very start of the
  *next* frame, before any ImGui window from this module is open. This is a general pattern, not
  specific to Define Keys — every screen's every transition went through the same fix.
- **Cosmetic: ImGui's cursor over its own content was a generic arrow, not the game's actual
  cursor sprite** (`cursor_horny.png`), mismatched against the correct sprite shown everywhere
  else. Fixed with a new `ImGuiCursorImageFn` callback (`gui/ImGuiContext.h`): `io.MouseDrawCursor`
  now always stays false, and `ImGuiContextNewFrame()` instead draws the callback's texture via
  `GetForegroundDrawList()->AddImage()` whenever `WantCaptureMouse` is true.
  `FeStyleGetCursorImage()` (`frontgui_style.cpp`) supplies it by rendering `GFS_cursor_horny`
  (already the frontend's real cursor sprite, `vidmode.c:293`, hotspot (0,0)) into an off-screen
  RGBA buffer through `RendererSwapFramebufferTarget` -- the same seam the eye-lens effect uses --
  so no new asset/path dependency was introduced (the raw PNG in the gitignored local
  `FXGraphics-main/` checkout was only useful as a pointer to the right sprite ID, not something
  to read directly: it isn't part of the shipped game data).

All four build configs, `check_layering.py --strict`, and the full 1486-test suite pass after these
fixes too.

**A third interactive-testing round on the cursor fix itself found two further, layered bugs** —
both in code from the previous round, neither previously caught because nothing had exercised the
cold-start path where the cursor sprite sheet isn't loaded yet:

- **Checkerboard cursor.** Self-caught before shipping (temporary debug PNG dump, not reported by
  the user): `get_frontend_sprite(GFS_cursor_horny)` was being called before `frontend_sprite`
  (`vidmode_data.cpp`) had been populated at all, returning garbage data off an empty
  `TbSpriteSheet*` that then got permanently cached as "the" cursor image.
  `get_frontend_sprite()`/`custom_sprites.c` doesn't itself distinguish "not loaded yet" from
  "loaded, index empty" — it just indexes whatever's there. Fixed by mirroring
  `set_pointer_graphic_menu()`'s (`vidmode.c`) own `frontend_sprite == NULL` guard in
  `build_cursor_pixels()` (`frontgui_style.cpp`), checked *before* latching
  `s_cursor_build_attempted`, so a too-early call now fails cleanly and retries on a later frame
  instead of caching a wrong result.
- **Cursor invisible again, from the fix above.** `ensure_cursor_texture()`
  (`gui/ImGuiContext.cpp`) had its own, independent latch — `s_cursor_texture_attempted` was set
  unconditionally on the very first call, before checking whether the callback actually succeeded —
  so the first (now correctly-failing, per the fix above) attempt on an early frame made it give up
  permanently and never build the texture at all. Fixed by dropping the separate flag and gating
  purely on `s_cursor_texture != nullptr`: the function now retries every frame until
  `FeStyleGetCursorImage()` succeeds, then never rebuilds once it has a texture.

Only three call sites populate `frontend_sprite` at all (`frontend_load_data()`'s state-exit
dispatch for `FeSt_LAND_VIEW`/`FeSt_TORTURE`/`FeSt_NETLAND_VIEW`, `frontend.cpp`) — the retry loop
means the ImGui cursor now appears as soon as one of those has run at least once in the session
rather than needing it before the first ImGui frame, but a session that never visits any of those
three states (e.g. Main Menu → Options → quit, without ever opening Land View) would still see no
custom cursor at all, only whatever the legacy cursor draw shows underneath. Not yet confirmed
whether that gap is real in practice or whether some other loading path covers it — flagged rather
than silently assumed fixed.

All four build configs, `check_layering.py --strict`, and the full 1486-test suite pass after these
fixes too.

**These two retry-loop fixes finally let the cursor swap actually execute (previously it never got
past the two latch bugs), which surfaced a real, pre-existing bug in the swap primitive itself:**
the entire background became severely corrupted (diagonal/fractal-looking noise across the whole
screen) as soon as the cursor sprite rendered once. Root cause: `RendererRestoreFramebufferTarget()`
(`RendererManager.cpp`) only ever restored `lbDisplay.WScreen`, never
`GraphicsScreenWidth`/`GraphicsScreenHeight` — so after `RendererSwapFramebufferTarget()` pointed
the framebuffer at the tiny cursor-sized off-screen buffer and "restored" it, the real screen's
stride stayed stuck at the cursor's width/height for the rest of the session, corrupting every
subsequent draw's row math. `thing_creature.c`'s eye-lens effect uses the same pair and has the same
bug, but it's invisible there because its render target's width/height already equal the real
screen's, so nothing actually changes on restore. Fixed by having
`RendererSwapFramebufferTarget()`/`RendererRestoreFramebufferTarget()` (`RendererManager.cpp`) save
and reapply `GraphicsScreenWidth`/`GraphicsScreenHeight` themselves (single-level, matching how both
existing callers already use the pair: swap, draw, restore, never nested) — no call-site changes
needed at either use site.

All four build configs, `check_layering.py --strict`, and the full 1486-test suite pass after this
fix too.

**With the corruption fixed, the cursor itself still rendered wrong — the correct silhouette but
speckled with essentially random-coloured noise pixels instead of the sprite's real (brown/orange)
colours.** Diagnosed with a temporary debug dump (`SDL_SavePNG` to `/tmp/cursor_dbg.png` plus a
pixel/palette stats log line, both removed once root-caused). That dump actually showed the active
palette entirely zeroed (0/768 nonzero bytes) — a second, genuine bug, but *not* the explanation for
the speckle pattern itself: an all-zero palette would make every non-transparent pixel a uniform
opaque black, not scattered noise. The sprite dimensions matched the source art exactly (52×40),
ruling that out too. The speckle noise's actual cause was pre-existing and general, not specific to
the cursor at all: `LbSpriteDrawLineFastCpy()`'s "draw some pixels" branch (`bflib_vidraw.c`) — the
path `LbSpriteDrawImmediate()` takes whenever neither a transparency nor a flip draw flag is set —
used a raw `memcpy()` of the sprite's 1-byte-per-pixel RLE palette-index data directly into the
4-byte-per-pixel `TbPixel*` (RGBA) destination buffer, instead of expanding each index through the
palette via `LbDrawBufferSolid()`/`expand_indexed_pixel()` the way every sibling drawing routine
(`LbSpriteDrawLineSolid`, `LbSpriteDrawLineTranspr`) already does. A straight `memcpy` of index bytes
into an RGBA buffer reads as near-random colour noise. This is leftover code from the pre-stage-2
8-bit-indexed-pixel renderer, never updated when `TbPixel` became a 4-byte RGBA struct — invisible
until now only because nothing else in the codebase calls `LbSpriteDrawImmediate()`/
`RendererSpriteDraw()` with every draw flag cleared; our cursor capture explicitly resets flags to 0
to avoid inheriting unrelated ambient state (§ the checkerboard-cursor fix above), making it the
first real caller to exercise this branch. Fixed by replacing the `memcpy` with the same
`LbDrawBufferSolid()` call the other routines use, and removing the resulting double-advance of the
destination pointer (`LbDrawBufferSolid` already advances it internally) both here and in this
function's own left-edge-clipped-block case, which had the identical double-advance bug latent
already (harmless until now since our draw is never clipped, but a real bug for any future flags-0
caller drawing a sprite clipped on its left edge).

All four build configs, `check_layering.py --strict`, and the full 1486-test suite pass after this
fix too.

**With the memcpy bug fixed, the cursor rendered with the correct silhouette but solid black instead
of its real colours** — exactly what the all-zero-palette finding above predicted. Root cause:
`front_fmvids.c` momentarily does `memset(frontend_palette, 0, PALETTE_SIZE)` followed by
`RendererPaletteSet(frontend_palette)` around Smacker cutscene playback, and `build_cursor_pixels()`
(`frontgui_style.cpp`) only ever captures once, permanently, as soon as `frontend_sprite` is
non-`NULL` — the exact `frontend_load_data()` state-exit dispatch that populates `frontend_sprite`
(Land View/Torture/NetLand View, §the earlier retry-latch fix) can plausibly coincide with this
palette-blanking window, and the one-shot capture then locks in that blanked palette forever. A
genuinely loaded `front.pal` is essentially never all-zero across all 768 bytes, so
`build_cursor_pixels()` now treats an all-zero active palette the same way it already treats
`frontend_sprite == NULL` — "not ready yet" — and keeps retrying on later frames instead of latching
a black cursor permanently.

All four build configs, `check_layering.py --strict`, and the full 1486-test suite pass after this
fix too.

**The all-zero guard stopped the pure-black case, but a milder version of the same problem
remained: the cursor rendered with correct colours but significantly darker than the source
art.** `fade_in()`/`fade_out()` (`vidfade.c`) don't just blank the palette outright — they
transiently scale the *active* palette down and back up (`ProperFadePalette`/`LbPaletteFade`) around
state transitions, including the same Land View/Torture/NetLand View transitions that first make
this capture possible. A capture landing mid-fade is non-zero (passing the earlier guard) but still
darker than intended, and — same as the black case — gets latched permanently. Rather than trying to
detect "still fading" from the active palette's brightness (fragile, no clean signal), switched to
not depending on the active palette's transient state at all:
`frontend_palette` (`vidfade.h`) is the stable, un-faded target the frontend's own rendering is
ultimately driven from, so `build_cursor_pixels()` now saves the actual active palette
(`RendererPaletteGet()`), forces `frontend_palette` active for just this one draw
(`RendererPaletteSet()`), and restores whatever was really active afterward — same
save/force/restore pattern this function already uses for draw flags and the graphics window, just
extended to the palette too. The readiness guard was updated to match: it now checks
`frontend_palette` (the buffer this draw actually uses) for all-zero, rather than the active
palette, since that's what "not ready yet" now means here — `frontend_palette` itself is only ever
genuinely all-zero before `frontend.cpp`'s `FeSt_INITIAL` exit loads `front.pal` into it, which is
well before Land View/Torture/NetLand View can ever be reached, so this guard should no longer ever
actually retry in practice, but is kept as a cheap defensive check.

All four build configs, `check_layering.py --strict`, and the full 1486-test suite pass after this
fix too.

Phase E (§7): `FeSt_CAMPAIGN_SELECT`, `FeSt_MAPPACK_SELECT` (the merged Free play screen) and
`FeSt_MP_MAPPACK_SELECT` migrated. **Scope correction found during investigation**: `FeSt_LEVEL_SELECT`
is dead code (`frontmenu_select.h`'s own comment; every entry point now routes to the merged
`FeSt_MAPPACK_SELECT` instead), so Phase E's real scope was 3 live screens, not the 4 originally
listed. Also found: the legacy screens were already far more evolved than this plan's own §3 (gap
analysis cross-reference) assumed — a real highlight-vs-commit split and a non-lossy palette-swap
approach had already landed in commit `23edef9c5` before this phase started, and
`land_preview_remap_screen_to_shared_palette` (this phase's other stated goal) never existed as a
real function; it was only ever a named placeholder for a mechanism the sprite-path implementation
had already solved. Phase E's actual work was narrower than planned as a result: a re-skin of an
already-correct interaction design, not new interaction-design work.

`frontmenu_select.c` extraction, mirroring Phase D's `frontend_format_key_binding()`/
`finalize_high_score_entry()` pattern: every row-highlight click_event (`frontend_campaign_select`,
`frontend_mappack_select`, `frontend_level_select`) split into an index-keyed `_by_index` variant
(the ImGui screens iterate their lists directly and already have a real index — no row-button
`content.lval` to decode one from) plus a thin gbtn-decoding wrapper for the unchanged legacy
click_event. Every commit-style handler that used to call `frontend_set_state()` directly
(`frontend_land_selection_enter`, `frontend_freeplay_enter`, `frontend_mp_mappack_select`,
`frontend_back_from_mp_mappack_list`) got the same split, but returning the target state (`int`,
convention: -1 = nothing to do) instead of transitioning — a `_resolve`/`_target` suffix — so the
ImGui buttons can request the transition themselves (`frontend_set_state()` is still unsafe to call
synchronously from inside an active ImGui window) while the legacy click_events keep calling
`frontend_set_state()` directly, unchanged. `land_selection_highlighted_campaign`/
`freeplay_highlighted_mappack`/`freeplay_highlighted_level` had `static` dropped (same reasoning as
Phase C's `sound_volume_ctrl`) so the ImGui screens can read the current highlight for list
selection state and the detail panel, single source of truth with the legacy widgets.

**The land preview panel (`frontmenu_landpreview.c`) is embedded as a live ImGui-composited texture,
not left on the sprite path underneath ImGui the way other migrated screens' backdrops are (§3.4).**
It's genuinely interactive (drag-to-pan, ensign click, hover animation) and needed to size/position
itself within ImGui's own layout, not a fixed screen rect — the same off-screen-render-then-composite
technique the ImGui cursor already uses (`RendererSwapFramebufferTarget`, round 3 of the cursor fixes
above), scaled from a single small static sprite up to a full panel rebuilt every frame. New facility
this needed, since nothing before this phase created dynamic ImGui textures from raw pixel data:
`ImGuiContextCreateTexture()`/`UpdateTexture()`/`DestroyTexture()` (`gui/ImGuiContext.h`/`.cpp`,
`SDL_TEXTUREACCESS_STREAMING`, LINEAR-filtered per §5.1's own decision for scaled/upscaled art —
unlike the cursor's NEAREST, chosen for a small overlay that must stay crisp at 1:1), facaded through
`RendererCreateDynamicTexture()`/`UpdateDynamicTexture()`/`DestroyDynamicTexture()`
(`RendererManager.h`) the same way every other ImGui-adjacent entry point on that file already is, so
`kfx_frontend` never needs to reach into `gui/ImGuiContext.h` directly. `land_preview_draw()` itself
needed no changes — it already resolves its destination through
`RendererGetFramebuffer()`/`LbGraphicsScreenWidth()`/`Height()` rather than `lbDisplay.WScreen`
directly, exactly the seam this redirect needs (its own doc comment already cross-references this).

**A real input-ordering bug, caught before shipping (design review, not a live report):**
`land_preview_maintain()`'s own right-click handling (clears the preview's ensign highlight) has to
consume `right_button_clicked` before `frontscreen_end_input()`'s unconditional right-click "go back"
check does, or a right-click meant only to clear the highlight would always bounce the whole screen
back to Main Menu instead. The legacy path gets this ordering for free — `get_gui_inputs()`'s
per-button `maintain_call`s (including `land_preview_maintain`) run before `frontscreen_end_input()`,
both inside the same `frontend_input()` call. Calling `land_preview_maintain()` from
`draw_land_preview_panel()` (i.e. from `FrontendImGuiFrame()`, which runs from
`RendererSoftware::PresentFrame()` — *after* `frontend_input()` in the same frame) would run too
late, every time. Fixed with a new `FrontendImGuiLandPreviewInput(state)` (`frontgui_screens.h`),
called from `frontend_input()`'s switch before `frontscreen_end_input()`, using the preview panel's
rect as cached from *last* frame's draw (this frame's own ImGui layout pass hasn't run yet at input
time) — the layout is static/percentage-of-`DisplaySize`, so a one-frame-stale rect is never visibly
wrong except across a live window resize.

**`FeSt_MP_MAPPACK_SELECT` deliberately kept as a plain list, no preview/highlight split** — behaviour
parity with the legacy screen, which never had one (no preview panel exists in its `GuiButtonInit`
array, and its row click_event always committed immediately). Not a gap Phase E needed to close; the
plan doc's original "master-detail select screens" framing for all 4 states was already inaccurate
for this one before this phase started.

All four build configs, `check_layering.py --strict`, and the full 1486-test suite pass (unchanged
at 1486 — this phase is UI composition, matching Phase C/D's own precedent of not adding tests for
screen-layout code). **Not verified interactively:** none of Phase E's three screens were reached by
hand yet this pass — correctness rests on code review and the same wrapper/list-box/texture-embedding
mechanics proven live in earlier phases (the cursor's off-screen-texture technique in particular).

**Post-Phase-E fixes, from real interactive testing (Land selection reached by hand for the first
time):**

1. **Land preview panel too small, corner decorations too large relative to it.** Root cause:
   `land_preview_draw_ornate_frame()`'s corner/edge sprites are all sized via
   `scale_ui_value_lofi()`, which scales off a *cached* value (`units_per_pixel_ui`, set once at
   actual window-resolution-change time, `vidmode.c`) — not off `gbtn->width`/`height`, i.e. not off
   this panel's own size at all. At the panel size Phase E originally gave it
   (`content_h * 0.62`, sharing the right column roughly evenly with the detail text below it), that
   fixed-size frame overhead ate a large fraction of the panel, crowding out the land art and its
   ensigns. First fix: rebalanced the split to `0.80`/`0.16` (preview/detail) in both
   `frontgui_campaignselect_frame()` and `frontgui_freeplayselect_frame()` — detail text is short (a
   name plus a few lines of description) and doesn't need much room, so giving the preview the large
   majority of the column is a straightforward win with no other tradeoff. **Confirmed live: the
   split helped, but the corner ornaments themselves were still too large** — the rebalance grows the
   *content* rect, but does nothing about the frame's own fixed absolute pixel size (see the root
   cause above: it never responds to the panel's dimensions in the first place). Second fix: a new
   `land_preview_set_frame_extra_scale_den()` (`frontmenu_landpreview.h`/`.c`) applies an *additional*
   runtime divisor on top of the existing `LAND_PREVIEW_FRAME_SCALE_NUM/DEN` — every sprite-size and
   positioning-offset call inside `land_preview_draw_ornate_frame()` already goes through the same
   `land_preview_frame_scale()` helper, so one extra divisor there uniformly shrinks the whole frame
   (sprites and their offsets together, so they still hug the content rect rather than leaving a
   gap). `draw_land_preview_panel()` sets it to `2` (half size) immediately before its
   `land_preview_draw()` call and resets it to `1` immediately after, so the legacy (`-classicmenu`)
   screen — which never touches this setter — keeps its original, already-correctly-tuned appearance.
   **Confirmed live: the ornaments shrank correctly, but a large empty margin opened up above and to
   the sides of the (now smaller) frame+content.** Root cause: `LAND_PREVIEW_FRAME_INSET` (how far the
   content rect is pulled in from the panel's own edges) was a separate `#define` using
   `scale_ui_value_lofi(26)` directly — not routed through `land_preview_frame_scale()`, so it never
   picked up the new extra-scale divisor and stayed at its full, real-resolution-relative size while
   the frame itself shrank around it. Converted to a `land_preview_frame_inset()` function that
   applies the same `land_preview_frame_extra_scale_den` divisor, closing the gap. Also rebalanced the
   preview/detail split again, from `0.80`/`0.16` to `0.75`/`0.20` per request.
2. **Ensign not clickable on the Campaign select screen.** Suspected (not independently confirmed) to
   be the same root cause as (1) — `land_preview_draw()` skips drawing any ensign that doesn't fully
   fit within the inset content rect, so ensigns likely weren't visible at all, not just hard to hit.
   **Confirmed fixed live** after fix (1)'s rebalance.
3. **Return/Enter button order backwards.** Both screens had "Enter this land"/"Play" on the left,
   "Return to Main" on the right — the opposite of the legacy screens' own order
   (`frontend_land_selection_return_to_main_maintain`/`frontend_land_selection_enter_maintain`,
   `frontmenu_select.c`, position Return at the left margin and Enter after it). Swapped in both
   ImGui screens to match. **Confirmed fixed live.**

**A separate bug, confirmed to affect the legacy (`-classicmenu`) screen too, not something Phase E
introduced, and confirmed fixed live:** highlighting a campaign or mappack in the list doesn't start its `SOUNDTRACK`/
`LAND_AMBIENT` music and ambient sound preview. Root cause: both are wired entirely into
`frontmap_load()` (`front_landview.c`), which only runs once `FeSt_LAND_VIEW` is actually entered —
before the highlight/commit split landed (commit `23edef9c5`, pre-dating Phase E), every row click
committed immediately and went straight to Land View, so this was never noticeably missing; now that
highlighting only calls `change_campaign()` + loads the preview without navigating anywhere, the
music/ambient hook is simply never reached until the user commits. Fixed with a new
`frontend_play_campaign_preview_audio()` (`frontmenu_select.c`) — the same start sequence
`frontmap_load()` uses (`frontmap_start_music()`, newly given a proper header declaration in
`front_landview.h` rather than the ad-hoc forward declarations it had at each of its two call
sites, plus the `Ft_AdvAmbSound`-gated `campaign.ambient_good`/`ambient_bad` `play_sample()` pair) —
called from `frontend_campaign_select_by_index()` and `freeplay_highlight_mappack()`, both shared by
the legacy and ImGui screens, so the fix covers both draw paths from one call site each.
`frontend_campaign_list_load()` was also simplified to delegate to
`frontend_campaign_select_by_index(0)` instead of duplicating its body, so the *first* auto-highlighted
campaign on screen entry gets the preview too, not just subsequent clicks. Deliberately **not** hooked
into per-level highlighting (`frontend_level_select_by_index`) — the theme is a mappack-level
property; restarting it on every level click within the same mappack would be disruptive. Also added:
a stop (`StopAllSamples()` + `stop_music(false)`, the same pair `frontmap_unload()` itself uses) to
`frontend.cpp`'s `FeSt_CAMPAIGN_SELECT`/`FeSt_MAPPACK_SELECT` exit handling — without it, committing
("Enter this land"/"Play" → `FeSt_START_KPRLEVEL`) never passes through `FeSt_LAND_VIEW`'s own cleanup
(the merged screens replace that step entirely), so the preview music/ambient would otherwise keep
playing into actual gameplay.

All four build configs, `check_layering.py --strict`, and the full 1486-test suite pass after these
fixes too. Items 2, 3 and the audio fix confirmed live; item 1's inset fix and the 0.75/0.20
rebalance have not yet been re-verified interactively.

Phase F (§7): `FeSt_MAIN_MENU`, `FeSt_LEVEL_STATS`, `FeSt_NET_SERVICE`, `FeSt_NET_SESSION`, and
`FeSt_NET_START` migrated, plus a new independent error-box overlay. Investigation before writing any
code found the plan's own Phase F paragraph accurate in scope this time (unlike Phase E's `FeSt_LEVEL_SELECT`
surprise) — exactly these 5 states are the real remaining gap against §2.1/§2.2, with `FeSt_LAND_VIEW`/
`FeSt_NETLAND_VIEW`/`FeSt_TORTURE` still deliberately deferred per §2.3/§2.4/"Decisions on record", and
`FeSt_UNUSED_STATE1` confirmed genuinely dead (its own header comment: "draws GUI but not used", no
`frontend_setup_state()`/`frontend_shutdown_state()` case at all). The one place reality was harder than
the plan doc's framing suggested: two of the network flow's own click handlers reach `frontend_set_state()`
several layers down, across the `kfx_net`/`kfx_frontend` boundary (`setup_network_service()` →
`net_callbacks->enter_net_session_screen()` → `frontend_set_state()`), too deep for the `_resolve()`
-returns-target-state split Phase D/E used everywhere else.

**Extraction, `frontend.cpp`/`frontmenu_net.c`/`frontmenu_net_data.cpp`:** every Main Menu/network click
handler that only calls `frontend_set_state()` after some straightforward side effect (loading a default
campaign, `LbNetwork_Stop()`, `LbNetwork_Create()`) got the established `_resolve()` treatment —
`frontend_start_new_game_resolve()`, `frontend_load_continue_game_resolve()`,
`frontend_ldcampaign_change_state_resolve()`, `frontend_netservice_change_state_resolve()`,
`frontnet_session_join_resolve()`, `frontnet_session_create_resolve()`,
`frontnet_return_to_main_menu_resolve()` — returning the target state (`int`, -1 = nothing to do) for the
ImGui screen to request itself, legacy click_events unchanged. Two row-highlight handlers with no
`frontend_set_state()` call at all (`frontnet_session_select`, `frontnet_select_alliance`) got the
simpler Phase-E-style index-based extraction only (`frontnet_session_select_by_index()`,
`frontnet_select_alliance_by_index()`) — directly safe to call from an active ImGui window, no deferral
needed.

**New pattern this phase needed, beyond anything Phase D/E required:** a generic one-shot deferred-action
mechanism (`s_pending_action`, a raw `void(*)(void)`, `frontgui_screens.cpp`) alongside the existing
`s_pending_state`/`s_pending_load_slot`, for the handful of call chains too deep or too branchy for a
`_resolve()`-returns-target-state split to cover:
- `frontnet_service_select_by_index()` — the `setup_network_service()` cross-layer chain above. The
  ImGui screen stores the clicked index in a small file-scope holder (`s_pending_net_service_index`)
  and defers the whole by-index function via a trampoline (`run_pending_net_service_select()`).
- `init_menu_state_on_net_stats_exit()` (Level Stats' OK button) and
  `frontnet_return_to_session_menu()` (Net Start's Cancel button) — both already no-arg
  `void(void)`-shaped and both have a conditional fallback baked around their own
  `frontend_set_state()` call (try to stay in net service, else fall back further), too branchy to
  usefully split into "compute target, then transition" — deferred wholesale, no extraction needed in
  their own files at all.

**Main Menu** (`frontgui_mainmenu_frame()`): six primary buttons plus Options/High Score/Quit, gated the
same way the legacy `*_maintain` functions gate button *enablement* (`ImGui::BeginDisabled()` on
`kfx_frontend_state.continue_game_option_available`/`mappacks_list.items_num`/`number_of_saved_games`).
The "wordmark asset" the plan's own §7 paragraph names as a Phase F prerequisite turned out not to
exist anywhere in the repo or the local `FXGraphics-main/` checkout, and the legacy screen itself never
had one either (`frontend_draw_large_menu_button()` draws chrome + a text caption, not an image) — kept
as a `FeHeading()` text title, the same placeholder treatment every other migrated screen already uses,
rather than treating it as a blocker.

**Level Stats** (`frontgui_levelstats_frame()`): structurally identical to the already-migrated
`FeSt_HIGH_SCORES`/`FeSt_CREDITS` — two `for` loops over the existing `main_stats_data[]`/
`scrolling_stats_data[]` tables (`front_lvlstats_data.cpp`, now declared in `front_lvlstats.h` alongside
`frontstats_data` — a one-line header addition, not an extraction, since they already had external
linkage), native `FeBeginListBox` scrolling entirely replacing `frontstats_update()`'s marquee
auto-scroll for the 54-entry scrolling block. Reproduces `frontstats_draw_main_stats`/
`_draw_scrolling_stats`' own `GUIStr_Time` special case (showing the wall-clock HH:MM:SS:MS breakdown
instead of the raw stat value) in a shared `draw_stat_row()` helper.

**The error box is not a `FrontendMenuState`-scoped screen at all** — it's an independent `GuiMenu`
(`GMnu_FEERROR_BOX`) toggled via `turn_on_menu()`/`turn_off_menu()` and normally drawn through
`draw_active_menus_buttons()` regardless of which state is active, so it can pop up *over* any migrated
screen (network errors, map-desync/fxdata-mismatch messages triggered from deep inside `kfx_net`/
`kfx_game` via `net_callbacks->create_frontend_error_box`), not just the state it happened to be
triggered from. Migrating `FeSt_NET_SERVICE`/`_SESSION`/`_START` onto the gated `draw_gui()`-skipped path
means `draw_active_menus_buttons()` — the only thing that used to draw it — stops running whenever the
underlying screen is ImGui-active. Fixed with a new `draw_error_box_overlay()`, polled every
`FrontendImGuiFrame()` call independent of `frontend_menu_state` (mirroring
`frontend_maintain_error_text_box()`'s own ESC-or-timeout dismiss logic), rather than folded into any
one screen's `case`.

**`FeSt_MP_MAPPACK_SELECT` (Phase E) and `FeSt_MAIN_MENU`/`FeSt_NET_SERVICE`/`FeSt_NET_SESSION`/
`FeSt_NET_START` (this phase) all still share `frontend.cpp`'s previously-ungated draw-switch group** —
restructured so `FeSt_MAIN_MENU`/`FeSt_LEVEL_STATS`/`FeSt_NET_SERVICE`/`FeSt_NET_SESSION`/
`FeSt_NET_START` moved into the `FeSt_FEOPTIONS`-style gated group (`frontend_copy_background();` then
conditional `draw_gui()`), leaving only the two genuinely-dead states (`FeSt_UNUSED_STATE1`,
`FeSt_LEVEL_SELECT`) in the original always-`draw_gui()` group.

**The add-session overlay (`GMnu_FEADD_SESSION`) is deliberately not ported** — same reasoning as Phase
E's `FeSt_LEVEL_SELECT` finding, one level down: it's dead-but-present code, not a working feature to
preserve. `frontend_add_session_buttons[]` is headed `//TODO GUI prepare add session screen`, its two
click_events (`frontnet_add_session_done`/`_back`) are both empty stubs with their own `//TODO NET
Finish session add menu` comments, and its only trigger (`frontnet_session_add()`, wired to
`FEBtn_MnuAddComputer`) is itself commented out in `frontend_net_session_buttons[]`. There is no
IP-address text field or session-add logic anywhere to reimplement.

**`FeSt_NET_START` is the largest and least-testable-by-hand screen in this phase** — a live multiplayer
session (two real clients) is needed to exercise the player list, alliance grid, chat, and Start Game
button for real, not just a local menu click. Built as thoroughly as the other screens (4×N alliance
grid via `ImGui::BeginTable()` + `Checkbox`, chat log + `FeTextInput` with Enter-to-send mirroring
`frontnet_start_input()`'s own body, computer-players toggle, mappack picker) but flagged here as the
one piece of this phase with the least confidence pending real multiplayer testing.

All four build configs, `check_layering.py --strict`, and the full 1486-test suite pass (unchanged at
1486 — matching Phase E's own precedent of not adding tests for screen-layout/UI-composition code).
**Not verified interactively at all yet** — none of this phase's five screens or the error-box overlay
have been reached by hand; correctness rests on code review and the same wrapper/list-box/
deferred-action mechanics proven live in Phases C-E.

Phase G (§7), first sub-step: the `keeperfx.cfg` writer §6.2 finding 1 calls for, landed independent
of everything else in this phase (as the plan itself frames it — buildable before the schema
exists). `keeperfx_cfg_write_values_to_file()`/`keeperfx_cfg_write_values()`
(`config_keeperfx.c`/`.h`) take a small array of `{key, value}` edits and rewrite the file line by
line: each key is matched the exact way `recognize_conf_command()` (`config.c`) already recognizes
it when *loading* (case-insensitive, whitespace/`=`-bounded, so `INGAME_RE` can't false-match
`INGAME_RES`'s line), only that line's value is replaced, and every other line — comments, blank
lines, key order, keys the caller didn't mention — is copied through untouched. A key with no
existing line is appended at the end; a key that appears more than once in the file (user error, or
a stale duplicate) has every occurrence updated, so none is left stale. `load_configuration()` now
remembers the exact path it loaded from (`loaded_keeperfx_cfg_path`, including a `-config <file>`
override) so the real-usage wrapper always writes back to the file the game actually read, not a
freshly re-resolved default — the explicit-path variant is what's unit-tested, for the same reason
`load_configuration()` itself isn't (`config_keeperfx_test.cpp`'s own long-standing comment: no way
to point it at a fixture without a real file-I/O side effect against the actual game install).
Eight new test cases cover the round-trip (comments/order/unrecognised-keys preserved), multi-edit
batches, append-when-missing, duplicate-key update, the prefix-false-match guard, creating a
nonexistent file from scratch, and input validation — all four build configs, `check_layering.py
--strict`, and the full 1494-test suite (1486 + these 8) pass.

Phase G, step 2: the §6.2 finding 2 config keys, and the §6.3 option schema's first slice.
Investigating CD music first found the finding's own framing slightly stale: `MUSIC_FROM_DISK`
(config key 29) already round-trips it end to end (`Ft_NoCdMusic`, with `Clo_CDMusic` already
overriding it from `-cd`) — the launcher's "GAME_PARAM_USE_CD_MUSIC" is just that key's inverse
boolean, a schema-mapping detail rather than a missing key. The three genuinely missing ones got
new `conf_commands[]` entries and matching `Clo_*` overrides (`CMDLINE_OVERRIDES` bumped 5→8):
`EASTER_EGG` (`start_params.easter_egg`, already `kfx_config`-owned — no new plumbing), `ALT_INPUT`
(`lbMouseGrab`, `kfx_platform`-owned but *below* `kfx_config` in the layering, so a plain
`#include` — no callback needed either), and `VID_SMOOTH` (`smooth_on`, `kfx_render`-owned and
*above* `kfx_config`, so this one did need a new `ConfigReloadCallbacks` entry,
`set_vid_smooth` — the positional-initializer struct literals in `config.c`'s
`default_config_reload_callbacks` and `main.cpp`'s `config_reload_callbacks_impl` both needed the
new slot inserted at the exact matching position, same trap the concurrent resolution-collapse
work above had already hit once this session). All three follow the existing `Clo_ClassicMenu`
shape: one-directional "force on" launch flags, guarded in the config-file parser so an already-set
override skips re-parsing the file's own value entirely, applied unconditionally in
`process_cmdline_overrides()`.

**The option schema** (`config_settingschema.h`/`.c`, new `kfx_config` files): `struct SettingOption`
carries a `cfg_key`, `type` (`SOptT_Bool`/`SOptT_Int` so far — §6.3 also names enum/float/string/
keybind/composite, not yet modeled), `category` (`SCat_Game`/`Graphics`/`Sound`/`Input`, mirroring
the launcher's own tab grouping per §6.2/§6.3's decision), `apply_class`
(`SApply_Live`/`SApply_NeedsRestart`), a `label_stridx` (`help_stridx` support exists but every row
is 0/none for now — labels landed, help text didn't), and get/set accessor pairs matching the type.
`setting_option_apply_bool()`/`_int()` call the accessor immediately (live engine effect) and then
`keeperfx_cfg_write_values()` (Phase G step 1's writer) to persist it — the schema is the writer's
first real caller. `is_enabled` models §6.2 finding 4's "some controls gate others": `ALT_INPUT`
flips which of `UNLOCK_CURSOR_WHEN_GAME_PAUSED`/`LOCK_CURSOR_IN_POSSESSION` is meaningful, exactly
the pair the finding names.

**The table itself is a representative slice, not all ~35 options** — one row per category, both
apply-classes, the one real enable-condition pair, deliberately choosing options needing no new
cross-layer callbacks beyond what step 2's config-keys work already added (proving the schema's
shape before populating the rest of it, same "narrower than the ideal, on purpose" discipline as
every prior phase). Ten new `GUIStr_*` labels added to `lang/gtext_eng.pot` (guitext 1125-1134,
English-first per §6.3) for exactly this slice's options.

Phase G, step 3: the generic renderer, wired into the already-migrated `FeSt_FEOPTIONS` screen
(`frontgui_options_frame()`, Phase C) as four `FeBeginTabBar`/`FeTab` tabs — Game/Graphics/Sound/
Input, mirroring the launcher's own grouping per §6.2/§6.3's decision (and matching the tab names
Phase B's style-sheet proving-ground screen already anticipated). `draw_setting_options_for_category()`
is the actual generic bit: one `for` loop over `setting_options[]` filtered by category, one
`FeCheckbox`/`FeSlider` per row picked by `type`, `ImGui::BeginDisabled` when `is_enabled()` says no,
applied immediately via `setting_option_apply_bool()`/`_int()` on change. A trailing `" *"` on the
label plus a one-line `draw_setting_options_restart_note()` footnote (shown only when the visible
tab actually has one) is the whole "needs-restart" UI treatment — no separate widget needed.

**Sound and Input additionally carry the pre-existing binary-`GameSettings` controls** (volume
sliders, mouse sensitivity/invert) above their schema rows, unchanged from before this phase --
those predate the schema entirely, aren't `keeperfx.cfg`-backed (they persist through
`save_settings()`'s different binary struct, §6.1), and folding them into the *shape* of a settings
screen with real tabs is what makes this land as one coherent screen rather than two competing
settings surfaces side by side. They stay hand-written rather than becoming schema rows -- a
second, later schema (or a schema extension) for the binary-settings side is future work, not
something this phase's `keeperfx.cfg`-focused schema needs to absorb.

**Game and Graphics tabs use plain English literals for their own tab caption** (`"Game"`/
`"Graphics"`), not `get_string()` -- same as the Phase B proving-ground screen's own tab labels; no
existing `GUIStr_*` fits a short tab caption (the closest candidate, `GUIStr_DisplayResolution`, is
a full in-game tooltip sentence, wrong for this). English-first is an accepted interim per §6.3's
own decision; a dedicated pair of `GUIStr_*` tab-caption strings is left for whichever pass adds
help text to the schema, rather than added speculatively here.

All four build configs, `check_layering.py --strict`, and the full 1503-test suite pass unchanged
(no new tests -- screen-layout/UI-composition code, same precedent Phase E/F both used).

**Confirmed live, with one known follow-up deliberately deferred:** the `##FeOptions` window is
`ImGuiWindowFlags_AlwaysAutoResize`, so its height changes per tab with however many rows that tab
currently has -- noticeably not ideal once tabs have very different row counts. The fix (a fixed
window size with an internal scroll region per tab, `FeBeginScrollArea`) is deliberately not done
yet: picking a sensible fixed height needs to know the *eventual* row count once every option in
§6.2's list has a schema row, not just this slice's 10 -- sizing it now would mean resizing it again
later anyway. Revisit once the schema is fully populated.

**Follow-up landed** (after the full 34-row schema shipped, requested again once the user actually
saw tabs jump size against each other live): `##FeOptions` now uses `ImGui::SetNextWindowSize(...,
ImGuiCond_Always)` for a fixed size on every frame (a `0.5 x 0.8` fraction of the display) instead
of `ImGuiWindowFlags_AlwaysAutoResize`, and each tab's own row list -- including Sound/Input's
pre-existing hand-written volume/sensitivity controls, not just the schema-driven rows -- is wrapped
in its own `FeBeginScrollArea`/`FeEndScrollArea` sized to a shared fixed height
(`GetContentRegionAvail().y - 130.0f`, floored at 80px). The tab bar's own headers and the bottom
Define Keys/Return buttons sit outside every scroll area, in the fixed window, so they stay visible
regardless of scroll position or which tab is active -- confirmed as the exact intended layout mid-
request ("scrollbar should be along options, the tabs, and the return button should remain
visible"). Verified: all four build configs, `check_layering.py --strict`, full 1553-test suite
(no new cases -- screen-layout code, same precedent as the rest of this section).

**Two more fixes from that live confirmation**: (1) `0.5x` width was too narrow -- both the tab
bar (all four headers didn't fit, so ImGui fell back to its own scroll-arrows/truncation, e.g.
"Graph...") and individual rows (a control's own default width left too little room for its label,
clipping text like "Display Num[ber]" against the window's edge). Widened to `0.7x`, and
`SOptT_Enum`/`SOptT_Int` rows now get an explicit `ImGui::SetNextItemWidth(220.0f)` instead of
ImGui's own default (a large fraction of the available width) -- bounding the control leaves
predictable room for the label regardless of window width or `UI_FONT_SCALE`. (2) Text visibly
softened/blurred at smaller `UI_FONT_SCALE` percentages -- `FeStylePushFont()`'s computed size
(`display_h/32`, further scaled by each font role's own multiplier and by `UI_FONT_SCALE`) is
essentially never an integer, and 1.92's dynamic font system rasterizing a fresh glyph bitmap at a
fractional pixel size still means a fractional baseline, forcing the renderer to blend every glyph
across two rows of pixels vertically. Fixed by rounding the final per-role size to the nearest
whole pixel immediately before `ImGui::PushFont()`. Verified: all four build configs,
`check_layering.py --strict`, full 1553-test suite (no new cases).

**The pixel-rounding fix above did not resolve the blur** -- confirmed live, text still looked soft
after it shipped. Root cause not conclusively identified (traced the render pipeline as far as
`RendererSoftware::PresentFrame()`'s own backdrop-to-window blit, which is explicitly
`SDL_SCALEMODE_NEAREST` and shares no texture with ImGui's own SDLRenderer3 draw calls, so a
whole-frame upscale doesn't explain it either -- likely just the inherent softness of anti-aliased
TrueType rendering at these sizes, next to Bullfrog's own crisp hand-drawn bitmap fonts elsewhere in
the game, but not confirmed). Rather than keep chasing the root cause, `UI_FONT_SCALE` changed from
a free 50-200 slider (`SOptT_Int`) to a curated `SOptT_Enum` of six standard sizes (50/75/100/125/
150/200) -- a continuous range let the player land on a size that happened to render worse than a
neighbouring one; a small, deliberately-chosen set sidesteps that regardless of the underlying
cause. `.name` stays a bare number (e.g. `"150"`), not a friendlier label, since
`setting_option_apply_enum_index()` writes it straight to `keeperfx.cfg` and this key's own parser
(`config_keeperfx.c` case 52) still reads it with plain `atoi()` -- unchanged persistence format,
so an existing `UI_FONT_SCALE` line from the old slider still loads (falling back to the schema's
usual "no exact match" index-0 behaviour if its value isn't one of the six). Verified: all four
build configs, `check_layering.py --strict`, full 1553-test suite (one existing test case rewritten
for the new shape, no new cases). Not yet re-confirmed live.

**Live-testing follow-up hypothesis**: "i think it might be linked to the resolution not updating
so the menu might always be rendering at 640x480, and the 50% & 75% sizes suffer from lack of
pixels?" Investigated three candidate mechanisms rather than guessing:

1. HiDPI logical-vs-pixel mismatch (`io.DisplaySize` from `SDL_GetWindowSize()`, a *logical*/point
   size, vs `io.DisplayFramebufferScale` derived from `SDL_GetWindowSizeInPixels()`, the real pixel
   size — `imgui_impl_sdl3.cpp`'s `ImGui_ImplSDL3_GetWindowSizeAndFramebufferScale()`). Ruled out:
   `keeperfx.exe.manifest` declares `dpiAwareness = permonitorv2,permonitor`, and
   `WindowSystemSDL::CreateWindow()` doesn't set `SDL_WINDOW_HIGH_PIXEL_DENSITY` — on the actual
   shipping platforms (Windows/Linux, not macOS/Wayland) this combination keeps window
   coordinates and pixels 1:1, so `DisplayFramebufferScale` is 1.0 and can't be the mechanism here.
2. A fixed low-resolution render target/logical-presentation scaling everything ImGui draws (which
   would match "always rendering at 640x480" literally). Ruled out: no
   `SDL_SetRenderLogicalPresentation` (or equivalent) call anywhere in `src/kfx_platform/`; the
   `SDL_Renderer`'s output size just tracks the real window size. `RendererSoftware`'s small
   `lbDrawSurface`-sized texture stretch (§ above, `SDL_SCALEMODE_NEAREST`) is a wholly separate
   draw call from ImGui's own SDLRenderer3 vertex/texture calls, confirming the earlier finding that
   the two don't share a texture.
3. Missing dynamic-font-atlas support making `PushFont(font, size)` merely rescale a single baked
   bitmap rather than re-rasterize (`IMGUI_ENABLE_FREETYPE` is commented out in `imconfig.h`, no
   FreeType sources are wired into any `CMakeLists.txt`). Ruled out as the primary cause: the
   vendored `imgui_impl_sdlrenderer3.cpp` sets `ImGuiBackendFlags_RendererHasTextures`, which is
   exactly the flag that makes 1.92's dynamic font system re-rasterize glyphs on demand at whatever
   size `PushFont` asks for -- FreeType is an optional rasterizer-quality upgrade on top of that,
   not a requirement for dynamic sizing itself.

**User confirmed they had restarted, and `INGAME_RES` still reported 640x480** -- ruling out "forgot
to restart" and pointing at something not applying across a restart at all. Traced the actual
startup order in `setup_game()` (`main.cpp`) rather than guessing further, and found a real,
previously-latent bug:

`Lb_SCREEN_MODE_INVALID` is `0` (`bflib_video.h`) -- indistinguishable from a real mode that happens
to land at registry index 0, since `LbRegisterVideoMode()` just returns `lbScreenModeInfoNum` as the
new mode's index and increments it (`bflib_video.c`). This is normally safe because
`LbRegisterStandardVideoModes()` always registers an `"INVALID"` placeholder *first*, reserving index
0 before anything else can land there -- but that function only ever ran from inside
`LbScreenInitialize()`, which `setup_game()` doesn't call until deep inside `setup_screen_mode_zero()`
(for the legal screen), well *after* `load_configuration()` -- and `load_configuration()` is exactly
where `INGAME_RES`'s case-7 parser calls `LbRegisterVideoModeString()` on whatever's in
`keeperfx.cfg`. On a config containing a custom `INGAME_RES` resolution not already known, that call
ran against a completely empty mode table, so the user's *own* resolution became the table's actual
first entry and landed on index 0 itself -- indistinguishable from `Lb_SCREEN_MODE_INVALID`, and
silently rejected by case 7's `k > 0` check (`config_keeperfx.c`). `set_screen_vidmode()` was never
called, `screen_vidmode` stayed at its compiled default (`Lb_SCREEN_MODE_640_480_8`), and the window
came up at 640x480 every single time, exactly matching the report. `config_settingschema_test.cpp`
had actually already noted this exact index-0 hazard for its own test binary (guarding it with an
`ensure_screen_mode_registry_nonempty()` helper) under the assumption "In the real game this never
bites because `LbRegisterStandardVideoModes()` always fills index 0 before any user config gets
parsed" -- that assumption was the bug; it was never actually true.

**Fix**: extracted the mode-table population out of `LbScreenInitialize()` into its own idempotent
`LbRegisterDefaultVideoModesIfNeeded()` (`bflib_video.c`/`.h`, guarded on `lbScreenModeInfoNum == 0`,
so `LbScreenInitialize()`'s existing call becomes a safe no-op second call) and invoke it in
`setup_game()` *before* `load_configuration()`, reserving index 0 with the "INVALID" placeholder
ahead of any config parsing that might call `LbRegisterVideoModeString()`. Regression test added:
`bflib_video_test.cpp` ("`LbRegisterDefaultVideoModesIfNeeded` reserves index 0 so a fresh custom
resolution never collides with `Lb_SCREEN_MODE_INVALID`") registers a resolution never seen before in
the process and checks it doesn't land on index 0. `config_settingschema_test.cpp`'s misleading
comment updated to record what actually happened. Verified: all four build configs,
`check_layering.py --strict`, full test suite (one new test case, 0 → this fix). **Not yet
re-confirmed live** -- this explains and fixes why `INGAME_RES` never actually took effect across a
restart at all; whether the originally-reported `UI_FONT_SCALE` blur was solely a symptom of that (the
window/font-size math genuinely was stuck at 640x480 the whole time) or has some remaining cause of
its own can only be judged once the user re-tests against a resolution that now actually applies.

**Follow-up, before restarting was even involved**: "the ingame res button isnt changing from 640x
when clicked" -- a second, distinct bug in the same area, this time in the live Options-screen combo
itself. `get_ingame_res()` (its `SOptT_Enum` getter, used both to display the combo's current
selection and to compute `setting_option_enum_current_index()`) read
`LbScreenGetModeInfo(LbScreenActiveMode())` -- the mode *currently applied* to the real window. But
`INGAME_RES` is `SApply_NeedsRestart`: picking a new resolution calls `set_ingame_res()` →
`set_screen_vidmode()`, which only updates the *pending* `screen_vidmode` global
(`kfx_render/vidmode.c`) for `setup_screen_mode()` to pick up next launch -- the actually-active mode
never changes mid-session. So the very next frame, the combo re-read `get_ingame_res()`, got back the
same old active resolution, and immediately snapped back to displaying 640x480 -- looking exactly
like the click did nothing.

Fixed by adding a `get_screen_vidmode()` getter to `ConfigReloadCallbacks` (`config.h`/`config.c`'s
noop stub and default array entry/`main.cpp`'s direct wiring, same position throughout, same
"getter added later for the Options screen" shape as `get_base_mouse_sensitivity`) that reads the
*pending* `screen_vidmode` value instead, and switching `get_ingame_res()` to use it. Now the combo
reflects whatever was just picked immediately, and only the real window resolution itself waits for
the restart -- which is what the `*`/"takes effect after restarting" note next to the row already
promises. Regression test rewritten (`config_settingschema_test.cpp`): registers two distinct modes,
sets one as `lbDisplay.ScreenMode` (active) and overrides the callback to return the other (pending),
and checks `get_enum()` returns the pending one, not the active one. `config_reload_callbacks_test.cpp`
extended with the matching noop smoke-test line. Verified: all four build configs,
`check_layering.py --strict`, full test suite. Not yet re-confirmed live.

**Follow-up, restart still lands on 640x480**: user changed to 1920x1080, restarted, and shared a
`keeperfx.log` excerpt showing `LbScreenSetup: Mode 640x480 setup succeeded` right at startup -- so
the *previous* fix (the index-0 reservation) wasn't the whole story. Traced `setup_screen_mode()`'s
own failsafe path (`kfx_render/vidmode.c`): if `LbScreenIsModeAvailable(nmode, display_id)` rejects
the requested mode, it calls `try_failsafe_vidmode()`, which unconditionally returns
`Lb_SCREEN_MODE_640_480_8` -- the exact value observed. `LbScreenIsModeAvailable()` →
`LbHwCheckIsModeAvailable()` (`bflib_video.c`), for a mode that's neither windowed/borderless/FILLALL/
DESKTOP (i.e. every custom `INGAME_RES` resolution), calls `PlatformManager_GetClosestDisplayMode()`
and rejects the mode unless the closest match's width *and* height come back identical.
`GetClosestDisplayMode()` (`WindowSystemSDL.cpp`) called `SDL_GetClosestFullscreenDisplayMode(...,
0.0f, false, ...)` -- `include_high_density_modes = false` -- while `GetFullscreenDisplayModeAt()`/
`GetFullscreenDisplayModeCount()`, which populate the `INGAME_RES` picker's own list in the first
place, call `SDL_GetFullscreenDisplayModes()` with no density filtering at all. A resolution that
only exists as a high-density mode on the user's display would be offered by the picker but then
rejected by the availability check that runs right after picking it -- a real, confirmed inconsistency,
fixed by passing `true` instead (matching what the picker itself already listed) in both
`GetClosestDisplayMode()` and the equivalent call in `SetWindowDisplayMode()` (which actually applies
the mode once it's been accepted as available -- left inconsistent with the check would have meant a
mode that passed availability could still fail to apply). Verified: all four build configs,
`check_layering.py --strict`, full test suite (no new cases -- this path needs a real SDL display to
exercise, same reason `config_keeperfx_test.cpp` already documents `load_configuration()`'s own gap).
**This turned out not to be it either.** The user's next `keeperfx.log` (read directly, this session's
own build/test machine -- `core_files/keeperfx.cfg` already correctly held `INGAME_RES=1920x1080x32`,
confirming the write side was never the problem) showed no "not available" `ERRORLOG` anywhere, and
`LbScreenSetup: Mode 640x480` appeared with no failsafe warning preceding it at all -- meaning
`setup_screen_mode_zero()` was asked for 640x480 *directly*, never rejected. So `screen_vidmode` was
never becoming 1920x1080 in the first place, and the actual root cause was upstream of everything
investigated so far.

**Actual root cause, found by tracing `setup_game()`'s own startup order in `main.cpp`**:
`set_config_reload_callbacks(&config_reload_callbacks_impl)` was called at the *end* of a long stretch
of engine wiring -- **after** `load_configuration()` already ran. But `config_keeperfx.c`'s own
switch-case config parser calls straight through `config_reload_callbacks->X()` *during*
`load_configuration()` itself for several settings, `INGAME_RES` (case 7, `set_screen_vidmode()`)
among them. Until `set_config_reload_callbacks()` runs, `config_reload_callbacks` points at
`default_config_reload_callbacks` -- every field a do-nothing noop stub (`config.c`). So for the
entire duration of `load_configuration()`, `INGAME_RES`'s case 7 correctly parsed
`INGAME_RES=1920x1080x32`, correctly registered a new video mode via `LbRegisterVideoModeString()`
(no warning, because it *did* succeed), and then called `config_reload_callbacks->set_screen_vidmode()`
-- which quietly ran `config_reload_noop_set_screen_vidmode()` and did nothing at all.
`screen_vidmode` never moved from its compiled 640x480 default, no matter what the config file said,
on every single restart -- a bug entirely independent of, and upstream of, everything in the two
follow-ups above. (It would have applied identically to every other config-parse-time
`config_reload_callbacks` consumer in that same switch -- `POINTER_SENSITIVITY`, `VID_SMOOTHING` --
though those weren't independently confirmed broken here.)

Fixed by relocating `config_reload_callbacks_impl`'s declaration and its `set_config_reload_callbacks()`
call from their old position (after a long stretch of unrelated engine-wiring calls) to immediately
before `load_configuration()`, right after the `LbRegisterDefaultVideoModesIfNeeded()` call added
earlier. Confirmed safe to move: every one of the struct's ~80 entries is a plain `&function_name`
reference to an already-declared free function (no captured local state, no lambda), so relocating the
assignment changes *when* the wiring takes effect without changing *what* it wires up. Verified: all
four build configs, `check_layering.py --strict`, full test suite (no behavioural change to any
existing test, since the struct's contents are untouched).

**Confirmed live**: user picked 1920x1080, restarted, and confirmed both the resolution itself and
the earlier `UI_FONT_SCALE` blur are resolved -- the blur really was just a symptom of the window
being stuck at 640x480 the whole time, exactly as suspected. This closes out the INGAME_RES-never-
applies saga (three separate, real bugs found and fixed along the way: the index-0 collision, the
Options-combo revert-on-pick, and this callback-wiring-order issue -- only the last one was the actual
blocker for "never applies across a restart", but the other two were genuine bugs in their own right
and stay fixed).

**Immediate follow-up, found live now that resolution genuinely varies**: "the mouse cursor is
enormous at high resolutions, and makes it difficult to click on buttons." The ImGui-hover cursor
overlay (`ImGuiContext.cpp`, added earlier this document to fix a *different* bug -- the cursor
visibly popping in size crossing the legacy/ImGui boundary) sized itself via `scale_ui_value_lofi()`,
the same legacy DK-asset scale `LbI_PointerHandler::OnBeginSwap` (`bflib_mspointer.cpp`) uses for the
cursor over legacy content. That scale is tuned for bitmap UI stretched proportionally from a
640x400 reference (`units_per_pixel_ui`, `vidmode.c`'s `update_screen_mode_data()`: grows roughly with
`io.DisplaySize.y/25`), while ImGui content is laid out in real native pixels sized off
`io.DisplaySize.y/32` (`FeStylePushFont`, `frontgui_style.cpp`) -- a visibly gentler curve. The two
scales happen to roughly agree around 640x480 (where the original fix was tuned and verified, back
when the window was -- unknowingly, per the bug above -- *always* 640x480 regardless of what
`INGAME_RES` said) but diverge sharply at higher resolutions: by 1080p the legacy scale is already
~2.7x its 640x480 value against ImGui's own ~2.25x, on top of the cursor's sprite already being
sized to look right standing alone rather than sitting inside a button.

Fixed by decoupling the ImGui-hover cursor's size from `scale_ui_value_lofi()` entirely and deriving
it from the same reference metric `FeStylePushFont` uses instead (`io.DisplaySize.y/32`, clamped
11-96px, since `kfx_platform` can't reach `kfx_frontend`'s actual function -- `kfx_config`/
`kfx_frontend` both sit above it -- so this re-derives the same shape locally rather than adding a new
cross-layer callback for one value), targeting a cursor height of `1.2x` that reference (visibly
bigger than body text, comfortably smaller than a button) and scaling the cached sprite texture's
width/hotspot proportionally to preserve its aspect ratio. This trades the previous fix's "matches the
legacy cursor exactly, no pop crossing the boundary" guarantee for "stays clickable-sized against
ImGui content at any resolution" -- the right trade now that every clickable control lives in ImGui.
A minor size difference crossing the legacy/ImGui boundary at resolutions far from 640x480 is an
accepted, documented cost of this trade, not an oversight. Also dropped `ImGuiContext.cpp`'s
`bflib_video.h` include entirely now that `scale_ui_value_lofi()` is unused there -- a small layering
tidy-up (`kfx_platform`'s own render/GUI code reaching into `kfx_platform`'s own video code is legal
either way, but the file no longer needed the dependency at all). Verified: all four build configs,
`check_layering.py --strict`, full test suite.

**Two more issues from the same live check**: (1) the cursor was visibly drawn *twice* -- once baked
into the framebuffer by the legacy cursor path, once by ImGui's own foreground-draw-list overlay.
`LbI_PointerHandler::OnBeginSwap()`/`OnMove()` (`bflib_mspointer.cpp`) always drew the legacy cursor
into the backdrop, with no awareness that `ImGuiContext.cpp` was *also* drawing it whenever
`io.WantCaptureMouse` was true -- `ImGuiContextWantCaptureMouse()` existed already (kfx_platform,
same layer) but nothing had ever actually called it. Fixed by gating both call sites on it: skip the
legacy draw entirely (`OnBeginSwap`) or fall back to the plain `NewMousePos()`-only branch
(`OnMove`'s advanced-redraw path) whenever ImGui already owns the cursor -- `OnMove` needed the same
gate, not just a "don't draw twice" fix, because its `Undraw()` restores whatever `Backup()` last
saved, and skipping `Backup()`/`Draw()` in `OnBeginSwap()` without also skipping `Undraw()` here would
paste back a stale backup instead of leaving the ImGui-drawn frame alone.

(2) "there's an alignment issue with the cursor, that makes it difficult to select things at higher
resolutions" -- `GFS_cursor_horny`'s hotspot was hardcoded to (0,0), matching `vidmode.c`'s own
convention for that sprite, but the sprite is an elaborate gauntlet with no unambiguous "tip" pixel at
its own bounding-box corner -- any inherent error in that convention is a fixed number of *sprite*
pixels, so it grows right along with the cursor at higher resolutions. Per the user's own suggestion,
replaced it with a plain arrow (`dkfans/FXGraphics`' `enginefx/pointer-256/cursor_arrow.png`), loaded
directly via `spng` (already available to `kfx_frontend` through the shared `kfx_common_opts`
interface target -- no `CMakeLists.txt` change needed) rather than routed through the classic
RLE-sprite pipeline, mirroring how `frontgui_style.cpp` already loads the Exocet/Cinzel fonts straight
from `fxdata/`. Direct inspection of the source PNG's alpha channel (not a live test) found it ships
as a 128x124 canvas with the actual ~36x36 claw glyph tucked in one corner, tapering to its visual
point at the glyph's own bottom-right -- using the full padded canvas with a naive (0,0) hotspot would
have put the click point 48-60 pixels from anything visible. `load_cursor_arrow_png()` crops to the
glyph's own opaque bounding box and sets the hotspot at that crop's bottom-right corner to match.
Falls back to the existing `GFS_cursor_horny` sprite path (unchanged) if the PNG is missing or fails
to decode, e.g. an install predating this asset -- attempted exactly once, up front, since (unlike the
sprite fallback) a plain file read has no game-state readiness to wait on.

The PNG itself isn't part of this repo (`fxdata/` is entirely runtime/install data, never tracked in
git, per this document's own opening notes) -- copied into this session's own `core_files/fxdata/` for
local testing only. Shipping it for real needs adding to whatever assembles `fxdata/` for a
distributed install (`pkg-enginegfx`'s own FXGraphics-to-`.dat` pipeline is for the classic RLE-sprite
format specifically and doesn't apply here, since this loads the PNG directly) -- not done as part of
this fix. Verified: all four build configs, `check_layering.py --strict`, full test suite (no new
cases -- both fixes need a real ImGui/SDL context to exercise). Not yet confirmed live, and the
cropped hotspot placement in particular is a considered best guess from pixel inspection, not a
verified-correct value -- flagged as such to the user.

**That guess was wrong on both counts, live**: "significantly too large" and "the click location
should align with the tip (upper-left corner of the arrow)" -- not the bottom-right the pixel
inspection concluded. The user also pointed out the fix duplicated an asset that's already shipped:
"since this is the same asset as already used in the in-game interface, can't that be used instead of
adding as a 2nd copy in fxdata?" -- `enginefx/pointer-256/cursor_arrow.png` is the *source* art for
`pointer-64`'s baked `cursor_arrow.png`, which is already loaded into `pointer_sprites`
(`vidmode.h`/`.c`) as `MousePG_Arrow`, the in-game engine's own normal-state cursor. Confirms the
bottom-right guess was simply wrong too: `set_pointer_graphic()`'s own `MousePG_Arrow` case
(`vidmode.c:336`) hardcodes that sprite's hotspot as `(12, 15)` in its native pixel space -- and
`12*4=48`, `15*4=60` match the *top-left* corner of the opaque bounding box the pixel inspection had
already found in the 4x-larger 256px source PNG, not the bottom-right this doc previously guessed at.

Fixed properly this time by dropping the standalone-PNG loader (`spng`/`fxdata/cursor_arrow.png`)
entirely and rendering the shipped `MousePG_Arrow` sprite instead, through the exact same off-screen
`RendererSwapFramebufferTarget` technique `GFS_cursor_horny` used originally -- same code shape, just
a different sprite sheet (`pointer_sprites` via `get_sprite()`, `bflib_sprite.h`) and index
(`get_player_colored_pointer_icon_idx(MousePG_Arrow, 0)`, `config_spritecolors.h` -- player 0 since
menus aren't player-specific), with the hotspot taken verbatim from `vidmode.c`'s own established
`(12, 15)` rather than re-derived by inspection. No second copy of the art anywhere, and the hotspot
is now definitionally correct (it's the same value the shipped engine already trusts for this exact
sprite) rather than guessed. Also dropped `ImGuiContext.cpp`'s cursor-size multiplier from `1.2x` to
`0.7x` its reference metric -- a smaller, more conservative starting point now that the sprite source
changed; not yet confirmed live either. Verified: all four build configs, `check_layering.py
--strict`, full test suite.

**Two more items raised in the same message**: (1) "The legacy background cursor is still rendered,
why are we overlaying the imgui boxes over that rather than just controlling the background directly
with imgui, for the menus?" -- asked the user which fork they wanted (harden the existing
`WantCaptureMouse` suppression further, vs. having menu screens forgo concurrent legacy backdrop
rendering entirely). **Chose the latter** -- a genuinely bigger architectural change (every menu
screen currently composites ImGui over a live legacy-rendered backdrop, by original design going back
to §5.1's responsive-layout work, not an oversight), not yet started as of this entry; needs its own
design pass before implementation, matching how `docs/refactor/gui/05-campaign-progress-and-landview.md`
handled the last feature of comparable size in this codebase. Plan written up in
[05-imgui-owned-menu-backdrop.md](05-imgui-owned-menu-backdrop.md) -- not yet implemented as of that
document's own status line.

(2) "There are a number of options on the in-game graphics menu, that don't appear on the main-menu->
settings route. They should be added to the graphics tab" -- investigated by diffing the classic
in-game "Video" options menu (`frontmenu_ingame_opts_data.cpp`'s `video_menu_buttons`) against the
schema's `SCat_Graphics` rows. Found five gaps, but they split into two structurally different groups:
`video_shadows`/`video_view_distance_level` (`gui_video_shadows()`/`gui_video_view_distance_level()`,
`frontmenu_options.c`) just assign `settings.video_shadows`/`settings.view_distance` directly -- plain
local rendering preferences, no active-game dependency, portable to the main-menu route. `rotate_mode`/
`cluedo_mode`/`gamma_correction` (`gui_video_rotate_mode()`/`gui_video_cluedo_mode()`/
`gui_video_gamma_correction()`, same file) instead call `set_packet_action()`/`set_players_packet_action()`
-- routed through the game's packet/command system, which needs a live player and packet queue that
simply doesn't exist at the main menu. Asked the user how to proceed; **chose shadows + view_distance
only**, leaving the packet-routed three as in-game-only since they structurally can't work any other
way today.

A second wrinkle surfaced along the way: `settings.video_shadows`/`settings.view_distance` (both
already kfx_config-owned state, `struct GameSettings settings`, `config_settings.h` -- no cross-layer
callback needed for the get/set half) persist via `save_settings()` to `save/settings.toml`, a
completely different file from every other schema row's `keeperfx.cfg`. The schema had no notion of a
second persistence backend at all. Added one: `struct SettingOption` gained
`persist_via_save_settings` (`config_settingschema.h`), an opt-in bool that makes
`setting_option_apply_bool()`/`_apply_int()` call `save_settings()` instead of
`keeperfx_cfg_write_values()` when set, and exempts the row from the schema's usual "every row has a
cfg_key" shape check (`config_settingschema_test.cpp`, same exemption shape as `SOptT_Action`'s own).
Two new `SOptT_Int` rows (0-3, matching `config_settings.c`'s own `clamp()` on load) added under
`SCat_Graphics`: `SHADOWS`/`VIEW_DISTANCE` (new `GUIStr_Set*`/`GUIStr_Help*` string pairs, `gtext_eng.pot`
guitext 1199-1202). Tests cover the row shape and `get_int`/`set_int` round-tripping directly against
`settings.video_shadows`/`view_distance` -- not the full `setting_option_apply_int()` path, since that
now calls `save_settings()`, which does real file I/O against `save/settings.toml` with no
test-supplied path override, the same class of gap `config_settings_test.cpp` already documents for
`load_settings()`/`save_settings()` themselves. Verified: all four build configs,
`check_layering.py --strict`, full test suite (two new test cases). Not yet confirmed live.

**Follow-up, found live while verifying `05-imgui-owned-menu-backdrop.md` Phase A/B**: "backdrop
looks correct on every screen, but no mouse cursor visible" -- a real bug in `build_cursor_pixels()`
(`frontgui_style.cpp`), not anything to do with the backdrop change itself. `s_cursor_build_attempted`
was set `true` *before* the final `get_sprite(pointer_sprites, icon_idx)` lookup was checked for
success -- so if that one step ever failed on its first attempt, the failure latched permanently,
directly contradicting every comment in the function promising "keeps retrying instead of caching a
premature ... result." The MousePG_Arrow switch introduced a new dependency this exposed:
`get_player_colored_pointer_icon_idx()` calls `config_reload_callbacks->get_player_color_idx()` →
`get_dungeon(plyr_idx)->color_idx` (`player_data.c`) -- real per-game dungeon state that's far more
likely to be in some not-yet-meaningful state at the main menu (no game in progress) than
`GFS_cursor_horny`'s old simple frontend-sprite lookup ever was, even though `get_player_colored_idx()`'s
own bounds-checked fallback (`config_spritecolors.c`) means the *value* it returns should still be
safe either way -- the actual bug was purely the latch-before-verifying-success ordering, not a bad
value making it through. Fixed by moving the `s_cursor_build_attempted = true` assignment to after
confirming `get_sprite()` succeeded (also added the `num_sprites()` bounds check
`set_pointer_graphic()`'s own equivalent lookup already does, `vidmode.c`, which this code was
missing). Verified: all four build configs, `check_layering.py --strict`, full test suite.

**That fix alone didn't resolve it either** -- user confirmed the cursor was still never visible.
Rather than guess a third time, added temporary `SYNCLOG` diagnostics to `build_cursor_pixels()`
reporting which gate was actually failing. The resulting `keeperfx.log` was unambiguous:
`pointer_sprites is NULL`, on every single call, for the entire session, from the main menu straight
through to quitting -- never once became non-null. Traced why: `pointer_sprites` is populated by
exactly one function, `load_pointer_file()` (`vidmode.c`), called from exactly one place,
`setup_screen_mode()` -- itself only reached via `reenter_video_mode()`, whose only two callers
(`main_game.c`, `game_session_loop.cpp:850`) are both gameplay-*entry* paths. A session that stays in
the frontend and never actually starts a level never runs any of that code, so `pointer_sprites`
stays permanently `NULL` -- the ImGui menu cursor could never have worked via this sprite sheet in a
menu-only session, no matter how the lookup itself was written. (`GFS_cursor_horny`, the original
choice before the `MousePG_Arrow` switch, never had this problem: `frontend_sprite`, the sheet it
lives in, is loaded by `frontend_load_data()`, which *does* run for menu screens.)

Fixed at the actual source: `frontend_load_data()` (`frontend.cpp`) now also calls
`load_pointer_file()`, guarded on `pointer_sprites == NULL` so it only loads once -- necessary because
`load_pointer_file()` doesn't free any previous sheet before reassigning it, and unlike
`frontend_sprite`'s own unconditional reload just above it in the same function,
`frontend_load_data()` runs repeatedly throughout a session (every unguarded call would leak the
previous sheet). Once an actual game starts, `setup_screen_mode()`'s existing unload-then-reload
sequence runs exactly as before, unaffected by the sheet already being populated. Removed the
temporary diagnostics now that the cause is confirmed. Verified: all four build configs,
`check_layering.py --strict`, full test suite.

**Cursor now visible, but "tiny & black"** -- the load-order fix worked (it appeared at all, for the
first time), but exposed a second, real bug in `MousePG_Arrow`'s own rendering: `LbSpriteDrawUsingScalingUpDataSolidLR`
(`bflib_vidraw_spr_norm.c`, what the *classic in-game* cursor actually decodes through, confirmed by
reading `PointerDraw()`'s own call chain in `bflib_mspointer.cpp`) resolves sprite colours against
`RendererGetActivePalette()` -- the game's own live/active palette -- not `frontend_palette`. But
`build_cursor_pixels()` was forcing `RendererPaletteSet(frontend_palette)` before every draw, carried
over unchanged from the original `GFS_cursor_horny` code without reconsidering whether that's still
the right palette for a *different* sprite source -- it isn't: `pointer_sprites`' colour indices mean
something else entirely under the frontend's own palette, rendering mostly black. ("Tiny" was a
separate, softer issue -- the `1.2x`→`0.7x` size correction from the previous entry had overshot in
the other direction.)

Per the user's own call ("might be easier to use the horny cursor again, and adjust position of
sprite") -- **reverted the sprite source back to `GFS_cursor_horny`** rather than chase a third fix on
`MousePG_Arrow` (which now has *two* independent problems: the load-order one just fixed, and this
palette one). `GFS_cursor_horny` is a genuine frontend asset (`frontend_sprite`, loaded by
`frontend_load_data()` -- always available in menu contexts) already correctly decoded against
`frontend_palette` -- none of `MousePG_Arrow`'s baggage. Reverted `frontend_load_data()`'s
`load_pointer_file()` addition too, since nothing needs `pointer_sprites` early any more. Hotspot set
back to `(0, 0)` -- what `set_pointer_graphic_menu()` itself already uses for this exact sprite when
drawing the classic menu cursor, so not an arbitrary guess, but this gauntlet shape still has no
single unambiguous "tip" pixel the way a plain arrow does, so it remains a starting point pending live
confirmation of which corner needs to align with clicks. Also nudged the ImGui-native cursor-scale
multiplier from `0.7x` to `1.0x` (`ImGuiContext.cpp`) as a fresh middle ground, since the prior `1.2x`
(too large)/`0.7x` (too small) data points were both taken against `MousePG_Arrow`'s own different
native dimensions and don't necessarily transfer. Verified: all four build configs,
`check_layering.py --strict`, full test suite.

**Live-testing result, and one more bug found**: hotspot corrected precisely -- the user identified
the actual cursor image directly (52x40, a gauntlet whose grip is roughly centred rather than
anchored to a corner) and gave the exact value, `(26, 18)`, replacing the `(0, 0)` guess. Separately:
"Cursor only appears when hovering over menus. It disappears when over background." Traced this to a
real interaction with `05-imgui-owned-menu-backdrop.md`'s Phase A/B (landed earlier, backdrop drawn
via ImGui but the legacy framebuffer blit still running underneath, deliberately, pending its own
Phase C): `draw_menu_backdrop()` draws the same backdrop image over the *entire* screen every frame,
via ImGui's background draw list -- which paints over whatever the legacy cursor draw
(`bflib_mspointer.cpp`) had just put into `lbDrawSurface` for the area outside the small centred menu
panel, since that backdrop image is drawn *after* the legacy blit in frame order. `WantCaptureMouse`
is only true over the panel itself, so ImGui's own cursor draw didn't pick up the slack there either
-- the cursor had nowhere left to render for that whole region. Fixed by finishing
`05-imgui-owned-menu-backdrop.md`'s Phase C/D now (see that document's own status) rather than
patching around it here: once the legacy blit is skipped outright for these screens, a new
`ImGuiContextScreenOwned()`/`RendererScreenOwned()` query (mirroring the existing `WantCaptureMouse`
check) makes the ImGui cursor draw unconditionally for them instead of only over the panel, and makes
the legacy cursor draw skip itself entirely (nothing legacy composites there any more to matter
against). Verified: all four build configs, `check_layering.py --strict`, full test suite.

**Two more live reports, one root cause for both**: "almost everything seems consistent. the cursor
alignment seems to be (0,0) when over the landview preview, while being (26,18) everywhere else" and,
separately, "when clicking quit, it appears to momentarily jump to 640x480 with a correspondingly
large cursor, before closing." Neither is actually about the custom sprite's hotspot at all --
traced by reading the vendored ImGui SDL backend directly rather than guessing further:
`ImGui_ImplSDL3_UpdateMouseCursor()` (`imgui_impl_sdl3.cpp`) calls `SDL_ShowCursor()`/`SDL_SetCursor()`
every single frame whenever `io.MouseDrawCursor` is false and `ImGui::GetMouseCursor()` isn't
`ImGuiMouseCursor_None` -- which is *always*, since nothing in this codebase ever calls
`ImGui::SetMouseCursor()`. And nothing in this codebase calls `SDL_HideCursor()`/`SDL_ShowCursor()`/
`SDL_SetCursor()` directly either (confirmed by grep) -- OS cursor visibility has always been left
entirely to SDL's own relative mouse mode (`Ft_RelativeMouseMode`) hiding it automatically. ImGui's
backend calling `SDL_ShowCursor()` every frame directly fights that, intermittently winning the race
and showing the **real OS cursor** -- its own native shape and hotspot, unrelated to
`FeStyleGetCursorImage()`'s sprite or hotspot at all -- on top of or instead of the custom-drawn one.
Most visible wherever the timing happens to tip in its favour (an embedded widget like the land
preview panel, apparently), and unmissable once the custom cursor stops drawing entirely (the brief
`FeSt_QUIT_GAME`/`FeSt_INITIAL` handoff, neither ImGui-owned, so nothing else is competing with it) --
a plain OS arrow at that point would read as both "(0,0)-aligned" and, depending on the desktop's own
cursor theme/DPI scaling, "large", matching both reports without needing two separate explanations.
Fixed with one flag at ImGui context creation: `io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange`
makes the backend never touch OS cursor visibility/shape at all (`imgui_impl_sdl3.cpp`'s own early-out
for this flag), leaving SDL's relative-mode hiding as the sole authority, uncontested. Verified: all
four build configs, `check_layering.py --strict`, full test suite. Not yet confirmed live.

**Live-testing result: `NoMouseCursorChange` didn't fix it, and it turned out to be a different bug
entirely.** The user confirmed: "cursor on landview / campaign preview pane is still misaligned."
Re-traced the mouse-position plumbing end to end rather than guessing at a second cursor-drawing
mechanism: `land_preview_maintain()`'s hit-testing (`frontmenu_landpreview.c`) reads
`GetMouseX()`/`GetMouseY()` directly; the ImGui cursor's `io.MousePos`
(`ImGuiContext.cpp:ImGuiContextNewFrame()`) is fed from those exact same two functions every frame,
via the `ImGuiMousePositionFn` callback wired in `main.cpp`. Same value, same coordinate space, both
sides -- no divergence to find there, confirmed by the user's own follow-up ("in the landview
screenshot, the ensign is selected at that position": the highlighted ensign and the visible cursor
graphic agree with each other). Asked the user to characterise the offset rather than keep guessing
blind; the answer -- "offset constantly, only in that panel" (not drag-only, not zoom-scaling) --
pointed straight at a *fixed-pixel* mismatch specific to that one panel, independent of pan/zoom
state. Found it: `draw_land_preview_panel()` (`frontgui_screens.cpp`) wraps its
`land_preview_draw()` call with `land_preview_set_frame_extra_scale_den(2)` (halving
`land_preview_frame_inset()`'s return value -- see that function's own comment on why the ornate
frame reads oversized at this panel's size) and resets it to `1` immediately after. But
`FrontendImGuiLandPreviewInput()` -- which calls `land_preview_maintain()` for the *same* panel's
hit-testing and pan/drag, computing its own `frame_inset` for the mouse-in-rect bound and the
ensign-relative-coordinate math -- runs from `frontend_input()`, *earlier* in the same frame, before
`FrontendImGuiFrame()` ever reaches `draw_land_preview_panel()`. It always saw the global at its
resting value (`1`, unhalved), while the content it was hit-testing against had actually been drawn
with the halved value -- a constant `land_preview_frame_inset()`-sized discrepancy between where the
ensigns/map visually sit and where the interactive coordinate frame thought they started, present
only for this ImGui-embedded panel (the legacy full-screen land-preview draw path never touches
`land_preview_set_frame_extra_scale_den()` at all, so `-classicmenu` was never affected). Fixed by
wrapping `land_preview_maintain()`'s call the same way, using a new shared constant
(`kLandPreviewImGuiFrameScaleDen`, `frontgui_screens.cpp`) at both call sites instead of the same
magic number `2` living independently in two places where it could drift apart again. Verified: all
four build configs, `check_layering.py --strict`, full test suite. Not yet confirmed live.

**Same live-testing round: "When changing ui font size, menu items dont stay centered."**
`frontgui_mainmenu_frame()`'s buttons use a hardcoded `const ImVec2 btn_size(260, 0)` that doesn't
scale with `UI_FONT_SCALE`, while the window is `ImGuiWindowFlags_AlwaysAutoResize` and the heading
above them (`FeHeading()`, plain `ImGui::TextUnformatted`, no self-centering) does grow with font
size -- so at larger scales the window grows to fit the wider heading/bottom-row text while the
260px-wide top buttons stay flush left, no longer centred under it. Rather than scale `btn_size`
itself (which would only patch this one screen's one specific cause), added a general
`FeCenterNextItem(item_width)` wrapper (`frontgui_widgets.h`/`.cpp`) that nudges the cursor so the
next item centres against `GetContentRegionAvail()` -- tracks the window's actual current width
every frame regardless of *why* it changed (font scale here, but the same fix covers any future
window-width/content-width mismatch). Applied before the heading (width measured via
`FeStylePushFont(FeFont_Heading)` + `CalcTextSize()`), each of the five fixed-`btn_size` buttons, and
the bottom Options/High Scores/Quit row (whose combined auto-sized width is measured under
`FeFont_Body` before drawing, since `FeCenterNextItem()` only positions the first item in a
`SameLine()`-chained row). Verified: all four build configs, `check_layering.py --strict`, full test
suite. Not yet confirmed live.

**Live-testing result, both fixes confirmed**: "that works." Followed immediately by a new report:
"using the mouse wheel on campaign/scenario/skirmish menus, causes screen to scroll. Need to have
the same fixed height approach as applied to options menu (fixed item count with scroll bar on
listboxes, fixed line count & scroll on detail box)." Root-caused two real height-budget bugs, both
producing genuine content overflow in a fixed-size window that (like `frontgui_feoptions_frame()`)
carries no scroll flags of its own -- once content exceeds the window's height, ImGui doesn't clip
it, it grows a scrollbar for the *whole window*, so any wheel-scroll over the screen scrolled the
entire menu instead of whichever list/panel the pointer was actually over:

- `frontgui_campaignselect_frame()`'s right column budgeted `content_h` between
  `draw_land_preview_panel()` (75%) and `draw_select_detail_panel()` (20%) but never reserved any
  room at all for `draw_landview_slider()`'s own row, drawn between them. Fixed by reserving a fixed
  slot for it up front (`ImGui::GetFrameHeightWithSpacing()`, measured under `FeFont_Body` to match
  `FeSlider()`'s own font so it holds at any `UI_FONT_SCALE`), unconditionally -- whether or not the
  slider actually renders this frame (fewer than 2 unlocked levels hides it) -- so the layout doesn't
  jump depending on progress, only the remainder is then split 79/21 between the two panels.
- `frontgui_freeplayselect_frame()`'s left column (shared with Skirmish) subtracted a flat `40.0f`
  from `content_h` "for the second caption line" but never accounted for the *first* one at all --
  silently overflowing by about one `FeCaption` line every time. Fixed by measuring one caption
  line's real height (`ImGui::GetTextLineHeightWithSpacing()` under `FeFont_Caption`) and reserving
  two of them.

Separately, per the report's explicit "fixed line count & scroll on detail box": `draw_select_detail_panel()`
used `FeBeginPanel()`, which hardcodes `ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse`
(deliberate for its other, genuinely static callers) -- a level description longer than the panel's
fixed height just clipped invisibly, with no way to see the rest and no way for the panel to consume
the wheel itself. Added an opt-in `scrollable` parameter to `FeBeginPanel()` (defaults to `false`,
every existing call site unaffected) and passed `true` here -- the text equivalent of a listbox's
fixed item count with its own scrollbar, matching the options menu's own fixed-height-plus-scroll-area
pattern (§6.3) instead of introducing a second, differently-shaped mechanism. Verified: all four build
configs, `check_layering.py --strict`, full test suite. Not yet confirmed live.

**Live-testing result: same screen still scrolled -- an off-by-one-`ItemSpacing` in both budgets.**
The user confirmed the screen they meant was Campaign Select itself ("titled Land selection, accessed
by clicking Campaign on Main Menu") -- the exact screen the previous fix targeted, so the previous
fix's height-budget arithmetic was still leaving genuine (if small) overflow. Root cause in both
spots: `ImGui::GetFrameHeightWithSpacing()`/`GetTextLineHeightWithSpacing()` each bundle exactly one
`ItemSpacing.y` -- the gap *after* that one item -- but a stack of 3+ items has one gap *between*
every consecutive pair, and a single "WithSpacing" call per item silently double-counts nothing while
still leaving one gap uncounted whenever items outnumber calls:

- Campaign Select's right column is 3 stacked items (land preview panel, slider, detail panel) --
  2 gaps between them -- but reserving `GetFrameHeightWithSpacing()` for the slider only ever
  accounted for its own trailing gap, missing the leading one between the preview panel and the
  slider. Fixed by reserving the slider's bare frame height (`GetFrameHeight()`) plus both gaps
  explicitly (`2.0f * ImGui::GetStyle().ItemSpacing.y`).
- Free Play/Skirmish's left column is 4 stacked items (caption, list, caption, list) -- 3 gaps --
  but calling `GetTextLineHeightWithSpacing()` once per caption only ever covers 2 of the 3 (each
  call bundles only the gap immediately after *that* caption, never the gap after the list box
  between them). Fixed the same way: bare `GetTextLineHeight()` per caption plus all three gaps
  explicit (`3.0f * ImGui::GetStyle().ItemSpacing.y`).

General lesson recorded here since it'll recur: a "WithSpacing" convenience always means "this one
item's own trailing gap," never "every gap in whatever stack you're about to build" -- safer to
reason about a fixed stack's total height as bare item heights plus `(item_count - 1) *
ItemSpacing.y` explicitly than to multiply a per-item "WithSpacing" call by the number of items and
assume it comes out even. Verified: all four build configs, `check_layering.py --strict`, full test
suite. Not yet confirmed live.

**Live-testing result: still occurring -- switched from precise budgeting to a structural
guarantee.** User: "still occurring, likely that the campaign listbox, also needs a scrollbar
(possibly move the landview slider above it,)". Two rounds of exact-arithmetic fixes for this same
screen still left it scrollable, which is the real signal: chasing the budget to be pixel-perfect
against `ImGui`'s own font/spacing metrics is fragile (any metric not accounted for -- and there's no
way to enumerate all of them with confidence -- reopens the same bug), and reordering the layout
(the user's own suggested workaround) only relocates where the next unaccounted gap could hide, it
doesn't remove the underlying fragility. Switched to the same defensive pattern `FeBeginPanel()`
already uses for its own non-scrolling variant: `ImGuiWindowFlags_NoScrollbar |
ImGuiWindowFlags_NoScrollWithMouse` added to both outer windows
(`frontgui_campaignselect_frame()`/`frontgui_freeplayselect_frame()`'s own `ImGui::Begin()` calls).
This makes the *window itself* structurally incapable of scrolling regardless of any residual
budget error -- wheel input can now only ever reach an actual scrollable child (the campaign list
box, the mappack/level list boxes, the now-scrollable detail panel), each of which has its own
independent child-window scroll state unaffected by the parent's flags. The height-budget math from
the previous two fixes is left in place (a correctly-budgeted screen has nothing clipped, which is
strictly better than relying on the backstop to hide overflow), but is no longer what actually
prevents the reported symptom. Worth revisiting for `frontgui_feoptions_frame()` too if it's ever
reported broken -- not touched here since it hasn't been. Verified: all four build configs,
`check_layering.py --strict`, full test suite. Not yet confirmed live.

**Live-testing result: the scroll fix's own side effect -- the bottom buttons lost their margin.**
"there needs to be a slight gap between the return/enter buttons and the bottom of the pane. thats
been lost in this pass." Before the `NoScrollWithMouse` fix, `content_h`'s `-60.0f` bottom-of-window
reservation being genuinely tight against the separator+button row's real height was invisible --
the window could still scroll those last couple of pixels into view. With scrolling now deliberately
disabled, that same tightness reads as the buttons sitting flush against the window's own padding
instead. Rather than tighten the budget arithmetic further (the same fragile exercise the last two
entries already moved away from), added an explicit trailing `ImGui::Dummy(ImVec2(0,
GetStyle().WindowPadding.y))` after the button row in both `frontgui_campaignselect_frame()` and
`frontgui_freeplayselect_frame()`, right before `ImGui::End()`. Blank space at the very end of a
window's content can't affect anything drawn above it even if the window's fixed height is
razor-tight, so this is safe regardless of how precise the rest of the budget turns out to be.
Verified: all four build configs, `check_layering.py --strict`, full test suite. Not yet confirmed
live.

Phase G, step 4: nine more schema rows, plus a schema fix the previous slice's simplicity had
hidden. `CENSORSHIP`/`FLEE_BUTTON_DEFAULT`/`IMPRISON_BUTTON_DEFAULT` (Game), `LINE_BOX_SIZE`/
`NEUTRAL_FLASH_RATE` (Graphics), `ATMOSPHERIC_SOUNDS`/`MUSIC_FROM_DISK`/(Sound), and
`CURSOR_EDGE_CAMERA_PANNING`/`TAG_MODE_TOGGLING` (Input) all needed only kfx_config-owned storage,
same as step 2's slice.

**`MUSIC_FROM_DISK` (the row behind the launcher's "Use CD Music") surfaced §6.2 finding 3 for
real**: its own `TRUE`/`FALSE` meaning is the *opposite* of what "Use CD Music" should display and
write ("Use CD Music" on == `Ft_NoCdMusic` off == `MUSIC_FROM_DISK=FALSE`). The schema's get/set
accessors already presented the right user-facing sense, but
`setting_option_apply_bool()`'s persistence write didn't know to flip it back before writing under
`cfg_key` -- it would have silently written the opposite of what the user just chose. Fixed by
adding `cfg_bool_inverted` to `struct SettingOption` (defaults to false via designated-initializer
zero-fill, so every other row is untouched); `apply_bool()` now writes `cfg_bool_inverted ? !val :
val`. `CURSOR_EDGE_CAMERA_PANNING` looked like it might need the same treatment (its *storage*,
`Ft_DisableCursorCameraPanning`, is inverted relative to the option) but doesn't: config key 24's
own parser already treats `TRUE` as "panning enabled" against that inverted flag, so the *cfg_key's*
sense already matches the schema row's displayed sense -- only `MUSIC_FROM_DISK` has the key itself
meaning the opposite thing.

**The table's positional struct initializers stopped working the moment a row needed to omit
`cfg_bool_inverted`** (a 14th field, most rows now specifying only 6-8 of them): `-Werror=missing-
field-initializers` fires on positional aggregate init unless every field is given or none are, but
not on designated initializers. Converted every row in `setting_options[]` to `.field = value` form
-- fixes the immediate build break, and makes the table self-documenting field-by-field rather than
relying on comment-only field-position callouts as more optional fields accumulate.

13 new test cases (`config_settingschema_test.cpp`) cover the new rows' round-trips and specifically
pin down the inversion behaviour: `MUSIC_FROM_DISK`'s row is asserted `cfg_bool_inverted`, and its
get/set are checked against `Ft_NoCdMusic` in both directions.

All four build configs, `check_layering.py --strict`, and the full 1507-test suite (1494 + 13 schema
tests, replacing the prior slice's 9) pass. Not yet verified interactively.

Phase G, step 5: `SOptT_Enum` -- a small closed set of named values, backed by the same
`struct NamedCommand` tables `config_keeperfx.c`'s own parser already matches `keeperfx.cfg` tokens
against (`atmos_volume[]`, `atmos_freq[]`, `tag_modes[]`, now `extern`-exposed from
`config_keeperfx.h` for the schema to reuse verbatim rather than duplicating). `get_enum`/`set_enum`
trade in a table entry's `.num` (e.g. `atmos_volume[]`'s 64/128/255), not a UI index -- a combo box
wants a 0-based index into a name list, so `setting_option_enum_count()`/`_current_index()`/
`_item_name()`/`apply_enum_index()` (`config_settingschema.h`) do that translation both ways.
`setting_option_apply_enum_index()` persists `enum_table[index].name` itself (the literal
`keeperfx.cfg` token, e.g. `"MEDIUM"`) under `cfg_key`, not the numeric `.num` -- matching what a
human hand-editing the file would actually type. Three new rows: `ATMOS_VOLUME`/`ATMOS_FREQUENCY`
(Sound), `DEFAULT_TAG_MODE` (Input) -- bringing the table to 22 rows. Wired into
`draw_setting_options_for_category()` (`frontgui_screens.cpp`) via `FeCombo`, alongside the existing
checkbox/slider branches.

8 new test cases cover the three new rows' round-trips, the index/`.num` translation helpers
directly (`ATMOS_VOLUME`'s three entries by name and by index), and that the enum helpers return
0/empty rather than misreading a non-enum option's unrelated fields.

All four build configs, `check_layering.py --strict`, and the full 1511-test suite pass. Not yet
verified interactively.

Phase G, step 6: four more options, closing out everything `SOptT_Enum` can represent without a new
schema type. `LANGUAGE` (Game, needs-restart) and reuses `lang_type[]` verbatim -- 24 real entries,
which is also what exposed a latent limit in the renderer: `draw_setting_options_for_category()`'s
enum branch (`frontgui_screens.cpp`) built its combo items into a fixed `const char *items[8]`, sized
against the 3-4-entry Sound/Input tables from the prior two steps. Switched to a `std::vector`, sized
to the option's own `setting_option_enum_count()`, so it scales to any table size without another
silent truncation. `DELTA_TIME` (Game, live) is a plain `Ft_DeltaTime` bool, same shape as every
other feature-flag row already in the table.

`ZOOM_TO_MOUSE`/`ROTATE_AROUND_MOUSE` (Input, live) needed the "a bit more care" flagged last step:
their config-key parsing (case 42/43, `config_keeperfx.c`) checks `logicval_type` for `ALWAYS`/`NEVER`
*before* falling back to a dedicated table with only the `WHEEL`/rotation-key tokens -- no single
existing `NamedCommand` table covers the whole value space the way `atmos_volume[]` does for its own
option. Two purpose-built local tables (`zoom_to_mouse_enum[]`/`rotate_around_mouse_enum[]`,
`config_settingschema.c` only) fill the gap: their names are literal tokens the parser's combined
logic already accepts, and their `.num` values were set to match `keeperfx_ui_config.zoom_to_mouse_option`/
`rotate_around_mouse_option`'s own storage convention exactly (confirmed by tracing the parser, not
assumed) -- e.g. writing `"ALWAYS"` under `ZOOM_TO_MOUSE` round-trips to `zoom_to_mouse_option == 3`
(`ZoomToMouse_Always`) exactly like a hand-written `keeperfx.cfg` line would.

6 new test cases: `LANGUAGE`'s 24-entry count (also guarding against the renderer's fixed-array limit
regressing), `DELTA_TIME`'s round-trip, and both new tables' `.num` values pinned against the real
storage convention in both directions (`ALWAYS`/`NEVER` for each). All four build configs,
`check_layering.py --strict`, and the full 1515-test suite pass. Not yet verified interactively.

Phase G, step 7: `SCREENSHOT` and `HAND_SIZE`, the two options step 6 had deferred specifically for
needing a new getter callback. Both `screenshot_format` (kfx_render-owned, `scrcapt.h`) and
`global_hand_scale` (kfx_sim-owned, `power_hand.h`) had only ever had a *setter* registered in
`ConfigReloadCallbacks` -- every field in that table before this step was load-time-only
(`keeperfx.cfg` -> engine), so nothing had ever needed to read a value back out until the settings
screen did. Added `get_screenshot_format`/`get_hand_scale` right alongside their existing setters
(same "insert at the exact matching position in both `config.c`'s `default_config_reload_callbacks`
and `main.cpp`'s `config_reload_callbacks_impl`" discipline `set_vid_smooth` established, since both
tables are still positional-initializer struct literals).

`SCREENSHOT` reuses `scrshot_type[]` verbatim, the same `SOptT_Enum` shape as `ATMOS_VOLUME` --
straightforward once the getter existed. `HAND_SIZE` turned out **not** to need the `SOptT_Float`
type flagged as missing last step: its own config-key parsing (case 30, `config_keeperfx.c`) already
reads/writes an integer *percentage* (`atoi()`, then `set_hand_scale(i/100.0)`) even though the live
engine value is a float scale factor -- so it's a plain `SOptT_Int` row keyed on the same percentage
the file already uses (`get_hand_size_pct()`/`set_hand_size_pct()` do the `*100`/`/100` conversion),
not a new schema type. `SOptT_Float` stays unimplemented; nothing in the remaining option list
actually needs it once `HAND_SIZE` turned out to be int-shaped after all.

Testing these needed a different approach than every prior row: neither `screenshot_format` nor
`global_hand_scale` is reachable from a `kfx_config`-only test binary (both owned by libraries above
it). Followed `config_sounds_test.cpp`'s own established `ResetConfigReloadCallbacks` pattern --
copy the current (default noop) callbacks table, override just `get_screenshot_format`/
`set_screenshot_format` or `get_hand_scale`/`set_hand_scale` with a lambda closing over a `static`
variable, install it, verify the schema calls through correctly, restore the original table after.
Also extended `config_reload_callbacks_test.cpp`'s existing single sweep-every-noop test case with
the two new getters, in their correct struct-field position.

Bringing the table to 25 rows: every option in §6.2's list is now schema-backed except the ones
needing real new machinery. All four build configs, `check_layering.py --strict`, and the full
1517-test suite pass. Not yet verified interactively.

**Bug found interactively, real crash reports from the user**: opening the Options screen (any
route into `FeSt_FEOPTIONS`, not specific to the new Sound/Input tabs) could abort the whole process.
Root cause, confirmed with a live debug-build backtrace: `RendererSoftware::PresentFrame()`
(`RendererSoftware.cpp`) unconditionally starts a new ImGui frame
(`ImGuiContextNewFrame()`/`RendererRunImGuiFrameCallback()`/`ImGuiContextRender()`) on *every* call,
with no reentrancy guard. `FrontendImGuiFrame()`'s own deferred-pending-state application (§3's
`s_pending_state` mechanism -- safe with respect to ImGui's *window stack*, the problem it was built
to solve) calls `frontend_set_state()` at the top of the frame, before any window is open; for states
whose transition includes a palette fade, that reaches `fade_out()`/`fade_in()` ->
`ProperFadePalette()` -> `LbPaletteFadeStep()`, whose own multi-step animation loop calls
`RendererPresentFrame()` again per step to actually show the backdrop dimming/brightening. Each of
those nested calls re-entered the same `if (RendererImGuiEnabled() ...)` block and tried to start a
*second* ImGui frame before the outer one had reached `Render()` -- tripping ImGui 1.92's own
`ErrorCheckNewFrameSanityChecks()` ("Forgot to call Render() or EndFrame()...") as a hard
`SIGABRT`, not the softer window-stack corruption the existing safeguard was designed around.

The initial report (an optimized release build) showed a *different* crash signature entirely (a
SIGSEGV inside the new Sound tab's enum rendering) because the built-in crash handler's own
backtrace doesn't unwind past itself (useless for either diagnosis) and heavy inlining hid every
local variable. A second capture against an unoptimized debug build caught the real fault instead:
this reentrancy assertion, firing during the *state transition into* Options, before any tab
content is even drawn -- meaning it's pre-existing (predates this phase entirely, tied to whichever
states have a fade in their transition) and timing/build-dependent in exactly when it manifests,
not something introduced by the settings-schema work. The originally-reported SIGSEGV remains
unconfirmed and may not exist at all; re-test once this fix is in.

Fixed with a `static bool` reentrancy guard in `PresentFrame()`: a nested call (already inside the
ImGui block) skips `ImGuiContextNewFrame()`/the frame callback/`ImGuiContextRender()` entirely --
it still needs (and keeps) the plain SDL backdrop blit/present above the guard, which is exactly
what shows each fade step -- and the outer call's own `NewFrame()`/`Render()` pair still correctly
wraps the whole state-transition-then-draw sequence once the fade loop returns. No unit test added
(consistent with this codebase's existing renderer code: needs a real SDL window/renderer context,
not practical to isolate). All four build configs, `check_layering.py --strict`, and the full
1511-test suite pass. **Confirmed live**: Options now opens cleanly and both the Sound and Input
tabs work -- the originally-reported SIGSEGV never was a separate bug; it was a downstream symptom
of this same reentrancy (the debug capture that caught the abort simply never got far enough to
reach the enum rendering code the release-build report had pointed at).

Phase G, step 8: `RESIZE_MOVIES` and `POINTER_SENSITIVITY`, both int/enum-shaped after closer
investigation rather than needing the `SOptT_Composite` type step 7's notes had assumed they would.

`RESIZE_MOVIES`'s own config-key parsing (case 14, `config_keeperfx.c`) is really two storage
locations folded into one option: `Ft_Resizemovies` (on/off) plus `vid_scale_flags` (which scaling
mode, only meaningful while the feature is on). `vidscale_type[]` itself -- the table the parser
matches against -- isn't a clean fit for a combo box as-is: it's an *alias* table ("ON"/"ENABLED"/
"TRUE"/"YES"/"1" all mean the same thing as "FIT"), so reusing it verbatim would show duplicate-
valued entries in the dropdown. A purpose-built local table (`resize_movies_enum[]`, same "not every
table is fit to reuse" reasoning `ZOOM_TO_MOUSE`/`ROTATE_AROUND_MOUSE` needed) keeps just the seven
canonical names, and `get_resize_movies()`/`set_resize_movies()` combine the two underlying storage
locations into the single value the row presents.

`POINTER_SENSITIVITY` needed a new getter callback the same way `SCREENSHOT`/`HAND_SIZE` did --
`base_mouse_sensitivity` (kfx_render-owned, `vidmode.h`) had an existing free *setter* function
(taken by address directly in `main.cpp`'s callback table, no wrapper needed) but nothing to read it
back with. Modeled as a plain `SOptT_Int` row, the same percentage-in-file-vs-scaled-internal-value
shape `HAND_SIZE` already established (the file stores 0-10000, scaled by `256/100` before reaching
`base_mouse_sensitivity`). **Deliberately not built as the checkbox-gated composite §6.2 finding 3
describes** for the launcher's own UI: that finding says the launcher's raw-input checkbox writes
`POINTER_SENSITIVITY=0`, but tracing what `base_mouse_sensitivity == 0` actually does at the engine
level found only `LbMouseChangeMoveRatio(0, 0)` -- no documented "raw input mode" this pass could
confirm. Shipping a checkbox labeled with a behaviour that couldn't be verified was judged worse
than not having the checkbox; the plain slider still reaches 0 by dragging it down. `SOptT_Composite`
remains unimplemented as a result -- `STARTUP` is the only option left that would need it, and its
shape (a multi-token list, not a checkbox-gated value) wouldn't reuse this one anyway.

2 new schema rows (26 total) plus a new `get_base_mouse_sensitivity` callback, added the same way as
the previous step's two getters. 4 new test cases: `RESIZE_MOVIES`'s combined on/off-plus-mode
behaviour in both directions, and `POINTER_SENSITIVITY`'s percentage round-trip via the same
callback-double pattern `SCREENSHOT`/`HAND_SIZE` established. All four build configs,
`check_layering.py --strict`, and the full 1519-test suite pass. Not yet verified interactively.

Phase G, step 9: `STARTUP`, closed out **without** the `SOptT_Composite` type every prior step's
"still ahead" note had assumed it would need. Its own config-key parsing (case 22,
`config_keeperfx.c`) accepts a space-separated token list -- `LEGAL`/`FX`/`BULLFROG`(hidden)/
`EA`(hidden)/`INTRO`, each just a bit in `start_params.startup_flags` -- and §6.2's own Game-tab
description already named it "the splash-screens + intro list" (the legacy
`DISABLE_SPLASH_SCREENS`+`-nointro` pair), which turned out to be the right frame: two ordinary
`SOptT_Bool` rows, "splash screens" toggling `LEGAL`+`FX` together (they've always moved as a pair --
`set_default_startup_parameters()`'s own default is `SFlg_Legal|SFlg_FX|SFlg_Intro`) and "intro"
toggling `INTRO` alone, both sharing the single `STARTUP` cfg key.

**The one genuine gap this exposed**: `setting_option_apply_bool()`'s generic write path assumes an
option's *own* boolean fully determines what gets written under its `cfg_key` -- true for every row
so far, false here, since `STARTUP`'s value is five bits' worth of state and each row only owns two
of them (or one). Writing just `"TRUE"`/`"FALSE"` for whichever checkbox changed would have silently
dropped every other token -- `BULLFROG`/`EA`'s hidden bits included, if a config file already had
them. Added `format_cfg_value`, an optional per-row override consulted before the generic bool/int/
enum formatting: when set, it returns the *whole* string to write, computed from live state rather
than the single value being applied. Both `STARTUP` rows point at the same
`format_startup_cfg_value()`, which reconstructs the complete token list from
`start_params.startup_flags` on every apply -- so toggling either checkbox correctly leaves the
other's bits, and `BULLFROG`/`EA`'s, exactly as they were.

2 new test cases (28 rows now, `find_option()`'s single-result-per-`cfg_key` shape needed a
`find_option_by_label()` sibling since `STARTUP`'s two rows share a key): the splash/intro rows
toggle independently, and `format_cfg_value` preserves hidden bits across an apply. All four build
configs, `check_layering.py --strict`, and the full 1521-test suite pass. Not yet verified
interactively.

Phase G, step 10: `FRAMES_PER_SECOND`, the last option in §6.2's list -- **every option is now
schema-backed**, closing this out the same way `STARTUP` did, without the string schema type its own
"still ahead" note had assumed it would need.

Its own config-key parsing (case 39, `config_keeperfx.c`, via `parse_draw_fps_config_val()`) stores a
mode plus a number: `"AUTO"` (optionally with a second, fallback number) sets
`start_params.num_fps_draw_main` to `-1` -- `bflib_video.h`'s own comment already says "-1 if auto",
confirmed against `redetect_screen_refresh_rate_for_draw()`'s actual use of it: `-1` means "use the
display's own detected refresh rate, falling back to the second number if that can't be detected" --
while a plain number sets `num_fps_draw_main` directly (`0`, its real default, means uncapped;
positive means a fixed FPS ceiling). Same shared-`cfg_key` + `format_cfg_value` shape `STARTUP`
established: a bool row ("Auto Frame Rate") toggles the `-1` sentinel, an int row ("Frame Rate
Limit") edits the fixed-cap number, greyed out via `is_enabled` while Auto is on (same gating
`ALT_INPUT`'s pair of rows already uses). The fallback secondary number isn't exposed as its own
control -- a rarely-needed value only meaningful when Auto's own display-refresh-rate detection
fails -- but `format_fps_cfg_value()` preserves it verbatim if a config file already set it, same
"don't drop what wasn't touched" reasoning `STARTUP`'s hidden `BULLFROG`/`EA` bits needed.
`setting_option_apply_int()` needed the same `format_cfg_value` consultation `apply_bool()` already
had, since this is the first `SOptT_Int` row to need it.

2 new test cases (30 rows now): the Auto/Limit gating in both directions, and `format_cfg_value`
writing `AUTO`/a plain number while preserving the untouched secondary fallback. All four build
configs, `check_layering.py --strict`, and the full 1523-test suite pass. Not yet verified
interactively.

(Correction to prior steps' own counts: the table has 34 rows, not the "~30" figure a few of the
earlier per-step notes used -- `STARTUP` and `FRAMES_PER_SECOND` each contributing two rows sharing
a `cfg_key` was undercounted along the way. 34 is the real, final total for every option in §6.2's
list.)

Phase G, step 11: help text for all 34 rows, plus the generic renderer mechanism to show it.
`label_stridx`'s sibling field, `help_stridx`, existed in the schema from the very first slice but
was 0 (unset) on every row until now -- this step writes one accurate, one-sentence `GUIStr_Help*`
string per row (34 new strings, `lang/gtext_eng.pot` guitext 1159-1192, English-first per §6.3's own
decision) and wires them up.

**The rendering mechanism is a new, reusable wrapper, not a one-off**: `FeHelpTooltip(text)`
(`frontgui_widgets.h`/`.cpp`) shows `text` as a hover tooltip for whichever widget was drawn
immediately before it, with ImGui's own normal hover delay, and is a safe no-op for an empty/null
string or an unhovered widget -- callable after *any* widget, not just settings rows.
`draw_setting_options_for_category()` calls it once per row, right after whichever of
`FeCheckbox`/`FeCombo`/`FeSlider` that row drew, guarded by `opt->help_stridx != 0` (`0` isn't "no
help text" in `get_string()`'s own id space -- a row has to explicitly have a nonzero id, which
every row now does).

The schema-shape test (`setting_options covers all four categories...`) now also asserts every row's
`help_stridx != 0`, so a future row that forgets help text fails the test rather than silently
shipping without it.

All four build configs, `check_layering.py --strict`, and the full 1523-test suite pass (no new
test *cases* -- the existing shape test's stronger assertion covers this). Not yet verified
interactively.

**`INGAME_RES` was investigated but not built this step** -- it's real UI work, not a schema row,
and turned out larger than initially scoped. §6.2's plan calls for "a list of detected display
modes," but the engine has no such list today: `lbScreenModeInfo[]` (`bflib_video.c`) only holds
modes explicitly *registered* from config-string parsing (`DESKTOP`, `WxHxBPP`, ...), not a genuine
enumeration of the display's own supported resolutions, and isn't exposed via a header for other
layers to read anyway. Building a real picker would mean adding an actual hardware-mode-enumeration
call (SDL3's `SDL_GetFullscreenDisplayModes` -- not used anywhere in this codebase yet; the only
existing display query, `SDL_GetDesktopDisplayMode` via `WindowSystemSDL::GetDisplayRefreshRate()`,
fetches a single current mode, not a list) behind a new `IWindowSystem` virtual method, a new
`PlatformManager_*` wrapper, and a new getter callback up through to the schema -- a real, standalone
feature addition to `kfx_platform`, not a variation on anything this phase has built so far. Left as
a deliberately separate follow-up rather than folded into this step.

Phase G, step 12: `INGAME_RES`'s resolution picker -- the `kfx_platform` feature step 11 scoped out.
Two new `IWindowSystem` virtual methods, `GetFullscreenDisplayModeCount(display)`/
`GetFullscreenDisplayModeAt(display, index, &w, &h)`, wrap SDL3's `SDL_GetFullscreenDisplayModes`
(`WindowSystemSDL.cpp`), deduplicated to distinct `(width, height)` pairs -- callers want a
resolution picker, not one entry per refresh rate -- with matching `PlatformManager_*` C wrappers
for `kfx_config` (a plain include: `kfx_platform` is the lowest layer, no callback struct needed for
this direction).

**The schema needed one new mechanism, not a new option type.** `INGAME_RES` is still `SOptT_Enum`
-- every existing enum plumbing (`FeCombo`, `setting_option_enum_*()`, `apply_enum_index()` writing
`enum_table[index].name` straight to `keeperfx.cfg`) reused verbatim, because
`LbRegisterVideoModeString()` (`bflib_video.c`) already accepts exactly the `"WxHx32"` string this
row's table entries use as their name. What's new is `ensure_enum_table`, an optional per-row hook
(`config_settingschema.h`) that every `setting_option_enum_*()` helper calls first: every other enum
row's table is a compile-time `NamedCommand[]` constant, but the real list of resolutions a monitor
supports isn't known until the platform layer is asked, so `INGAME_RES`'s table
(`ingame_res_enum[]`) starts zero-filled and is populated lazily, once, on first actual use.
`.num` encodes width/height as `(width << 16) | height` -- deliberately not
`LbRegisterVideoModeString()`'s own `TbScreenMode` return value, which is an unstable registration-
order index, not a value worth comparing "what's active now" against. `get_ingame_res()` reads the
*active* mode via `LbScreenGetModeInfo(LbScreenActiveMode())` (both plain `kfx_platform` includes,
no callback needed); `set_ingame_res()` re-encodes the chosen width/height as a `"WxHx32"` string,
calls `LbRegisterVideoModeString()` (which registers a genuinely new mode on the fly if that exact
string hasn't been seen before -- not limited to a fixed pre-registered list), and applies it via
the already-existing `set_screen_vidmode` callback. `apply_class` is `SApply_NeedsRestart`, matching
`DISPLAY_NUMBER`: `set_screen_vidmode()` (`kfx_render/vidmode.c`) just stores the mode for
`setup_screen_mode()` to pick up next launch, not a live switch.

Testing this needed one shape-test change: the existing "every enum row's table has at least one
entry" assertion doesn't hold for a table that's legitimately empty in an environment with no
detected fullscreen resolutions (this test binary's headless one, potentially, or a real user's odd
setup) -- rows with `ensure_enum_table` set are checked for internal consistency (populating is
safe, the count/current-index helpers stay in range) instead. Four new `INGAME_RES`-specific test
cases cover the row's shape, `get_enum`/`set_enum`'s round-trip through the real
`LbRegisterVideoModeString`/`LbScreenGetModeInfo` machinery (working around `Lb_SCREEN_MODE_INVALID`
being `0` and thus ambiguous with a real mode landing at registry index 0 -- true of
`config_keeperfx.c`'s own `INGAME_RES` parsing too, just never visible there because
`LbRegisterStandardVideoModes()` always fills index 0 first in the real game), and
`apply_enum_index` writing the exact `"WxHx32"` token. No renderer changes were needed --
`draw_setting_options_for_category()`'s existing `SOptT_Enum` branch already handles a
dynamically-sized combo (it already does for `LANGUAGE`'s 24-entry table), and `ImGui::Combo` with
zero items is a safe no-op (verified by reading `imgui_widgets.cpp`'s own bounds check, not just
assumed).

Two new strings (`GUIStr_SetIngameRes`/`GUIStr_HelpIngameRes`, guitext 1193-1194) are appended after
`GuiStrEnd`'s existing tail rather than grouped with the other `GUIStr_Set*`/`GUIStr_Help*` blocks,
to avoid shifting every already-numbered entry after an insertion point.

All four build configs, `check_layering.py --strict`, and the full 1527-test suite (4 new cases)
pass. **Not yet verified interactively** -- this needs a real run to confirm the combo box populates
with sensible resolutions and a chosen one survives a restart, per this project's standing rule
against launching GUI builds against a live desktop without asking first; ask the user to try it and
report back.

**Still ahead of Phase G**: only interactive verification of `INGAME_RES`'s picker, per the above.
The legacy sprite-drawn Options screen (`-classicmenu`) remains untouched and still shows
only the pre-existing binary-settings controls -- these `keeperfx.cfg` options were never exposed
there and this phase doesn't change that, consistent with the established precedent of not
backporting ImGui-era
functionality into the declining legacy path.

> **Revision note (2026-09-04) — the recommendation in this document has been reversed.**
> The original pass recommended using Dear ImGui *only* as a batched-quad rendering backend behind
> `IUIRenderer`/`ITextRenderer`, with `kfx_frontend`'s hand-authored sprite chrome and
> `GuiButtonInit` arrays untouched, and explicitly listed "replace hand-rolled menu code with real
> ImGui widgets" under **Not recommended**. That disposition was written before stage 2 landed and
> before the GUI-side limits below were understood as structural rather than incidental. Four
> things changed; §1 records them. The new recommendation is the opposite one: **real ImGui
> widgets for the main-menu frontend, in-game GUI untouched.** The renderer-backend idea is not
> resurrected — it solved a problem (batching for the sprite path) that this plan removes instead.

---

## 1. Why the recommendation changed

### 1.1 The palette objection is gone

The original "not recommended" argument rested substantially on preserving paletted sprite chrome
exactly. Stage 2 shipped: `TbPixel` is a true-colour pixel, `lbDrawSurface` is RGBA32, and
`RendererSoftware::PresentFrame()` uploads it directly with no INDEX8 conversion
(`RendererSoftware.cpp:121-139`). Alpha compositing, gradients, rounded corners and drop shadows —
every item [gui/02-menu-v2-mockup-gap-analysis.md](../gui/02-menu-v2-mockup-gap-analysis.md) §1
called "expensive, general-purpose" — are now ordinary operations rather than palette-remap tricks.
The mitigation described in [gui/colordepth/00-notes.md](../gui/colordepth/00-notes.md) (nearest-
colour remapping land art into `front.pal`) is retired by the same change; land previews can be
composited at full fidelity.

### 1.2 "Preserve the existing chrome exactly" was never actually available

`frontend_draw_scroll_box` / `frontend_draw_scroll_box_tab` (`gui_frontbtns.c`) derive **both**
their border scale and their per-row height from `gbtn->width` alone, because a row is a fixed
sequence of six *distinct* sprites (corner, four different decorative segments, corner) uniformly
scaled to a target width. There is no repeatable tile, so width and height cannot vary
independently, and no narrower-than-~450px working instance exists anywhere in the codebase to
copy. The Land-selection screen had to abandon the control entirely and use a flat black panel
(`frontend_draw_land_selection_panel_bg`, `frontmenu_select.c`) — recorded in
[gui/colordepth/00-notes.md](../gui/colordepth/00-notes.md) and
[gui/04-phase2-landview-panel-investigation.md](../gui/04-phase2-landview-panel-investigation.md).

**The limitation is in the code, not in the art.** Checking the source PNGs those sprites are built
from (§5.1) settles this: `hugearea_thn_*` is a complete, properly-cut 9-slice set — four corners
plus `cor_ml`/`cor_mr` side pieces at 9x22, `tx1_tc`/`tx1_bc` top and bottom bands at 108x13, and
`tx1_mc` fill at 108x22, with `tx2`/`tx3`/`tx4` as decorative *variants* of the middle band rather
than four mandatory segments in sequence. The art has always supported independent width and
height; `frontend_draw_scroll_box` simply doesn't, because it scales a fixed row by `gbtn->width`.

That reframes the disposition. It is not "commission new art either way" — it is that the existing
art is already the right shape and only the drawing code stands between it and arbitrary panel
geometry. Rewriting that drawing code inside the current sprite/`GuiButtonInit` system would mean
building a 9-slice compositor, a layout pass, hit-testing and focus handling by hand; doing it
against a draw list means nine `AddImage` calls and getting layout, hit-testing, scrolling and focus
from a library.

### 1.3 The bitmap fonts do not scale

`frontend_font[0..3]` are `TbSpriteSheet*` bitmap fonts loaded from `ldata/frontft{1..4}.dat`
(`vidmode.c:150-158`), authored for 640×400/640×480. Frontend layout scales by
`units_per_pixel_menu` (`vidmode.c:781-783`, `height/30` against a 640×480 reference), so at 1440p
or 4K the glyphs are magnified bitmaps — no hinting, no true intermediate sizes, no italics, and
nothing between the four authored faces. The gap analysis already flagged typography as the
**blocking** decision for the whole menu-v2 effort ("Open questions for the user" — bitmap sheets
vs. a real font renderer). This plan answers it: real font rasterization, frontend-only, in-game
HUD text untouched.

### 1.4 There is a clean, pre-existing seam for "main menu only"

The scope the original doc treated as all-or-nothing turns out to be already separated in code:

- The frontend runs its **own loop**, `wait_at_frontend()` (`game_session_loop.cpp:648`), distinct
  from `keeper_gameplay_loop()`. Input, update, draw and present for the menus are one contained
  block (`game_session_loop.cpp:780-838`).
- `frontend_draw()` (`frontend.cpp:3423`) dispatches per `FrontendMenuState`, and one arm of its
  switch — `frontend_copy_background(); draw_gui();` — covers exactly the menu screens in scope.
- Crucially, `draw_gui()` itself is **shared** with the in-game HUD (`engine_redraw.c:555,613,635`
  and `gui_parchment.c:951,963`), so migration must be **per-menu-state**, never by replacing
  `draw_gui()`. The `frontend_draw()` switch is the discriminator, and it makes the two systems
  coexisting during migration a per-state opt-in rather than a flag day.

---

## 2. Scope

### 2.1 In scope — the 15 frontend `GuiMenu`s

| Menu | State | Defined in |
| --- | --- | --- |
| `frontend_main_menu` | `FeSt_MAIN_MENU` | `frontend.cpp:137,182` |
| `frontend_option_menu` | `FeSt_FEOPTIONS` | `frontmenu_options_data.cpp:68,94` |
| `frontend_define_keys_menu` | `FeSt_FEDEFINE_KEYS` | `frontmenu_options_data.cpp:48,92` |
| `frontend_load_menu` | `FeSt_FELOAD_GAME` | `frontmenu_saves_data.cpp` |
| `frontend_high_score_table_menu` | `FeSt_HIGH_SCORES` | `frontend.cpp:163,186` |
| `frontend_statistics_menu` | `FeSt_LEVEL_STATS` | `frontend.cpp:151,184` |
| `frontend_error_box` | (overlay) | `frontend.cpp:175,188` |
| `frontend_select_campaign_menu` | `FeSt_CAMPAIGN_SELECT` | `frontmenu_select_data.cpp` |
| `frontend_select_mappack_menu` | `FeSt_MAPPACK_SELECT` | `frontmenu_select_data.cpp` |
| `frontend_select_level_menu` | `FeSt_LEVEL_SELECT` | `frontmenu_select_data.cpp` |
| `frontend_select_mp_mappack_menu` | `FeSt_MP_MAPPACK_SELECT` | `frontmenu_select_data.cpp` |
| `frontend_net_service_menu` | `FeSt_NET_SERVICE` | `frontmenu_net_data.cpp` |
| `frontend_net_session_menu` | `FeSt_NET_SESSION` | `frontmenu_net_data.cpp` |
| `frontend_net_start_menu` | `FeSt_NET_START` | `frontmenu_net_data.cpp` |
| `frontend_add_session_box` | (overlay) | `frontmenu_net_data.cpp` |

### 2.2 Also in scope — the "backdrop plus text" screens

Several `FeSt_*` states are not `GuiMenu`s at all but are structurally trivial: they call
`frontend_copy_background()` and then draw text over it. They belong in this migration — they are
the cheapest possible proving ground for the font stack, and leaving them on the bitmap path would
mean the same screen shows crisp text in one state and magnified bitmap text in the next.

| State | Function | What it actually draws |
| --- | --- | --- |
| `FeSt_STORY_POEM` | `frontstory_draw` (`front_credits.c:81`) | backdrop + one centred string in a 70px-inset text window, `frontstory_font` |
| `FeSt_STORY_BIRTHDAY` | `frontbirthday_draw` (`front_easter.c:97`) | backdrop + two centred lines |
| `FeSt_CREDITS` | `frontcredits_draw` (`front_credits.c:96`) | backdrop + a vertically scrolling list built from `campaign.credits[]`, switching between `frontend_font[]` faces per item |

The same treatment covers `draw_defining_a_key_box()` (`frontend.cpp:3177`), which is
`draw_text_box(get_string(GUIStr_PressAKey))` — a modal text box over whatever is behind it. It
migrates with `FeSt_FEDEFINE_KEYS` in Phase D.

`FeSt_CREDITS` is the most interesting of the three: it is a scrolling text region with mixed
faces, which is precisely what an ImGui scroll region plus the §5 wrappers does well, and it
exercises the multi-face font stack before any settings screen depends on it.

### 2.3 Explicitly out of scope

- **All in-game GUI**: `main_menu`, `room_menu`, `spell_menu`, `spell_lost_menu`, `trap_menu`,
  `creature_menu`, `query_menu`, `event_menu`, `options_menu`, `instance_menu`, `quit_menu`,
  `error_box`, `autopilot_menu`, `video_menu`, `sound_menu`, `message_box`, `load_menu`,
  `save_menu`, `text_info_menu`, `battle_menu` (`frontmenu_ingame_*_data.cpp`), plus
  `gui_boxmenu.c`, `gui_parchment.c`, `gui_tooltips.c`, `frontmenu_ingame_tabs.c`. These keep the
  sprite path, `draw_gui()`, and `get_gui_inputs()` exactly as they are.

  A known and accepted consequence: the in-game pause options menu (`GMnu_OPTIONS`) will look and
  behave differently from the new frontend settings screen — two visually unrelated settings UIs in
  one game. Accepted for now. The in-game GUI is a **separate, longer-term project** needing more
  careful thought than a port: it is gameplay-critical, latency-sensitive, sits over live 3D
  content, and its panel/tab chrome is far more entangled with game state than any frontend screen.
  It should be revisited **after the GPU work** ([03-gpu-renderer.md](03-gpu-renderer.md)) lands,
  not before, and it is explicitly not scoped by this document.
- **Video playback and animated set-pieces**: `FeSt_INTRO`, `FeSt_DEMO`, `FeSt_OUTRO`,
  `FeSt_DRAG`, `FeSt_CAMPAIGN_INTRO` (Smacker playback), and `FeSt_TORTURE` (an interactive
  animated screen, not a menu).
- **Splash/loading raw bitmaps** (`front_simple.c`'s `bitmaps_*` tables).
- **In-engine HUD/bitmap text** — `bflib_sprfnt` and the `font_sprites`/`winfont` path stay.

### 2.4 Land view — low priority, deferred past Phase F

`FeSt_LAND_VIEW` / `FeSt_NETLAND_VIEW` (`frontmap_draw`, `front_landview.c`) are a full-screen map
image with hotspot buttons — frontend screens, but almost none of their pixels are chrome.

**`FeSt_LAND_VIEW` is now mostly dead** following the merged campaign-selection screen work
(`docs/refactor/gui/04-phase2-landview-panel-investigation.md`, commits through `1c11001ca`): the
campaign list, land preview and description it used to be the only home for now live on the merged
select screen. It remains reachable — campaign intro redirects into it (`front_fmvids.c:138,143`)
and returning from a singleplayer level lands there (`frontend.cpp:3734`) — but it is no longer
where the interesting content is, so it is **explicitly low priority**: schedule it after Phase F,
or leave it on the sprite path indefinitely if nothing forces the issue.

**`FeSt_NETLAND_VIEW` is planned to receive the same merged-screen treatment** its singleplayer
counterpart already got. That is a separate piece of frontend work, not part of this stage; when it
happens, the resulting screen should be authored directly in ImGui rather than built on the sprite
path and migrated twice. Sequence the two so that work lands after Phase B (wrappers available) —
otherwise it will be built against chrome this stage is retiring.

---

## 3. Integration architecture

### 3.1 Where the code lives (layering)

**Superseded 2026-09-23** by
[05-imgui-linkage-consolidation.md](05-imgui-linkage-consolidation.md) (landed): ImGui context
ownership described below as living in `kfx_platform` moved to `kfx_frontend` instead — see that
doc for the full rationale (in short: the original split still left every ImGui-adjacent call
threaded through a `kfx_platform`/`kfx_frontend` callback boundary for no benefit, since
`kfx_frontend` was always the only real widget-submitting consumer). The description below is kept
as a historical record of Phase A's original design, not the current shape.

`scripts/check_layering.py` governs `src/kfx_*` inter-library includes only; third-party headers
are unconstrained. The split that respected the ladder, as originally shipped in Phase A:

- **`kfx_platform`** owned the ImGui *context*: creating/destroying it, `ImGui_ImplSDL3_*` and
  `ImGui_ImplSDLRenderer3_*` lifecycle, feeding SDL events, new-frame/render. It already owns the
  `SDL_Window` (`WindowSystemSDL`) and the `SDL_Renderer` (`RendererSoftware::m_renderer`), which
  are exactly what both backends need. A narrow `extern "C"`-friendly service alongside
  `RendererManager` — `src/kfx_platform/{include,src}/gui/ImGuiContext.{h,cpp}` — kept nothing
  above having to `#include <imgui.h>` for lifecycle. **Now:** that service is
  `src/kfx_frontend/{include,src}/gui/FrontendImGui.{h,cpp}` instead; `kfx_platform` reaches it
  through the `RendererImGuiCallbacks` struct (`renderer/RendererManager.h`).
- **`kfx_frontend`** owns the *screens* and the §5 wrapper layer: it includes `imgui.h` and submits
  widgets. This is upward-legal (frontend ranks above platform) and keeps menu authoring in the
  library that already owns menu behaviour.
- **`kfx_apploop`** (`wait_at_frontend`) drives the per-frame ordering.

### 3.2 Vendoring

Follow the existing in-tree source-dep convention (`deps/centitoml`, `deps/CUnit-2.1-3` are
git-tracked; downloaded binary deps are gitignored). Add `deps/imgui/` with:

`imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`, `imgui.h`,
`imgui_internal.h`, `imconfig.h`, the three `imstb_*.h`, plus `backends/imgui_impl_sdl3.{cpp,h}`
and `backends/imgui_impl_sdlrenderer3.{cpp,h}`. MIT licensed, consistent with existing third-party
posture. `imgui_demo.cpp` is worth vendoring too — it costs nothing in a release build if not
referenced, and is the fastest way to validate the backend wiring in Phase A.

**Vendor from upstream `ocornut/imgui`, not from the `pthom/imgui` fork carried by the local
`imgui_bundle` checkout.** This was checked, not assumed: that fork's `imgui.h` carries
`#include "imgui_stacklayout.h"` at line 4758 and ships `imgui_stacklayout.{cpp,h,_internal.h}`
alongside the core files — a real fork patch, not a clean mirror. The rest of the original doc's
findings on `imgui_bundle` stand and still apply: do not use `imgui_bundle`'s CMake target,
`hello_imgui` or `immapp` (its runner layer is SDL2-only — there is no `runner_sdl3.cpp` — while
KeeperFX is SDL3 throughout).

Choose `imgui_impl_sdlrenderer3` over `imgui_impl_opengl3` for the same reason as before: it
targets the same `SDL_Renderer` the software backend already presents through, so there is one
graphics context, one present call, and no second API to stand up. If stage 3 later moves game
content onto the GPU, this backend composites into the same renderer with no rework.

The local checkout is Dear ImGui **1.92.7 WIP** (`IMGUI_VERSION_NUM 19263`) and defines
`IMGUI_HAS_TEXTURES` — the dynamic font/texture system introduced at `19198`. Pin at or above that;
§4 depends on it.

### 3.3 Per-frame wiring

Three touch points, all small:

1. **Events** — `LbPollInputs()` (`bflib_inputctrl.cpp:526-534`) drains `SDL_PollEvent` into
   `process_event()`. Add `ImGui_ImplSDL3_ProcessEvent(&ev)` in that loop, gated on the ImGui
   context existing. While an ImGui-owned frontend state is active, `io.WantCaptureMouse` /
   `io.WantCaptureKeyboard` gate the legacy `get_gui_inputs(0)` path so the two systems never both
   claim a click.
2. **Submission** — in `frontend_draw()`, the migrated states call
   `ImGuiFrontendNewFrame()` + the screen's submit function instead of
   `frontend_copy_background(); draw_gui();`. Un-migrated states are byte-for-byte unchanged.
3. **Present** — in `RendererSoftware::PresentFrame()`, between `SDL_RenderTexture(...)` and
   `SDL_RenderPresent(...)` (`RendererSoftware.cpp:136-137`), render the ImGui draw data. The
   software framebuffer therefore remains the backdrop layer and ImGui is a true overlay — which is
   why this stage needs nothing from stage 3.

Set `io.MouseDrawCursor = false`: the game already draws its own pointer sprite around the swap
(`LbMouseOnBeginSwap`/`LbMouseOnEndSwap`, `RendererSoftware.cpp:125,138`) and that behaviour is
kept.

### 3.4 Backdrop

Simplest correct option for Phase A–C: keep `frontend_copy_background()` drawing the existing
full-screen backdrop through the software path (already true-colour) and let ImGui draw on top.
Uploading backdrops as ImGui textures becomes worthwhile only when a screen needs the backdrop
sampled/tinted per-widget; defer it.

### 3.5 Runtime toggle — both paths ship, ImGui on by default

**The legacy sprite path is not deleted while this stage is in progress.** Every migrated screen
keeps its existing `GuiButtonInit` array and draw callbacks intact, and a runtime switch chooses
which one runs. This is what makes the migration safe to ship incrementally: any screen that turns
out to have a regression can be worked around by the user with one flag while it is fixed, and any
bug report can be bisected to "does it also happen on the classic menu?" in one run.

- **Flag**: a new command-line parameter parsed in `process_command_line()`
  (`main.cpp:1669-1940`), following the existing `strcasecmp(parstr, ...)` pattern — `-classicmenu`
  (with `-noimgui` as an accepted alias) forces the legacy path. Default is ImGui **on**.
- **Runtime, not compile-time.** Both paths must be present in one binary. Do not put this behind
  `#ifdef` — the point is that a user with a shipped release can flip it, and that both paths stay
  compiled and therefore stay compiling.
- **Also a config key**, so it survives without editing a shortcut: add it to `conf_commands[]`
  (`config_keeperfx.c:146-194`) and use the established precedence mechanism — command line beats
  config, tracked via `start_params.overrides[Clo_*]` (`config_keeperfx.h:44-49`,
  `config_keeperfx.c:923,961,1205`). This adds one entry to `enum CmdLineOverrides` and requires
  bumping `CMDLINE_OVERRIDES` (currently 4, `config_keeperfx.h:41`).
- **Resolution of the flag is per-screen, not global.** The effective decision is
  *"ImGui enabled AND this state has been migrated"*. Un-migrated states ignore the flag entirely,
  which means the flag is meaningful from Phase C onward and harmless before it.
- **The toggle is temporary.** It exists while issues are worked out. Retiring it — and with it
  the legacy frontend menu code — is a deliberate later decision, made once every screen in §2 has
  shipped and stabilised, not an automatic consequence of Phase F. Record that decision here when
  it is taken.

---

## 4. Typography

### 4.1 Fonts: Exocet installed into `fxdata/`, Cinzel bundled as fallback

The named file is `EXL_____.TTF` in the DK2 Windows theme. Reading its `name` table directly:

> `Copyright (C) 1992 Emigre Graphics, Designed by Jonathan Barnbrook` — `Exocet Light`

Its sibling `EXH_____.TTF` is `Exocet Heavy`, same copyright. **Exocet is a commercial Emigre
retail typeface.** KeeperFX is GPL v2 and its releases are redistributed as packages, so the font
cannot be bundled in `dist/` or the CPack package, and it must not enter this repository — that
constraint is absolute regardless of anything else.

It does not, however, prevent *using* it. KeeperFX already requires the user to supply the original
Dungeon Keeper data files, which are equally proprietary and equally not shipped; the fonts join
that same list of user-supplied assets, copied out of the user's own DK/DK2 installation. So:

- **Install-time copy into `fxdata/`.** The installer/launcher copies `EXL_____.TTF` and
  `EXH_____.TTF` into the game's `fxdata/` directory alongside `gamecontrollerdb.txt` and the
  `.cfg`/`.toml` data, exactly as it already copies the DK data files into place. The engine then
  resolves them through the existing path helper — `prepare_file_path(FGrp_FxData, "EXL_____.TTF")`
  (`globals.h:362-384`'s `TbFileGroups`, the same mechanism `custom_sprites.c` and `lua_base.c`
  already use) — which means no new search logic, no install-path probing, and mod/override
  directories work for free via `prepare_file_path_mod`.
- **Fallback, bundled: Cinzel** (SIL OFL, redistributable under GPL packaging) — shipped into the
  same `fxdata/` directory and used whenever the Exocet files are absent. Chosen for its carved
  Roman-capital character and, decisively, for having a weight range: the design needs a display and
  a heading weight, which is what Exocet Light/Heavy provides. The engine's behaviour is a simple
  present/absent check at font-load time, with the result logged.

Two consequences to handle:

1. **The fallback is likely the common case.** These TTFs ship in *"Dungeon Keeper Extras/theme/
   DK2 Theme"* — a DK2-era extras bundle, **not** the base DK install KeeperFX already requires. A
   user with a working setup does not necessarily have them, and the launcher's copy step will
   often find nothing. So the bundled face is a first-class design target, not a degraded mode, and
   §5's style must look deliberate with either. Metrics differ between the two, so the wrappers'
   padding and line-height must derive from the loaded font's metrics rather than hardcoded pixels.
2. **The packaging globs need a `.ttf` rule.** `build/make/package.mk:31-35` stages `config/fxdata/`
   by extension (`*.cfg`, `*.toml`, `*.txt`, `lua/*.lua`) — the bundled fallback font would be
   silently omitted today. One rule to add, in `package.mk` and correspondingly in
   `Packaging.cmake`'s `gamedata` component.

### 4.2 CJK and Cyrillic coverage is a solved problem here

`lang/` carries `chi`, `cht`, `jpn`, `kor`, `rus`, `ukr` among others, and `bflib_sprfnt.c` already
maps codepoints through a `codepage_map` (`bflib_sprfnt.c:1414-1471`). Neither Exocet nor a Latin
display face covers any of that. Fortunately the answer already exists in-tree:
`tools/fxfontmaker/` (`make_fonts.bat`) builds today's CJK bitmap fonts from **GNU Unifont** and
**WenQuanYi** — both freely licensed and both available as TTF/OTF. Merge them as ImGui fallback
fonts behind the display/body faces (`ImFontConfig::MergeMode`), so the aesthetic face handles
Latin and the fallback handles everything else. No new licensing question, and it is the same
provenance the current fonts already have.

ImGui is UTF-8 native throughout, which matches what `get_string()` already returns.

### 4.3 Sizing

1.92's dynamic font system (`IMGUI_HAS_TEXTURES`) rasterizes glyphs on demand at the requested
size rather than requiring a pre-baked atlas per size. Two consequences worth designing around:

- Derive the frontend's base text size from actual window height (the same input
  `units_per_pixel_menu` uses, `vidmode.c:781`) and re-derive on resolution change, instead of
  magnifying a 640×480-authored bitmap. This is the direct fix for "the bitmapped fonts don't scale
  well to higher resolutions."
- Large CJK ranges no longer need to be baked up front, so the memory objection to a full-coverage
  frontend font disappears.

Text *layout* should use ImGui's own metrics; do not route ImGui text through `bflib_sprfnt`'s
`units_per_px` model. The two scaling systems coexist because they never share a screen.

---

## 5. Chrome, and the widget wrapper layer

### 5.1 Chrome: reuse the existing source art, don't commission new

**Decision: responsive layout, not a scaled 4:3 virtual canvas.** Screens lay out against the real
window and use the full widescreen area, with a max content width so ultrawide displays don't
stretch a two-column screen into unreadability. Every wrapper's sizing contract in §5.2 follows
from this: sizes derive from font metrics and available space, never from 640x480-relative
constants.

The brief allows departing from both the 1997 design and the repo's current variant. It turns out
less of that permission needs spending than expected, because the chrome already exists as source
art. `FXGraphics-main/menufx/frontend-64/` (a local working copy of `dkfans/FXGraphics`, GPL v3 —
the same repo CI already clones for `make pkg-enginegfx`; gitignored like the other game-data
folders) holds the raw PNGs every frontend sprite is built from, and they map almost one-to-one
onto §5.2's wrapper list:

| Source PNGs | Wrapper |
| --- | --- |
| `hugearea_thn_*` / `hugearea_thc_*` — corners, side pieces, top/bottom bands, four fill variants | `FeBeginPanel`, `FeBeginListBox` |
| `largearea_nx1/nx2/xts_*` — `cor_l`, `cor_r`, `tx1..tx5_c` | inset/label areas |
| `hugebutton_a01..a05_{l,c,r}`, `largebutton_a01..a05_{l,r}` | `FeButton`, `FeNavButton` (5 states each, already a 3-slice) |
| `scrollbar_{top,btm}arrow_{std,act}`, `scrollbar_indicator_{std,act}`, `scrollbar_vert_ct_{short,long}` | `FeBeginScrollArea`'s scrollbar |
| `slider_horiz_{l,c,r}`, `slider_indicator_{std,act}`, `slidrect_indicator_{std,act}` | `FeSlider` |
| `specicon_{music,sound,voice}` | the settings screen's category icons |
| `front_background-{64,128,256}` | backdrop |

So the plan for chrome is **reuse and recompose**, not commission:

- **Panels and lists**: draw the existing 9-slice pieces via `ImDrawList::AddImage` at whatever
  independent width and height the layout asks for — the thing §1.2 showed the art already supports
  and only the old drawing code prevented. Use the `tx1..tx4` variants for decorative variety along
  long edges rather than treating them as a required sequence.
- **Buttons, sliders, scrollbars**: the five-state button sets and the complete slider/scrollbar
  sets transfer directly.
- **Procedural where no art exists**: translucent card fills, gradients, hover tints, drop shadows —
  `AddRectFilled`, `AddRectFilledMultiColor`, layered translucent rects. Source the palette from
  `front.pal` so the colour identity is inherited. This is what
  [gui/02-menu-v2-mockup-gap-analysis.md](../gui/02-menu-v2-mockup-gap-analysis.md) §1 called
  "expensive, general-purpose"; stage 2 made it free, and that doc's "bake gradients into the
  backdrop art" recommendation should be considered withdrawn.

**One honest limitation, accepted.** The chrome art exists only at the `-64` scale —
`frontend-64/` has no `-128`/`-256` sibling, unlike `gui1`/`gui2` (which go to `-256`) and unlike
the backdrop itself (`front_background-64/128/256` = 640x480 / 1280x960 / 2560x1920). So panel and
button ornament is upscaled at high resolutions, roughly 6x at 4K. This is much less damaging than
it is for text — edges, corners and flat bands upscale acceptably with linear filtering, where
glyphs do not — but it is the same resolution ceiling in a quieter place.

**Decision: ship the low-resolution chrome as-is.** Use linear filtering, and draw ornament at
modest thickness rather than scaling it proportionally with the panel. A general asset-improvement
pass is separately planned; higher-resolution chrome belongs to that effort, not to this stage. Do
not block, redraw, or commission anything here on account of the ceiling — Phase B's style-sheet
screen at 4K is worth looking at for information, not as a gate.

New art genuinely needed is therefore small: the main-menu wordmark (Phase F), and anything the
redesigned screens introduce that has no existing counterpart. Placeholder treatment (procedural
panels, text wordmark) is fine for Phases B-E.

### 5.2 Screens do not call ImGui directly

**Every panel, list, scroll region, button, slider and text style gets a KeeperFX wrapper, and
screen code calls only wrappers.** This is a hard rule, not a preference, and it is the single
most important structural decision in the plan after the scope boundary. Without it, styling
decisions leak into fifteen screens as ad-hoc `PushStyleVar`/`PushStyleColor` pairs and the result
drifts exactly the way the current `GuiButtonInit` arrays drifted — inconsistent padding, per-screen
one-offs, and no way to restyle anything centrally.

Proposed home: `src/kfx_frontend/{include,src}/frontgui_widgets.{h,cpp}` — one wrapper module,
`imgui.h` included there and in the screens, nowhere else in `kfx_frontend`.

The wrapper set, roughly:

| Wrapper | Replaces / covers |
| --- | --- |
| `FeBeginPanel` / `FeEndPanel` | bordered content panel, ornament corners, title bar; the successor to `frontend_draw_scroll_box` |
| `FeBeginListBox` / `FeEndListBox`, `FeListRow` | scrollable selection lists — the `FrontendSelectList` screens and the key-remap rows |
| `FeBeginScrollArea` / `FeEndScrollArea` | scrolling text or content with the styled scrollbar |
| `FeButton`, `FeIconButton`, `FeNavButton` | the large/small menu button families in `gui_frontbtns.c` |
| `FeSlider`, `FeCheckbox`, `FeCombo`, `FeTextInput`, `FeKeybindRow` | settings controls; the §6.3 schema renderer is built entirely from these |
| `FeHeading`, `FeSubheading`, `FeBodyText`, `FeCaption`, `FeSeparator` | the type scale, so font-role choices live in one file |
| `FeBeginTabBar` / `FeTab` | the settings screen's Game/Graphics/Sound/Input tabs (§10) |
| `FeBeginModal` / `FeEndModal` | error box, add-session box, "press a key" |

Rules the wrappers enforce, each of which is a bug class that otherwise recurs per screen:

- **Style push/pop is owned by the wrapper**, always balanced, never left to the caller.
- **Sound feedback lives here.** Menu hover and click sounds are behaviour, not decoration, and
  wiring them once inside `FeButton`/`FeListRow` is the difference between menus that feel right
  and fifteen screens that each forgot a different one.
- **Focus and navigation defaults** — nav flags, default-focused item, Esc-to-back — are set
  consistently rather than per screen. Gamepad play is **not** a target: no screen is designed
  around it, and nothing is held back waiting for it. But the wrappers still set ImGui's nav flags
  uniformly, because consistency is the point of the layer and because uniform focus handling is
  what keyboard navigation needs anyway. Whatever gamepad behaviour falls out of that is a
  by-product, not a supported feature — do not add gamepad-specific screens, prompts or affordances.
- **Sizing derives from font metrics and window scale**, not literal pixels, so §4.1's two possible
  display faces and §4.3's resolution scaling both work without per-screen adjustment.
- **No default ImGui look reaches a player-facing screen.** The wrappers are what guarantee this.

Build the wrappers in Phase B, before the first screen migrates, and treat "a screen calls
`ImGui::` directly for something a wrapper covers" as a review failure.

---

## 6. The settings menu — the case that motivates this

### 6.1 What exists today

`frontend_option_buttons[]` (`frontmenu_options_data.cpp:68-88`) is the whole settings surface:
three volume sliders (sound `BID_SOUND_VOL`, music `BID_MUSIC_VOL`, mentor/speech `BID_MENTOR_VOL`),
a mouse-sensitivity slider (`BID_MOUSE_MUL`), an invert-mouse toggle, and a button to the key-remap
screen. `frontend_define_keys_buttons[]` (`frontmenu_options_data.cpp:48-66`) is the remap screen:
**twelve hand-declared row buttons** plus up/down/scroll-tab, each row carrying its index in
`content.lval`, all rendered through the width-derived scroll box from §1.2.

`FrontendSliderCtrl` / `FrontendCheckboxCtrl` (`frontmenu_settingctrl.h`) already abstract the
value binding — get/set/nonlinear-mapping — so the *data* half of a generic settings system exists.
What doesn't exist is a generic *presentation* half: every control is still a hand-placed pixel
rectangle in a static array. In ImGui the remap screen is a `for` loop over
`num_definable_keys()` inside an `FeBeginListBox`, and the twelve row buttons plus their
`_maintain`/`_up`/`_down`/`_scroll` callbacks all disappear.

### 6.2 Launcher options to bring in-game

Scope: the launcher's **Game, Graphics, Sound and Input** tabs
(`ui/settingsdialog.ui`, `src/settingsdialog.cpp::saveSettings`), **excluding** packet save
(`GAME_PARAM_PACKET_SAVE_ENABLED`, `GAME_PARAM_PACKET_SAVE_FILE_NAME`), exit-on-Lua-error
(`EXIT_ON_LUA_ERROR`), extra launch options (`EXTRA_GAME_LAUNCH_OPTIONS`), command character
(`COMMAND_CHAR`), and enable-sound (`GAME_PARAM_NO_SOUND`). The Multiplayer and Launcher tabs stay
in the launcher entirely.

That leaves, by tab:

**Game** — `LANGUAGE`, `CENSORSHIP`, `SCREENSHOT`, `DELTA_TIME`, `FREEZE_GAME_ON_FOCUS_LOST`,
`FLEE_BUTTON_DEFAULT`, `IMPRISON_BUTTON_DEFAULT`, `STARTUP` (the splash-screens + intro list; the
legacy `DISABLE_SPLASH_SCREENS` + `-nointro` pair on older builds), plus cheats
(`GAME_PARAM_ALEX`) and gameturns (`GAME_PARAM_FPS`).

**Graphics** — `DISPLAY_NUMBER`, `RESIZE_MOVIES`, `INGAME_RES` (the single resolution option,
post-§6.4 collapse), `CREATURE_STATUS_SIZE`, `LINE_BOX_SIZE`, `HAND_SIZE`, `FRAMES_PER_SECOND`
(including its `AUTO` prefix form), `GUI_BLINK_RATE`, `NEUTRAL_FLASH_RATE`, plus smooth video
(`GAME_PARAM_VID_SMOOTH`).

**Sound** — `PAUSE_MUSIC_WHEN_GAME_PAUSED`, `MUTE_AUDIO_ON_FOCUS_LOST`, `ATMOSPHERIC_SOUNDS`,
`ATMOS_FREQUENCY`, `ATMOS_VOLUME`, plus CD music (`GAME_PARAM_USE_CD_MUSIC`).

**Input** — `POINTER_SENSITIVITY`, `UNLOCK_CURSOR_WHEN_GAME_PAUSED`, `LOCK_CURSOR_IN_POSSESSION`,
`CURSOR_EDGE_CAMERA_PANNING`, `ZOOM_TO_MOUSE`, `ROTATE_AROUND_MOUSE`, `TAG_MODE_TOGGLING`,
`DEFAULT_TAG_MODE`, plus alt input (`GAME_PARAM_ALT_INPUT`).

Four findings from reading both sides, each of which is real work the schema alone won't absorb:

1. **`keeperfx.cfg` is read-only to the engine.** `config_keeperfx.c` contains no writer — no
   `fopen`/`fprintf`/save path of any kind. `save_settings()` (`config_settings.h:92`) persists the
   binary `struct GameSettings` (`config_settings.h:64-86`), a *different, much smaller* set: video
   detail, shadows, view distance, volumes, keybindings, mouse invert/sensitivity, zoom levels.
   Nearly every option above lives in the file the engine cannot write. **A comment- and
   order-preserving `keeperfx.cfg` writer is a prerequisite**, independent of ImGui, and could be
   built first. Launcher and game never run write operations concurrently, so no locking or
   conflict-resolution scheme is needed — but both write the same file at different times, so the
   writer must round-trip faithfully: preserve comments, key order, and any key it does not
   recognise (the launcher already relies on this for `STARTUP`'s hidden tokens).
2. **Five of the wanted options have no config key at all.** The `GAME_PARAM_*` entries are the
   launcher's own settings, converted to command-line flags at launch — `-alex`
   (`main.cpp:1869`, `start_params.easter_egg`), `-vidsmooth` (`main.cpp:1775`, `smooth_on`),
   `-altinput` (`main.cpp:1792`, `lbMouseGrab`), CD music (`Clo_CDMusic`, `main.cpp:1735`), and
   `-fps` (`main.cpp:1757`, which *does* already have a `TURNS_PER_SECOND` config key and uses the
   override mechanism). Owning the rest in-game means **adding config keys** to `conf_commands[]`
   and giving them the same command-line-beats-config precedence via
   `start_params.overrides[Clo_*]` — the same mechanism §3.5's own toggle needs, so build it once.
3. **Some controls are not 1:1 with keys.** `POINTER_SENSITIVITY` is one key driving *two* widgets
   — the raw-input checkbox writes `0`, otherwise the slider's value is written. `STARTUP` is a
   space-separated token list assembled from two checkboxes plus tokens the launcher preserves but
   does not display. `FRAMES_PER_SECOND` carries an optional `AUTO ` prefix. And the resolution
   keys are worse than they look — see §6.4.
4. **Some controls gate others.** Alt input enables the possession-cursor-lock checkbox and
   disables the unlock-when-paused one, and vice versa. The schema needs an enable-condition
   field, not just a type.

Apply-class, which the UI must show honestly: *live* (volumes, sensitivity, `ZOOM_TO_MOUSE`,
`ROTATE_AROUND_MOUSE`, `TAG_MODE_TOGGLING`, `CURSOR_EDGE_CAMERA_PANNING`, `GUI_BLINK_RATE`,
`DELTA_TIME`) versus *needs restart* (`INGAME_RES`, `DISPLAY_NUMBER`, `LANGUAGE`, `STARTUP`, and
everything in finding 2 that stays a launch parameter).

### 6.3 The shape that makes this scale

Hand-authoring ~35 controls is exactly the work that produced the current unmaintainable arrays.
Instead: a **declarative option schema** in `kfx_config` — one row per option carrying key name,
type (bool/enum/int/float/string/keybind/composite), range or enum table, localized label and help
string id, category (tab), apply-class, enable-condition, and get/set accessors reusing the
`FrontendSliderCtrl`/`FrontendCheckboxCtrl` pattern. The frontend then renders *the schema*, not
the options: one generic renderer per type, built from §5's wrappers, and adding an option becomes
one table row plus a `.pot` string. Findings 3 and 4 above are why the schema needs composite
types and enable-conditions from the start rather than bolted on later.

**Localization: English first.** Each option contributes a label and a help string, and ~35 options
across 17 languages is a translation burden that should not gate the feature. Ship labels and help
text in English, added to `lang/gtext_eng.pot` as usual, and let translations land incrementally
afterwards — the schema already routes every string through `get_string()`, so a translated entry
starts working the moment it exists with no code change.

This is the single largest payoff in the whole plan, and it is the reason to prefer real widgets
over a re-skinned sprite path. It also feeds the earlier idea (kept from the original doc) of a
debug/cvar inspector — same schema, developer-facing view, effectively free once the schema exists.

### 6.4 Resolution settings collapse to one — done

Resolution used to be four screen-specific settings wearing two config keys — the single messiest
thing the settings screen would otherwise have had to expose:

- **`FRONTEND_RES`** was three slots feeding three *different* targets — failsafe mode, movie
  playback mode, and frontend mode, dispatching to
  `set_failsafe_vidmode`/`set_movies_vidmode`/`set_frontend_vidmode`.
- **`INGAME_RES`** was up to 6 slots feeding `switching_vidmodes[]` — the list Alt+R cycled
  through, with the current position stored in `settings.switching_vidmodes_index`.

The four distinct modes were historical: separate failsafe, movie and frontend modes made sense
when the frontend was a fixed 640×480 paletted surface and Smacker playback needed its own mode.
§5.1's responsive layout removes the reason the frontend needs a mode of its own, `RESIZE_MOVIES`
(`SMK_FullscreenFit` and friends) already handles movie scaling independently, and a true-colour
renderer at native resolution removes most of what failsafe was insuring against.

**This landed ahead of Phase G, as engine work rather than settings-screen work.** `FRONTEND_RES`
is gone; `INGAME_RES` now takes exactly one mode string and is the single resolution used for
frontend, movies, and in-game alike, via `vidmode.c`'s `get_screen_vidmode()`/
`set_screen_vidmode()`. Alt+R cycling was removed entirely rather than narrowed to one entry —
`switching_vidmodes[]`, `switch_to_next_video_mode()`, its packet action, and the legacy
options-menu resolution button are gone; the only fallback left is an internal hardcoded constant
used by `setup_screen_mode()`'s existing `failsafe` bool path, not a user-configurable mode. The
settings screen is authored against this single resolution option from the start and never ships
the four-control interim — see §7. The resolution-picker UI itself landed as Phase G, step 12.

One correction to the original planning note here: `struct GameSettings` (which held
`switching_vidmodes_index`) turned out to **not** be one of the structs `memcpy`'d wholesale for
saves and network resync — `kfx_net`'s resync code copies individual named `settings.*` fields by
value (see `packets_misc.c`, `net_game.c`), never the whole struct. Removing the field needed no
layout-migration story, just a plain deletion.

---

## 7. Phasing

Each phase is independently shippable; the frontend never has a broken intermediate state, because
un-migrated `FrontendMenuState`s keep their existing draw path and §3.5's flag can force the whole
frontend back to it.

**Phase A — backend up, toggle in place.** Vendor ImGui + the two backends; `kfx_platform` context
service; event feed in `LbPollInputs`; render hook in `PresentFrame`; the `-classicmenu` flag and
its config key, including the `CMDLINE_OVERRIDES` bump. Prove it with `imgui_demo` behind a debug
launch flag, over a live main menu. *Exit:* demo window renders and takes input at several
resolutions on both toolchains, the legacy menu still fully functional underneath, and the toggle
demonstrably switches paths at runtime.

**Phase B — typography, style, and the wrapper layer.** Font loading from `fxdata/` (Exocet when
the installer has copied it in, bundled Cinzel otherwise, Unifont/WenQuanYi merge for CJK/Cyrillic),
the `.ttf` packaging rule, size derived from window height and re-derived on resolution change, the
KeeperFX `ImGuiStyle`, importing the §5.1 chrome PNGs into an ImGui texture atlas, and **all of
§5.2's wrappers** built to the responsive sizing contract. *Exit:* a style-sheet test screen
exercising every wrapper and every shipped language's sample text, legible at 640×480 through 4K,
with both display faces, and a first read on how the `-64` chrome art holds up upscaled.

**Phase C — text screens, then the Options screen.** `FeSt_STORY_POEM`, `FeSt_STORY_BIRTHDAY` and
`FeSt_CREDITS` first — they are backdrop-plus-text (§2.2), so they validate the font stack and the
text wrappers with almost no logic at risk. Then `FeSt_FEOPTIONS`: small, self-contained, and the
first exercise of sliders, checkboxes, navigation and sound feedback. *Exit:* settings change and
persist identically to before; keyboard and gamepad navigation work; credits still scroll at the
same rate; the sprite path is untouched and still used by every other state.

**Phase D — list-shaped screens.** `FeSt_FEDEFINE_KEYS` (deletes the twelve-row pattern, and brings
`draw_defining_a_key_box` with it), `FeSt_HIGH_SCORES`, `FeSt_FELOAD_GAME`. Text entry (high-score
names, network chat) is **Latin-only** — ImGui's SDL3 backend text input is enough; no IME
composition work. Displaying CJK is unaffected and still works via §4.2's fallback fonts; only
typing it is out of scope. *Exit:* scrolling lists of arbitrary length, key capture still works,
high-score name entry still works.

**Phase E — master-detail select screens.** `FeSt_CAMPAIGN_SELECT`, `FeSt_MAPPACK_SELECT`,
`FeSt_LEVEL_SELECT`, `FeSt_MP_MAPPACK_SELECT`, and the land preview at full colour — the highlight-
vs-commit split described in the gap analysis §3, plus retiring
`land_preview_remap_screen_to_shared_palette`. Decide the `FeSt_LAND_VIEW` treatment here.

**Phase F — main menu and network screens.** `FeSt_MAIN_MENU` (needs the wordmark asset),
`FeSt_NET_SERVICE`/`SESSION`/`START`, `FeSt_LEVEL_STATS`, error/add-session overlays. *Exit:* every
screen in §2.1 and §2.2 is ImGui and the flag switches the entire frontend between two complete
implementations. The legacy code is **not** deleted at this point — see §3.5.

**Phase G — settings expansion.** *Prerequisite, landing before this phase:* the resolution
collapse (§6.4), so the settings screen is authored against one resolution option and never ships
the four-control interim. Then: the `keeperfx.cfg` writer, the new config keys and overrides from
§6.2 finding 2, the option schema, the generic schema renderer, and the four tabs' worth of options,
with labels and help text English-first (§6.3). Coordinate with the launcher so the two agree on
which side owns what.

**Later, separately — retire the toggle.** Once §2's screens have shipped and stabilised, decide to
remove `-classicmenu` and the legacy frontend menu code. Deliberate, and not part of this plan's
completion criteria. **Done 2026-09-12** (docs/refactor/ingame-gui/00-overview.md §1/§8) — the
legacy frontend menu code is gone; the in-game HUD's own legacy sprite renderer (a separate
project, docs/refactor/ingame-gui/) was kept live on purpose, moved onto `GUI_ICON_PACK=CLASSIC`
rather than deleted with the rest.

---

## 8. Risks and things that will bite

- **Screenshots will miss the ImGui layer.** `RendererSoftware::ScheduleScreenshot`
  (`RendererSoftware.cpp:105-119`) saves `lbDrawSurface` — the CPU framebuffer — and
  `perform_any_screen_capturing()` (`scrcapt.c:142`) is called inside `frontend_draw()` *before*
  present. Neither sees an overlay composited at present time. Fix in Phase A: for ImGui-composited
  frames, capture with `SDL_RenderReadPixels` after the ImGui render pass instead. Movie recording
  (`movie_record_frame`) has the same problem.
- **Two input systems in one process.** Mitigated by `WantCaptureMouse`/`WantCaptureKeyboard`
  gating and by the fact that a given `FrontendMenuState` belongs entirely to one system. Watch
  `frontend_mouse_over_button` (a single global, reset each frame in `wait_at_frontend`) and
  `snap_to_direction` (`button_snapping.c`) — ImGui's own nav replaces the latter on migrated
  screens, and the two must not both run.
- **Two live code paths is a maintenance cost, deliberately accepted.** §3.5's toggle means every
  frontend behaviour change during this stage has to be made twice or consciously made
  ImGui-only. Keep the window short; that is the argument for retiring the toggle promptly once
  Phase F is stable.
- **`gui_frontbtns.c` is shared.** It holds both frontend-only drawing and helpers used by in-game
  code. Whenever legacy frontend chrome is eventually deleted, it needs a per-function check, not
  a file-level one.
- **Menu sound feedback is behaviour, not decoration.** Centralised in the §5.2 wrappers rather
  than left to each screen — that is what the wrapper layer is for.
- **Functional tests.** Any `src/ftests/` test that drives frontend menus by simulated clicks at
  fixed coordinates will need updating as screens migrate. The `-classicmenu` flag gives such tests
  a stable path in the interim, which is a second reason for it to exist.
- **Build surface.** This is the first substantial third-party **C++** source compiled into the
  game. Verify under mingw-w64 cross-compile, MSVC/clang-cl via vcpkg, and native Linux —
  `imgui_impl_sdlrenderer3` in particular must match the vendored SDL3 version
  (`Dependencies.cmake` pins 3.4.12 for the Windows path).
- **Aesthetic drift is the real risk, not a technical one.** The permission to modernize makes it
  easy to land somewhere generic. Phase B's style test screen exists to make that visible early;
  treat it as a gate, not a formality.

## 9. Verification

- **Not** screenshot-diff against the pre-migration baseline — this stage deliberately changes
  appearance, so the original doc's "any visible difference is a bug" criterion no longer applies.
  Verify *behaviour* parity instead: every navigation path, every setting written and re-read,
  every list scrolled to its end, per migrated screen.
- **Every phase must be tested both ways**, with the flag on and off. The legacy path staying
  correct is a shipped feature until the toggle is retired, not a courtesy.
- Legibility check per phase at 640×480, 1920×1080 and 3840×2160, for a Latin, a Cyrillic and a
  CJK language, and with both the Exocet and the bundled fallback face.
- `KFX_OS=linux ./build-cmake.sh` plus the mingw cross-compile, both variants.
- `python3 scripts/check_layering.py --strict`.
- Confirm un-migrated states are pixel-identical after each phase — that *is* a valid screenshot
  diff, and it's the guard that the two systems stay independent.

---

## 10. Decisions on record

**No open questions remain.** Every decision this plan depends on was settled on 2026-09-04 and is
recorded here so the reasoning isn't relitigated; each links to the section that acts on it. The
plan is decision-complete — the only thing sequenced ahead of it is §6.4's resolution collapse,
which is engine work in its own right.

### Scope and approach

- **Real ImGui widgets for the main-menu frontend; in-game GUI untouched.** The reversal of this
  document's original recommendation — see the revision note and §1.
- **In-game GUI is a separate, longer-term project, revisited after the GPU work.** See §2.3. Two
  visually unrelated settings UIs (frontend vs. `GMnu_OPTIONS`) is an accepted interim consequence.
  It needs more careful thought than a port: gameplay-critical, latency-sensitive, over live 3D
  content, chrome entangled with game state.
- **`FeSt_LAND_VIEW` is low priority**, mostly superseded by the merged campaign screen; deferred
  past Phase F, possibly indefinitely. `FeSt_NETLAND_VIEW` gets the same merged-screen treatment as
  separate work — sequence it after Phase B so it is authored in ImGui rather than built on chrome
  this stage retires. See §2.4.
- **Both paths ship behind a runtime toggle**, ImGui on by default, `-classicmenu` to opt out. See
  §3.5. Retiring the toggle and the legacy code is a later, deliberate decision.

### Presentation

- **Bundled fallback face — Cinzel.** See §4.1. The weight range decides it: the design needs
  display and heading weights, matching what Exocet Light/Heavy gives us. Exocet itself is copied
  into `fxdata/` at install time and used when present.
- **Responsive layout, not a scaled 4:3 virtual canvas**, with a max content width so ultrawide
  doesn't stretch two-column screens. This is the wrappers' sizing contract. See §5.1.
- **Reuse `FXGraphics-main/menufx/frontend-64/` for chrome.** The source PNGs are already correctly
  cut for 9-slice panels, five-state buttons, and complete slider and scrollbar sets. Only the
  main-menu wordmark is genuine new art; placeholders carry Phases B–E. See §5.1.
- **Ship the low-resolution chrome as-is.** Linear filtering, ornament drawn at modest thickness
  rather than scaled proportionally. A general asset-improvement pass is separately planned and owns
  any higher-resolution chrome; nothing here blocks on it. See §5.1.
- **Gamepad is not a target** — nothing designed around it, nothing blocked on it — but the wrappers
  set nav flags uniformly, because that is what keyboard navigation needs anyway. No
  gamepad-specific affordances. See §5.2.
- **Text entry is Latin-only.** Displaying CJK is unaffected (§4.2's fallback fonts); typing it is
  out of scope. See Phase D.

### Settings

- **Tabs, mirroring the launcher's Game/Graphics/Sound/Input grouping.** Too many options for one
  scrolling list, and matching the launcher's grouping makes parity review straightforward. The
  schema's category field maps directly to a tab. See §6.2, §6.3.
- **`FRONTEND_RES` and `INGAME_RES` collapse to one resolution option**, replacing the four
  screen-specific modes (failsafe, movies, frontend, in-game cycle list) and removing Alt+R
  cycling entirely. Landed as engine work ahead of Phase G so the settings screen never ships four
  resolution controls it would then lose three of; the picker UI itself landed as Phase G, step 12.
  See §6.4.
- **No launcher/game write-conflict handling needed** — they never write concurrently. The writer
  must still round-trip faithfully (comments, key order, unrecognised keys), because both edit the
  same file at different times. See §6.2 finding 1.
- **English-first strings.** Labels and help text ship in English; translations land incrementally
  afterwards with no code change, since every string already routes through `get_string()`. See
  §6.3.
