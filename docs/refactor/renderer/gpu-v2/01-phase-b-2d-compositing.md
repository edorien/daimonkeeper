# Phase B — 2D compositing on the GPU

See [00-overview.md](00-overview.md) for stage status and shared context.

The original Phase B was "route `IUIRenderer`/`ITextRenderer` through the GPU, with ImGui as one
candidate implementation". That framing is retired. The frontend 2D layer is already GPU-composited
via ImGui/SDLRenderer3. What remains is a short list of concrete, mostly independent items.

## B1 — Frontend backdrop and residual CPU present cost (small) — **landed 2026-09-23**

- `frontend_copy_background()` still paints a full-screen backdrop into `lbDrawSurface` every
  frontend frame, which is then `SDL_UpdateTexture`'d wholesale. For a mostly-static backdrop this
  is a full-resolution CPU fill + full-frame upload per frame behind a UI that changes little.
  Option: upload the backdrop once as a dynamic/static ImGui texture and draw it via
  `GetBackgroundDrawList()`, skipping the software fill and the per-frame texture upload on frames
  where neither backdrop nor 3D content changed. Stage 4 §3.4 deliberately deferred this ("only
  worthwhile when a screen needs the backdrop sampled/tinted per-widget"); revisit here with real
  profiling, since this stage is where per-frame upload cost is the subject.

  **Landed as: already done, by a different project.** Found already implemented while re-verifying
  this bullet against current code:
  [05-imgui-owned-menu-backdrop.md](../05-imgui-owned-menu-backdrop.md) (landed before this stage's
  Phase A/B work started, per that doc's own Phase A–D) built exactly this — `frontend.cpp::
  frontend_draw()`'s switch already skips `frontend_copy_background()`/`draw_gui()` entirely for
  every `frontend_imgui_screen_active()` screen, and `FeStyleGetMenuBackdropTexture()`
  (`frontgui_style.cpp`) decodes `frontend_background` into an `ImTextureID` exactly once (latched
  via `s_menu_backdrop_build_attempted`), handing back the same cached handle every frame after
  that; `draw_menu_backdrop()` (`frontgui_screens.cpp`) draws it via `GetBackgroundDrawList()`. This
  gpu-v2 plan document predates that project and was never updated to cross-reference it — nothing
  left to do here.
- Decide whether `m_texture` upload can be skipped entirely on pure-frontend frames (no 3D view,
  backdrop unchanged) — i.e. present ImGui over a cleared/again-textured target without re-uploading
  an unchanged CPU framebuffer.

  **Landed as:** this part was genuinely still open. `RendererSoftware::PresentFrame()`
  (`RendererSoftware.cpp`) called `SDL_UpdateTexture(m_texture, ...)` unconditionally every frame,
  even though the very next block already skips drawing `m_texture` at all when
  `RendererScreenOwned()` is true (every migrated frontend screen *and* the in-game parchment map,
  not just "pure-frontend" as this bullet assumed) — a full framebuffer CPU→GPU upload (~8MB at
  1080p) discarded unread, every such frame. Fixed by reading `RendererScreenOwned()` once and
  gating the upload on the same condition that already gated the draw, so a frame either
  uploads-and-draws `m_texture` or does neither — no path where a drawn texture can be stale.

## B2 — Capture path: screenshots and movie recording (now blocking) — **landed 2026-09-23**

Stage 4 left this as a footnote; Phase B makes the gap unavoidable, because from here on more and
more of what's on screen exists only in the `SDL_Renderer`, never in `lbDrawSurface`. Two separate
problems, both surfaced by stage 2/4 and neither yet addressed:

- **Screenshots miss the overlay.** `RendererSoftware::ScheduleScreenshot` saves `lbDrawSurface`
  (the CPU backdrop only). It must instead capture **after** the ImGui/GPU composite:
  `SDL_RenderReadPixels` on `m_renderer` after `ImGuiContextRender()` and *before*
  `SDL_RenderPresent` (some backends leave the backbuffer undefined after present), into an
  `SDL_Surface` that `IMG_SavePNG`/`SDL_SaveBMP` then writes — with an explicit format/row-order
  conversion, since `SDL_RenderReadPixels` returns the renderer's native format, not necessarily
  `RGBA32`. The read-back belongs in the **outer** present only (respect `s_presenting_imgui_frame`).
  There is no ImGui-disabled fallback mode left to keep the old `lbDrawSurface`-only capture path
  for (`-classicmenu` and `RendererImGuiEnabled()`'s toggle are both retired) — drop the old path
  outright rather than keeping it "for compatibility."

  **Landed as:** `RendererScheduleScreenshot()` now only validates and queues the request (path +
  format, in new `RendererSoftware` members); the real `SDL_RenderReadPixels` → `SDL_ConvertSurface`
  (to `RGBA32`, exactly as this bullet anticipated) → `IMG_SavePNG`/`SDL_SaveBMP` sequence runs from
  a new `perform_pending_screenshot()`, called from `PresentFrame()` right after
  `renderer_imgui_callbacks->render()`, inside the reentrancy-guarded block (so a nested present,
  which skips ImGui submission, never captures an overlay-less frame — the request just stays
  queued and retries on the next outer present). The return value's meaning changed accordingly:
  `true` now means "queued", not "saved" — see `RendererManager.h`'s updated comment. The
  `scrcapt.c:46-62` lock/unlock dance is gone, per the bullet below.
- **FLC movie recording is already dead, not just incomplete.** `anim_record()`
  (`bflib_fmvids.cpp:1408`) hard-fails with *"Cannot record movie in non-8bit screen mode"* because
  `LbGraphicsScreenBPP()` now returns 32, and the `.flc`/FLI codec is intrinsically 8-bit-palettized.
  `movie_record_start` → `GSF_CaptureMovie` therefore does nothing useful today. This stage must
  **decide**: (a) formally retire FLC recording and delete `anim_record*`/`movie_record_*`/the
  `cap_palette` plumbing, or (b) re-implement capture on top of the same post-composite
  `SDL_RenderReadPixels` path — a PNG/BMP frame sequence, or frames piped to the ffmpeg encoder the
  build already links (`Dependencies.cmake` builds ffmpeg from source for Smacker *decode*; an
  encoder is a bigger ask). Option (a) is the honest default unless someone actually wants gameplay
  video capture back.

  **Landed as: option (a), confirmed with the user before implementing** (this bullet's "honest
  default" recommendation, not assumed silently). `bflib_fmvids.cpp`'s entire FLI-encoder half
  (everything below `play_smk()`, a clean contiguous block — `anim_record`/`anim_stop`/
  `anim_record_frame` and their `anim_make_FLI_*`/`anim_open`/`anim_write_data` helpers) is deleted,
  along with `movie_record_start/stop/frame` and `cap_palette` in `scrcapt.c`. `Gkey_ScreenRecord`
  (the Shift+M keybinding) stays defined and bound rather than renumbering every `Gkey_*` enum value
  after it in `globals.h` — pressing it is just a consumed no-op now (`front_input.c`). Likewise
  `GSF_CaptureMovie` stays defined in `kfx_sim_state.h` (a plain bitmask literal, not auto-numbered,
  so nothing to gain from deleting it) but is set nowhere any more.
- `perform_any_screen_capturing()` (`scrcapt.c:142`) runs *before* present
  (`game_session_loop.cpp:430`, `frontend.cpp:3736`) and draws the "REC" indicator into the CPU
  buffer via `LbTextDraw`. A post-composite capture inverts that ordering — the REC indicator must
  move to an ImGui draw (or a second composited pass), or it won't appear in captured frames.

  **Landed as: dropped, not ported.** The "REC" flash only ever fired alongside
  `cumulative_screen_shot()`'s own `show_onscreen_msg()` call (a "File X saved"/"Cannot save"
  message) — and since movie recording is retired (previous bullet), screenshots are the only
  remaining trigger. That message is already ImGui-composited for every migrated in-game session
  (`frontgui_ingame_text.cpp`'s text overlay) and therefore already visible in a post-composite
  screenshot; a second, purely decorative confirmation would need the same new cross-layer ImGui
  plumbing (`kfx_render` → `kfx_frontend`) this bullet flagged as one option, for no benefit over
  what already exists. Removed rather than built.
- The `take_screenshot()` / `LbScreenIsLocked()` / `RendererLockFramebuffer()` dance
  (`scrcapt.c:46-62`) is meaningless for a GPU read-back — remove it rather than leave confusing
  dead lock/unlock code.

  **Landed as:** `take_screenshot()` is now a one-line forward to `RendererScheduleScreenshot()`.

## B3 — In-game HUD / GUI onto the GPU — **DONE, via a separate project; the remainder is a permanent non-goal**

Originally scoped as an either/or ("wait for an ImGui port" vs. "build a thin GPU 2D compositor").
Route 1 is what happened: the
[in-game-GUI-as-ImGui project](../../ingame-gui/00-overview.md) shipped ahead of this stage,
migrating the sidebar, tab content, query panels, save/load, options, event/battle boxes, the
parchment map, and the first-person HUD onto the same `imgui_impl_sdlrenderer3` layer the frontend
menus already use. There is nothing left for B3 to build.

What's different from the original plan: that project's own scope decision (§8 of its overview)
kept the **legacy sprite HUD** (`GUI_ICON_PACK=CLASSIC`) as a permanent, deliberately-CPU-only
style choice rather than deleting it once ImGui coverage was complete — "a deliberate live-tested
request, not a leftover." That means:

- **Route 2 (a thin GPU 2D compositor for the legacy sprite HUD) is now a non-goal, not just
  unneeded today.** Nobody is going to ask "when does `CLASSIC` get GPU-composited" — the answer is
  never, by design. Drop route 2 from this stage's scope entirely rather than carrying it as
  deferred work.
- **The sprite→GPU-texture cache below is no longer motivated by an in-game HUD compositor.** Its
  remaining justification is Phase C's world-view textures, which is sufficient on its own — see
  [03-pixel-format-and-texture-cache.md](03-pixel-format-and-texture-cache.md).
- **Phase C inherits a real question the original B3 write-up never had to ask**: what happens to
  `CLASSIC`'s CPU sprite draws — which assume they're compositing in-place onto a `lbDrawSurface`
  that already contains the rendered 3D scene — once Phase C moves the 3D view onto its own GPU
  texture and `lbDrawSurface` stops containing it? See
  [04-architecture-and-ir-boundary.md](04-architecture-and-ir-boundary.md) and R13.

## Sprite → GPU-texture cache (needed by Phase C) {#sprite-texture-cache}

`RendererCreateDynamicTexture` is per-frame streaming, not a keyed cache. Build a real cache keyed
on **sprite pointer + expansion palette + remap table** (extending stage 2 §2's per-draw expansion
cache if one was built), producing static textures drawn each frame. `TbSprite` data is still
8-bit-indexed RLE on disk and in memory (stage 2 expands per-draw, not on load), so the cache's fill
step is "expand indices through the active palette/remap into RGBA, upload once, reuse until the
sprite or palette changes". Invalidation on palette change is the tricky part — frontend fades and
in-game lighting both mutate the active palette (see R10).

**This is also Phase C's texture source.** Phase C's world-view renderer needs the identical
RGBA-resolved-once shape for tile/wall textures as this cache already plans for sprites. Build one
cache with two population paths (sprite expansion, static level-texture expansion), not two
differently-shaped caches with separate invalidation rules. Full design (including how this
interacts with the chosen backend API's texture-object type) is in
[03-pixel-format-and-texture-cache.md](03-pixel-format-and-texture-cache.md).

## Cursor — **landed 2026-09-23**

Stage 4 gave the problem a proven pattern (off-screen software render → dynamic texture → draw
list), currently used only for the ImGui-overlay cursor. Fold the legacy
`bflib_mspointer.cpp`/`LbScreenSurfaceBlit` path into the same mechanism so there is **one** cursor
draw, composited on the GPU at present time, at the game's tracked position — removing the
unlocked-`lbDrawSurface` bypass and the current double-cursor-draw. This is stage 4's Part-D
"give the cursor its own `IUIRenderer`-routed draw call" recommendation, now with infrastructure to
do it against.

**Landed as:** see [08-risks.md](08-risks.md) R7 for the full "what was actually found live" writeup
(the legacy draw path turned out to already be entirely dead code, not just redundant, and the two
gates were a real one-frame race, not just an architectural smell). Summary of the change:
- `bflib_mspointer.cpp`/`.hpp`'s `LbI_PointerHandler` now only tracks position/sprite/hotspot state
  (`Initialise`/`Release`/`SetHotspot`/`ClipHotspot`) — its CPU-buffer drawing
  (`Draw`/`Backup`/`Undraw`/the free `PointerDraw` function/`OnBeginSwap`/`OnEndSwap`/`OnMove`'s
  redraw branch/`NewMousePos`) is gone, along with the `LbCursorSpriteSetScaling*` wrappers and
  `cursor_xsteps_array`/`cursor_ysteps_array` those alone used.
- `bflib_mshandler.cpp`/`.hpp`'s `MouseStateHandler::PointerBeginSwap`/`PointerEndSwap` (which only
  ever bracketed that draw) are gone, and `SetPosition`/`SetPointer` no longer call into the
  now-removed `LbI_PointerHandler::OnMove`.
- `LbMouseOnBeginSwap`/`LbMouseOnEndSwap` (`bflib_mouse.h`/`.cpp`) are gone, along with their call
  sites in `RendererSoftware::PresentFrame()`.
- `bflib_vidsurface.c`/`.h`'s `LbScreenSurface*()`/`struct SSurface` family (the CPU-buffer draw's
  own off-screen-surface RAII+blit helper) is gone too — confirmed unused anywhere else first.
  `lbDrawSurface` itself (defined in the same file) is untouched.
- `gui/FrontendImGui.cpp`'s cursor draw is unconditional now (gated only on a valid cursor image
  existing, via `s_cursor_have`) instead of `io.WantCaptureMouse || FrontendImGuiScreenOwned()` —
  there is no more legacy fallback to defer to on the rest of the screen.
- `RendererWantCaptureMouse()` (`RendererManager.h`/`.cpp`) — the legacy draw's one remaining
  caller — is retired too; `renderer_imgui_callbacks->want_capture_mouse` itself stays in the
  struct.
- `bflib_mspointer_test.cpp` (only ever testing the now-removed `LbCursorSpriteSetScaling*`
  wrappers) is deleted; `RendererManager_test.cpp` updated to match.

## B — verification

- B3 is verified as part of the in-game-GUI-as-ImGui project itself, not here — nothing further to
  check for it in this stage. Frame-time comparison against the current build still applies to
  B1/B2.
- Screenshots and recorded movies must now contain the ImGui overlay — that is the B2 acceptance
  test, and it should also cover an in-game frame with the migrated HUD visible, not just the
  frontend.
- `GUI_ICON_PACK=CLASSIC` sessions unaffected by B1/B2/cursor changes.

**B1 landed as:** native Linux build, mingw Windows cross-compile, `check_layering.py --strict`,
and the full Catch2 suite (1939 tests) are all clean. No behaviour change — `m_texture` is now
uploaded if and only if it's drawn this frame, same as before for the non-`RendererScreenOwned()`
path, and both skipped together for the `RendererScreenOwned()` path (previously: uploaded but not
drawn). **Not verified:** an actual frame-time measurement of the saved upload cost — no display in
the implementing environment to profile against; the change is a straightforward skip of dead work
(the exact bytes that used to go into `m_texture` on a screen-owned frame were never read by
anything), not a behaviour change requiring interactive confirmation, but a live frame-time number
would still be worth capturing before calling B1's motivation ("real profiling") fully addressed.

**B2 landed as:** same four checks clean (both toolchains, layering, full Catch2 suite). **Not
verified:** an actual screenshot taken from a live session and inspected for the ImGui overlay —
this is exactly the acceptance test this section already calls for, and it needs a real display and
game data neither of which are available in the implementing environment. The capture path change
is mechanical (read back what the renderer already composited, instead of reading a CPU buffer that
no longer holds the full picture) and the movie-recording retirement removes only code that has
been unconditionally unreachable since stage 2 (confirmed by reading `anim_record()`'s own guard,
not assumed) — but "the screenshot file actually contains the overlay" is a claim only a real
screenshot can confirm, and that run is still outstanding.

**Cursor unification landed as:** same four checks clean (both toolchains, layering, full Catch2
suite, 1934 tests — 5 fewer than B1/B2's count, matching the deleted `bflib_mspointer_test.cpp`
cases for the now-removed scaling helpers). `CLASSIC` sessions are unaffected: `FeStyleGetCursorImage()`
branches on `kfx_sim_state.game_kind`, not on `ingame_gui_use_classic_hud()`, so the same in-game
sprite feeds the (now sole) cursor draw regardless of which HUD style is active. **Not verified:** an
actual interactive session confirming a single, correctly-positioned cursor across the frontend, the
3D view, and every migrated ImGui screen — this section's own "screenshot and recorded movies" bar
extends naturally to "and show exactly one cursor," and that still needs a real display/game data to
confirm, same gap as B2's own outstanding verification above.

See [09-verification.md](09-verification.md) for the full cross-phase verification baseline.
