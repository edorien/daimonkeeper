# Risks and things that will bite

See [00-overview.md](00-overview.md) for stage status and shared context. Found by reading the
current code (2026-09-06), roughly worst-first. Several are pre-existing consequences of stages 2/4
that this stage is simply the first to *have* to resolve. R12/R13 were added 2026-09-13 alongside
Phase C's detailed scoping; R9 was marked resolved-by-outcome the same day once it became clear B3
shipped via route 1, not route 2. R14/R15 were added when this document was split into sub-plans,
alongside [02-graphics-api-choice.md](02-graphics-api-choice.md)'s SDL_GPU recommendation and
[06-call-site-consolidation.md](06-call-site-consolidation.md)'s new prerequisite phase.

### R1 — The present path is called from ~21 sites, in many libraries, and each one runs a full ImGui frame

`RendererPresentFrame()` call sites, all of which now trigger
`ImGuiContextNewFrame` → `RendererRunImGuiFrameCallback` → `ImGuiContextRender`:
`kfx_apploop/game_session_loop.cpp` (×8), `kfx_net/net_exchange_gameplay.c`,
`kfx_net/packets_misc.c` (pause/unpause resync), `kfx_frontend/{front_fmvids,front_landview,
front_network,front_simple}.cpp/.c`, `kfx_platform/{bflib_video.c,bflib_fmvids.cpp}`,
`kfx_render/vidmode.c` (×2). Full current list in
[06-call-site-consolidation.md](06-call-site-consolidation.md).

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
  the two obvious ones — this is exactly the audit burden
  [06-call-site-consolidation.md](06-call-site-consolidation.md) exists to shrink from "~21 sites"
  to "2 named entry points."
- **Mitigation, sequenced ahead of Phase B — both landed 2026-09-23:**
  [../05-imgui-linkage-consolidation.md](../05-imgui-linkage-consolidation.md) moved all ImGui
  knowledge into `kfx_frontend` behind one `RendererImGuiCallbacks` struct (named that, not
  `RendererOverlayCallbacks` as originally sketched — see that document's own note), so the overlay
  begin/submit/render is a single choke point and Phase B's additions live in one library instead
  of straddling the `kfx_platform`/`kfx_frontend` seam. It did *not* reduce the call-site count
  itself — [06-call-site-consolidation.md](06-call-site-consolidation.md), landed immediately after
  it, is the phase that did: the raw `RendererPresentFrame()` is `static` now, reachable only
  through `RendererPresentGameFrame()`/`RendererPresentStepFrame()`, so this risk's "~21 sites, each
  independently audited" framing is obsolete — it's 2 entry points now.

### R2 — `SDL_Renderer` is single-threaded; keep every present on the main thread

SDL's renderer API is main-thread-only. `kfx_net` spawns `SDL_Thread`s
(`net_matchmaking.c`, `net_portforward.cpp`) but those do socket/DNS work and do **not** call
`RendererPresentFrame` — the present calls in `net_exchange_gameplay.c`/`packets_misc.c` are on the
main game-loop thread. This holds today; it is an invariant Phase B/C must not break (e.g. by
moving capture read-back or texture uploads to a worker). Worth a one-line assert or comment at the
seam. **Phase C specifically**: a dedicated render thread (as the reviewed upstream branch built,
see [05-reviewed-branch-lessons.md](05-reviewed-branch-lessons.md)) is the most direct way to break
this invariant — see R12. SDL_GPU's own command-buffer submission model (chosen in
[02-graphics-api-choice.md](02-graphics-api-choice.md)) is likewise designed around a
submission-thread concept that this codebase's current usage keeps pinned to the main thread —
worth re-confirming at C.0, not assuming, since SDL_GPU's threading rules differ in detail from
classic `SDL_Renderer`'s.

### R3 — Dynamic-texture lifetime across `SDL_Renderer` recreation is unmanaged

`ImGuiContextCreateTexture()` returns raw `SDL_Texture*` handles that the context does **not**
track; `shutdown_backends()` frees only `s_cursor_texture`. `ensure_present_target()` recreates
`m_renderer` (and tears down + rebuilds the ImGui context) whenever
`SDL_GetRenderWindow(m_renderer) != lbWindow` — i.e. on a genuine `SDL_Window` handle change. Any
frontend-cached dynamic-texture handle (the land-preview panel today; more in Phase B/C) then
dangles into a freed renderer → use-after-free on the next `ImGui::Image`/`UpdateDynamicTexture`.

