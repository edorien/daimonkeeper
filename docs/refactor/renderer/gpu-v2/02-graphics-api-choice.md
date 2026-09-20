# Graphics API choice for Phase C

See [00-overview.md](00-overview.md) for stage status and shared context.

The original single-file plan never actually chose a graphics API for Phase C's 3D world-view
pass — it wrote "`SDL_RenderGeometry` or a raw GL/Vulkan pass" and left it there, deferring the
decision. This file makes the decision, grounded in what this repository's own build already has
available rather than a from-scratch survey.

## What this repository already has, checked directly

- **SDL3 is pinned at `3.4.12`** (`build/cmake/modules/Dependencies.cmake`), fetched both for
  mingw (prebuilt dev tarballs) and native Linux (system `pkg-config` first, `FetchContent`-from-
  source fallback).
- **That SDL3 build ships SDL3's own cross-platform GPU API, `SDL_GPU`, with Vulkan, D3D12, and
  Metal backends built in** — verified directly against the fetched source tree
  (`out_mergecheck/_deps/sdl3-src/include/SDL3/SDL_gpu.h`,
  `out_mergecheck/_deps/sdl3-src/src/gpu/{SDL_gpu.c,d3d12,metal,vulkan}`).
- **This project's own merge-check build already configures SDL3 with it turned on** — its
  `CMakeCache.txt` shows `SDL_GPU:BOOL=ON`, `SDL_RENDER_GPU:BOOL=ON`, `SDL_VULKAN:BOOL=ON`,
  `SDL_RENDER_VULKAN:BOOL=ON`. Nothing needs to be newly enabled to get this; it is already part of
  the dependency this codebase links.
- **No vendored ImGui backend exists yet for GL3, Vulkan, or SDL_GPU** — `deps/imgui/backends/`
  currently has only `imgui_impl_sdl3.{h,cpp}` and `imgui_impl_sdlrenderer3.{h,cpp}`. This matters
  for scoping (a new backend needs adding whichever API wins, *if* ImGui itself ever needs to speak
  that API directly — see below, it doesn't for the design recommended here) but is not itself a
  reason to prefer one option over another.
- **No shader cross-compilation tooling (`SDL_shadercross` or equivalent) is vendored.** SDL_GPU
  takes precompiled shader bytecode per backend; there is no "just write GLSL and go" path the way
  a raw-GL renderer has. This is real, uncosted build-pipeline work under any option that uses
  SDL_GPU, and this document does not paper over it (see "Open items," below).

This turns the user's original either/or ("OpenGL vs. Vulkan") into a three-way comparison, and the
option neither named wins.

## The three options

### Option 1 — Raw OpenGL

The path `origin/feature/opengl-renderer` (the reviewed external branch, see
[05-reviewed-branch-lessons.md](05-reviewed-branch-lessons.md)) already took, so there is real
in-codebase-domain prior art for what a GL 3D pass here has to solve.

- **For:** simple, extremely well-documented API; direct evidence from the reviewed branch of what
  this specific engine's blend/lighting model needs from a shader; no new dependency.
- **Against:** industry-declining investment (Apple has deprecated it outright; Khronos itself has
  moved focus to Vulkan); its bind-to-modify global-state model doesn't map as naturally onto R6's
  "backend-agnostic GPU resource-mapper" façade as an explicit-object API does; and — the point that
  actually matters here — **it buys nothing that SDL_GPU doesn't also buy**, while SDL_GPU also
  gets Vulkan/D3D12/Metal backend coverage for free. There is no dependency-cost or simplicity
  advantage left once SDL_GPU is on the table; going raw GL only inherits the reviewed branch's own
  choice without the specific SDL_GPU-shaped benefits below.

### Option 2 — Raw Vulkan

- **For:** maximum control, best perf ceiling, explicit-object model matches R6's resource-mapper
  façade directly, useful groundwork if a render thread (Phase C.5) is ever pursued.
- **Against:** this is the option that costs the most relative to what Phase C actually needs.
  Instance/device/queue selection, swapchain management, descriptor pools/sets, pipeline layouts,
  render-pass/framebuffer (or dynamic-rendering) setup, memory allocation (hand-rolled or a new
  `VMA` dependency), and explicit synchronization (fences/semaphores/barriers) is an order of
  magnitude more code and more failure surface than "submit buckets of textured/gouraud triangles
  with stage 2's blend math" warrants. It needs a **new third-party dependency** (a Vulkan loader
  at minimum, likely `VMA` too) wired into `Dependencies.cmake` — unlike SDL_GPU, which needs none.
  It needs its own ImGui backend (`imgui_impl_vulkan`, not vendored) that expects the *app* to own
  descriptor pools/render passes/MSAA config directly — meaningfully more integration surface than
  today's `imgui_impl_sdlrenderer3`. And it cuts directly against this project's own stated delivery
  philosophy elsewhere in this stage's docs — small, independently-reviewable, profiled-before-
  optimized phases, no dedicated render thread "because an external branch happened to include one"
  (R12) — by asking for the single largest, least-incrementally-shippable piece of new
  infrastructure of the three options, for a codebase whose GPU rendering need is "one world-view
  pass, painter's-algorithm order, no depth buffer yet" (see
  [04-architecture-and-ir-boundary.md](04-architecture-and-ir-boundary.md)).

