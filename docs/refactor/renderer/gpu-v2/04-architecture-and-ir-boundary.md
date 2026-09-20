# Architecture: where the code lives, and the boundary it must not cross

See [00-overview.md](00-overview.md) for stage status and shared context,
[02-graphics-api-choice.md](02-graphics-api-choice.md) for the backend API, and
[03-pixel-format-and-texture-cache.md](03-pixel-format-and-texture-cache.md) for the texture
source this design consumes.

Per [`docs/Architecture/architecture.md`](../../../Architecture/architecture.md) §2.1/§2.4,
`kfx_platform` (home of `IRenderer`/`RendererManager`/`RendererSoftware`) is the **bottom** of the
nine-library dependency ladder — it may depend on external libs only. `kfx_render` (home of
`engine_render.c`'s bucketed rasterizer, `engine_render_data.cpp`, lens effects, `vidmode.c`) sits
above it and already depends on `kfx_sim`+`kfx_platform`. This shape is non-negotiable and is
exactly what the reviewed branch violated (see
[05-reviewed-branch-lessons.md](05-reviewed-branch-lessons.md)). Phase C's split:

```
kfx_sim / kfx_render          →  produce a WorldFrame IR command buffer (pure data)
        │  (no upward calls; a plain struct handed down through the existing
        │   RendererManager façade, same shape as every other Renderer* entry point)
        ▼
kfx_platform (renderer seam)  →  IRenderer::SubmitWorldFrame(const WorldFrame&)
        │
        ├─ RendererSoftware   →  existing CPU path (do_a_gpoly_* / bflib_render_gpoly.c) — unchanged, kept as correctness reference and fallback
        └─ RendererGpu3D (new) →  SDL_GPU graphics pipeline implementing stage 2's real blend math against RGBA textures from the shared sprite/texture cache
```

- **The `WorldFrame` IR struct** (naming aside; lives in `kfx_platform` next to `IRenderer.h`, same
  home as `DrawState.h`) is the *only* thing crossing the seam. It should carry already-resolved
  data — texture-cache handles (not raw sprite pointers or palette bytes), transformed or
  transform-ready vertex positions, per-vertex shade/colour values — not a redo of `engine_render.c`'s
  bucket structs verbatim. `engine_render_data.cpp`/`engine_arrays.c` (this fork's existing bucket
  producer) is the natural place to grow a "flatten the current frame's buckets into a `WorldFrame`"
  step, run *instead of* (not in addition to) the CPU rasterizer when `RendererGpu3D` is active.
- **Bucket order, kept — at least initially.** A real depth buffer is "closer to a from-scratch 3D
  renderer" than a port (visibility-ordering concern, below). The reviewed branch sidesteps this
  question by *not* answering it — it consumes the same `BucketKind`/`BUCKETS_COUNT` sort order the
  CPU path already produces and submits geometry in that order, rather than introducing a depth
  buffer. That pragmatic choice is worth copying for Phase C.1: reuse the existing bucket sort as
  the GPU submission order (painter's-algorithm draw calls, no depth test needed for opaque
  geometry, blending still respects submission order for transparency) and treat "replace bucket
  ordering with real depth buffering" as an explicit, separately-justified Phase C.5 item — it
  becomes worth doing once something needs per-pixel depth (dynamic shadows, arbitrary transparent
  geometry) rather than as a prerequisite to getting tiles and creatures on the GPU at all.
- **Fragment shader = stage 2's blend math, directly.** Because stage 2 already turned every
  table lookup into real per-pixel arithmetic (`../02-32bit-software-renderer.md` §1's table), the
  fragment shader spec is now "translate `bflib_render_trig.c`'s per-fragment math into a shader
  program for the chosen backend," not "reverse-engineer 8-bit table semantics into a shader" the
  way the reviewed branch had to. Concretely: lighting is `colour * shade_factor` (a per-vertex or
  per-fragment float uniform/varying, no texture fetch), ghost/alpha is a standard
  `src·a + dst·(1-a)` blend (fixed-function blend state for the common case via
  `SDL_GPUGraphicsPipeline`'s blend descriptor, shader math only where the ratio itself varies
  per-pixel), gouraud is native vertex-colour interpolation (free on any GPU, and explicitly called
  out in stage 2's own doc as "the one case where going to true colour *simplifies* the code"). See
  [02-graphics-api-choice.md](02-graphics-api-choice.md)'s "shader delivery" open item for how this
  shader program actually gets built and shipped per platform.
- **Texture data = the [sprite/texture cache](03-pixel-format-and-texture-cache.md#sprite-texture-cache),
  extended to cover static level geometry too.** Same cache, same invalidation-on-palette-change
  design, just a second population path for `engine_textures.c`'s `block_ptrs[]`. Do not build a
  second, atlas-shaped cache with different invalidation rules.
- **Visibility ordering**, restated from the original scoping: the bucket/sort structure
  (`engine_arrays.c`, `engine_render_data.cpp`) exists because software rasterization needs
  explicit ordering; a GPU path would more naturally use depth-buffering instead — a
  different-enough algorithm that reusing bucket order (above) is a pragmatic first step, not the
  final design. Full depth-buffering is Phase C.5.
- **The `CLASSIC` legacy HUD's compositing assumption breaks once the 3D view leaves `lbDrawSurface`.**
  Per [00-overview.md](00-overview.md)'s "What the in-game-GUI project already built,"
  `GUI_ICON_PACK=CLASSIC` is a permanent, deliberately-CPU-only style — not something Phase C needs
  to bring to the GPU. But its draw calls (`draw_gui()`'s classic branches) currently assume they're
  painting sprite/box/text pixels directly on top of a `lbDrawSurface` that *already contains* the
  CPU-rasterized 3D scene, in the same buffer, in place. Once `RendererGpu3D` is active, the 3D
  scene is a separate GPU texture and `lbDrawSurface` no longer holds it — a `CLASSIC` HUD drawn
  with the old assumption would either paint over garbage/stale pixels or need `lbDrawSurface`
  cleared to a fully-transparent buffer first and composited as an overlay layer, exactly the
  pattern the ImGui overlay already uses today. This needs an explicit decision, not a silent
  assumption — see R13 in [08-risks.md](08-risks.md).
