# Stage 3 — GPU acceleration: overview

Status: **Phase A landed** (with stage 2). **Phase B3 (in-game HUD) landed** via the separate
[in-game-GUI-as-ImGui project](../../ingame-gui/00-overview.md), run ahead of this stage by
explicit agreement. **The [imgui-linkage-consolidation](../05-imgui-linkage-consolidation.md) and
[call-site-consolidation](06-call-site-consolidation.md) prerequisites have both landed**
(2026-09-23, steps 1–2 of the sequence below) — automated verification clean on both; the
interactive screenshot-identical soak for step 2 is still outstanding (R15), and should run before
Phase C work builds further on it. **Phase B is fully landed** (2026-09-23: B1, B2, and cursor
unification). B1: most of it turned out already done by a separate project this doc hadn't
cross-referenced; the one genuinely open piece (skip the CPU→GPU present-texture upload on
`RendererScreenOwned()` frames, since it was never drawn) is fixed. B2: screenshots now capture the
actual composited output (post-ImGui-render, pre-present) instead of the CPU backdrop alone; FLC
movie recording — already permanently broken since stage 2, confirmed by reading the code, not
assumed — is formally retired (confirmed with the user before deleting it) rather than rebuilt on
the new capture path. Cursor (R7): the legacy CPU-buffer cursor draw
(`bflib_mspointer.cpp`/`LbScreenSurfaceBlit`) turned out to already be entirely dead code (its
gating flag was never set `true` anywhere), and its coexistence with the ImGui-overlay cursor was a
real one-frame race, not just redundancy — both findings from reading the code, not assumed. Now
there is exactly one cursor draw, unconditional, in `gui/FrontendImGui.cpp`. **Phase B is now fully
verified except for interactive/live checks** (a real screenshot showing the overlay, a real session
showing exactly one cursor, a live frame-time number for B1's saved upload) — none possible without
a display/game data in the implementing environment; tracked per-item in
[01-phase-b-2d-compositing.md](01-phase-b-2d-compositing.md)'s own verification section. **C.0
(seam + IR skeleton) has landed** (2026-09-23) — see [07](07-phased-delivery.md)'s C.0 row for the
full "Landed as" note; in short, the `WorldFrame` IR and `IRenderer::SubmitWorldFrame()` exist and
are unit-tested, scoped to `QK_PolygonStandard` only (not the full bucket set the original C.0 text
implied), and deliberately not yet wired into the live present path — see that note for why. The
SDL_GPU smoke-test spike, the Vulkan-forcing decision, and the shader-delivery decision are all
recorded there too. **C.1 has landed** (2026-09-24): `RendererGpu3D`, the Options → Graphics renderer switch, a GPU block-texture cache, and live wiring — with Vulkan selected the game's terrain renders on the GPU (verified in the real game against the software renderer); **C.2 has landed too** (same day): sprites, effects and creature shadows now draw on the GPU in terrain's draw order, so walls occlude creatures correctly (see 07's C.2 note). **C.3 landed** (lens effects stay CPU post-passes, fed by a GPU read-back; not yet run live). **Lighting pass v1 landed** (Options → Graphics → Lighting: Classic | Per-pixel; terrain only, see 07). **C.4 audit done** (one resize bug fixed; full-session live pass still owed by a human). **C.5 first slice landed** (selection lines/boxes now occluded by walls via the op stream; depth buffer and render thread deliberately not built — need profiling first). Remaining CPU-only: front view, ghost-blended boxes; there is still no frame-time measurement and no Windows/Vulkan run. **The rest of Phase C is scoped in detail** across
[04](04-architecture-and-ir-boundary.md)–[07](07-phased-delivery.md), informed by a review of an
external attempt at the same problem; not started.

This directory (`gpu-v2/`) is the **single source of truth for the whole GPU-acceleration stage**,
superseding [`../03-gpu-renderer.md`](../03-gpu-renderer.md) (kept as a pointer back here — see its
own header). This file is the entry point; it carries the cross-cutting context every sub-plan
depends on. The stage is broken into the following files so each concern gets its own
build/test/review cycle instead of one document that keeps growing:

| # | File | Covers |
|---|---|---|
| 00 | (this file) | Status, shared context, Phase A, `RendererType`/backend selection, non-goals |
| [01](01-phase-b-2d-compositing.md) | Phase B — 2D compositing on the GPU | B1 (backdrop), B2 (capture path), B3 status, cursor unification |
| [02](02-graphics-api-choice.md) | **Graphics API choice for Phase C** (new) | OpenGL vs. Vulkan vs. SDL_GPU — recommendation and rationale |
| [03](03-pixel-format-and-texture-cache.md) | Pixel format & the sprite/texture cache | Why RGBA-resolved-once, not palette-index-on-GPU; the shared cache Phase B and C both need |
| [04](04-architecture-and-ir-boundary.md) | Architecture & the IR boundary | Layering, the `WorldFrame` IR struct, bucket order vs. depth buffering, the `CLASSIC` HUD compositing question (R13) |
| [05](05-reviewed-branch-lessons.md) | Lessons from the reviewed branch | What to take and not take from `origin/feature/opengl-renderer` |
| [06](06-call-site-consolidation.md) | **Call-site consolidation** (new) | Shrinking the ~20 `RendererPresentFrame()` call sites before Phase C's façade lands |
| [07](07-phased-delivery.md) | Phased delivery | The C.0–C.5 table, exit criteria, sequencing against 06 |
| [08](08-risks.md) | Risks | R1–R15 |
| [09](09-verification.md) | Verification | Cross-phase verification baseline |

Depends on [../02-32bit-software-renderer.md](../02-32bit-software-renderer.md) (**landed** —
`TbPixel` is true-colour RGBA, `lbDrawSurface` is `SDL_PIXELFORMAT_RGBA32`). See
[../00-overview.md](../00-overview.md) for the broader four-stage context.

## Proposed implementation sequence

The sub-plans above are written to be independently reviewable, but they aren't independent to
*land* — each step below is easier or safer because of the one before it. This is the recommended
order, not a restatement of the file list:

1. **[`../05-imgui-linkage-consolidation.md`](../05-imgui-linkage-consolidation.md)** — **landed**
   (2026-09-23). Foundation for everything after it: moved all ImGui knowledge into `kfx_frontend`
   (`gui/FrontendImGui.{h,cpp}`) behind one choke point, the `RendererImGuiCallbacks` struct. Step
   2 and Phase B can now proceed.
2. **[`06-call-site-consolidation.md`](06-call-site-consolidation.md)** — **landed** (2026-09-23).
   Collapsed the 21 `RendererPresentFrame()` call sites down to `RendererPresentGameFrame()` /
   `RendererPresentStepFrame()` (the raw present is `static` now, reachable only through those two)
   — makes B2's capture-read-back hook a two-entry-point change instead of a 21-site audit, and
   means neither Phase B nor Phase C.0 has to touch the same call sites a second time to add its
   own obligations. Automated verification (both toolchains, layering, full Catch2 suite, the
   call-site grep) is clean; the interactive screenshot-identical soak (R15) is **not yet done** —
   run it live before starting step 3/4 (Phase B) or step 6+ (Phase C).
3. **[`01-phase-b-2d-compositing.md`](01-phase-b-2d-compositing.md) B1** (frontend backdrop) —
   **landed** (2026-09-23). Both toolchains, layering, full Catch2 suite clean; the backdrop-as-
   texture half was already done by another project, the present-texture-upload-skip half is new.
4. **[`01-phase-b-2d-compositing.md`](01-phase-b-2d-compositing.md) B2** (capture path) —
   **landed** (2026-09-23). Both toolchains, layering, full Catch2 suite clean; a real
   screenshot-contains-the-overlay check is still outstanding (no display available). FLC movie
   recording retired, by explicit user confirmation, rather than rebuilt on the new capture path.
5. **[`01-phase-b-2d-compositing.md`](01-phase-b-2d-compositing.md) cursor unification** —
   **landed** (2026-09-23). Both toolchains, layering, full Catch2 suite clean (1934 tests). The
   legacy software cursor path turned out to already be dead code end to end; deleted rather than
   folded in, since there was nothing live left to fold.
6. **[`02-graphics-api-choice.md`](02-graphics-api-choice.md) + Phase C.0**
   ([`07-phased-delivery.md`](07-phased-delivery.md)) — the seam/IR skeleton, plus the two open
   items 02 leaves for C.0 specifically: the SDL_GPU smoke-test spike on both CI targets, the
   `SDL_Renderer`/`SDL_GPU` interop decision, and the shader-delivery decision. Nothing in Phase C
   past this point should start until these three are resolved and recorded, not assumed.
7. **Phase C.1** (static level geometry, [`03`](03-pixel-format-and-texture-cache.md)/[`04`](04-architecture-and-ir-boundary.md)) — first real GPU-rendered pixels, and where R13's `CLASSIC`/`RendererGpu3D` mutual-exclusion decision gets made.
8. **Phase C.2** (creatures, shadows, sprites) — extends C.1's IR and pipeline; depends on the
   sprite cache from [`03`](03-pixel-format-and-texture-cache.md), already built for Phase B.
9. **Phase C.3** (lens effects) — depends on C.1's texture/compositing plumbing being stable enough
   to hang a post-process (or per-effect shader) off of.
10. **Phase C.4** (compositing with Phase B) — a verification-focused pass once C.1–C.3 exist; low
    new-code risk, mostly a full-session regression check.
11. **Profile, then decide on Phase C.5** (depth buffering / render thread) — deliberately not
    scheduled as part of this sequence; only take it up once C.1–C.4 are shipped and profiled, per
    [`07-phased-delivery.md`](07-phased-delivery.md) and R12.

Steps 3–5 (Phase B) and step 6 onward (Phase C) don't have a hard ordering dependency between them
once step 2 has landed — Phase B could in principle follow Phase C.0's design work rather than
precede it — but landing Phase B first keeps each phase's diff smaller and gives Phase C.0 a
stable, already-shipped capture/cursor story to build its own verification screenshots against,
rather than changing underneath it mid-design.

> **Revision note (2026-09-06).** This doc was originally written before stage 4
> ([../04-imgui-gui-foundation.md](../04-imgui-gui-foundation.md)) landed. Stage 4 did far more than
> "evaluate ImGui as a rendering backend behind `IUIRenderer`": it reversed its own recommendation
> and migrated the **entire main-menu frontend** (all 15 `GuiMenu`s + the backdrop-plus-text
> screens, Phases A–G) to real Dear ImGui widgets, rendered through `imgui_impl_sdlrenderer3` into
> the *same* `SDL_Renderer` this stage's present path already owns.

> **Revision note (2026-09-13a).** Phase C, scoped: a review of `origin/feature/opengl-renderer`
> (a since-abandoned upstream attempt at a full OpenGL 3D renderer) surfaced enough concrete
> design evidence to write Phase C in detail rather than wait for Phase B to ship first.

> **Revision note (2026-09-13b).** The in-game-GUI-as-ImGui project shipped in the interim,
> taking the route this doc already called the recommended default — see
> [01](01-phase-b-2d-compositing.md) for the corrected B3 status and what it means for Phase C's
> compositing model.

> **Revision note (2026-09-13c), this split.** The single `03-gpu-renderer.md` file (866 lines) is
> split into the files listed above so implementation work can proceed sub-plan by sub-plan, each
> independently reviewable. Two things are added that the single-file version did not have:
> - **[02-graphics-api-choice.md](02-graphics-api-choice.md)**: the original doc never actually
>   chose between graphics APIs — it wrote "`SDL_RenderGeometry` or a raw GL/Vulkan pass" and left
>   it there. Checked against this project's own vendored dependencies: the SDL3 build this
>   repository already fetches (`out_mergecheck/_deps/sdl3-src`, version pinned in
>   `Dependencies.cmake`) is configured with `SDL_GPU=ON`, `SDL_RENDER_GPU=ON`, `SDL_VULKAN=ON`,
>   `SDL_RENDER_VULKAN=ON` — i.e. **SDL3's own cross-platform GPU API is already part of the
>   dependency this codebase links, with Vulkan (Linux) and D3D12 (Windows) backends built in, at
>   no new dependency cost.** That materially changes the "GL vs. Vulkan" question the user asked
>   about into a three-way choice, and the third option wins — see 02 for the full case.
> - **[06-call-site-consolidation.md](06-call-site-consolidation.md)**: R1 and
>   [../05-imgui-linkage-consolidation.md](../05-imgui-linkage-consolidation.md) both already
>   flagged that the ~20 `RendererPresentFrame()` call sites are *not* being reduced by that
>   stage's ImGui-linkage cleanup — it was explicitly left as "a separate, optional cleanup...not
>   required for the linkage goal." Phase C's new GPU-submission façade (R6) is what makes it
>   non-optional: every one of those ~20 sites is a place the façade's reentrancy/threading
>   contract (R1, R2, R12) has to hold, and each one silently exempts itself if consolidation stays
>   undone. Scoped as its own phase, sequenced before C.0.

> **Revision note (2026-09-23), steps 1–2 landed.**
> [05-imgui-linkage-consolidation.md](../05-imgui-linkage-consolidation.md) and
> [06-call-site-consolidation.md](06-call-site-consolidation.md) both landed, in that order, each
> its own commit. Automated verification (native Linux + mingw Windows builds, `check_layering.py
> --strict`, full Catch2 suite, the grep checks each doc specifies) is clean for both. Two
> deliberate deviations from the original drafts, both recorded in place with "Landed as:" notes:
> the shared callback struct is named `RendererImGuiCallbacks` (not `RendererOverlayCallbacks` —
> collides with `kfx_config`'s pre-existing, unrelated `RenderOverlayCallbacks`), and the
> `game_session_loop.cpp` physical-reduction estimate landed at 8→4 call sites, not the "2–3"
> originally guessed. One thing genuinely not done yet: 06's interactive screenshot-identical soak
> (R15) needs a real display, game data, and a second network client, none of which were available
> in the implementing environment — run that pass before relying on this consolidation for Phase
> B/C work.

---

## Goal

Move drawing work onto the GPU without a single risky rewrite. Phased, each phase independently
shippable and each smaller than the last major stage.

---

## What stage 4 already built (the shared starting point)

Everything in this list is live in the shipped build today, on every presented frame, frontend
**and** in-game — the session-global `RendererImGuiEnabled()` gate this originally ran behind has
itself since been retired (per the in-game-GUI-as-ImGui project): `RendererSoftware::PresentFrame()`'s
ImGui pipeline is unconditional now, not a per-state or per-session toggle:

- **One graphics context, already GPU-accelerated.** `RendererSoftware` owns an `SDL_Renderer`
  (`m_renderer`) and an `SDL_Texture` (`m_texture`, `SDL_PIXELFORMAT_RGBA32`).
  `PresentFrame()` (`RendererSoftware.cpp`) does: `SDL_UpdateTexture(m_texture, …, lbDrawSurface)`
  → `SDL_RenderClear` → `SDL_RenderTexture` (the CPU framebuffer, full-screen) → **ImGui draw data
  composited on top** (`ImGuiContextNewFrame` / `RendererRunImGuiFrameCallback` / `ImGuiContextRender`)
  → `SDL_RenderPresent`. SDL3's renderer is hardware-accelerated (Direct3D/Vulkan on Windows,
  GL/Vulkan on Linux — and, per the 2026-09-13c revision note above, optionally the `SDL_RENDER_GPU`
  driver, already enabled in this project's SDL3 build; see [02](02-graphics-api-choice.md)).
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
    Used for stage 4's land-preview panel.
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
  size asked for. No baked atlas, no `ITextRenderer` GPU implementation needed for the menus.
- **Cross-platform `SDL_Renderer` exposure is already paid down.** `imgui_impl_sdlrenderer3`
  forces the mingw-w64/Windows and native-Linux `SDL_Renderer` paths that this stage's verification
  section calls for; stage 4 has been exercising both.

### What the in-game-GUI project already built

This landed as its own project, run *before* this stage's own Phase B by explicit user agreement —
see [`docs/refactor/ingame-gui/00-overview.md`](../../ingame-gui/00-overview.md) §8's decision
record ("Do this project before stage 3's GPU Phase B... it is stage 3 §B3 route 1"). It shipped
incrementally, landing 2026-09-06 through 2026-09-12. Its result is the actual current default
behaviour of the game, not a plan:

- **`menu_is_migrated()`** (`frontgui_ingame.cpp`) marks essentially every substantive in-game
  `GMnu_*` as ImGui by default: the sidebar frame (`GMnu_MAIN`), all four build/power/trap/creature
  grids (`GMnu_ROOM`/`SPELL`/`TRAP`/`CREATURE`), the query/creature-detail panels (`GMnu_QUERY`,
  `GMnu_CREATURE_QUERY1-4`), save/load, options, quit, armageddon/hold-audience, and the
  event/battle/text-info boxes. The parchment map and the first-person/possession HUD are migrated
  too.
- **`ingame_gui_use_classic_hud()`** (`src/kfx_config/src/config_keeperfx.c`) is the single gate
  every migrated draw call checks — reading the `GUI_ICON_PACK=CLASSIC` config value. This is
  **not** the retired `-classicmenu`/`RendererImGuiEnabled()` session-wide switch (that one's gone
  outright for the frontend); it's a narrower, permanent, in-game-HUD-only style choice, kept by
  deliberate request rather than as a migration safety net to be removed later.
- **The legacy sprite HUD (`CLASSIC`) is not a Phase B/C migration target.** It was kept
  specifically so it can stay CPU-rastered indefinitely as a supported style choice, not a
  stepping-stone implementation. Phase C needs to account for what that means once the 3D view
  itself moves to the GPU — see [04](04-architecture-and-ir-boundary.md) and R13.

### What is still genuinely CPU-rastered every frame

- **The 3D dungeon view** — `engine_render.c`'s bucketed `do_a_gpoly_*` rasterizer → `vec_screen`
  → `lbDrawSurface`. Unchanged. This is Phase C.
- **The in-game GUI / HUD, only when the user opts into `GUI_ICON_PACK=CLASSIC`.** By default the
  sidebar, tab content, query panels, save/load, options, event/battle boxes, and the parchment map
  are already ImGui. The legacy sprite/box/text path still exists and still writes `lbDrawSurface`
  directly, but only when `CLASSIC` is selected, and it is meant to **stay** CPU-rastered
  permanently — see [04](04-architecture-and-ir-boundary.md)'s compositing note.
- **The frontend backdrop** — `frontend_copy_background()` still draws the full-screen backdrop
  through the software path; ImGui draws over it. Migrated screens skip `draw_gui()` but still call
  `frontend_copy_background()`.
- **Non-migrated frontend paths** — `FeSt_LAND_VIEW`, `FeSt_NETLAND_VIEW`, `FeSt_TORTURE`, and
  Smacker video states are still CPU/sprite-drawn on their own merits, independent of any toggle.
- **The legacy mouse cursor** — `bflib_mspointer.cpp`'s `LbI_PointerHandler` still composites the
  software cursor via `LbScreenSurfaceBlit` (`SDL_BlitSurface` against `lbDrawSurface`), a parallel
  path, still **unlocked**. Stage 4 added a *second* cursor draw (the ImGui-overlay one) rather than
  replacing this.
- **Lens effects** — `LensManager`/`LensEffect` (`MistEffect`/`FlyeyeEffect`/`OverlayEffect`/
  `DisplacementEffect`/`PaletteEffect`) as a CPU post-process over the framebuffer. Phase C.
- **Screenshot & movie capture** — `RendererSoftware::ScheduleScreenshot` still saves
  `lbDrawSurface` (the CPU buffer, backdrop only); `perform_any_screen_capturing()` runs before
  present. Neither sees the ImGui overlay. Stage 4 flagged this and did not fix it — it is now
  **blocking for this stage** (see [01](01-phase-b-2d-compositing.md) B2).

---

## Phase A — Direct texture upload — **DONE**

Landed with stage 2 (`13442110f`, "Flip lbDrawSurface to true-colour"). `PresentFrame()`'s old
lock/blit/unlock `SDL_BlitSurface` INDEX8→RGBA conversion is gone; it is now a single
`SDL_UpdateTexture(m_texture, NULL, lbDrawSurface->pixels, lbDrawSurface->pitch)` straight from the
already-RGBA CPU framebuffer. Nothing further to do here.

---

## `RendererType` and backend selection

The original plan said `enum RendererType` (`IRenderer.h`) gains a `RENDERER_GPU` value and
`create_renderer()` (`RendererManager.cpp:32`) gains a matching case, keeping `RENDERER_SOFTWARE`
alive as a correctness reference.

That framing needs revising: **there is no longer a clean "software vs. GPU backend" split.**
`RendererSoftware` already owns an `SDL_Renderer`, already GPU-composites (ImGui), and already
presents through hardware. What each phase actually changes is *which draw categories flow through
the `SDL_Renderer` that class already has* — not a swap to a parallel backend class.

- **Phase B** (B1/B2, cursor): evolve `RendererSoftware` in place. Consider renaming it
  (`RendererSDL`?) once "software" is no longer accurate, but that is cosmetic and can wait.
- **Phase C**: *this* is where a real second backend or a real config switch earns its place —
  "CPU 3D rasterizer" vs "GPU 3D rasterizer" is a genuine either/or with real correctness-reference
  value. Add a `RENDERER_GPU3D`-shaped `RendererType` value and the `create_renderer()` case
  *then*, scoped to the 3D path, not now. Keep the CPU 3D path selectable (via `RendererInit`) as
  the diff target and the fallback for driver combinations the GPU 3D path doesn't handle. See
  [02](02-graphics-api-choice.md) for what "the GPU 3D path" actually means once SDL_GPU is chosen
  as the backend API — it changes what a "driver combination that doesn't handle it" looks like
  (SDL_GPU's own backend-selection fallback chain, not a from-scratch GL-vs-Vulkan runtime probe).
- `-classicmenu` provided a "no ImGui, CPU-composite frontend" mode for bisecting 2D-layer
  regressions — **retired 2026-09-12** (docs/refactor/ingame-gui/00-overview.md §1/§8), the
  frontend has no legacy path left to fall back to at all, so this specific bisect mode no longer
  exists. `GUI_ICON_PACK=CLASSIC` still forces the *in-game HUD* to its legacy sprite renderer.

Update [architecture.md](../../../Architecture/architecture.md) §2.1/§2.4 if/when Phase C adds the
`RendererType` value and changes `IRenderer`'s framebuffer contract.

---

## Non-goals for this stage

- **No change to `kfx_frontend`'s screen/menu authoring.** Phase B changes what happens *behind*
  the draw calls, not how screens are authored. For the frontend, stage 4 already moved authoring
  to ImGui — this stage does not touch it further.
- **The in-game-GUI-as-ImGui port was never this stage's job, and it's done.** Its permanent
  `CLASSIC` legacy-HUD carve-out is likewise not something this stage migrates; Phase C only has to
  decide how it composites once the 3D view is on the GPU (R13), not whether to port it.
- **Phase C is scoped, but not committed to a start date.** Scoping it in detail makes it ready to
  pick up quickly; it does not move its timeline ahead of Phase B or the call-site-consolidation
  prerequisite ([06](06-call-site-consolidation.md)).
- **Phase C's document is not a final API.** `WorldFrame`/`RendererGpu3D` names and shapes are
  illustrative of the boundary, not a locked interface; the real design should happen at C.0 with
  actual code in hand. The choice of SDL_GPU as the backend API ([02](02-graphics-api-choice.md))
  *is* a firmer recommendation than the naming, but is still confirmed properly only once C.0 has
  written real code against it.
- **Phase C's document is not a UI/2D-layer change.** Phase B is unaffected by anything in Phase C.

---

## Cross-references to update once implemented

- [../04-imgui-gui-foundation.md](../04-imgui-gui-foundation.md) §8 ("Screenshots will miss the
  ImGui layer") — mark resolved once Phase B2 lands.
- [../04-imgui-gui-foundation.md](../04-imgui-gui-foundation.md) §2.3 / §10 — **done**: the
  in-game-GUI-as-ImGui project shipped B3 route 1 ahead of this stage, not after it.
- [docs/refactor/ingame-gui/00-overview.md](../../ingame-gui/00-overview.md) — the project that
  shipped B3; [00-overview.md](00-overview.md)'s "What the in-game-GUI project already built"
  section summarizes it. If that project's own scope changes, update both.
- [architecture.md](../../../Architecture/architecture.md) §2.1/§2.4 — revise if Phase C adds a
  `RendererType` value or changes the framebuffer contract.
- [../00-overview.md](../00-overview.md) roadmap table — point stage 3's row at
  `gpu-v2/00-overview.md` (this file), not `gpu-v2/03-gpu-renderer.md`.
- [../02c-post-migration-audit-and-refactor-opportunities.md](../02c-post-migration-audit-and-refactor-opportunities.md)
  — the "FLC movie recording is dead post-stage-2" finding belongs there too if it isn't already
  recorded.
- [`docs/merge-checks/opengl-renderer-review.md`](../../../merge-checks/opengl-renderer-review.md)
  — the source review [05](05-reviewed-branch-lessons.md) draws on; update these documents, not
  that one, if Phase C's actual implementation diverges from what's scoped here.