### Option 3 — SDL_GPU (SDL3's own cross-platform GPU API) — **recommended**

- **For:** zero new dependency — it's already inside the SDL3 this project fetches and already
  enabled in this project's own build configuration. One API gets Vulkan (Linux), D3D12 (Windows),
  and Metal (any future macOS port) without hand-writing three backends — SDL owns the
  backend-selection fallback chain and the per-platform driver quirks, which is exactly the kind of
  cross-platform-maintenance burden this project already prefers to delegate to a fetched
  dependency rather than own directly (the same posture `Dependencies.cmake`'s whole "fetch, don't
  hand-roll" design takes for every other third-party piece). Its API shape — explicit
  `SDL_GPUDevice`/`SDL_GPUBuffer`/`SDL_GPUTexture`/`SDL_GPUGraphicsPipeline` objects and command
  buffers — is handle/descriptor-based, the same spirit as Vulkan/D3D12, so it satisfies R6's
  resource-mapper façade and the reviewed branch's `GpuResourceDesc`/`GpuResourceHandle` shape
  ([05](05-reviewed-branch-lessons.md)) without Phase C having to hand-write the device/queue/sync
  plumbing underneath those shapes itself. It gives real vertex/index/uniform buffers and a real
  graphics-pipeline object with explicit blend-state configuration — exactly what "fragment shader =
  stage 2's blend math" ([03](03-pixel-format-and-texture-cache.md),
  [04](04-architecture-and-ir-boundary.md)) needs, and more than `SDL_RenderGeometry` (2D-oriented,
  no custom shaders, no depth state) can offer. And it keeps the whole GPU path inside SDL, so it
  extends rather than breaks the existing "SDL lives in kfx_platform" convention (R6) — no second,
  unrelated graphics library sitting next to `SDL_Renderer`.
- **Against — real open items, not reasons to reject it:**
  - **Shader delivery is the actual cost.** SDL_GPU wants precompiled bytecode per backend
    (SPIR-V/DXBC-DXIL/MSL). Authoring once and cross-compiling (`SDL_shadercross` is SDL's own tool
    for this, not currently vendored) versus hand-maintaining per-backend shader source is a real
    C.0 decision with real build-pipeline work behind it — scope it explicitly, don't assume it away.
  - **ImGui doesn't need to move, and shouldn't be asked to.** The compositing design keeps ImGui on
    `SDL_Renderer`/`imgui_impl_sdlrenderer3` unchanged; only Phase C's 3D pass touches SDL_GPU
    directly. No `imgui_impl_sdlgpu3` backend is needed, and adding one would needlessly reopen
    [../05-imgui-linkage-consolidation.md](../05-imgui-linkage-consolidation.md)'s already-landed
    work for no benefit.
  - **The `SDL_Renderer` interop path is a real design question, and C.0 must pick one explicitly**
    (see below) — it is not automatically solved by "compositing is already solved" the way the
    rest of Phase C's compositing story is.
  - **SDL_GPU has less multi-year field exposure than `SDL_Renderer` or raw GL.** It is real and
    buildable in this exact repo's vendored SDL3 today, but the honest comparison point is
    "newer API, still SDL, still gets SDL's own CI/driver-matrix coverage" — not "unproven."
    Treat "does it actually work cleanly on this project's two CI targets" as a cheap, fast C.0
    spike (SDL ships its own smoke test, `test/testgpu_simple_clear.c`, already present in the
    vendored source tree — point a throwaway program at it before writing any engine code against
    the API), not an assumption.

