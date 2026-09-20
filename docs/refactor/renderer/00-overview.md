# Renderer refactor: 8-bit → 32-bit → GPU, and where ImGui fits

Status: **planning — investigation complete, no implementation started.** Scope:
`src/kfx_render/`, the renderer/platform seam under `src/kfx_platform/{include,src}/renderer/`,
and (for stage 4) a new GUI-drawing backend touching how `src/kfx_frontend/` presents, not how
it's authored. Builds on top of `docs/Architecture/architecture.md` §2.1/§2.4 (the existing
`kfx_render`/platform-seam split) — this is a follow-on initiative within that boundary, not a
further layering change. Directly follows on from
[`docs/refactor/gui/colordepth/00-notes.md`](../gui/colordepth/00-notes.md), which hit the
single-8-bit-palette ceiling from the GUI side and deferred to "the renderer work" — this is that
work, scoped.

## Why now

Building the merged Land selection screen surfaced a real, user-visible limit: the engine has
**exactly one active 8-bit palette per frame** (`TbPixel` is `unsigned char`,
`bflib_video.h:54`), so two differently-authored palette assets (UI chrome vs. land art) can't
render at full fidelity in the same frame without a lossy remap. That's a symptom, not the
disease — the disease is that the whole pixel pipeline, not just that one screen, is genuinely
paletted end to end. Fixing it properly means widening the pixel format, which is also the
natural on-ramp to GPU acceleration, which is also the natural point to ask whether the
hand-rolled `kfx_frontend` immediate-mode GUI drawing should keep being reinvented or ride on a
library built for exactly this. Hence one investigation, four stages, written together so the
sequencing decisions are visible in one place.

## What's already there (don't re-build this)

This investigation found more existing infrastructure than expected. A prior effort already cut
a **renderer/platform seam** into `kfx_platform` — this is the load-bearing discovery for
everything below:

- `IRenderer` (`src/kfx_platform/include/renderer/IRenderer.h`) — backend-agnostic interface:
  `Init`/`Shutdown`, `SetDisplayPalette`, `ClearScreen`, `PresentFrame`,
  `LockFramebuffer`/`UnlockFramebuffer`, `ScheduleScreenshot`, and accessors for two
  sub-renderers, `ITextRenderer`/`IUIRenderer`.
- `RendererManager` (`src/kfx_platform/include/renderer/RendererManager.h`) — the `extern "C"`
  facade every other library actually calls: lifecycle (`RendererInit(RendererType)` — today only
  `RENDERER_SOFTWARE` exists, `RendererManager.cpp:29-36`), palette set/get, present, framebuffer
  lock/unlock, and per-primitive entry points (`RendererDrawBox`, `RendererSpriteDraw*`,
  `RendererDrawSlabBackground`, `RendererTextDrawResized`) plus ambient draw-call state (colour,
  flags) that used to live as raw fields on `lbDisplay`.
- `IUIRenderer`/`ITextRenderer` (`src/kfx_platform/include/renderer/{IUIRenderer,ITextRenderer}.h`)
  — the *sub-renderer* interfaces a backend overrides to intercept 2D drawing: sprite blits
  (raw/one-colour/scaled/scaled-remap), solid boxes, slab-background tiling, resized text.
  `KfxDrawState` (`DrawState.h`) is the immutable per-call descriptor (flags + one palette-index
  colour byte) that travels with each submission.
- `RendererSoftware` (`src/kfx_platform/{include,src}/renderer/RendererSoftware.{h,cpp}`) — the
  only backend today. Its `SoftwareUIRenderer`/`SoftwareTextRenderer` subclasses
  (`renderer/backends/Software{UI,Text}Renderer.h`) override **nothing** — the real bodies live
  as `IUIRenderer`/`ITextRenderer`'s own default virtuals
  (`src/kfx_platform/src/renderer/{IUIRenderer,ITextRenderer}.cpp`), which just call the same
  `Lb*Immediate` raster primitives the engine called directly before the seam existed. The seam
  is real as a *boundary*; it currently buys zero format independence, because its one
  implementation is the legacy code verbatim.
