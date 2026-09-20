# Pixel format & the sprite/texture cache

See [00-overview.md](00-overview.md) for stage status and shared context, and
[02-graphics-api-choice.md](02-graphics-api-choice.md) for the backend API (SDL_GPU) this cache's
output eventually feeds.

## The pixel-format decision: RGBA on the GPU, not palette-index

The reviewed branch (`origin/feature/opengl-renderer`) renders the 3D view by uploading sprite/tile
atlases as `GL_R8` **palette-index** textures, sampling a `256×1 RGBA8` palette texture per
fragment, and a `256×64` fade-table texture for lighting/remap — the standard technique for putting
an indexed-colour engine on a GPU (per
[the review](../../../merge-checks/opengl-renderer-review.md): the technique itself is executed
correctly, nearest-filtered throughout, no complaint there). **This fork does not use that
technique, and Phase C should not adopt it**, for a reason that predates this document and is worth
restating plainly: stage 2 ([../02-32bit-software-renderer.md](../02-32bit-software-renderer.md),
landed) already deliberately moved this engine off 8-bit palette indices entirely. `TbPixel` is a
real `{r,g,b,a}` struct (`bflib_video.h:62`), `lbDrawSurface` is `SDL_PIXELFORMAT_RGBA32`, and the
old `fade_tables`/`ghost`/`alpha_sprite_table` lookups were replaced with real per-pixel blend math
(`colour * shade_factor`, `src·a + dst·(1-a)`, interpolated gouraud colour). Reintroducing a
palette-index texture format for the GPU path would mean running two incompatible colour pipelines
side by side (RGBA everywhere else, indexed-plus-LUT for the one pass that's actually on screen the
most), and would silently resurrect the one-palette-per-frame ceiling stage 2 was built
specifically to remove ([../00-overview.md](../00-overview.md) §"Why now").

That's the architectural reason. There's also a performance reason, and it's the more important one
long-term: indexed-plus-LUT isn't free, and the cost model gets worse, not better, as the rest of
the renderer improves.

**Why palette-index-on-GPU doesn't scale the way "it's just one more texture fetch" suggests:**

- **Every fragment pays a dependent read.** `texture(u_sprite_atlas, uv).r` → use that result as
  the UV for `texture(u_palette, ...)`. The second sample's address isn't known until the first
  completes, so it can't be prefetched or overlapped the way two independent samples can. At DK's
  native sprite resolution this is lost in the noise; it stops being free once overdraw or fill
  rate actually matter.
- **It actively fights dynamic lighting.** Real per-pixel lighting (additional light sources,
  attenuation, coloured light, anything beyond a single baked shade factor) wants to do math in
  linear RGB space *before* any quantization back to a fixed 256-colour palette. Indexed-plus-LUT
  bakes the quantization into the sampling step itself — to light a paletted fragment "correctly"
  you either (a) resolve to RGB first and light that, which throws away the entire reason to stay
  indexed, or (b) try to keep the lighting inside palette space (a remap-table trick, exactly the
  `fade_tables`/`ghost` approach stage 2 already retired for being inflexible and visually limited).
  There is no version of "richer lighting/shadows" that composes cleanly with an indexed base layer.
- **It compounds with every additional post-effect.** Bloom, shadow mapping, tone-mapping/colour
  grading — all standard GPU techniques — operate on linear RGB. Chaining them after a
  palette-resolve step means either resolving to RGB early (again: why stay indexed at all) or
  reimplementing each effect to understand a quantized palette space (no prior art to lean on).
- **The sprite-refinement path this project actually has planned already produces RGBA, for free.**
  The [sprite → GPU-texture cache](#sprite-texture-cache) below, whose fill step is "expand indices
  through the active palette/remap into RGBA, upload once, reuse until the sprite or palette
  changes," sidesteps the performance question entirely for sprites: the resolve-to-RGBA cost is
  paid once per sprite/palette combination, not per fragment, and the GPU never sees an index at
  all.

None of this means the DOS-era palette *concept* is bad — DK's actual look depends on it, and
stage 2 preserved every one of its visual effects (fades, ghosting, gouraud shading, flash-remaps)
faithfully, just recomputed as real colour math instead of table lookups. The disagreement with
the reviewed branch is narrower and more specific: **the palette should be resolved once, on the
CPU or at cache-fill time, not smuggled onto the GPU as a runtime indirection on every fragment.**
Phase C should treat "produce true-colour texture data" as a solved problem it inherits from
Phase B/stage 2, not something to re-solve with a shader-side LUT.

## The sprite → GPU-texture cache {#sprite-texture-cache}

Originally scoped in Phase B (`RendererCreateDynamicTexture` is per-frame streaming, not a keyed
cache) and needed unchanged by Phase C — one cache, two population paths, not two.

- **Key:** sprite pointer + expansion palette + remap table (extending stage 2 §2's per-draw
  expansion cache if one was built).
- **Fill step:** expand indices through the active palette/remap into RGBA, upload once, reuse
  until the sprite or palette changes. `TbSprite` data stays 8-bit-indexed RLE on disk and in
  memory — this cache is where (and the only place) it gets resolved to true colour.
- **Second population path, needed by Phase C only:** `engine_textures.c`'s `block_ptrs[]` (the
  wall/floor texture blocks) — same cache, same key shape, same invalidation rules, just a second
  fill routine. Do not build a second, atlas-shaped cache with different invalidation rules for
  level geometry; that duplication is exactly the kind of parallel-system risk this fork's
  merge-review culture exists to catch.
- **Texture object type:** whatever [02-graphics-api-choice.md](02-graphics-api-choice.md)'s chosen
  backend needs — an `SDL_GPUTexture` if the world-view pass consumes it directly through SDL_GPU,
  or an `SDL_Texture` if only sprites (not level geometry) ever go through the classic
  `SDL_Renderer`/ImGui path. Design the cache's public shape (key → opaque handle) so the backend
  object type is an implementation detail behind it, not baked into callers — this is the same
  backend-agnostic-handle principle [04-architecture-and-ir-boundary.md](04-architecture-and-ir-boundary.md)'s
  `WorldFrame` IR uses for texture references.
- **Invalidation is the hard part (R10).** The cache key must include the active palette/remap
  table, and both frontend fades (`ProperFadePalette` mutating the active palette per animation
  step) and in-game lighting churn it. A naïve pointer-only key returns stale colours after any
  fade; a per-frame-palette key defeats the cache. Needs a generation counter on
  `RendererPaletteSet` and eviction keyed on it. Phase C's static-level-texture cache entries need
  the identical generation-counter invalidation as sprite entries, since level lighting also
  mutates the active palette — see R10 in [08-risks.md](08-risks.md).
