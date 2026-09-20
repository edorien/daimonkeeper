# Stage 3 — GPU acceleration

> **Superseded (2026-09-13, split 2026-09-13c).** This document was merged into
> `gpu-v2/03-gpu-renderer.md`, then split into per-concern sub-plans starting at
> [`gpu-v2/00-overview.md`](gpu-v2/00-overview.md), which is now the single entry point for this
> stage. That split also added a graphics-API recommendation
> ([`gpu-v2/02-graphics-api-choice.md`](gpu-v2/02-graphics-api-choice.md): SDL_GPU, not raw
> OpenGL/Vulkan) and a call-site-consolidation prerequisite
> ([`gpu-v2/06-call-site-consolidation.md`](gpu-v2/06-call-site-consolidation.md)) that this file
> never had. Read `gpu-v2/00-overview.md` instead; this file is kept for history and is not being
> updated further.

Status: **Phase A landed** (with stage 2 — see below); **Phase B substantially overtaken by
stage 4 for the frontend, re-scoped around what's left**; Phase C unchanged (still a separately
scoped stretch goal).

Depends on [02-32bit-software-renderer.md](02-32bit-software-renderer.md) (**landed** — `TbPixel`
is true-colour RGBA, `lbDrawSurface` is `SDL_PIXELFORMAT_RGBA32`). See
[00-overview.md](00-overview.md) for context.

> **Revision note (2026-09-06).** This doc was written before stage 4
> ([04-imgui-gui-foundation.md](04-imgui-gui-foundation.md)) landed. Stage 4 did far more than
> "evaluate ImGui as a rendering backend behind `IUIRenderer`": it reversed its own recommendation
> and migrated the **entire main-menu frontend** (all 15 `GuiMenu`s + the backdrop-plus-text
> screens, Phases A–G) to real Dear ImGui widgets, rendered through `imgui_impl_sdlrenderer3` into
> the *same* `SDL_Renderer` this stage's present path already owns. Phase B's original task list —
> "give `IUIRenderer`/`ITextRenderer` a second, GPU implementation; evaluate ImGui as that
> implementation" — is now the wrong shape: for the frontend it is **already done**, by a different
> and better mechanism than a hand-rolled quad submitter; for the in-game GUI it is deferred by
> stage 4's own §2.3 ("in-game GUI is a separate, longer-term project, revisited after the GPU
> work"). §"What stage 4 already built" records the new starting point; Phases B and C are rewritten
> against it.

## Goal

Move drawing work onto the GPU without a single risky rewrite. Phased, each phase independently
shippable and each smaller than the last major stage.

---

## What stage 4 already built (the new starting point)

Everything in this list is live in the shipped build today, on every presented frame (frontend
**and** in-game — `RendererImGuiEnabled()` is a session-global set once from `!use_classic_menu()`
in `main.cpp`, not a per-state gate):

- **One graphics context, already GPU-accelerated.** `RendererSoftware` owns an `SDL_Renderer`
  (`m_renderer`) and an `SDL_Texture` (`m_texture`, `SDL_PIXELFORMAT_RGBA32`).
  `PresentFrame()` (`RendererSoftware.cpp`) does: `SDL_UpdateTexture(m_texture, …, lbDrawSurface)`
  → `SDL_RenderClear` → `SDL_RenderTexture` (the CPU framebuffer, full-screen) → **ImGui draw data
  composited on top** (`ImGuiContextNewFrame` / `RendererRunImGuiFrameCallback` / `ImGuiContextRender`)
  → `SDL_RenderPresent`. SDL3's renderer is hardware-accelerated (Direct3D/Vulkan on Windows,
  GL/Vulkan on Linux).
