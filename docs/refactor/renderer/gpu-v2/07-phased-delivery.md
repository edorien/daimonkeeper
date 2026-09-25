# Phased delivery

See [00-overview.md](00-overview.md) for stage status and shared context. Deliberately
smaller-grained than the reviewed branch's single "preliminary" PR ([05](05-reviewed-branch-lessons.md)),
so each phase gets its own build+test+review cycle instead of a 21k-line tail of live-discovered
fixes.

| Phase | Scope | Exit criteria |
|---|---|---|
| **Pre-C.0 — Call-site consolidation** | See [06-call-site-consolidation.md](06-call-site-consolidation.md) in full: collapse `game_session_loop.cpp`'s 8 `RendererPresentFrame()` sites to 2–3 via a `RendererPresentGameFrame()` entry point; route the remaining ~13 progress/stepping call sites through one `RendererPresentStepFrame()` entry point. Pure refactor, no behaviour change. | `check_layering.py --strict` clean; screenshot-identical across a full session, a cutscene, a net resync, a loading screen, a landview transition, a palette fade; full Catch2 suite green. |
| **C.0 — Seam + IR skeleton** — **landed 2026-09-23, see note below the table** | `WorldFrame` IR struct in `kfx_platform`; `IRenderer::SubmitWorldFrame()` with only `RendererSoftware` implementing it (by translating the IR back into the existing CPU rasterizer call — proves the IR shape is sufficient before any GPU code exists); the bucket→IR flatten step. **Also where [02-graphics-api-choice.md](02-graphics-api-choice.md)'s open items get resolved**: a throwaway SDL_GPU smoke-test spike against `SDL_RENDER_GPU`/`SDL_GPUDevice` interop on both CI targets; a decision on shader delivery (author-once-and-cross-compile vs. hand-maintained per-backend source); whether `SDL_HINT_GPU_DRIVER=vulkan` is forced on Windows too (not just taken as Linux's default) — spike this against real Windows hardware, not just CI, since Vulkan driver quality there is vendor-dependent unlike guaranteed-D3D12. | `check_layering.py --strict` clean; CPU-rasterized output byte-identical to today's (the flatten+reconstruct round-trip changes nothing visible); Catch2 coverage for the flatten step in `kfx_render/tests/` (pure data transform, no GPU needed to test it); SDL_GPU spike result, shader-delivery decision, and Vulkan-forcing decision all recorded, not left implicit. |
| **C.1 — Static level geometry on the GPU** — **landed 2026-09-24 (device slice + texture cache/live wiring), see notes below the table** | `RendererGpu3D` (new, `kfx_platform`, built on SDL_GPU per [02](02-graphics-api-choice.md)) renders tile/wall geometry only (no creatures/sprites yet) from the IR, using the extended texture cache ([03](03-pixel-format-and-texture-cache.md)) and bucket-order submission (no depth buffer yet — [04](04-architecture-and-ir-boundary.md)). Also where R13 gets decided: `CLASSIC`/`RendererGpu3D` mutual exclusion (option 1) is the default. | Visual parity screenshot pass (a lit dungeon room, a torture-room lens-effect screen, an unlit/abyss area) against the CPU path; frame-time comparison; `RendererType` selectable at runtime, CPU path unaffected when GPU path is off; enabling `RendererGpu3D` with `GUI_ICON_PACK=CLASSIC` fails clearly (R13) rather than rendering a broken frame. |
| **C.2 — Creatures, shadows, sprites** — **landed 2026-09-24, see the C.2 note below the table** | Extend the IR + `RendererGpu3D` to cover `QK_JontySprite`/`QK_CreatureShadow`/status flowers/room flags — the sprite-heavy bucket kinds. Reuses the sprite cache directly ([03](03-pixel-format-and-texture-cache.md)). | Same visual-parity pass, now including a populated dungeon with multiple creature types and an active battle (shadow + status-flower overlap case). |
| **C.3 — Lens effects** — **landed 2026-09-24 (CPU post-pass over read-back), see the C.3 note below the table** | Either a GPU-shader equivalent per `LensEffect` subclass, or a documented CPU-post-pass-over-readback decision (`RendererSwapFramebufferTarget` + the `SDL_RenderReadPixels` path Phase B2 builds). `PaletteEffect` is the easy case (a shader uniform); `DisplacementEffect`/`FlyeyeEffect` need real design time. | Every lens effect (`Mist`/`Flyeye`/`Overlay`/`Displacement`/`Palette`) verified visually against the CPU path; decision recorded per-effect (GPU-native vs CPU-post-pass), not left implicit. |
| **C.4 — Compositing with Phase B** — **audited/partly verified 2026-09-24, see the C.4 note below the table** | Confirm the GPU 3D output slots into `PresentFrame` exactly where the CPU framebuffer texture does today (resolved concretely at C.0 via the SDL_Renderer/SDL_GPU interop decision, [02](02-graphics-api-choice.md)) — no new present architecture, one texture source swaps for another, ImGui/2D layer composites on top unchanged. | A full session (frontend → load level → play → in-game menu → parchment map → exit) with `RENDERER_GPU3D` active shows no compositing regression versus the CPU path. |
| **C.5 — Stretch: depth buffering, render thread** — **first slice landed 2026-09-24 (overlay lines/boxes in draw order); depth buffer and render thread deliberately NOT built, see the C.5 note below the table** | Only after C.1–C.4 have shipped and been profiled. Real depth buffering (replaces bucket-order submission) if transparent-geometry or dynamic-shadow work actually needs it; a dedicated render thread only if profiling shows the main-thread GPU submission cost is a real bottleneck, with its own R2/R12-aware design doc before any code. | Not scoped further here — deliberately, matching the "do not scope this until profiling data exists" posture the rest of this stage takes. |
| **C.6 — Future, unscoped: hardware ray tracing** | Not part of this delivery plan — recorded here only so it isn't silently forgotten. Per [02-graphics-api-choice.md](02-graphics-api-choice.md#future-hardware-ray-tracing-out-of-scope-for-phase-c), checked directly against the vendored SDL3 3.4.12 source: SDL_GPU has no acceleration-structure/ray-tracing-pipeline surface and no documented way to retrieve the native `VkDevice`/`VkQueue` it owns, so this cannot be grown incrementally out of `RendererGpu3D` — it needs its own raw-Vulkan-owned device/path designed fresh, whenever actually pursued. | Not scoped — depends on C.1–C.5 landing first and a real design doc at that time; re-check SDL_GPU's then-current capabilities before assuming this conclusion still holds. |

**C.0 — Landed as:** a real, tested slice, deliberately narrower than the row above's prose implies
in two ways, both corrected here rather than left as a silent gap.

- **Where the flatten step actually lives — a corrected assumption, not the plan's original guess.**
  The original text expected `engine_render_data.cpp` to grow this. Checked directly: that file only
  holds `stripey_line` colour-array data (unrelated to buckets); the real bucket list
  (`enum QKinds`, `buckets[BUCKETS_COUNT]`, every `struct BucketKind*`, and `display_drawlist()`,
  the function that walks them) lives entirely in `engine_render.c`, and those types are file-private
  (declared in the `.c`, not `engine_render.h`). The flatten step lives there instead —
  `flatten_polygon_standard_item()` and `collect_world_frame_polygon_standard_items()`, declared in
  `engine_render.h`, implemented in `engine_render.c` right after `display_drawlist()`. No bucket
  type was promoted to the header to make this work; the flatten functions' own signatures (already-
  extracted `struct PolyPoint`s + a resolved texture pointer, not a `BucketKindPolygonStandard*`) are
  what's testable from `kfx_render/tests/`, not the bucket list itself.
