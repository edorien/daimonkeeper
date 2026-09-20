# Phase B — 2D compositing on the GPU

See [00-overview.md](00-overview.md) for stage status and shared context.

The original Phase B was "route `IUIRenderer`/`ITextRenderer` through the GPU, with ImGui as one
candidate implementation". That framing is retired. The frontend 2D layer is already GPU-composited
via ImGui/SDLRenderer3. What remains is a short list of concrete, mostly independent items.

## B1 — Frontend backdrop and residual CPU present cost (small)

- `frontend_copy_background()` still paints a full-screen backdrop into `lbDrawSurface` every
  frontend frame, which is then `SDL_UpdateTexture`'d wholesale. For a mostly-static backdrop this
  is a full-resolution CPU fill + full-frame upload per frame behind a UI that changes little.
  Option: upload the backdrop once as a dynamic/static ImGui texture and draw it via
  `GetBackgroundDrawList()`, skipping the software fill and the per-frame texture upload on frames
  where neither backdrop nor 3D content changed. Stage 4 §3.4 deliberately deferred this ("only
  worthwhile when a screen needs the backdrop sampled/tinted per-widget"); revisit here with real
  profiling, since this stage is where per-frame upload cost is the subject.
- Decide whether `m_texture` upload can be skipped entirely on pure-frontend frames (no 3D view,
  backdrop unchanged) — i.e. present ImGui over a cleared/again-textured target without re-uploading
  an unchanged CPU framebuffer.

## B2 — Capture path: screenshots and movie recording (now blocking)

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
- `perform_any_screen_capturing()` (`scrcapt.c:142`) runs *before* present
  (`game_session_loop.cpp:430`, `frontend.cpp:3736`) and draws the "REC" indicator into the CPU
  buffer via `LbTextDraw`. A post-composite capture inverts that ordering — the REC indicator must
  move to an ImGui draw (or a second composited pass), or it won't appear in captured frames.
- The `take_screenshot()` / `LbScreenIsLocked()` / `RendererLockFramebuffer()` dance
  (`scrcapt.c:46-62`) is meaningless for a GPU read-back — remove it rather than leave confusing
  dead lock/unlock code.

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

## Cursor

Stage 4 gave the problem a proven pattern (off-screen software render → dynamic texture → draw
list), currently used only for the ImGui-overlay cursor. Fold the legacy
`bflib_mspointer.cpp`/`LbScreenSurfaceBlit` path into the same mechanism so there is **one** cursor
draw, composited on the GPU at present time, at the game's tracked position — removing the
unlocked-`lbDrawSurface` bypass and the current double-cursor-draw. This is stage 4's Part-D
"give the cursor its own `IUIRenderer`-routed draw call" recommendation, now with infrastructure to
do it against.

## B — verification

- B3 is verified as part of the in-game-GUI-as-ImGui project itself, not here — nothing further to
  check for it in this stage. Frame-time comparison against the current build still applies to
  B1/B2.
- Screenshots and recorded movies must now contain the ImGui overlay — that is the B2 acceptance
  test, and it should also cover an in-game frame with the migrated HUD visible, not just the
  frontend.
- `GUI_ICON_PACK=CLASSIC` sessions unaffected by B1/B2/cursor changes.

See [09-verification.md](09-verification.md) for the full cross-phase verification baseline.