- **The CPU framebuffer is the backdrop layer; ImGui is a true overlay.** This is *exactly* the
  hybrid the old Phase B described as its stopping point ("3D/software content uploaded as one
  texture, composited underneath the GPU-drawn 2D layer") — it already exists, for the menus.
- **`kfx_platform/include/gui/ImGuiContext.{h,cpp}`** owns ImGui + `ImGui_ImplSDL3_*` +
  `ImGui_ImplSDLRenderer3_*` lifecycle, window/renderer change detection (kept in step with
  `ensure_present_target`), the SDL event feed (from `LbPollInputs`), and the `NewFrame`/`Render`
  pair. Nothing above `kfx_platform` includes `imgui.h` for lifecycle.
- **`RendererManager` façades already in place** (all `extern "C"`, all documented in
  `RendererManager.h`):
  - `RendererImGuiFrameFn` / `RendererSetImGuiFrameCallback` / `RendererRunImGuiFrameCallback` —
    `PresentFrame` calls up into a registered submission function (`kfx_frontend`'s
    `FrontendImGuiFrame`) between `NewFrame` and `Render`, with no upward include.
  - `RendererSwapFramebufferTarget(target,w,h)` / `RendererRestoreFramebufferTarget(prev)` — point
    the software raster at an off-screen buffer and back. Saves/restores
    `GraphicsScreenWidth`/`Height` too (a stride bug fixed during stage 4, `318d55f2c`). Used by the
    eye-lens effect and by stage 4's off-screen captures.
  - `RendererCreateDynamicTexture(w,h)` / `UpdateDynamicTexture` / `DestroyDynamicTexture` — CPU
    RGBA → `SDL_TEXTUREACCESS_STREAMING` `SDL_Texture` → opaque handle castable to `ImTextureID`.
    Used for stage 4's land-preview panel (`land_preview_draw()` rendered off-screen, uploaded,
    `ImGui::Image()`'d into the layout every frame).
  - `RendererSetCursorImageCallback` (`ImGuiCursorImageFn`) — the frontend's real cursor sprite,
    rendered off-screen through `RendererSwapFramebufferTarget`, drawn over ImGui content via
    `GetForegroundDrawList()->AddImage()` while `WantCaptureMouse`.
  - `RendererSetMousePositionCallback` (`ImGuiMousePositionFn`) — feeds ImGui the game's own
    tracked cursor position instead of raw SDL motion (the game warp-grabs the OS cursor).
- **A reentrancy guard in `PresentFrame`** (`s_presenting_imgui_frame`): palette-fade transitions
  re-enter `PresentFrame` per animation step; nested calls do the plain SDL backdrop blit only and
  skip ImGui. Any new nested-present path this stage adds must respect the same guard.
- **Text is already GPU glyph-atlas rendering** for the frontend — ImGui 1.92's dynamic font
  system (`ImGuiBackendFlags_RendererHasTextures`) rasterises TTF glyphs on demand at the exact
  size asked for; `FeStylePushFont()` derives size from `io.DisplaySize.y`. No baked atlas, no
  `ITextRenderer` GPU implementation needed for the menus.
- **Cross-platform `SDL_Renderer` exposure is already paid down.** `imgui_impl_sdlrenderer3`
  forces the mingw-w64/Windows and native-Linux `SDL_Renderer` paths that this stage's verification
  section calls for; stage 4 has been exercising both.

### What is still genuinely CPU-rastered every frame

- **The 3D dungeon view** — `engine_render.c`'s bucketed `do_a_gpoly_*` rasterizer → `vec_screen`
  → `lbDrawSurface`. Unchanged. This is Phase C.
- **The in-game GUI / HUD** — `draw_gui()`, `get_gui_inputs()`, the `frontmenu_ingame_*` menus,
  parchment, tooltips, tabs, box menus. Stage 4 explicitly kept all of this on the sprite path
  (§2.3). This is the real remaining Phase B target.
- **The frontend backdrop** — `frontend_copy_background()` still draws the full-screen backdrop
  through the software path; ImGui draws over it. Migrated screens skip `draw_gui()` but still call
  `frontend_copy_background()`.
- **Non-migrated / opt-out frontend paths** — `FeSt_LAND_VIEW`, `FeSt_NETLAND_VIEW`, `FeSt_TORTURE`,
  Smacker video states, and everything under `-classicmenu`.
- **The legacy mouse cursor** — `bflib_mspointer.cpp`'s `LbI_PointerHandler` still composites the
  software cursor via `LbScreenSurfaceBlit` (`SDL_BlitSurface` against `lbDrawSurface`), a parallel
  path, still **unlocked**. Stage 4 added a *second* cursor draw (the ImGui-overlay one) rather than
  replacing this.
- **Lens effects** — `LensManager`/`LensEffect` (`MistEffect`/`FlyeyeEffect`/`OverlayEffect`/
  `DisplacementEffect`/`PaletteEffect`) as a CPU post-process over the framebuffer. Phase C.
- **Screenshot & movie capture** — `RendererSoftware::ScheduleScreenshot` still saves
  `lbDrawSurface` (the CPU buffer, backdrop only); `perform_any_screen_capturing()` runs before
  present. Neither sees the ImGui overlay. Stage 4 flagged this and did not fix it — it is now
  **blocking for this stage** (see Phase B2).

---

## Phase A — Direct texture upload — **DONE**

Landed with stage 2 (`13442110f`, "Flip lbDrawSurface to true-colour"). `PresentFrame()`'s old
lock/blit/unlock `SDL_BlitSurface` INDEX8→RGBA conversion is gone; it is now a single
`SDL_UpdateTexture(m_texture, NULL, lbDrawSurface->pixels, lbDrawSurface->pitch)` straight from the
already-RGBA CPU framebuffer. Nothing further to do here.

---

## Phase B — 2D compositing on the GPU

The original Phase B was "route `IUIRenderer`/`ITextRenderer` through the GPU, with ImGui as one
candidate implementation". That framing is retired. The frontend 2D layer is already GPU-composited
via ImGui/SDLRenderer3. What remains is a short list of concrete, mostly independent items.

### B1 — Frontend backdrop and residual CPU present cost (small)

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

### B2 — Capture path: screenshots and movie recording (now blocking)

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
  Keep the `lbDrawSurface` path only as a `-classicmenu` / ImGui-disabled fallback, or drop it.
- **FLC movie recording is already dead, not just incomplete.** `anim_record()`
  (`bflib_fmvids.cpp:1408`) hard-fails with *"Cannot record movie in non-8bit screen mode"* because
  `LbGraphicsScreenBPP()` now returns 32 (`SDL_BITSPERPIXEL(lbDrawSurface->format)`,
  `bflib_video.c:167`), and the `.flc`/FLI codec is intrinsically 8-bit-palettized
  (`anim_make_FLI_COLOUR256` etc.). `movie_record_start` → `GSF_CaptureMovie` therefore does
  nothing useful today. This stage must **decide**: (a) formally retire FLC recording and delete
  `anim_record*`/`movie_record_*`/the `cap_palette` plumbing, or (b) re-implement capture on top of
  the same post-composite `SDL_RenderReadPixels` path — a PNG/BMP frame sequence, or frames piped to
  the ffmpeg encoder the build already links (`Dependencies.cmake` builds ffmpeg from source for
  Smacker *decode*; an encoder is a bigger ask). Option (a) is the honest default unless someone
  actually wants gameplay video capture back.
- `perform_any_screen_capturing()` (`scrcapt.c:142`) runs *before* present
  (`game_session_loop.cpp:430`, `frontend.cpp:3736`) and draws the "REC" indicator into the CPU
  buffer via `LbTextDraw`. A post-composite capture inverts that ordering — the REC indicator must
  move to an ImGui draw (or a second composited pass), or it won't appear in captured frames.
- The `take_screenshot()` / `LbScreenIsLocked()` / `RendererLockFramebuffer()` dance
  (`scrcapt.c:46-62`) is meaningless for a GPU read-back — remove it rather than leave confusing
  dead lock/unlock code.

### B3 — In-game HUD / GUI onto the GPU (the real remaining target)

`draw_gui()` and the `frontmenu_ingame_*` menus, parchment, tooltips, tabs and box menus are still
100% CPU sprite/box/text writes into `lbDrawSurface`. Two routes, decide when this is scoped in
detail:

1. **Wait for the in-game-GUI-as-ImGui project.** Stage 4 §2.3 / §10 already commit to revisiting
   the in-game GUI "after the GPU work" as real ImGui widgets (it is gameplay-critical,
   latency-sensitive, sits over live 3D, and its chrome is entangled with game state — a port, not
   a reskin). If that project happens, B3 is subsumed: the in-game HUD composites through the same
   `imgui_impl_sdlrenderer3` layer the menus already use, over the 3D-view texture, and there is
   nothing separate for this stage to do. **This is the recommended default** — it avoids building a
   throwaway compositor.
2. **A thin GPU 2D layer under `draw_gui()`, if B3 is wanted before that project.** Give
   `IUIRenderer`/`ITextRenderer` a real second implementation that submits into the live ImGui
   frame's draw lists (`GetBackgroundDrawList()`/`GetForegroundDrawList()` — ImGui is already
   new-framed every present, in-game included) or directly via `SDL_RenderTexture`/
   `SDL_RenderGeometry` after `ImGuiContextRender`. This needs the sprite→texture cache below and
   the shade/blend math from stage 2 §2a as fragment-side work. It is the harder path and produces
   an implementation that route 1 would later replace — only take it if in-game HUD GPU compositing
   is needed on a timeline that route 1 can't meet.