- **Scope: `QK_PolygonStandard` only, not the full bucket set.** Of the eleven `QKinds`, C.0 flattens
  the simplest real case — a single `draw_gpoly()` call per bucket item — to prove the seam without
  first taking on `QK_PolygonNearFP`'s perspective-correct subdivision
  (`draw_subdivided_near_polygon()`, first-person near-plane geometry) or the sprite/creature kinds.
  This isn't a shortfall against the plan: C.1 is explicitly scoped to "tile/wall geometry only (no
  creatures/sprites yet)" and C.2 to the sprite-heavy kinds — QK_PolygonNearFP (also tile/wall) is
  C.1's job to add alongside `RendererGpu3D` itself, not C.0's.
- **The flatten+reconstruct path is real but deliberately dormant — not wired into
  `display_drawlist()`'s live `QK_PolygonStandard` case.** `RendererSoftware::SubmitWorldFrame()`
  reproduces that case's exact `vec_mode`/`vec_map`/`draw_gpoly()` sequence, so the round-trip is
  byte-identical by construction (same three operations, same values, just relocated across a
  function-call boundary) — but redirecting the actual hot per-frame loop through it now, with only
  one (CPU) consumer, would add an indirection cost for no live benefit and no way to verify "changes
  nothing visible" in this environment (no display — the same constraint every other phase this
  session has hit). Wiring it in is deferred to C.1, where `RendererGpu3D` becomes a second, real
  consumer that justifies paying for the redirection — at which point both backends exercise the same
  live call site, which is also the more meaningful place to do the interactive/screenshot check this
  row's exit criteria asks for.
- **Verified:** both `build-cmake-linux.sh` variants (`KFX_OS=linux`, `KFX_OS=windows`, both
  `keeperfx`/`keeperfx_hvlog`) build clean, zero warnings in any changed file; `check_layering.py
  --strict` clean; full Catch2 suite green (1936/1936, up from 1934 — the two new
  `flatten_polygon_standard_item()` tests in `kfx_render/tests/engine_render_worldframe_test.cpp`,
  covering the lossless-copy and no-aliasing properties of the one real transform C.0 introduces).