## The `SDL_Renderer` interop question C.0 must answer

"Compositing is already solved" (restated in
[04-architecture-and-ir-boundary.md](04-architecture-and-ir-boundary.md)) means a GPU 3D pass
renders into its own texture, which slots into `PresentFrame` exactly where `m_texture` (the
uploaded CPU framebuffer) sits today. Choosing SDL_GPU makes *how* an `SDL_GPUTexture` becomes
something the existing `SDL_Renderer`/`SDL_RenderTexture` path can draw a real design question,
with two candidate answers — C.0 should pick one explicitly rather than discover the gap mid-phase:

1. **Preferred: run `RendererSoftware`'s `SDL_Renderer` on the `SDL_RENDER_GPU` driver**, so it and
   Phase C's `SDL_GPUDevice` share the same underlying GPU device, and a texture the 3D pass renders
   is natively usable by the renderer that composites ImGui on top — no CPU round-trip. This is the
   only answer that fully honors "no new present architecture."
2. **Fallback: a private `SDL_GPUDevice` + explicit read-back/re-upload** each frame into a normal
   `SDL_Texture`. Works unconditionally, but reintroduces the CPU round-trip Phase C exists to
   remove, and stalls the same way `SDL_RenderReadPixels` does under vsync (R5) — a correctness
   fallback for a driver combination where (1) doesn't apply, not the default design.

Resolve this at C.0, before any world-view geometry is submitted — it changes what `RendererGpu3D`
needs to own (a shared device handle vs. its own device-and-bridge).

## Recommendation

**Use SDL_GPU for Phase C's 3D world-view pass.** Not raw OpenGL, not raw Vulkan. It is the only
option that adds no new third-party dependency, gets Vulkan/D3D12 (and Metal) coverage through one
already-vendored library, matches this fork's "SDL lives in kfx_platform" convention without
exception, and hands Phase C's resource-mapper design real primitives instead of asking it to
reimplement a device/queue/sync layer from scratch. Raw Vulkan buys marginal extra control at a
cost (boilerplate, a new dependency, a second backend needed for Windows, a philosophy mismatch
with this stage's own "small, reviewable, profile-before-optimizing" posture) this project isn't
short of a reason to avoid paying. Raw OpenGL is the path already tried by the reviewed branch —
it works, but offers no forward motion and no cost advantage over SDL_GPU to offset that.

This does not close the shader-delivery or `SDL_Renderer`-interop questions above — those remain
real C.0 design work. It replaces "`SDL_RenderGeometry` or a raw GL/Vulkan pass" with an actual,
justified answer.

## What would change this recommendation

- A C.0 spike finds `SDL_RENDER_GPU`/`SDL_GPUDevice` interop doesn't work cleanly on one of the two
  CI targets (mingw/Windows, native Linux) — fall back to the CPU-read-back interop path first;
  only reconsider raw GL/Vulkan if *that* also fails, which would itself be a serious SDL3 problem
  worth reporting upstream rather than a reason to route around SDL entirely here.
- Shader cross-compilation tooling turns out to be meaningfully harder to wire into
  `build-cmake.sh`/`Dependencies.cmake` than expected — this raises Phase C's cost estimate, but
  does not by itself favor raw GL/Vulkan: both still need shader source in some form, and GL's
  "compile GLSL at runtime" convenience doesn't offset losing dependency-free
  Vulkan/D3D12/Metal coverage.