### Sprite → GPU-texture cache (needed by B3 route 2 and by Phase C)

`RendererCreateDynamicTexture` is per-frame streaming, not a keyed cache. Build a real cache keyed
on **sprite pointer + expansion palette + remap table** (extending stage 2 §2's per-draw expansion
cache if one was built), producing static `SDL_Texture`s drawn via `SDL_RenderTexture`. `TbSprite`
data is still 8-bit-indexed RLE on disk and in memory (stage 2 expands per-draw, not on load), so
the cache's fill step is "expand indices through the active palette/remap into RGBA, upload once,
reuse until the sprite or palette changes". Invalidation on palette change is the tricky part —
frontend fades and in-game lighting both mutate the active palette.

### Cursor

Stage 4 gave the problem a proven pattern (off-screen software render → dynamic texture → draw
list), currently used only for the ImGui-overlay cursor. Fold the legacy
`bflib_mspointer.cpp`/`LbScreenSurfaceBlit` path into the same mechanism so there is **one** cursor
draw, composited on the GPU at present time, at the game's tracked position — removing the
unlocked-`lbDrawSurface` bypass and the current double-cursor-draw. This is stage 4's Part-D
"give the cursor its own `IUIRenderer`-routed draw call" recommendation, now with infrastructure to
do it against.