- **SDL_GPU smoke-test spike — run, with an honest partial result.** A minimal standalone program
  (not `SDL_test`-based, no window needed — device creation alone answers the question) was compiled
  and linked against this project's own vendored SDL3 3.4.12 (`out/linux/_deps/sdl3-build`) with zero
  changes needed. Findings: `SDL_GetNumGPUDrivers()` reports exactly one compiled-in backend on
  native Linux (`"vulkan"`), confirming the `CMakeCache.txt` finding from
  [02](02-graphics-api-choice.md) directly at runtime; `SDL_HINT_GPU_DRIVER`'s name-match-then-
  `PrepareDriver()` logic (`SDL_gpu.c`'s `SDL_GPUSelectBackend()`) behaves exactly as documented,
  confirmed by reading it, not assumed. **What could not be verified here:** actual
  `SDL_CreateGPUDevice()` success/failure, on either the default-priority path or forced
  `SDL_HINT_GPU_DRIVER=vulkan` — both fail in this specific dev sandbox because its Vulkan ICD loader
  is broken (every ICD under `/usr/share/vulkan/icd.d/` fails to `dlopen` with `undefined symbol:
  wl_fixes_interface`, a Wayland library version mismatch in the container image, independently
  reproduced with the system `vulkaninfo` tool). This is an environment packaging defect, not a
  finding about SDL_GPU, Vulkan, or this project's build — but it means the actual "does device
  creation succeed" question is still open, not settled, and must be re-run on the real CI runner (or
  any host with a working Vulkan ICD, e.g. Mesa's llvmpipe/lavapipe software rasterizer) before
  treating it as answered.
- **Shader-delivery decision: author GLSL, compile to SPIR-V via `glslangValidator`/`glslc`, no
  cross-compilation tooling needed.** A direct consequence of forcing Vulkan as the sole backend
  (below) rather than a separately-spiked decision: with exactly one target shader bytecode format,
  the "author once, cross-compile to three backends" problem [02](02-graphics-api-choice.md)
  originally posed doesn't arise — `SDL_shadercross` or an equivalent stays unneeded. Still real,
  uncosted build-pipeline work for C.1 (wiring a GLSL→SPIR-V compile step into `build-cmake-linux.sh`
  has no precedent in this codebase's asset pipeline), just a narrower one than the three-backend case.
- **Vulkan-forcing decision: recommended, mechanism confirmed correct, Windows outcome still open.**
  [02](02-graphics-api-choice.md#forcing-vulkan-as-the-sdl_gpu-backend-pre-phase-c-decision-checked-directly)'s
  recommendation to force `SDL_HINT_GPU_DRIVER=vulkan` on every platform stands — the spike confirms
  the forcing mechanism itself works exactly as documented. What it could *not* confirm, for the same
  broken-ICD-loader reason above, is whether a real Windows machine's Vulkan driver actually succeeds
  under this forcing (the concern the original C.0 row flagged: Vulkan driver quality on Windows is
  vendor-dependent, unlike guaranteed-D3D12) — that spike still needs real Windows hardware, not just
  this sandbox or the mingw cross-compile (which only proves the binary builds, not that it runs
  correctly). Carried forward as an open item, not silently assumed to have passed.

**C.1 — first slice, Landed as:** the GPU device/pipeline/shader machinery and the user-facing renderer
switch, verified against a real Vulkan device; **not** the visible GPU-rendered world view — that half of
C.1 is still open, stated plainly rather than implied done.

- **Built and verified:** `RENDERER_GPU3D` + `RendererGpu3D` (`kfx_platform`): a real `SDL_GPUDevice` (Vulkan
  forced via `SDL_HINT_GPU_DRIVER`, per [02](02-graphics-api-choice.md)), real GLSL vertex/fragment shaders
  (`src/kfx_platform/shaders/*.glsl`, compiled to SPIR-V, `spirv-val`-clean, embedded as
  `include/renderer/shaders/*.spv.h`), a graphics pipeline, vertex/uniform/sampler binding, and a real
  `SubmitWorldFrame()` draw into an offscreen render target. Everything else (`PresentFrame`, ImGui, UI/text,
  screenshots, dynamic textures) is forwarded to an internally-owned `RendererSoftware`, which
  `RendererSoftware::UseGpuDevice()` makes share the same `SDL_GPUDevice` (SDL_Renderer created on the `"gpu"`
  driver via `SDL_PROP_RENDERER_CREATE_GPU_DEVICE_POINTER`) — 02's "preferred" interop answer. **Not yet
  verified at runtime:** that shared-device `SDL_Renderer` path (`ensure_present_target()`'s GPU branch) — the
  spike below exercised `RendererGpu3D` alone, with no live game/present loop.
- **Runtime verification, real, not assumed:** a throwaway program linked against the real `libkfx_platform.a`
  drove `RendererGpu3D::Init()` + `SubmitWorldFrame()` on Mesa llvmpipe (a conformant software Vulkan device),
  read the target back, and checked pixels against `render_shade()`'s formula: shade 32 → exactly (255,255,255),
  shade 16 → (128,128,128), pixels outside the triangle untouched. Deliberately not a Catch2 test — it needs a real
  window/GPU, the boundary `RendererManager_test.cpp`'s header documents this suite declining.
- **Bug found only by running it:** SDL_GPU normalises all backends to NDC-Y-*up*, so the vertex shader must flip
  DK's Y-down screen coordinates (a no-flip version drew the mirrored half — and my first two test pixels sat
  exactly on the diagonal, which cost a long debugging detour; check off-edge pixels).
- **Not done in this first slice (done in the second slice below):** live wiring, real textures, `QK_PolygonNearFP`.
- **Renderer switch:** `RENDERER=SOFTWARE|VULKAN` (keeperfx.cfg key 60) and an Options → Graphics row
  ("Renderer", `SApply_NeedsRestart`, help text says it is experimental and currently renders identically to
  Software). `main.cpp::setup_game()` swaps backend right after `load_configuration()`, because
  `RendererInit(RENDERER_SOFTWARE)` in `LbBullfrogMain()` runs *before* config is loaded (needed for the
  legal/splash screens). `RendererInit()` now falls back to Software if a non-Software backend fails to
  initialise. **R13 resolved (fallback flavour):** Vulkan + `GUI_ICON_PACK=CLASSIC` logs a warning and uses
  Software instead of rendering a broken frame.
- **Environment note (the C.0 "broken ICD loader" was fixable):** the AMDGPU driver package installs a
  `libwayland-client.so.0` under `/opt/amdgpu/lib` that shadows the system one and lacks `wl_fixes_interface`,
  breaking every Mesa Vulkan ICD. `LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libwayland-client.so.0` fixes it; with
  `SDL_VIDEODRIVER=offscreen` (the vendored SDL3 has no x11 driver; `dummy` has no Vulkan surface support)
  `SDL_CreateGPUDevice()` succeeds on llvmpipe. This supersedes C.0's "device creation still open" caveat for
  this machine (Linux only; real Windows hardware remains untested).
- **Launch workaround (found by the first live trial, 2026-09-24):** on this machine the game logged
  `SDL_CreateGPUDevice (vulkan) failed: SDL_HINT_GPU_DRIVER vulkan unsupported!` and (correctly) fell back to
  Software — the same `/opt/amdgpu` libwayland shadowing described above, hitting the real game. Reproduced with
  and without the fix; `scripts/run-keeperfx-vulkan.sh` preloads the system `libwayland-client` only when it
  detects the shadowing, and with it the log shows `SDL_GPU device ready`. `RendererGpu3D` now logs a pointer to
  the script on this failure. Proper fix is per-machine (remove/reorder the `/opt/amdgpu` ld.so.conf entry);
  the game itself can't reasonably fix a process-wide loader-order problem.
- **Shader tooling:** no `glslang` is available/installable here; it was built from source into a scratch dir to
  compile the shaders. Regenerating after editing a `.glsl`: `glslang --target-env vulkan1.0 -V -S <vert|frag> -o
  x.spv x.glsl`, then re-emit the `.spv.h` (uint32 array). There is still no CMake step that does this.
- **Verified:** native Linux + mingw (`keeperfx`/`keeperfx_hvlog`) build clean, no warnings in changed files;
  `check_layering.py --strict` clean; ctest 1939/1939 (one editor test flaked once under `-j8` — file
  contention, passes alone and on rerun).

**C.1 — second slice (texture cache + live wiring), Landed as:** selecting Vulkan now renders the game's
terrain on the GPU, verified in the real game, not just a spike.

- **Live wiring:** every textured world-view triangle in `engine_render.c` — isometric `QK_PolygonStandard` and
  every first-person `QK_PolygonNearFP` subdivision case — now goes through one `world_draw_gpoly()` wrapper
  (all ~70 former `draw_gpoly()` call sites, plus the front-view textured quad), which reads `vec_map` and either
  rasterizes on the CPU (unchanged when Software is active, or when the target isn't the real framebuffer — the
  eye-lens off-screen render, parchment) or appends a `WorldFramePolyItem`. `display_drawlist()` brackets the
  bucket walk with `gpu_world_frame_begin()/end()` → `RendererSubmitWorldFrame()`. This replaces C.0's dormant
  `collect_world_frame_polygon_standard_items()`, now deleted; `flatten_polygon_standard_item()` stays.
  Front view (`display_fast_drawlist`) stays CPU.
- **Compositing:** `RendererGpu3D` renders into its own target and hands it to the internal `RendererSoftware`
  as an *underlay* (`SetWorldUnderlay()`, wrapped as an `SDL_Texture` via
  `SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER` on the shared device — no CPU round-trip). That present draws
  underlay first, then the CPU framebuffer alpha-blended on top; the view window of the CPU surface is cleared
  transparent for the frame, and the GPU target is cleared to the window's former clear colour so empty regions
  match. `WorldFrame` gained `view_x/y/w/h` (window origin + scissor) and `clear_r/g/b`.
- **Texture cache ([03](03-pixel-format-and-texture-cache.md), R10):** a 2D-array texture (1024 layers, fallback
  256) of 32×32 RGBA blocks, one layer per distinct `block_ptrs[]` pointer; the fragment shader indexes it by a
  per-vertex layer, so a whole frame is **one draw call**. Palette index → RGBA happens once at upload
  (`expand_indexed_pixel`, index 0 transparent, exactly the CPU rasterizer's expansion). Invalidation is by
  **content comparison, not a generation counter**: each block's 1024 indices are compared per frame (once per
  distinct block) and re-uploaded on change, and the whole cache is flushed when the 768-byte palette differs —
  which also catches in-place animated-texture rewrites that a pointer-keyed cache would miss (R10's core
  worry). Full array → extra blocks draw white for that frame, flushed next frame (logged once).
- **Parity:** the fragment shader samples U/V/shade half a pixel back along the screen gradient
  (`dFdx/dFdy`), because the CPU rasterizer evaluates attributes at a pixel's corner and the GPU at its centre —
  without it texel boundaries were off by a pixel across the whole image (measured: 79% → 92% of pixels within
  tolerance on a synthetic patterned triangle). Remaining differences are hypotenuse-edge coverage pixels and a
  few one-pixel texel-boundary columns. Real game, level 1, identical settings, software vs Vulkan, later frame:
  mean per-channel-sum difference 2.1, 0.3% of pixels differing by more than 24 (camera/turn timing keeps two
  runs from being pixel-identical, so this is an upper bound on the renderers' own difference).
- **Verified live:** the real game (`core_files/` data, `-level 1`) run headless (`SDL_VIDEODRIVER=offscreen`,
  llvmpipe) under both renderers, captured with a temporary env-driven screenshot hook (removed before
  commit); log shows `SDL_GPU device ready (llvmpipe…)` and `Presenting through SDL renderer: gpu`. Plus the
  spike, which asserts CPU sprite over GPU terrain, palette change and in-place texture rewrite re-upload,
  view-window offset + clipping, and no stale underlay on a CPU-only frame. Native Linux + mingw both
  variants clean, layering clean, ctest 1940/1940.
- **Live-trial fix — heart-intro flicker (2026-09-24):** repeated flashes during the heart intro were
  presents that carry no new world frame (palette-fade steps re-present the last frame) showing the CPU
  surface alone, whose world window is transparent → terrain vanished on those frames. The underlay now persists
  across presents until the next `ClearScreen()`/new world frame. Found by capturing every 3rd frame of the intro
  under both renderers and looking for isolated brightness dips (the GPU run had one; none after the fix). Also
  fixed while there: a fly-through accumulates more distinct blocks over time than the 1024 layers, so the cache
  now reclaims layers of blocks unused this frame instead of drawing white until a flush (verified: 1800
  distinct blocks over 6 frames, never white).
- **Known caveats until C.2 (superseded — fixed by C.2, see its note below; kept as the record of what C.1 shipped with):** (1) CPU-drawn creatures/things draw *over* all GPU
  terrain, ignoring nearer walls' occlusion (the painter's-order interleave is lost); (2) translucent CPU blends
  (creature shadows, ghosts) read a transparent destination in the GPU-covered window — shadows likely missing,
  not verified; (3) QK_TextureQuad front view is CPU; (4) **no frame-time measurement yet** — the game's tick
  limiter dominated both headless runs; a real GPU on real hardware, and a Windows/Vulkan check, are still
  untested; (5) a fresh `SDL_GPUTexture` wrapper is created only when the target texture changes (window
  resize) — resize/mode-change while in Vulkan mode is not exercised.

**C.2 — Landed as:** sprites, effects and shadows now render on the GPU **in the same draw order as
terrain**, so walls occlude creatures exactly as the CPU painter's algorithm did (the live trial's "overlapping
walls/creatures" report).

- **Design — capture at the primitive level, in order.** Like `world_draw_gpoly()` for terrain, the software
  sprite dispatchers record instead of rasterizing while a world frame is being built
  (`SwCaptureSprite()` in `bflib_vidraw.c`, called from `LbSpriteDrawUsingScalingData`,
  `DrawAlphaSpriteUsingScalingData`, `LbSpriteDrawOneColourUsingScalingData`, `LbSpriteDrawRemapUsingScalingData`),
  and `trig()` records creature-shadow triangles. Everything the engine draws through those — creatures, things,
  objects, flames, status icons, room flags, floating numbers — is captured with no per-caller changes. The IR
  is now an ordered op stream (`WF_OP_POLY/SPRITE/SHADOW`, `WorldFrame.h`); recording moved from `engine_render.c`
  into a pure, unit-tested `WorldFrameRecorder` behind `RendererWorldFrame*` in `RendererManager` (Begin also
  does the eligibility check + transparent-clear that used to live in `engine_render.c`).
- **Exact scaling, not approximated:** a sprite op carries the CPU path's own scaling step tables
  (`xsteps_array/ysteps_array`, already flipped/clipped for that draw) converted to per-destination-column/row
  source lookups (uploaded as an R16 texture), so the GPU's source→destination pixel mapping is the CPU's.
  Sprites are RLE-decoded once into an 8-bit-index atlas (4096², content-hashed so in-place changes re-upload)
  and coloured in the shader through a 256-entry RGBA table per op — the active palette or the remap/shade table
  the sprite was drawn with (a 64-row table texture, de-duplicated per frame) — so palette fades and per-creature
  shading never invalidate the atlas. (This is the palette-lookup-in-shader design [03](03-pixel-format-and-texture-cache.md)
  argues against for terrain; for sprites the CPU path itself already colours through per-draw RGBA tables, and
  index→table lookup is what keeps that cheap.)
- **Blend modes** map to fixed-function blend state: solid and one-colour overwrite; the two ghost modes
  (`(ref+2·dest)/3`, `(2·ref+dest)/3`) use blend constants; the alpha-ramp sprite (`render_alpha_blend`) is drawn
  twice — an additive pass and a subtract pass for its 'black' ramp; creature shadows (`render_shade` on the
  destination where a 256×256-mask texel is set) multiply the destination. Shadow masks are cut down to their used
  rectangle at record time (the engine reuses that scratch buffer) and cached by content hash.
- **Verified:** `src/kfx_platform/tests/gpu/gpu3d_verify.cpp` (opt-in target, run by hand, exits 0 on pass) checks
  against a real Vulkan device: 2× scaled / flipped / transparent-row sprites, remap-table colouring, ghost1/ghost2
  exact blend results over known terrain, both alpha ramps, one-colour, **sprite-behind-terrain occluded and
  sprite-in-front visible**, shadow darkening, plus everything from C.1. New unit tests for the recorder
  (`WorldFrameRecorder_test.cpp`, replacing C.0's flatten test). **Live, in the real game** (levels 1 and 2 under
  both renderers, paused at the same game turn so scenes are pixel-comparable): the dungeon heart, pillars, torches,
  red flames and walls match the software renderer (mean per-channel-sum difference ~2, ≤0.7% of pixels differing
  by more than 24, all on edges), with the front pillar correctly occluding the heart. Creatures themselves were
  not on screen in any captured scene — creature sprites/shadows are covered by the synthetic checks and share
  every code path with the objects above, but a live creature-and-shadow screenshot is still worth eyeballing.
- **Bugs found on the way (by running it):** the one-colour sprite mode's enum value collided with the shader's
  internal 'alpha-subtract' mode (silhouettes silently drew as a subtract pass); the pipeline array was one entry
  short after adding the shadow mode (crash in `Init`); both caught by the verification program.
- **Still CPU-drawn, over everything, on the transparent window:** selection outlines/boxes/pixels
  (`draw_clipped_line`, `LbDrawBox`, `LbDrawPixel`), the eye-lens effect (an off-screen render), the front view,
  and anything drawn outside `display_drawlist()` — so those ignore wall occlusion and any translucent CPU blend
  there sees a transparent destination. **Not measured:** frame time (the headless runs are on llvmpipe, a CPU
  Vulkan implementation, and were slower than software, as expected — this needs your GPU), and a Windows/Vulkan run.
- **Cost to know about:** the whole per-frame lookup table (a few hundred KB), the colour tables and any newly seen
  sprites are uploaded every frame; hundreds of sprite draw calls per frame, merged where adjacent and same-mode.

**C.3 — Landed as:** decision for **every** lens effect (`Mist`/`Flyeye`/`Overlay`/`Displacement`/`Palette`, plus Lua
custom lenses): **CPU post-pass over a read-back frame** — none is ported to a shader. Rationale: all of them are
already CPU passes reading an arbitrary source buffer (displacement maps sample anywhere in the source), they only
run in the possession first-person view, and porting them would duplicate `LensManager` and every Lua-defined
lens for no frame-time win that matters there. What changed is how the source frame is produced. Before, with any
lens active `draw_creature_view()` redirected the *whole engine* into a CPU buffer (`RendererSwapFramebufferTarget`),
which under Vulkan silently meant the CPU rasterizer (redirect disables world frames). Now, when
`RendererWorldFrameActive()`, it renders normally on the GPU, then `RendererCopyFrameRect()` builds the lens source:
`IRenderer::ReadbackWorldLayer()` (`RendererGpu3D`: download-from-texture into a reusable transfer buffer, fence
wait) gives the GPU image and the CPU framebuffer (overlays, swipe graphic) is alpha-blended over it; the
unchanged `draw_lens_effect()` then writes the result back into the framebuffer. The Software renderer keeps the
old redirect path untouched.
- **Verified:** `gpu3d_verify` frame 7 (read-back of a sub-rect equals the presented GPU image, is opaque, leaves the
  rest of the buffer untouched, and is refused after `ClearScreen()`); a Catch2 test for `RendererCopyFrameRect`'s
  no-GPU copy/clip path; layering, ctest (1946), Linux + Windows builds clean.
- **Not verified live:** the possession + lens path was **not run in the real game** (no headless way to possess a
  creature with a lens found in reasonable effort) — it wants a live check of each lens type. Cost: one blocking GPU
  download per lens frame (a stall, fine for a possession view but a real cost on slow GPUs).
- **Selection outlines/boxes** (`draw_clipped_line` → `LbDrawPixel`, opaque, into the CPU layer) are composited over
  the whole GPU image, so they show on top of everything — by design at this stage — but do not get occluded by nearer walls.

**C.4 — Landed as:** the compositing seam needed no new architecture (as planned) — the GPU world target is an
underlay drawn by the same `PresentFrame()` that draws the CPU texture, with ImGui on top. This phase was an audit
plus what could be driven headless:
- **Audited by code, findings:** (1) `RendererScreenOwned()` screens (migrated frontend screens, the parchment map)
  skip the underlay and CPU upload entirely, so no world pixels can show behind them; (2) the underlay is
  dropped by `ClearScreen()`, which every game frame (`engine_redraw.c`), mode switch, video and land-view transition
  issues — presents without a clear (palette-fade steps, loading progress) deliberately keep it; (3) screenshots
  and movie capture read the composited present, and `gpu3d_verify` confirms a capture contains the GPU layer.
- **Bug found and fixed:** the SDL texture wrapping the GPU world target was re-created only when the target's
  *pointer* changed; a resize could recycle the same pointer and leave a wrapper of the old size. It now also
  tracks the wrapped size. `gpu3d_verify` frame 8 resizes the draw surface mid-session and checks the result.
- **Headless real-game run:** level 2 under Vulkan for 400 game turns, no renderer errors or warnings in the log.
- **NOT done — needs your live pass:** the plan's full session (frontend → level → in-game menu → parchment map →
  exit) can't be driven headless (no input). Please walk it with Vulkan selected and report any regression;
  particular suspects: in-game pause/options overlay, parchment map open/close, level exit back to the frontend,
  ALT+R resolution switch, fullscreen toggle.

**C.5 — Landed as (first slice) and what was deliberately not built:**
- **Built: solid rects/lines/pixels now go through the op stream.** The one concrete defect C.1–C.4 left
  (selection outlines/boxes drawing on top of walls) turned out not to need a depth buffer: the lines are already
  queued in the depth *buckets*, so recording them in walk order gives exactly the CPU painter's result.
  `SwCaptureRect()` (`bflib_vidraw.c`) records a window-clipped opaque rectangle as a one-colour 1x1 sprite stretched
  over the rect (no new shader, pipeline or IR op), hooked into `LbDrawPixel` (the stripey selection lines),
  `LbDrawHVLine` and `LbDrawBoxClip` (health bars, room-flag boxes) — only while a world frame is being recorded,
  only for opaque colours, and not under the ghost draw flags (those stay on the CPU). `gpu3d_verify` frame 9 checks
  a rect is hidden by a nearer terrain triangle and visible elsewhere. Cost: one op per line pixel (a few thousand
  quads in a selection-heavy frame) — unmeasured; a dedicated line op would cut it if profiling shows it matters.
- **Depth buffering:** first built as behaviour-preserving infrastructure (see the depth-buffer note below) once per-pixel
  lighting was named a long-term goal; true per-vertex depth is still not built.
- **Not built: render thread.** The plan requires profiling first plus an R2/R12-aware design doc. There are no
  frame-time numbers on real hardware yet (headless runs are on llvmpipe). Highest-value next step is measurement,
  not code: per-frame cost of terrain upload, sprite upload/lookup traffic, the recorder walk, and the lens read-back.
- **Still CPU-drawn:** front view (`display_fast_drawlist`), the eye-lens pass itself, ghost-blended boxes/lines,
  and anything outside `display_drawlist()`.

**Measurement tooling (heavy-log build only), landed 2026-09-24.** `keeperfx_hvlog` writes an `RPROF` line to
`keeperfx.log` every 120 game frames, for either backend, in the same format:
`RPROF [gpu3d (vulkan)] 120 frames: period 7.4/8.9 ms (avg/max, 135 fps) | drawlist 3.0/4.2 submit 2.8/3.5 build .. encode .. gpuwait .. present 3.9/5.0 | ops .. polys .. sprites .. shadows .. verts .. draws .. blk_up .. spr_quads .. atlas_B .. lookups ..`.
Times are avg/max ms per frame; counters are per-frame averages. `drawlist` is the whole world walk (CPU raster on
Software, recording + submit on Vulkan); `submit`/`build`/`encode` are inside it (Vulkan only); `present` is the
composite + ImGui + present, **including vsync wait** (set `VSYNC=OFF`); `readback` is the eye-lens GPU->CPU copy.
`gpuwait` (fence wait for the world pass = real GPU time) is measured only with `KFX_GPU_PROF_SYNC=1` because forcing
the wait serialises CPU and GPU. Implementation: `renderer/RendererProfile.{h,cpp}` — the `RPROF_*` macros compile to
nothing when `BFDEBUG_LEVEL == 0`, verified by `nm` (no `RendererProfile` symbols in `keeperfx`). Caveat: the hvlog
build also logs at debug level 10, which itself costs frame time — compare hvlog-vs-hvlog only.
Reference run on this dev box (level 2, llvmpipe = CPU Vulkan, so **not** representative of a real GPU): Software
period ~6.3 ms, drawlist ~5.5 ms; Vulkan period ~7.4 ms, drawlist ~2.9 ms, of which ~2.6 ms is the GPU pass (llvmpipe
rasterising on CPU threads). A real-GPU run is still needed.
**Found by it, not by the numbers:** `keeperfx_hvlog` creates the SDL_GPU device with debug mode on, whose validation
asserts aborted on the sprite scaling-lookup texture (`R16_UINT` with `SAMPLER` usage — SDL_GPU forbids sampling integer
formats; llvmpipe and the non-debug build tolerated it, other drivers might not). Fixed: the lookup is `R16_UNORM`
read with `texelFetch` and rounded back to the integer in the shader. Running the hvlog build with Vulkan is now
also the way to get SDL_GPU's own validation.

**C.5 — depth-buffer infrastructure, landed 2026-09-24 (behaviour-preserving by construction).** Motivation: per-pixel
lighting is a long-term goal and needs a depth buffer to reconstruct positions from — so the plumbing goes in now.
- **What exists:** a `D32_FLOAT` depth target (same size as the world target, `SAMPLER` usage so a later lighting pass can read
  it) cleared to 1 (far) and *stored* every world frame; every terrain/sprite/shadow vertex carries a depth
  (`WorldFrameOp::depth`, 0 near .. 1 far) that becomes `gl_Position.z`; all pipelines depth-test `LESS_OR_EQUAL`, and
  only opaque overwrites (terrain, solid and one-colour sprites) write depth — ghost/alpha/shadow blends test but don't write.
  The terrain fragment shader now `discard`s fully transparent texels so they cannot write depth.
- **What the depth is, today:** the draw-list bucket (`display_drawlist()` calls `RendererWorldFrameSetDepth(bucket, BUCKETS_COUNT)`
  per bucket; buckets are 16 z-units wide, so this is world distance quantised, far = high index = 1). It is *not*
  per-vertex perspective depth — `PolyPoint` has no Z — so all of a polygon's pixels share one depth. The recorder clamps depth
  non-increasing in submission order (`SetMonotoneDepth(true)`, the default), so a later op is never farther than an earlier one,
  the depth test never rejects anything painter's order would have drawn, and **output is identical to before**
  (checked: level 2 in the real game looks right; `gpu3d_verify` frames 1–9 unchanged).
- **Verified:** `gpu3d_verify` frame 10 turns the clamp off and shows the depth test, not submission order, decides
  visibility (a farther op recorded later is rejected; a nearer one wins); two recorder unit tests for stamping/clamping;
  the hvlog build (SDL_GPU debug validation on) runs the real game with no assertions.
- **Cost:** one extra full-size `D32` attachment (4 bytes/pixel; ~3 MB at 1024x768, ~8 MB at 1080p) and its clear/store per frame —
  unmeasured on real hardware (the `RPROF` line will show).
- **Reading `RPROF`:** the frame `period` is dominated by the game's turn pacing (the loop waits for the next turn), so it
  hides headroom — a faster renderer barely moves it. Read `drawlist` + `present` (the work) rather than `period`.

**C.5 — per-vertex depth (the "z plumbing"), landed 2026-09-24; off by default behind `GPU_TRUE_DEPTH=ON`.**
- **What:** `struct PolyPoint` gained `Z` (`bflib_render.h`), the vertex's depth as fixed-point 0 (near) .. `WORLDFRAME_DEPTH_ONE` (far),
  `(1 - near/z) / (1 - near/far)` with near = 32, far = 65536 view-z units. That form is affine in 1/z, so it is **linear in screen space
  under perspective** and plain interpolation across a triangle gives the exact per-pixel depth (unit-tested: the depth of the
  1/z-midpoint is the mean of the endpoint depths). Filled at all 29 places `engine_render.c` builds a `PolyPoint` from an `EngineCoord`,
  and in `perspective_standard()`/`perspective_fisheye()` — which is what gives the first-person near-plane *subdivision* points
  their depth for free (they are made by projecting an averaged `XYZ`). The CPU rasterizer ignores `Z`.
- **Two modes**, chosen per frame from `GPU_TRUE_DEPTH` (`keeperfx.cfg`, experimental, no options-menu row; `RendererSetTrueDepth()`):
  *off* (default) = bucket depth, clamped non-increasing → identical to the CPU painter's algorithm; *on* = terrain triangles use their
  per-vertex `Z`, sprites/shadows/rects use their bucket's depth converted with the same function (so they compare consistently),
  the recorder does not clamp, and the GPU depth buffer decides visibility (`WorldFrame::true_depth`).
- **Verified:** `gpu3d_verify` frame 11 — a constant-depth green triangle and a red one whose depth ramps across it, drawn later: red
  wins on the near side of their intersection and loses on the far side (a painter's-order renderer would show all red). In the real game
  (level 2, headless) `ON` renders correctly and matches `OFF` (mean per-channel-sum diff 1.1; 0.6% of pixels differ by more than 24,
  in a few small slivers near wall edges and around moving creatures — **not investigated**, the pause point isn't frame-exact so animated
  things differ regardless). The hvlog run with `ON` and SDL_GPU validation shows no assertions.
- **Not done, deliberately:** sprites/shadows/lines still have one depth per object (their bucket), not per-pixel; the eye-lens read-back and
  the front view don't use it; the depth buffer isn't consumed by anything yet (that's the lighting pass); no depth read-back helper for tests.
  Making `ON` the default needs a real-hardware comparison first (bucket-vs-vertex disagreements are where output would change).

**C.5 — lighting pass v1 ("per-pixel" lighting), landed 2026-09-24 (its *reach-grid shadowing* was replaced the same day by the per-light shadow term below). Options → Graphics → Lighting: Classic | Per-pixel (`LIGHTING=` in `keeperfx.cfg`); live-switchable; only has an effect with the Vulkan renderer; forces per-vertex depth on.**
- **What it is:** the engine's lighting is render-only (nothing in `kfx_sim` reads it — checked), and `light_render_area()` rebuilds
  `lish.subtile_lightness` every frame as *static lights + ambient* (`stat_light_map`) then max-combines every dynamic light on top
  (`light_render_light_dynamic`: `intensity * (radius - dist) / radius`, gated per subtile by the light's shadow cache). In per-pixel mode it
  still renders the dynamic lights (that is the engine's own wall-shadow result) but (1) records *where any dynamic light reached* as a
  per-subtile 255/0 "reach" grid, (2) collects each light's `(x, y, radius, intensity)` (`light_perpixel_add`), and (3) restores the
  grid to static-only, so terrain vertex shade carries only static light. `submit_perpixel_lighting()` (`engine_render.c`) then sends
  the GPU the lights, the reach grid, the perspective (`lens`, centre), the vertex-shade distance fade, and the view→map x/y transform
  — measured by projecting four probe points through the engine's own `pers_set_transform_matrix` (the transform is affine) and inverting.
  The terrain fragment shader rebuilds each pixel's map position from its depth (`world_polygon.frag.glsl`: inverse of the hyperbolic
  depth, then the inverse perspective), evaluates the lights with the classic formula per pixel (max-combined like the engine), applies
  the same distance fade, multiplies by the bilinearly-filtered reach grid, and takes `max(static gouraud shade, dynamic)`.
- **Verified:** `gpu3d_verify` frame 12 (hand-built perspective, one light: full brightness at the light, an intermediate value at 21/30 of
  the radius that matches the formula, unlit far away — i.e. the depth→position reconstruction is right); recorder + manager unit tests;
  the real game (level 2, headless): classic and per-pixel screenshots are close, differing mostly around the dungeon-heart light
  (mean per-channel-sum difference 3.8 incl. animation noise; 3.6% of pixels >24); hvlog (SDL_GPU validation) clean; ctest 1951/1951.
- **Approximations / not done — read before trusting it:** terrain only (sprites and creatures keep the classic per-thing lighting);
  distance is 2D like the classic formula (so a wall pixel high above a light is lit as if at floor level); walls block light only at
  the engine's per-subtile grid resolution (the reach flags are bilinear-filtered, so shadow edges are soft over one subtile and a light
  can leak about one subtile through a wall; the `y+1` corner-index quirk of the engine's column builder was matched by eye, not proven);
  at most the 64 lights nearest the camera (of 256 collected); standard perspective only (other view modes silently use classic
  lighting — `light_perpixel_active()` is evaluated at both the grid and the draw so a mode change costs at most one frame of missing
  dynamic light); not tested in first-person/possession view, with the eye-lens, or on real hardware; it costs GPU time (about +0.4 ms of
  the ~2.8 ms world pass on llvmpipe — measure on a real GPU with the RPROF `gpuwait` figure).
- **Why this shape (and what it enables):** it keeps the engine's shadowing while moving the falloff per pixel, which is the smallest step
  that makes lighting resolution-independent; the reach grid is the placeholder for real shadowing. Next steps that build on it: replace the
  reach grid with a per-light shadow term (screen-space ray-march against the depth buffer, or a shadow map), give sprites depth-aware
  lighting (their depth is per-object), then lights as a first-class list with colour.

**C.5 — per-light shadow term (replaces v1's reach grid), landed 2026-09-24.**
- **What changed:** v1 gated the analytic lights by the engine's per-subtile "some dynamic light reached here" flags — one bit per subtile for *all*
  lights together. Now each light's visibility is decided per pixel by a **shadow ray**: the fragment shader marches (2D DDA over subtiles, up to 24
  steps) from the pixel to each light *through a height field of the map's solid columns* (`get_column_floor_filled_subtiles` per subtile, uploaded
  as an R8 texture, rebuilt each frame around the camera in `light_perpixel_build_heights()`), and the light contributes only if the ray clears every cell
  (tested at the ray's lowest point inside each cell). The pixel's own cell and the light's cell never occlude; the start is nudged 8 units toward the
  light and 8 up so surfaces don't shadow themselves. The pixel's map height comes from the same inverse view transform as its map x/y
  (`map_z` row, `WorldFrameLighting::map_z`); each light now carries its height. Cheap rejection first: a light only marches if it would beat the
  brightest unoccluded contribution so far.
- **Consequences:** shadows are per light and per pixel (a wall shades only the lights it actually blocks); faces pointing away from a light go dark
  (the classic grid could not do that); the engine no longer *renders* dynamic lights into the grid in this mode — `light_render_light()` still advances
  their state (interpolation, flicker, radius/intensity animation) but skips the grid/shadow-cache work (`light_pp_skip_dynamic`), which also saves CPU.
  Terrain vertices see static-only light, exactly as in v1.
- **Verified:** `gpu3d_verify` frame 13 — light, a 5-subtile-high solid column, and pixels at equal distance on both sides: lit before the wall (255), shadowed
  behind it (static shade only, 64), lit on the open side (142); the real game (level 2, headless) renders without artifacts and near-identical overall
  brightness to classic; hvlog SDL_GPU validation clean; ctest 1951/1951; Windows build clean. On llvmpipe the world pass is ~3.1 ms (v1: ~3.2, classic: ~2.8).
- **Limits (unchanged from v1 unless stated):** terrain only; distance falloff stays horizontal; a light inside a solid cell (wall-mounted torches) is treated
  as unoccluded within its own cell only; solid height is the *floor-filled* column height, so ceilings, overhangs and door-open state are only as accurate as
  that field; 64 nearest lights; standard perspective only; shadow-ray cost scales with lights-in-range x 24 fetches (unmeasured on real hardware); not tested in
  first-person view or with the eye-lens. **Next:** sprites/creatures lit and shadowed the same way (they have per-object depth only), coloured lights, and
  soft shadows.

**Fixes after the possession report (2026-09-24): per-pixel lighting fade blow-out; GPU overlay pass; alpha-aware ghost blends.**
The report: on possessing a creature the camera zooms toward it and the world momentarily loses its texturing (Vulkan, `LIGHTING=PERPIXEL`).
- **Found and fixed (verified by a new test): the fade band was 256x too bright.** The per-pixel term applies the engine's vertex-shade distance fade
  between `fade_min` and `fade_max`, but the engine's `fade_range` is `(fade_max - fade_min) >> 8` (units of 256, because its shade is 16.16 and its
  lightness 8.8); the shader divided by it raw, so any terrain whose view distance fell inside the band got shade ≈ 256x too large — saturated,
  textureless white. The band is at `cells_away * 192..256`, so it sweeps across the scene as the camera zooms — hence "momentarily", during the
  zoom. The shader now divides by `fade_range * 256` (and ignores a zero range). `gpu3d_verify` frame 12b renders inside a realistic band (the earlier
  tests used a fade so wide it never applied, which is how this shipped): 115 vs 255 before.
  **Caveat:** I could not reproduce the reported symptom in the headless sandbox (level 2 possession looked identical under Software and Vulkan,
  turn-for-turn, with and without per-pixel lighting), so this is the defect that matches the report and is provably wrong, not a confirmed match. If it
  still happens with the fix, the next things to try are `LIGHTING=CLASSIC` and `GPU_TRUE_DEPTH=OFF` to separate causes.
- **Also fixed (found while chasing it, not confirmed as the cause):** translucent CPU draws made *after* the world frame (the possession swipe,
  `draw_swipe_graphic()`) blended against the transparent CPU window instead of the GPU image. Two changes: (1) `render_ghost_blend/_2` (and the one-colour
  sprite variants) are now alpha-aware — on a non-opaque destination they compute the standard 'over' result on straight alpha, which the present
  compositor then lays over the GPU image, and on an opaque destination (always, under Software) they are byte-for-byte the old formulas (unit-tested);
  (2) a new *overlay pass* — `RendererOverlayBegin()/End()` records sprite draws and submits them as a second world frame that **loads** the existing GPU
  target and depth (no clear) at depth 0, so they get their real GPU blend modes (ghost/alpha/...). `draw_swipe_graphic()` uses it. `gpu3d_verify` frame 14 checks a ghost
  sprite over GPU terrain gives exactly `(ref + 2*dest)/3`. Alpha-ramp (additive) draws on a transparent destination still cannot be represented by
  straight alpha; only the overlay pass covers those.
- **Not reproduced / still open:** the eye-lens path was exercised only by unit/synthetic checks (my headless possession never had the lens ready). The reporter's log
  shows read-back frames, so that path does run for them.

**C.5 — lit sprites/creatures (per-pixel lighting, second slice), landed 2026-09-24.**
- **A correction to the notes above:** they said sprites "keep the classic per-thing lighting". They did not, quite: a thing's shade
  (`get_thing_shade()`) is read from the same `lish.subtile_lightness` grid, which per-pixel mode strips of dynamic lights — so creatures and
  objects had silently lost the light of torches/spells. This slice fixes that properly instead of patching the grid read.
- **How:** `prepare_jonty_remap_and_scale()` (the iso/perspective thing-sprite draw, `draw_jonty_mapwho()`) now tells the platform layer the base shade
  of a plainly shaded, non-tinted, non-`TRF_Unshaded` thing sprite (`RendererSpriteLightSet(shade x256)` .. `Clear`). While set — and only when the frame
  carries lighting inputs — `SwCaptureSprite()` records an opaque sprite as `WFS_LIT` with its **palette** colours (undoing the CPU shade remap: the
  remap-kind is tracked in `bflib_vidraw.c`) plus that base shade. The sprite shader (`world_sprite.frag.glsl`, mode 7) takes
  `max(base, brightest unoccluded dynamic light at the pixel)` and applies `render_shade`'s factor. Tinted, flashing, translucent/alpha and
  unshaded sprites, flames, status icons and the front view are untouched. A lit sprite is treated as a billboard: each pixel's map position is
  rebuilt from its screen position at the sprite's own depth (view plane), so a torch on one side lights that side of a creature and a wall between
  a creature and a light shadows it.
- **Shared code:** the light evaluation moved to `shaders/lighting_common.glsl` (glslang `#include`), used by the terrain and sprite shaders; the sprite
  shader gained the lighting uniform block and the height-field sampler. Shadow rays from sprites ignore occluders within 300 map units of the start
  (a billboard pixel can sit inside a neighbouring wall column) and, for everything, columns no taller than the pixel's own height never shade it
  (a wall top is not shadowed by the equally tall wall beside it) — that second rule also removed dark wedges on block tops seen in the first screenshots.
- **Visibility vs position depth split:** the second bullet is a bug found by this work — per-pixel lighting used to force per-vertex *visibility* depth
  on, which reproduces the "slivers/wedges at wall edges" of GPU_TRUE_DEPTH (a polygon of a block top rejected against a neighbour). Ops now carry a separate
  `view_depth` (hyperbolic, for lighting positions only); terrain vertices carry `zpos`. Visibility stays the clamped painter-parity bucket depth unless
  `GPU_TRUE_DEPTH=ON` asks otherwise, so per-pixel lighting no longer changes what is visible.
- **Verified:** `gpu3d_verify` frame 15 (a lit white sprite: 255 at the light, 142 at 21/30 of the radius, 64 far — the same numbers as terrain); a recorder
  unit test for the split depths; the real game (level 2, headless) renders without artifacts; hvlog SDL_GPU validation clean; ctest 1953/1953;
  Windows build clean. Cost on llvmpipe: world pass ~3.7 ms (was ~3.1 with terrain-only lighting).
- **Not verified / limits:** no side-by-side of a creature walking past a torch (no such scene captured), sprite depth is per-object and bucket-quantised
  (±8 map units), sprites get no distance fade on the dynamic term (the engine applies none in lens mode 0), a lit sprite near a wall may be slightly
  over- or under-shadowed because of the billboard approximation, and the eye-lens/first-person path was not exercised. Sprite shading still under
  `LIGHTING=CLASSIC` is untouched.

**C.5 — coloured lights + smooth distance falloff (per-pixel lighting, third slice), landed 2026-09-24.**
- **Colour:** lights gained an optional colour. `InitLight`/`struct Light` carry `colour_r/g/b` (0..255; all zero = white, the default, so nothing changes for existing
  content); objects take `LightRed`/`LightGreen`/`LightBlue` in `objects.cfg`, effects the same keys in `effects.cfg`. Only per-pixel lighting uses it — classic lighting is
  scalar. The shaders' per-light term is now `render_shade(shade_i) * colour_i` (a per-channel factor, so a shade above 31 "brightens beyond source" per channel),
  and lights combine by **per-channel maximum** with each other and with the static shade — the engine's max-combine, extended to colour: white lights behave exactly as before,
  overlapping red and green lights read yellow. Default data now colours the torch, candlestick, lantern post and the three mushrooms (`config/fxdata/objects.cfg`).
- **Coloured *static* lights:** torches etc. are static lights, baked into `stat_light_map`. In per-pixel mode a static light that has a colour is **not baked** (`light_render_light`
  skips it, so no white double-lighting) and is collected each frame like a dynamic one (nearest 64 win; height-field shadows apply, so a torch no longer lights through walls).
  Turning per-pixel lighting on or off rebuilds the static map once (`light_stat_refresh()` at the transition). Static lights without a colour stay baked as in classic.
- **Distance fade — what was and wasn't changed:** the *per-light* falloff was the original engine's linear ramp `1 - d/r` (a cone with a hard edge, a limit of its lighting
  tables). It is now `smoothstep(1 - d/r)` — soft core, zero slope at the radius, no visible ring (`LIGHT_FALLOFF_LINEAR` in `lighting_common.glsl` restores the old curve).
  The **terrain view-distance fade** (`fade_min..fade_max` = 192..256 units x `cells_away`, terrain shade blending to near-black) was *not* changed: it exists because the engine
  simply does not submit polygons beyond `cells_away`, so the fade hides the cutoff. A modern GPU can draw far more, but extending the visible range means raising the engine's
  view distance (bucket count, `Z_DRAW_DISTANCE_MAX`, the recorder's op volume) and re-deriving that fade — an engine change, not a shader one, and worth its own step
  with the RPROF numbers from a real GPU in hand (draw count and vertex volume are what scale).
- **Verified:** `gpu3d_verify` frame 16 (an orange light on white terrain: (221,111,64) near the light — red > green > blue over the static shade — and neutral far away), frames 12–15 re-baselined for the
  smooth curve (98 at 21/30 of the radius, was 142), a recorder test for the colour, ctest 1954/1954, Windows build clean, hvlog validation clean. In the real game (level 2, headless) the torch
  neighbourhood shows an orange wash under per-pixel lighting and the classic yellow-white under classic.
- **Caveats:** `struct Light` grew by 3 bytes and is part of the `lish` blob net resync copies (same-build peers only, as for any layout change); a coloured static light is dropped from
  the classic baked grid while per-pixel is on, so non-lit sprites (tinted/flashing) lose its light; light colours are not (yet) available on creature/trap/spell lights or the Lua
  light API; the smooth falloff makes light pools slightly darker at 25% radius and brighter at 75% than the linear ramp, so a level's tuned brightness will read a little differently.

**C.5 — colour keys on more light sources; lit tinted sprites (per-pixel lighting, fourth slice), landed 2026-09-24.**
- **More coloured sources:** traps (`LightRed/LightGreen/LightBlue` in `trapdoor.cfg`), shots (`LightColour = r g b` in `magic.cfg` — a separate key, because `Lighting = r i f` is
  validated as exactly three values and existing mods use that form), effect elements (`LightRed/Green/Blue` in `effects.toml` next to `LightRadius`), on top of objects and effects from the
  previous slice; all default to 0,0,0 = white, so nothing changes without opting in. Default data: fireball/firebomb/flame breath/explosions/eruption/lava flames orange-red,
  lightning shots and word of power pale blue, TNT warm yellow, fear circle violet, boulder-in-water pale cyan. Colour only matters in per-pixel mode, where these dynamic lights are already
  per-pixel; hard-coded lights (the possession light, the hero light, the cursor light, level-file static lights) stay white.
- **Lights on tinted sprites:** in classic rendering a tinted sprite (frozen = pale blue, the red damage flash, the white hover flash) is drawn with a tint colour table *instead of* its shade
  table, so it is unshaded — fullbright in a dark room. With per-pixel lighting the engine now also sets the lit-sprite base shade for tinted things, and `SwCaptureSprite()` keeps the tint /
  flash table as the sprite's colour table while recording it as `WFS_LIT` (only a plain shade table is swapped for palette colours). The shader then applies `max(base shade, dynamic lights)`
  to the *tinted* colour: a frozen creature is pale blue and dim in the dark, lit warm-blue by a torch; a damage flash is red and dim/bright with the room. Unshaded sprites (`TRF_Unshaded`),
  translucent/ghost and alpha sprites are still not lit. Note the behavioural change this implies in per-pixel mode: tinted creatures are no longer fullbright in dark areas.
- **Verified:** `gpu3d_verify` frame 17 (a pale-blue tint table on a lit sprite: 25,38,64 far from the light = tint x 0.25; 38,58,98 in partial light; blue-dominant throughout); ctest
  1954/1954 (including the config schema tests, which is what caught the `Lighting` value-count constraint); Windows build clean; hvlog validation clean with the new data.
- **Not verified live:** no frozen or damaged creature was captured in a lit room — the frame-17 check is synthetic, and the new data files' colours have not been eyeballed in-game (their effects
  are transient). Colours in `config/fxdata/*` are starting points meant for tuning.

**C.5 — word of power colour; configurable built-in light colours (per-pixel lighting, fifth slice), landed 2026-09-24.**
- **Word of power:** its light was pale blue; the effect is a red expanding circle, so `EFFECT_WORD_OF_POWER` now lights red (`255 70 60`) in `config/fxdata/effects.toml`.
- **Hard-coded lights are now data:** the four built-in lights had no config at all. `rules.cfg` (`[game]`, per player) gained `PossessionLightColour` (the light a possessed creature carries — both
  creation sites), `HeroLightColour` (the light some new hero creatures carry, `thing_factory.c`) and `CursorLightColour` (the player's cursor light, rule set 0); each `r g b`, 0-255, default `0 0 0` = white
  (unchanged behaviour), read into `GameRulesConfig` and copied into the `InitLight`. Shipped defaults are warm white for possession/cursor and cool white for heroes (`255 235 200`,
  `200 220 255`, `255 240 210`) — subtle, easy to change. Level-file lights: the extended light format (`light_create_light_adv`, the `.lgtfx`/Lua dictionary) accepts optional `LightRed/LightGreen/LightBlue`; the legacy
  binary light format has no colour field and stays white.
- **Verified:** builds, ctest 1954/1954 (config schema tests included), Windows build, hvlog run with the new rules loading with no warnings. Not eyeballed in-game (possession/hero/cursor lights are dynamic and
  I have no captured frame of them).
- **Limits:** cursor light reads rule set 0 (there is one cursor light per user, not per rule set); colours only take effect in per-pixel lighting mode.

Every phase from Pre-C.0 onward should land with the same verification baseline
[../00-overview.md](../00-overview.md) already sets for every stage in this series
(`build-cmake.sh` both variants, `check_layering.py --strict`, growing the relevant Catch2 suites,
a manual before/after screenshot pass) plus the phase-specific criteria above and the full baseline
in [09-verification.md](09-verification.md).

**C.5 — soft shadows (per-pixel lighting, sixth slice), landed 2026-09-25.**
- Shader-only (`lighting_common.glsl`; both fragment shaders regenerated), so it applies only with Lighting = Per-pixel on the Vulkan renderer; no config, engine or IR change.
- A light is now a small disc (`SOFT_LIGHT_SIZE`, half-width 128 map units) instead of a point. `shadow_amount()` casts the existing height-field DDA ray (`shadow_ray()`) to the light's centre and to its two side edges; where all three agree (the common case) that is the answer, otherwise two more rays refine it to five evenly spread across the light and the occlusion is their average. The penumbra is sharp next to an occluder and widens with distance from it, as physically expected.
- Each ray's occlusion is also a smooth ramp on how far it clears a column's top (`SOFT_BAND_BASE`/`SOFT_BAND_SLOPE`), not a hard yes/no, which softens the vertical edge of a wall's shadow. `SOFT_RAYS 1` with `SOFT_LIGHT_SIZE 0` restores hard shadows.
- Cost: 3 DDA rays per contributing light per pixel (was 1), 5 inside penumbrae. Not measured on real hardware.
- Test: `gpu3d_verify` frame 13b (a wall ending across the light's view: the shadow edge passes through intermediate brightness). Suite 1954/1954.
- Not eyeballed in a running game (llvmpipe only).