Latent right now because `INGAME_RES` is restart-only and mid-session fullscreen/resize
(`bflib_video.c:551-605`) reuses the same `SDL_Window` (so `m_renderer` survives, only `m_texture`
is resized). Phase B (cursor unification) and Phase C (more long-lived GPU textures, plus a
possibly separate `SDL_GPUDevice`/`SDL_GPUTexture` lifetime to track — see
[02-graphics-api-choice.md](02-graphics-api-choice.md)) raise the stakes. Fix: a tracked-texture
registry in `ImGuiContext` with invalidate-on-recreate, or a `RendererDynamicTextureInvalidated`
callback the frontend subscribes to.

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
before committing, rather than letting `engine_render.c` reach for SDL directly. The reviewed
branch's `GpuResourceDesc`/`GpuResourceHandle` shape
([05-reviewed-branch-lessons.md](05-reviewed-branch-lessons.md)) is a reasonable starting point for
this façade's design, and per [02-graphics-api-choice.md](02-graphics-api-choice.md), SDL_GPU's own
explicit device/buffer/texture/pipeline objects are a good match for it — the façade wraps SDL_GPU
primitives, it doesn't invent a parallel abstraction over them.

### R7 — Two cursor draws still coexist — **resolved 2026-09-23**

The legacy `bflib_mspointer.cpp` path still blits the software cursor into `lbDrawSurface`
**unlocked**, and the ImGui overlay draws its own cursor via `GetForegroundDrawList()->AddImage()`
while `WantCaptureMouse`. Stage 4 killed the *visible* double-cursor by syncing positions, but both
still render. A post-composite `SDL_RenderReadPixels` screenshot (B2) can therefore capture two
cursors depending on timing. Phase B's cursor unification is the real fix; until it lands, capture
tests will show this.

**Resolved as:** confirmed live (by reading the code, not assumed) that the two gates
(`lbPointerAdvancedDraw && lbInteruptMouse && !RendererWantCaptureMouse() && !RendererScreenOwned()`
in `bflib_mspointer.cpp`'s `OnMove()`, and the ImGui side's `io.WantCaptureMouse ||
FrontendImGuiScreenOwned()`) were exactly this risk's predicted race: `OnBeginSwap()` ran at the
very start of `RendererSoftware::PresentFrame()`, gated on *last* frame's `WantCaptureMouse`, before
that frame's own `begin_frame()` had recomputed it for the ImGui side's gate a few lines later —
so a `WantCaptureMouse` transition between two frames really could produce a frame with both drawn
or neither. Also found live: `lbPointerAdvancedDraw` (gating `bflib_mspointer.cpp`'s `Draw`/
`Backup`/`Undraw` double-buffered redraw path) was never set `true` anywhere in the codebase, and
its one live call site for `SetHotspot()` (`vidmode.c`) ran before `LbMouseSetup()`, while
`pointer.is_active` was still false — so that entire code path (the surface-pair backup/restore
machinery, `ScopedScreenSurface`, `LbScreenSurface*`/`struct SSurface` in `bflib_vidsurface.c`) was
already 100% dead, not just redundant, confirmed by grep before deleting rather than assumed.
Fixed by removing the legacy CPU-buffer cursor draw entirely (`bflib_mspointer.cpp`/`.hpp` now only
track position/sprite/hotspot, used via `LbMouseGetSprite()`/`GetPointerHotspot()`/`GetMouseX/Y()`;
`LbMouseOnBeginSwap/EndSwap` and `MouseStateHandler::PointerBeginSwap/EndSwap` — which only ever
bracketed that draw — are gone too) and making `gui/FrontendImGui.cpp`'s ImGui-overlay cursor draw
unconditional (gated only on a valid cursor image existing, via `s_cursor_have`) instead of on
`WantCaptureMouse`/`ScreenOwned`. One cursor mechanism now, not two kept in sync by matching gates.

### R8 — FMV / cutscene frames get ImGui submission too

The ImGui pipeline is unconditional now (`RendererImGuiEnabled()`'s old session-global gate is
retired), so `bflib_fmvids.cpp:510` and `front_fmvids.c` present Smacker frames with
`FrontendImGuiFrame()` still running `FeStyleSheetFrame()` (the `-imguistyle`
debug overlay) and the deferred-action drain — if anything, more certainly than when this risk was
first written, since there's no toggle left to disable it. Low severity today (the error box is
gated behind `frontend_imgui_screen_active`), but any Phase B in-game HUD submission needs an explicit
"suppress during video/intro states" gate or it will draw over cutscenes.

### R9 — `imgui_impl_sdlrenderer3` batching may not scale to an in-game HUD — **resolved by outcome, kept for history**

Written when B3 route 2 (a hand-rolled ImGui-draw-list HUD compositor) was still a live option; it
warned that `SDL_RenderGeometryRaw`'s per-draw-command texture/clip-rect overhead might not
coalesce hundreds of small HUD quads as well as a purpose-built atlas path. Moot now: B3 route 1
shipped instead (the in-game-GUI-as-ImGui project), and it uses the same widget-level ImGui
submission the frontend already does, not a hand-rolled draw-list compositor — so this specific
batching concern was never exercised in the form this risk anticipated. Live HUD frame-time is now
a real-build measurement question, not a speculative one; if it ever becomes a problem, it's a
stage on its own, not a re-litigation of B3.