### B — verification

- Screenshot-diff across every **in-game** GUI screen if B3 route 2 is taken (route 1 is verified
  as part of that project, not here); frame-time comparison against the current build for B1/B2.
- Screenshots and recorded movies must now contain the ImGui overlay — that is the B2 acceptance test.
- `-classicmenu` sessions unaffected by B1/B2/cursor changes.

---

## Phase C — 3D polygon rasterizer on the GPU (stretch goal, separately scoped)

Largely unchanged from the original plan; the compositing side is now settled by stage 4.

Porting `engine_render.c`'s bucketed gouraud/textured polygon fill (`do_a_gpoly_*` family, via
`bflib_render_gpoly.c` writing `vec_screen`) to real GPU draw calls (vertex/index buffers +
`SDL_RenderGeometry` or a raw GL/Vulkan pass + a fragment shader replicating stage 2's blend math)
is substantially larger and riskier than Phases A/B:

- **Visibility ordering.** The bucket/sort structure (`engine_arrays.c`,
  `engine_render_data.cpp`) exists because software rasterization needs explicit ordering; a GPU
  path would use depth-buffering instead — a different-enough algorithm that this isn't a
  mechanical port. It's closer to a from-scratch 3D renderer that must reproduce the original's
  specific visual behaviour.
- **The shade/blend model.** Stage 2 replaced the 8-bit `fade_tables`/`ghost`/`alpha_sprite_table`
  lookups with real per-pixel blend math (see [02a-pixel-format-design.md](02a-pixel-format-design.md)) —
  that math is now the fragment-shader spec, which is a genuine simplification versus the state this
  doc was originally written in (it no longer has to reverse-engineer table semantics).
- **Compositing is already solved.** A GPU 3D pass renders into its own `SDL_Texture` /
  render target, which then takes the exact place `m_texture` (the uploaded CPU framebuffer)
  occupies in `PresentFrame` today: drawn first, full-screen, with the ImGui/GUI layer composited
  on top. No new present architecture — one `SDL_RenderTexture` source swaps for another.
- **Lens effects** (`LensManager`/`LensEffect`) currently operate on the CPU framebuffer as a
  post-process. Each of `MistEffect`/`FlyeyeEffect`/`OverlayEffect`/`DisplacementEffect`/
  `PaletteEffect` needs either its own GPU-shader equivalent, or a documented decision to keep lens
  effects as a CPU post-pass over the read-back GPU frame (`RendererSwapFramebufferTarget` already
  gives lens code a clean off-screen seam, and the `SDL_RenderReadPixels` path from Phase B2 makes
  "GPU frame → CPU buffer → lens pass → re-upload" mechanically available). `PaletteEffect` in
  particular is trivially a shader uniform in an RGBA world.
- Recommend: **do not scope this in detail until Phases A/B have shipped and been played with.**
  Phase B alone (in-game HUD off the CPU, 3D view still correct) plus stage 2's true-colour path may
  be sufficient for KeeperFX's actual performance needs, given the fixed low-poly art style and that
  the CPU-rasterized 3D view already runs acceptably. Revisit Phase C's cost/benefit once real
  profiling data exists from Phase B's shipped state.

---

## `RendererType` and backend selection

The original plan said `enum RendererType` (`IRenderer.h`) gains a `RENDERER_GPU` value and
`create_renderer()` (`RendererManager.cpp:32`) gains a matching case, keeping `RENDERER_SOFTWARE`
alive as a correctness reference.

That framing needs revising: **there is no longer a clean "software vs. GPU backend" split.**
`RendererSoftware` already owns an `SDL_Renderer`, already GPU-composites (ImGui), and already
presents through hardware. What each phase actually changes is *which draw categories flow through
the `SDL_Renderer` that class already has* — not a swap to a parallel backend class.

- **Phase B** (B1/B2/B3-route-2, cursor): evolve `RendererSoftware` in place. Consider renaming it
  (`RendererSDL`?) once "software" is no longer accurate, but that is cosmetic and can wait.
- **Phase C**: *this* is where a real second backend or a real config switch earns its place —
  "CPU 3D rasterizer" vs "GPU 3D rasterizer" is a genuine either/or with real correctness-reference
  value. Add the `RendererType` value and the `create_renderer()` case *then*, scoped to the 3D
  path, not now. Keep the CPU 3D path selectable (via `RendererInit`) as the diff target and the
  fallback for driver combinations the GPU 3D path doesn't handle.
- `-classicmenu` provided a "no ImGui, CPU-composite frontend" mode for bisecting 2D-layer
  regressions -- **retired 2026-09-12** (docs/refactor/ingame-gui/00-overview.md §1/§8), the
  frontend has no legacy path left to fall back to at all, so this specific bisect mode no longer
  exists. `GUI_ICON_PACK=CLASSIC` (`ingame_gui_use_classic_hud()`) still forces the *in-game HUD*
  to its legacy sprite renderer, which may or may not be a close enough substitute for whatever
  Phase B needed this for -- revisit when Phase B is actually picked up.

Update [architecture.md](../../Architecture/architecture.md) §2.1/§2.4 if/when Phase C adds the
`RendererType` value and changes `IRenderer`'s framebuffer contract.

---

## Non-goals for this stage

- **No change to `kfx_frontend`'s screen/menu authoring.** Phase B changes what happens *behind*
  the draw calls, not how screens are authored (same principle as stage 1). Note that for the
  frontend, stage 4 already moved authoring to ImGui — this stage does not touch it further.
- **The in-game-GUI-as-ImGui port is not this stage's job.** This stage either waits for it
  (B3 route 1) or builds a thin interim compositor (B3 route 2); the port itself is separate work,
  as stage 4 §2.3 records.
- **Phase C is explicitly not committed** — see above.

---

## Risks and things that will bite

Found by reading the current code (2026-09-06), roughly worst-first. Several are pre-existing
consequences of stages 2/4 that this stage is simply the first to *have* to resolve.

### R1 — The present path is called from ~20 sites, in many libraries, and each one runs a full ImGui frame

`RendererPresentFrame()` call sites, all of which now trigger
`ImGuiContextNewFrame` → `RendererRunImGuiFrameCallback` → `ImGuiContextRender`:
`kfx_apploop/game_session_loop.cpp` (×7), `kfx_net/net_exchange_gameplay.c`,
`kfx_net/packets_misc.c` (pause/unpause resync), `kfx_frontend/{front_fmvids,front_landview,
front_network,front_simple,frontend}.cpp/.c`, `kfx_platform/{bflib_video.c,bflib_fmvids.cpp}`,
`kfx_render/vidmode.c` (×2).

- The `s_presenting_imgui_frame` reentrancy guard only *suppresses* ImGui on nested calls — a
  nested present shows a frame with **no UI overlay**. That's fine for palette-fade steps (what it
  was built for); it may not be fine for a nested present during a cutscene or a net resync.