- `SwDrawTarget` (`src/kfx_platform/{include,src}/renderer/software/SwDrawTarget.{h,c}`) — a
  thin accessor layer (`SwTargetWScreen`, `SwTargetScanline`, window rect) over
  `lbDisplay.WScreen`/`GraphicsWindow*`. This is the seam for *where* the software raster
  writes — narrow today, but the right extension point for stage 2's format change.
- **GPU-accelerated presentation already exists, just not for game content.**
  `RendererSoftware::PresentFrame()` (`RendererSoftware.cpp:122-146`) blits the CPU-side 8-bit
  indexed `lbDrawSurface` into an `SDL_PIXELFORMAT_RGBA32` streaming texture
  (`SDL_BlitSurface`, comment at line 133: *"INDEX8 (palette) -> RGBA and present"*) and presents
  it via a real `SDL_Renderer` (`SDL_RenderTexture`/`SDL_RenderPresent`, lines 143-144) — SDL3's
  renderer is hardware-accelerated. The GPU is already in the loop for the final scale-and-blit;
  it just never sees actual game/UI draw calls, only one composited raster per frame.

## What's still genuinely 8-bit, and where

The seam's coverage is much narrower than its API surface suggests:

- **Simple GUI drawing mostly routes through it, indirectly.** `kfx_frontend` calls the legacy
  `Lb*Draw*`/`LbDrawBox`/`LbTextDraw*` wrappers 272 times; `kfx_render` does 30 more
  (`engine_render.c`: 20, `engine_redraw.c`: 9, `scrcapt.c`: 1 — 302 combined). Those
  wrappers (`src/kfx_platform/src/renderer/software/bflib_vidraw.c:311-1028`) are now thin
  pass-throughs into `RendererManager` (e.g. `bflib_vidraw.c:313` → `RendererDrawBox`,
  `:1005` → `RendererSpriteDraw`). Only **one** call site in either library calls a `Renderer*`
  entry point directly: `src/kfx_frontend/src/gui_draw.c:237`
  (`RendererDrawSlabBackground`).