### R10 — Sprite→texture cache invalidation is the hard part

The cache key must include the active palette / remap table, and both frontend fades
(`ProperFadePalette` mutating the active palette per animation step) and in-game lighting churn it.
A naïve pointer-only key returns stale colours after any fade; a per-frame-palette key defeats the
cache. Needs a generation counter on `RendererPaletteSet` and eviction keyed on it — design this
alongside stage 2 §2a's per-draw expansion cache if that was built. **Phase C inherits this
directly**: its static-level-texture cache entries need the same generation-counter invalidation as
sprite entries, since level lighting also mutates the active palette. Full design:
[03-pixel-format-and-texture-cache.md](03-pixel-format-and-texture-cache.md).

### R11 — `RendererClearScreen(colour)` still takes a palette index

`RendererClearScreen` resolves an 8-bit index through the palette (`RendererSoftware.cpp:34`);
every caller passes a literal (`0`, `144`). Any GPU clear path in Phase C must preserve that
contract or update all call sites — easy to miss because the signature type doesn't change.

### R12 — A dedicated render thread is an unproven pattern for this codebase

The reviewed upstream branch runs its GL renderer on its own thread (`RenderThreadManager`,
`Signal()`/`WaitForCompletion()` synchronization with the main game loop), and its own commit
history doesn't show that pattern being stress-tested against this engine's specific present-path
complexity (R1's ~21 call sites, R3's texture-lifetime issue, R4's non-nesting shared primitive) —
it's simply untested by anyone but the branch's single author, on a codebase in a different
(pre-refactor, pre-ImGui) state than this fork's. Introducing a render thread here would need to
answer, before any code: which of R1's present call sites can tolerate the main thread not blocking
on GPU submission; whether `RendererSwapFramebufferTarget` (R4) and the dynamic-texture registry
(R3) are thread-safe to touch from two threads at once; and how `s_presenting_imgui_frame` (R1)
generalizes to "GPU work in flight on another thread" rather than just "nested call on this
thread." None of this is a reason a render thread can't work — it's a reason it needs its own
design pass with this codebase's actual present-path shape in hand, not adoption because an
external branch happened to include one. See Phase C.5 in [07-phased-delivery.md](07-phased-delivery.md).

### R13 — `GUI_ICON_PACK=CLASSIC`'s compositing assumption doesn't survive Phase C unmodified — **mitigated 2026-09-24 (C.1: Vulkan + CLASSIC falls back to Software with a logged warning; see 07)**

Per [04-architecture-and-ir-boundary.md](04-architecture-and-ir-boundary.md)'s note: the legacy
sprite HUD draws directly into `lbDrawSurface`, assuming that buffer already holds the rendered 3D
scene underneath it — true today (CPU 3D rasterizer, same buffer, in-place compositing) and false
once `RendererGpu3D` owns the 3D view (its own GPU texture, `lbDrawSurface` no longer the visible
scene). Two ways to resolve this, and Phase C.1 needs to pick one explicitly rather than discover
the gap live:

1. **Make `CLASSIC` and `RendererGpu3D` mutually exclusive.** Selecting the GPU 3D path forces
   `GUI_ICON_PACK` off `CLASSIC` (or refuses to enable `RendererGpu3D` while `CLASSIC` is active),
   with a clear config-time error rather than a silently broken frame. Simplest, smallest, and
   consistent with `CLASSIC` being a deliberate, narrow style choice rather than a first-class
   target every renderer combination must support.
2. **Redirect `CLASSIC`'s draws through an overlay-texture pattern**, matching what ImGui already
   does: render into an off-screen, alpha-cleared buffer, then composite it after the GPU 3D pass,
   before the ImGui layer. More consistent (every renderer combination works), more work (every
   classic-HUD draw call's "assume opaque black background" behaviour — the same class of issue R11
   flags for `RendererClearScreen` — needs auditing for whether it now needs to preserve alpha=0
   instead of painting over the frame).

Recommend option 1 for Phase C.1–C.4 (cheap, unblocks everything else) and revisit option 2 only if
real user demand for "`CLASSIC` HUD + GPU 3D view" together shows up — matching this document's
general bias toward not building compositor infrastructure nobody asked for (see B3's history in
[01-phase-b-2d-compositing.md](01-phase-b-2d-compositing.md)).

### R14 — SDL_GPU is a real but newer, less field-proven SDL API, and its shader pipeline is uncosted (new)

[02-graphics-api-choice.md](02-graphics-api-choice.md) recommends SDL_GPU over raw OpenGL/Vulkan
because it is already vendored, already enabled in this project's own build, and needs no new
dependency. That recommendation carries two open risks that shouldn't be waved away by "it's just
SDL":

- **Field exposure.** SDL_GPU has meaningfully less multi-year, multi-driver-combination exposure
  than `SDL_Renderer`'s classic backends or raw OpenGL. It is real and buildable in this exact
  repo's vendored SDL3 source today, but "confirmed present" is not the same claim as "confirmed
  robust across this project's driver matrix." C.0's spike (a `testgpu_simple_clear`-equivalent
  smoke test on both CI targets, per [02](02-graphics-api-choice.md)) exists specifically to convert
  this from an assumption into a measured fact before any engine code depends on it.
- **Shader delivery has no free path.** Unlike raw GL's "compile GLSL source at runtime," SDL_GPU
  wants precompiled bytecode per backend. Whatever C.0 decides (cross-compile from one source via
  tooling not currently vendored, or hand-maintain per-backend source), it is new build-pipeline
  surface with its own failure modes (a stale generated shader binary, a cross-compiler version
  mismatch between CI and a contributor's machine) that this codebase's asset pipeline
  (`mingw32-make pkg-*`) has no precedent for. Scope this as real work in C.0's exit criteria, not
  as a footnote.
- **Backend selection isn't automatically "Vulkan," and RT isn't reachable from SDL_GPU at all —
  both checked directly against the vendored SDL3 3.4.12 source, 2026-09-23.** SDL_GPU's own
  driver-priority order (`SDL_gpu.c:314`) picks D3D12 over Vulkan on Windows; only Linux reaches
  Vulkan by default. Forcing Vulkan everywhere via `SDL_HINT_GPU_DRIVER` is a one-line, C.0-scoped
  decision (see [02](02-graphics-api-choice.md#forcing-vulkan-as-the-sdl_gpu-backend-pre-phase-c-decision-checked-directly)),
  not something that falls out of "use SDL_GPU" automatically. Separately: SDL_GPU has **no**
  ray-tracing surface today (no acceleration structures, no RT pipeline, no trace-rays command —
  confirmed via `grep` across `SDL_gpu.h`/`src/gpu/`), and the one property-query function it
  exposes (`SDL_GetGPUDeviceProperties`) gives back only name/driver strings, not the native
  `VkDevice`/`VkQueue` handles an RT extension would need to issue real ray-tracing commands. If
  hardware-RT-enhanced lighting is ever pursued, it is a **new raw-Vulkan-owned device/path**, not
  an incremental `RendererGpu3D` feature — see
  [02](02-graphics-api-choice.md#future-hardware-ray-tracing-out-of-scope-for-phase-c) and
  [07](07-phased-delivery.md)'s C.6 row. Not a blocker to Phase C.1–C.4 (rasterization only, no RT
  dependency), but keep `WorldFrame`/the texture cache backend-agnostic while building them so that
  future fork is cheap.

### R15 — The call-site-consolidation phase is itself a regression risk if rushed (new) — **landed 2026-09-23, live soak still pending**

[06-call-site-consolidation.md](06-call-site-consolidation.md) touches ~21 call sites across five
libraries (`kfx_apploop`, `kfx_net`, `kfx_frontend`, `kfx_platform`, `kfx_render`) purely to reduce
future audit burden — no visible behaviour is supposed to change. That combination (broad textual
reach, zero intended visible effect) is exactly the shape of change most likely to hide a real
regression until it's live: a missed call site still calling the old raw present function directly,
a subtly different reentrancy behavior at one of the two new named entry points that only manifests
during a specific interleaving (a palette fade that starts *during* a net resync, say), or a
Smacker/loading-screen path that turns out to depend on present timing in a way the two-entry-point
model doesn't preserve. Mitigation: treat this phase with the same rigor
[../05-imgui-linkage-consolidation.md](../05-imgui-linkage-consolidation.md) already applies to its
own "no behaviour change" refactor — full screenshot-identical verification across every affected
path (listed in [06](06-call-site-consolidation.md)'s verification section), land it as its own
reviewed, soaked phase, and do not bundle it into the same PR as any Phase C.0 GPU code, so a
regression here is trivially bisectable to "the consolidation" rather than tangled up with "the new
GPU seam."

**Landed as:** the automated half of this mitigation is done — both toolchains build clean,
`check_layering.py --strict` is clean, the full Catch2 suite (1939 tests) is green, and the
call-site grep confirms no site was missed. The interactive half — screenshot-identical across a
full game-loop session, a Smacker cutscene, a net resync, a loading screen, a landview transition,
a palette fade — was **not** exercised (no display/game-data access in the implementing
environment); [06](06-call-site-consolidation.md)'s verification section tracks this as
outstanding. Land this on its own commit, separate from any Phase C.0 work as this risk already
recommends, and run that live pass before treating this phase as fully soaked.