- `RendererRunImGuiFrameCallback()` also drains `s_pending_state` / `s_pending_load_slot` /
  `s_pending_action` (`frontgui_screens.cpp`). Those now execute from **whichever present fires
  first each frame** — including a resync present deep inside `kfx_net`. Any Phase B in-game HUD
  submission added to that callback inherits this: it must be null-safe when invoked re-entrantly,
  mid-netsync, and mid-cutscene, not just from the two normal loop bodies.
- Anything Phase B/C adds to `PresentFrame` (a capture read-back, a second composited pass, an
  in-game HUD submit) must be explicitly audited against every one of these call sites, not just
  the two obvious ones.
- **Mitigation, sequenced ahead of Phase B:**
  [05-imgui-linkage-consolidation.md](05-imgui-linkage-consolidation.md) moves all ImGui knowledge
  into `kfx_frontend` behind one `RendererOverlayCallbacks` struct, so the overlay begin/submit/
  render is a single choke point and Phase B's additions live in one library instead of straddling
  the `kfx_platform`/`kfx_frontend` seam. It does *not* reduce the call-site count itself (that's a
  separate optional cleanup, noted there).

### R2 — `SDL_Renderer` is single-threaded; keep every present on the main thread

SDL's renderer API is main-thread-only. `kfx_net` spawns `SDL_Thread`s
(`net_matchmaking.c`, `net_portforward.cpp`) but those do socket/DNS work and do **not** call
`RendererPresentFrame` — the present calls in `net_exchange_gameplay.c`/`packets_misc.c` are on the
main game-loop thread. This holds today; it is an invariant Phase B/C must not break (e.g. by
moving capture read-back or texture uploads to a worker). Worth a one-line assert or comment at the
seam.