- **Raw `lbDisplay.WScreen` bypasses are wider than a first grep-based pass found** — a Tier-3
  codebase-graph audit (2026-09-03) re-verified this and found materially more. Within
  `kfx_frontend` alone: **14 sites across 7 files** (not 7/5) — `gui_draw.c:265,952`,
  `gui_parchment.c:192,306,782`, `front_landview.c:112,808,887`,
  `frontmenu_ingame_map.c:124,973,1268`, `frontend.cpp` (1 site), plus two files a
  substring-only grep missed entirely because `WScreen` was passed as a bare pointer argument
  rather than indexed with a literal `[`: `front_torture.c:194`, `front_simple.c:173`. Beyond
  `kfx_frontend`, whole-codebase search found bypasses in layers the first pass never scoped:
  `src/kfx_apploop/src/game_session_loop.cpp:260-261` (screen-scroll memcpy/memset),
  `src/kfx_sim/src/thing_creature.c:4350-4378` (`draw_creature_view` swaps
  `lbDisplay.WScreen` to a lens render-target buffer and back — via the legitimate
  `RenderOverlayCallbacks` mechanism, but still a raw pointer assignment underneath),
  `src/kfx_platform/src/bflib_fmvids.cpp:104,154` (FMV/movie blit),
  `src/kfx_platform/src/bflib_mspointer.cpp:328` (`PointerDraw`, the mouse-cursor blit — notably
  **unlocked**, i.e. it writes without the lock/unlock discipline the seam otherwise expects),
  and `src/kfx_platform/src/bflib_sprfnt.c:993` (`LbTextSetWindow` aliases a raw `WScreen`
  pointer into `lbTextJustifyWindow.ptr`, later indexed for text-clipping — the exact
  pointer-alias pattern a literal-substring grep can't catch).
- **`engine_redraw.c` also writes pixels directly** — separately from the layering violations
  `docs/refactor/stage-07-kfx-render.md` already documented (reaching into gui/frontend/
  power-hand/net state). It reads/writes `lbDisplay.WScreen` directly at
  `engine_redraw.c:384,395,413,429,558,613` (fade-table blend and screen-smoothing paths). This
  file wasn't in the first pass's bypass list at all.
- **A second, independent surface-lock path exists**: `LbScreenSurfaceLock`
  (`src/kfx_platform/src/bflib_vidsurface.c:160`, a generic `SSurface` lock) is called from
  `bflib_mspointer.cpp:202` for the mouse-cursor backup/restore surfaces, entirely outside
  `RendererSoftware`'s own `LockFramebuffer`/`UnlockFramebuffer`. A backend swap has to account
  for this path too, or cursor rendering breaks silently.
- **The actual bulk of per-frame pixel writes bypasses the seam entirely.** `engine_render.c`'s
  bucketed polygon/sprite rasterizer (`do_a_gpoly_*` family, e.g.
  `do_a_gpoly_gourad_tr` at `engine_render.c:4414`) queues into buckets consumed by
  `src/kfx_platform/src/renderer/software/bflib_render_gpoly.c`, which writes straight into the
  global raw pixel array `vec_screen` (declared `bflib_vidraw.c:57`, pointed at the locked
  framebuffer at `bflib_vidraw.c:1621`; written at `bflib_render_gpoly.c:760,794`, with `:706`
  touching the paired `vec_screen_width` stride variable rather than the pointer itself).
  `engine_textures.c:382,441` indexes `vec_screen` directly too. **None of this — the 3D dungeon
  view, the bulk of what's on screen every frame — goes through `IUIRenderer` or any interface
  at all.** It's a raw global pointer the software rasterizer happens to own. The lens-effect
  system (`LensManager`/`LensEffect` and its subclasses) was checked separately and is clean —
  no `vec_screen`/`WScreen` references anywhere in that hierarchy; `draw_lens_effect`
  (`lens_api.c:170-203`) takes plain `dstbuf`/`srcbuf` parameters, so the only bypass touching
  lens rendering is the `thing_creature.c` call site above, one layer up from the lens code
  itself.
- **The lighting/shading model is a second, deeper 8-bit dependency**, independent of the pixel
  format question: `struct TbColorTables pixmap` (`vidmode.h:113-121`, instance `:150`) holds
  `fade_tables[64*256]`, `ghost[256*256]`, four `flat_colours_*[2*256]` corner-shade ramps, and
  `map_abyss[256]` — classic precomputed remap tables, `output = table[(shade << 8) | palette_index]`.
  `alpha_sprite_table` (`vidmode.h:123-135`) is the same idea for transparency (8 named ramps ×
  256). `engine_render.c` has ~109 shade/shade-factor variables feeding these lookups and 14
  direct references to the tables/pointers (`render_fade_tables`/`render_ghost`/
  `lbSpriteReMapPtr`, set at `engine_render.c:6815-6816,7250-7251,5198,5207,8172,8180`), consumed
  inside the rasterizer as `fade_table[texel | shade]` (`bflib_render_gpoly.c:693`). **Every lit,
  faded, or ghosted pixel in the game is a table lookup that only makes sense for an 8-bit index.**
  This has no direct RGB equivalent — it has to become real per-pixel blend math, not a wider
  table.
- **Assets are 8-bit-indexed on disk and stay that way through the whole pipeline.**
  `struct TbSprite` (`bflib_sprite.h:37-46`) wraps `TbSpriteData` (`typedef unsigned char*`,
  `:35`) — one byte per source pixel, RLE-coded, resolved to a colour only at draw/composite time
  via the active palette or a remap table (confirmed in `bflib_vidraw_spr_{norm,remp}.c`'s blit
  primitives, which take a `TbPixel *cmap` for the remap variants). No RGB sprite data exists
  anywhere today.
- **22 files reference `TbPixel` directly** (`grep -rl TbPixel src/kfx_render src/kfx_frontend
  src/kfx_platform/src/renderer`) — the blast radius of widening the typedef itself:
  `engine_render.{c,h}`, `engine_lenses.c`, `LensManager.cpp`, `vidfade.h`, `lens_api.h`,
  `vidmode.c`, `front_simple.{c,h}`, `front_landview_multiplayer.c`,
  `frontmenu_ingame_evnt.c`, `gui_parchment.c`, `frontmenu_ingame_map.c`, `gui_draw.{c,h}`,
  `bflib_vidraw_spr_onec.c`, `bflib_vidraw_spr_remp.c`, `bflib_render.c`, `SwDrawTarget.c`,
  `bflib_vidraw_spr_norm.c`, `bflib_vidraw.c`.
- **Unresolved, flagged for stage 1's own audit**: `vidmode_data.cpp` holds several more
  `unsigned char*` globals (`gui_slab`, `scratch`, `hires_parchment`, `frontend_backup_palette`,
  `red_palette`, `dog_palette`, `vampire_palette`) whose write sites weren't traced in this
  investigation — confirm each is a genuine drawable/palette buffer (vs. e.g. a static config
  table) and whether it needs seam coverage before stage 1's Part C is considered complete.

### A note on how this was verified

The bypass inventory above was expanded past a first grep-only pass using a Tier-3
codebase-knowledge-graph audit (2026-09-03) — worth recording because the graph itself has a
real blind spot here: `vec_screen` and the other bare C globals this doc cites aren't tracked as
graph nodes (no USAGE edges for plain global-variable reads/writes), and several of the
heaviest-hit files (`engine_render.c`, `engine_redraw.c`, `engine_textures.c`, `front_landview.c`,
`front_torture.c`, `front_simple.c`, `engine_lenses.c`) are marked `parse_partial` across nearly
their entire body, so function-level call-graph claims for those files are unreliable until a
reindex. Every finding in this doc that involves those files or those globals was verified by
direct source read/grep, not graph traversal — the graph was still useful for the whole-codebase
sweep (finding bypasses in `kfx_apploop`/`kfx_sim`/`kfx_platform` files no one had thought to
check), just not as the sole method. Re-run `check_index_coverage` on the files above before
trusting a *new* graph-based claim about them.

## The roadmap

Stage docs, meant to be read and executed in order — each is a prerequisite for the next,
except stage 4 which can start any time after stage 1 and is independent of stages 2–3's
completion (it targets the GUI 2D layer specifically, not the 3D engine). Stage 5 is a
post-stage-4 cleanup that should land before stage 3's Phase B.

| # | Doc | What it does | Depends on |
|---|-----|---------------|-------------|
| 1 | [01-close-the-seam.md](01-close-the-seam.md) — **complete 2026-09-03** | Extend `IUIRenderer`/a new raster-target abstraction to actually cover `engine_render.c`'s polygon/sprite rasterizer and `engine_textures.c`, and eliminate the residual raw `WScreen[]` pokes in `kfx_frontend`. Makes the seam load-bearing instead of decorative. | — |
| 2 | [02-32bit-software-renderer.md](02-32bit-software-renderer.md) — **complete** (design spec: [02a-pixel-format-design.md](02a-pixel-format-design.md)) | Widen the CPU raster path to 32-bit RGBA, replace the 8-bit fade/ghost/alpha remap tables with real per-pixel blend math, expand palette-indexed sprite data to RGBA per-draw (removing the one-palette-per-frame ceiling that `colordepth/00-notes.md` hit). Still CPU-rasterized. | 1 |
| 3 | [gpu-v2/00-overview.md](gpu-v2/00-overview.md) — **Phase A landed with stage 2; Phase B3 landed via the in-game-GUI project; Phase B1/B2 and a call-site-consolidation prerequisite not started; Phase C now scoped in detail across sub-plans, not started** | Move drawing onto the GPU in phases — Phase A (direct texture upload) shipped with stage 2. Stage 4 then GPU-composited the whole frontend via ImGui/SDLRenderer3, so Phase B is now: frontend backdrop cost, a capture path that includes the overlay, and cursor unification (the in-game HUD itself already landed via the in-game-GUI-as-ImGui project). Phase C (3D polygon rasterizer on the GPU) was a stretch goal, deliberately left unscoped — it's now been scoped in detail across `gpu-v2/`'s sub-plan files, informed by a companion review of an external attempt at the same problem ([`docs/merge-checks/opengl-renderer-review.md`](../../merge-checks/opengl-renderer-review.md)) and including a graphics-API recommendation (SDL_GPU, not raw OpenGL/Vulkan — `gpu-v2/02-graphics-api-choice.md`) and a call-site-consolidation phase (`gpu-v2/06-call-site-consolidation.md`) sequenced ahead of it. The original `03-gpu-renderer.md` is superseded by `gpu-v2/00-overview.md`, which is now the single entry point for this stage. | 2 |
| 4 | [04-imgui-gui-foundation.md](04-imgui-gui-foundation.md) — **Phases A–G landed; recommendation reversed** | Originally evaluated Dear ImGui only as a batched-quad backend behind `IUIRenderer`/`ITextRenderer`. That was reversed (revision note, 2026-09-04): the whole main-menu frontend (all 15 `GuiMenu`s + backdrop-text screens + a full settings schema) was migrated to real ImGui widgets, rendered through `imgui_impl_sdlrenderer3` into stage 3's own `SDL_Renderer`. In-game GUI untouched, deferred to after the GPU work. | 1 (can run in parallel with 2–3) |
| 5 | [05-imgui-linkage-consolidation.md](05-imgui-linkage-consolidation.md) — **planning** | Pure refactor: move the one remaining non-frontend ImGui translation unit (`kfx_platform/src/gui/ImGuiContext.cpp`) up into `kfx_frontend` behind a `RendererOverlayCallbacks` struct, so ImGui is compiled/linked into exactly one `kfx_*` library instead of contaminating all ten `*_utest` targets. Prereq for stage 3 Phase B. | 4 |

## Cross-references to update once implemented

- [`docs/refactor/gui/colordepth/00-notes.md`](../gui/colordepth/00-notes.md) predicted this work
  and should be updated once stage 2 lands: `land_preview_remap_screen_to_shared_palette` and the
  `frontend_palette` backup/restore dance in `land_preview_load` become removable, and the
  "quality ceiling" finding stops applying.
- [`docs/Architecture/architecture.md`](../../Architecture/architecture.md) §2.1/§2.4 describe the
  renderer seam and `kfx_render`'s ownership as currently built — revise once `RendererType`
  gains a second value and `IRenderer`'s framebuffer contract changes shape.

## Verification baseline (applies to every stage)

- `KFX_OS=linux ./build-cmake.sh` (and the mingw cross-compile) after every phase — the fastest
  signal for a seam/type change touching 22+ files.
- `python3 scripts/check_layering.py --strict` — none of these stages should need a new
  cross-library `#include`; the whole point of stages 1–3 is that they stay inside
  `kfx_platform`'s renderer seam and `kfx_render`, with `kfx_frontend` untouched at the call-site
  level.
- Existing Catch2 suites: `src/kfx_render/tests/` (`engine_camera_test.cpp`,
  `engine_redraw_test.cpp`, `LensEffect_test.cpp`, `light_data_test.cpp`,
  `LuaLensEffect_test.cpp`) and `src/kfx_frontend/tests/` are the regression net for logic that
  moves during stage 1's seam extension — grow them alongside the code they cover, per the
  existing `KFX_BUILD_TESTS` harness (`docs/Architecture/testing-harness.md`).
- Visual parity has no automated check today. Every stage needs a manual before/after comparison
  pass (main menu, in-game HUD, a lit dungeon room, a torture-room/lens-effect screen, the Land
  selection screen that started this) against screenshots from the current build — call this out
  explicitly in each stage doc's own verification section, not just here.