### R3 — Dynamic-texture lifetime across `SDL_Renderer` recreation is unmanaged

`ImGuiContextCreateTexture()` returns raw `SDL_Texture*` handles that the context does **not**
track; `shutdown_backends()` frees only `s_cursor_texture`. `ensure_present_target()` recreates
`m_renderer` (and tears down + rebuilds the ImGui context) whenever
`SDL_GetRenderWindow(m_renderer) != lbWindow` — i.e. on a genuine `SDL_Window` handle change. Any
frontend-cached dynamic-texture handle (the land-preview panel today; more in Phase B/C) then
dangles into a freed renderer → use-after-free on the next `ImGui::Image`/`UpdateDynamicTexture`.

Latent right now because `INGAME_RES` is restart-only and mid-session fullscreen/resize
(`bflib_video.c:551-605`) reuses the same `SDL_Window` (so `m_renderer` survives, only `m_texture`
is resized). Phase B (cursor unification) and Phase C (more long-lived GPU textures) raise the
stakes. Fix: a tracked-texture registry in `ImGuiContext` with invalidate-on-recreate, or a
`RendererDynamicTextureInvalidated` callback the frontend subscribes to.

### R4 — `RendererSwapFramebufferTarget` is a fragile, non-nesting shared primitive

Single-level save/restore via file-scope `s_saved_screen_width/height` (`RendererManager.cpp:159`),
no nesting support, already had one stride bug fixed in stage 4 (`318d55f2c`). Callers today: the
eye-lens effect (`thing_creature.c`), the ImGui cursor capture, the land-preview capture. Phase B's
cursor unification and Phase C's "lens as a CPU post-pass over the read-back GPU frame" both add
callers. If any two overlap in one frame (a lens effect on a frame that also rebuilds the cursor
texture) the save/restore silently corrupts screen stride. Phase B should convert it to a real
save/restore stack, or document + assert the non-overlap invariant.

### R5 — VSync and the manual frame limiter both run

`SDL_SetRenderVSync` is toggled from `vsync_enabled` (`RendererSoftware.cpp:59`), and
`game_session_loop.cpp` also has a `tick_ns_one_frame` + `LbSleepFor` software limiter. With both
active, frame pacing and input-to-photon latency can beat against each other. Not introduced here,
but Phase A already changed presentation cost and B/C change frame timing more — so verification
must measure **frame-time distribution with vsync on and off**, plus a `SDL_RenderReadPixels`
screenshot's stall cost under vsync (it blocks up to a frame → visible hitch on capture).

### R6 — Phase C breaks the "SDL lives in kfx_platform" convention

`check_layering.py` allows `kfx_render` → `kfx_platform` includes, and SDL headers are
unconstrained, so a GPU 3D path in `engine_render.c` is not a *layering* violation. But
`SDL_Renderer` usage is by convention confined to `kfx_platform`'s renderer seam. Phase C needs a
new geometry/vertex-buffer submission façade on `RendererManager` (a real widening of the seam,
with its own `KfxDrawState`-style descriptor for the shade/blend uniforms) — design that interface
before committing, rather than letting `engine_render.c` reach for SDL directly.

### R7 — Two cursor draws still coexist

The legacy `bflib_mspointer.cpp` path still blits the software cursor into `lbDrawSurface`
**unlocked**, and the ImGui overlay draws its own cursor via `GetForegroundDrawList()->AddImage()`
while `WantCaptureMouse`. Stage 4 killed the *visible* double-cursor by syncing positions, but both
still render. A post-composite `SDL_RenderReadPixels` screenshot (B2) can therefore capture two
cursors depending on timing. Phase B's cursor unification is the real fix; until it lands, capture
tests will show this.

### R8 — FMV / cutscene frames get ImGui submission too

`RendererImGuiEnabled()` is a session global, so `bflib_fmvids.cpp:510` and `front_fmvids.c`
present Smacker frames with `FrontendImGuiFrame()` still running `FeStyleSheetFrame()` (the `-imguistyle`
debug overlay) and the deferred-action drain. Low severity today (the error box is gated behind
`frontend_imgui_screen_active`), but any Phase B in-game HUD submission needs an explicit
"suppress during video/intro states" gate or it will draw over cutscenes.

### R9 — `imgui_impl_sdlrenderer3` batching may not scale to an in-game HUD (B3 route 2 only)

The backend issues `SDL_RenderGeometryRaw` per draw command with texture switches and per-command
clip rects. Fine for the frontend's dozens of draws. An in-game HUD routed through ImGui draw lists
is hundreds of small textured quads with frequent clip changes per frame — SDL_Renderer's own
batching may not coalesce that as well as a purpose-built atlas path would. Measure a
representative HUD frame before committing to B3 route 2 (another reason route 1 is the default).

### R10 — Sprite→texture cache invalidation is the hard part

The cache key must include the active palette / remap table, and both frontend fades
(`ProperFadePalette` mutating the active palette per animation step) and in-game lighting churn it.
A naïve pointer-only key returns stale colours after any fade; a per-frame-palette key defeats the
cache. Needs a generation counter on `RendererPaletteSet` and eviction keyed on it — design this
alongside stage 2 §2a's per-draw expansion cache if that was built.

### R11 — `RendererClearScreen(colour)` still takes a palette index

`RendererClearScreen` resolves an 8-bit index through the palette (`RendererSoftware.cpp:34`);
every caller passes a literal (`0`, `144`). Any GPU clear path in Phase C must preserve that
contract or update all call sites — easy to miss because the signature type doesn't change.

---

## Verification

- **Phase A:** done — nothing to verify beyond the stage-2 regression pass that already covered it.
- **Phase B1:** frame-time measurement (around `PresentFrame` and `frontend_copy_background`)
  before/after; screenshot-identical frontend output (B1 changes nothing visible).
- **Phase B2:** a scheduled screenshot must contain the ImGui overlay (main menu, an Options tab,
  a migrated list screen) — it does not on the current build. Movie capture: either the feature is
  gone (verify the menu entry / `-recordmovie` path reports so cleanly, no silent flag set), or the
  re-implemented capture produces a playable file. Screenshot format/row-order correct on both
  toolchains. Screenshot taken with vsync on does not corrupt or stall beyond one frame.
- **Phase B3 (route 2 only):** screenshot-diff across every in-game GUI screen (HUD tabs, parchment
  map, tooltips, box menus, pause options) plus a frame-time comparison against the current
  baseline; a representative busy HUD frame's draw-call count and present time measured.
- **Cursor:** exactly one cursor visible at all times, at the tracked position, on both ImGui and
  non-ImGui frames, frontend and in-game, and in `-classicmenu`; a B2 screenshot shows one cursor.
- **Reentrancy / unusual present sites (R1):** trigger a screenshot during a palette fade, during
  Smacker cutscene playback, and during a multiplayer pause/unpause resync — no crash, no
  half-composited capture, no double-applied pending state.
- **Renderer recreate (R3):** on platforms where a fullscreen transition recreates the `SDL_Window`,
  toggle fullscreen on a screen that owns a dynamic texture (land preview) and confirm no
  use-after-free.
- `KFX_OS=linux ./build-cmake.sh` + mingw cross-compile (both variants) after each phase.
- `python3 scripts/check_layering.py --strict` — no new cross-library `#include` should be needed;
  the ImGui-adjacent façades on `RendererManager.h` are the extension points.
- Test on both the mingw/Windows and native-Linux SDL3 targets — `SDL_Renderer`'s backing API
  differs by platform. Stage 4 has been exercising both through `imgui_impl_sdlrenderer3`, so a
  platform-specific regression here is more likely to show up as a *capture* (`SDL_RenderReadPixels`
  format/flip) difference than a compositing one.

## Cross-references to update once implemented

- [04-imgui-gui-foundation.md](04-imgui-gui-foundation.md) §8 ("Screenshots will miss the ImGui
  layer") — mark resolved once Phase B2 lands.
- [04-imgui-gui-foundation.md](04-imgui-gui-foundation.md) §2.3 / §10 ("in-game GUI … revisited
  after the GPU work") — this doc is that GPU work; note which route B3 took.
- [architecture.md](../../Architecture/architecture.md) §2.1/§2.4 — revise if Phase C adds a
  `RendererType` value or changes the framebuffer contract.
- [00-overview.md](00-overview.md) roadmap table — Phase A row can be marked done.
- [02c-post-migration-audit-and-refactor-opportunities.md](02c-post-migration-audit-and-refactor-opportunities.md)
  — the "FLC movie recording is dead post-stage-2" finding (§B2) belongs there too if it isn't
  already recorded; it is a stage-2 consequence this stage inherits, not a stage-3 change.
