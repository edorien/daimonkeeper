# Lessons from the reviewed branch

See [00-overview.md](00-overview.md) for stage status and shared context. This file records what
survives, and what doesn't, from a real external attempt at the same problem: a full-size,
single-author attempt at a GPU world-view renderer
(`origin/feature/opengl-renderer`, 31 commits, 189 files, ~21k lines), assessed in detail by
[`docs/merge-checks/opengl-renderer-review.md`](../../../merge-checks/opengl-renderer-review.md).
That branch is **not being merged** — its architecture doesn't fit this fork (below) — but it's
real, working evidence about what a GPU world-view renderer for this specific codebase has to
solve, and several of its design choices are worth adopting even though its implementation isn't
and even though its choice of graphics API (raw OpenGL) is superseded here by
[02-graphics-api-choice.md](02-graphics-api-choice.md)'s SDL_GPU recommendation.

## What's worth taking from the reviewed branch anyway

Despite the pixel-format disagreement
([03-pixel-format-and-texture-cache.md](03-pixel-format-and-texture-cache.md)), the graphics-API
disagreement ([02-graphics-api-choice.md](02-graphics-api-choice.md)), and the layering problems
(below), several structural ideas in `feature/opengl-renderer` are sound independent of pixel
format or API, and Phase C should be designed around them rather than reinventing the same shapes
from nothing:

- **An intermediate-representation (IR) command buffer as the sole crossing point.** The branch
  has `src/kfx/renderer/ir/{WorldCommands,UICommands,TextCommands}.h` plus
  `IRCommandBuffer.h`/`IRenderTaskProducer.h`/`RenderTaskProducerRegistry` — a retained scene
  description that a "producer" (game/render-layer code) fills in, and a "consumer" (a specific
  backend) draws from. This is the right shape for this fork's layering rules and should be the
  **actual mechanism**, not an optional nicety: the reviewed branch built this scaffolding but then
  didn't consistently use it (its `GLWorldViewRenderer.cpp` still reaches past it into
  `player_data.h`/`creature_graphics.h`/`engine_render.h`/`game_legacy.h` directly for most of its
  2661 lines — see the review §2). Phase C should finish the idea the reviewed branch started and
  then abandoned partway through — see [04-architecture-and-ir-boundary.md](04-architecture-and-ir-boundary.md)'s
  `WorldFrame`.
- **A backend-agnostic GPU resource-mapper.** `GpuResourceDesc.h`/`GpuResourceHandle.h` +
  `GLResourceMapper.cpp` describe textures/buffers by value (dimensions, format, filter mode) and
  hand back an opaque handle, so the code that decides *what* a texture should look like doesn't
  need to know *how* a specific backend allocates one. Directly reusable shape for the new
  geometry/vertex-buffer submission façade R6 calls for — and, per
  [02-graphics-api-choice.md](02-graphics-api-choice.md), a shape SDL_GPU's own explicit
  device/buffer/texture object model already matches closely, so this façade sits *on top of*
  SDL_GPU's primitives rather than reimplementing a device/queue/sync layer the way the reviewed
  branch's `GLResourceMapper` had to for raw GL.
- **Tile-atlas packing as its own concern.** `TileAtlasPacker.{h,cpp}` separates "how do texture
  blocks get packed into atlas pages" from both the resource mapper and the world-view renderer.
  Worth keeping as a separate small component rather than folding packing logic into the renderer
  itself.
- **One pass per concern.** `GLWorldViewRenderer` (dungeon view), `GLUIRenderer`, `GLTextRenderer`,
  `GLCursorLayer`, `GLMapFadePass`, `GLZoomBoxTilesPass`, `GLImagePresentPass` are separate classes
  with a narrow job each, rather than one large "draw everything" object. This maps cleanly onto
  Phase C's own scope (world view + lens effects only — UI/text/cursor are Phase B's job, already
  headed toward ImGui) and is worth keeping as the internal shape even though the file contents
  don't port.
- **A config-level experimental gate.** The reviewed branch ships
  `RENDERER=SOFTWARE`/`RENDERER=OPENGL` in `keeperfx.cfg` with an explicit "experimental — opt-in
  for testing only" comment, alongside keeping the CPU path as the shipped default. This matches
  [00-overview.md](00-overview.md)'s "`RendererType` and backend selection" section almost exactly
  — good validation that this is the right posture, from an independent source arriving at the same
  answer, even though the specific backend name differs (`RENDERER_GPU3D` via SDL_GPU here, not
  `RENDERER_OPENGL`).

## What we are explicitly not taking

- **Palette-index-on-GPU** — see [03-pixel-format-and-texture-cache.md](03-pixel-format-and-texture-cache.md).
- **Raw OpenGL as the backend API** — see [02-graphics-api-choice.md](02-graphics-api-choice.md).
  The reviewed branch's choice was reasonable for a from-scratch attempt with no SDL_GPU available
  to it at the time; this fork's own vendored SDL3 build already has SDL_GPU switched on, which
  changes the calculus (no new dependency, Vulkan/D3D12/Metal coverage for free).
- **Direct upward `#include`s from renderer code into gameplay state.** The reviewed branch's
  `GLWorldViewRenderer.cpp` pulls in `player_data.h`, `creature_graphics.h`, `engine_buckets.h`,
  `engine_render.h`, `engine_textures.h`, `vidmode.h`, `local_camera.h`, and `game_legacy.h`
  directly (review §2) — in this fork's terms, that's a `kfx_platform → kfx_sim`/`kfx_render`/
  `kfx_game` violation on a scale `check_layering.py --strict` would reject outright, and exactly
  what the IR command-buffer boundary
  ([04-architecture-and-ir-boundary.md](04-architecture-and-ir-boundary.md)) exists to prevent.
  Every value the world-view pass needs — camera state, tile/creature draw data, lighting snapshot,
  palette-resolved texture handles — must arrive through the IR command buffer or an
  existing/new callback struct, never through a raw include.
- **The dedicated render thread.** `RenderThreadManager`/`RendererThread` run GL work on its own
  thread, synchronized with `Signal()`/`WaitForCompletion()`. This directly conflicts with R2 (in
  [08-risks.md](08-risks.md)): *"`SDL_Renderer` is single-threaded; keep every present on the main
  thread"* — true today because nothing net-new here has been checked against a second
  GPU/render thread's context-sharing rules, its interaction with the `PresentFrame` reentrancy
  guard (R1), or `RendererSwapFramebufferTarget` (R4, already documented as non-nesting and
  fragile). The reviewed branch's own thread synchronization is unreviewed and untested by anyone
  but its single author. Phase C should ship single-threaded first (C.0–C.4, see
  [07-phased-delivery.md](07-phased-delivery.md)) and treat a render thread as a distinct, later,
  separately-justified optimization — not a day-one assumption (see R12).
- **Building it as one large branch.** The reviewed branch is 31 commits from one author over
  about a week, with a visible tail of correctness/perf fixes discovered after the fact (`fix:
  worldview renders again`, `fix: camera rotation`, `perf(selector): stop killing the game with
  30x the IR quads`, a screen-width/height accessor mix-up that silently broke movie playback,
  text clipping, and map-fade buffers across four files simultaneously — see the review §5). None
  of that is a character flaw in the approach; it's what happens when ~21k lines land with zero
  automated coverage and get shaken out live. Phase C should ship in small, tested,
  independently-reviewable slices ([07-phased-delivery.md](07-phased-delivery.md)) specifically to
  avoid reproducing that tail here.
- **A naming trap worth avoiding on sight, not after the fact.** The reviewed branch's
  `RendererScreenWidth()`/`RendererScreenHeight()` accessors were swapped at several call sites —
  width used where the stride (which equals width, not height, for a row-major buffer) was meant —
  across `bflib_fmvids.cpp`, `bflib_sprfnt.c`, and `engine_redraw.c`, all fixed in one commit
  (review §5, `6cbc5d597`). Those specific functions don't exist in this fork, so there's no bug to
  port, but the failure mode is generic: any new stride/dimension accessor Phase C introduces
  (e.g. on the geometry-submission façade, R6) should have a name that makes "stride" and "width"
  impossible to confuse — `TbBytePitch` (`bflib_video.h:123`) already exists precisely to make this
  class of mistake a compile error rather than a silent wrong-by-4x; follow that pattern rather
  than adding a bare `int`-returning accessor pair that invites the same mix-up again.

## Lessons from the same work landing on upstream `master` (2026-09 merge)

After the review above, the GL backend reached `dkfans/keeperfx` `master` unreviewed
(`37b5d8788` "Preliminary OpenGL renderer backend" plus "OpenGL fixes" parts 1–5 of 8, and
follow-ons). Merging that range commit by commit (ledger:
[`docs/merge-checks/upstream-merge-2026-09-19.md`](../../../merge-checks/upstream-merge-2026-09-19.md))
gave a second, more concrete data set than the branch review. Parts 6–8 of 8 were not upstream
yet; repeat the exercise when they land.

**Renderer work finds real core bugs — harvest them, separately, with tests.** Of ~5 "fix" commits,
two contained genuine software-path defects found incidentally: `panel_map_update(x,y,w,h)` called with
end-coordinates in three places (minimap refreshed the wrong region), and the local camera applying
scroll controls after a parchment jump (prediction drifted from the authoritative camera). Three more
were latent robustness bugs the GL fade path exposed (a map-fade hold/restore that could leave
tooltips/status menu hidden, a lens palette that reset a running possession fade, abyss texture scroll
that tore slab seams). None needed GL. Process: read every non-GL hunk of every renderer commit, port the
defect alone, write the test first and watch it fail. Budget for this in Phase C: our own renderer
work will surface the same class of latent bug, and each should land as its own small commit.

**Renderer capability must not become gameplay policy.** Upstream gated a gameplay behaviour (whether the
parchment map fades) on `MapFadePass_SupportsNativeResolution()` — a GL pass answering a question the
game logic asks — and then needed a second commit to put it behind a config option because the default
flipped for every resolution. Any capability the renderer reports should be a plain setting/struct read
by callers (through `RendererManager`/IR), and a backend change must not change default gameplay
behaviour. Same smell: `VIEWPORT_MODE`, `PARCHMENT_MAP_FADE` and a shipped-default flip of
`UNLOCK_CURSOR_WHEN_GAME_PAUSED` all arrived inside renderer PRs. New config keys and default changes
need their own review, not a ride-along.

**Restructuring startup for the new backend regressed config loading.** Moving config load into a new
`resolve_startup_config()` (so the renderer type is known early) made the hard-coded feature defaults run
*after* `load_configuration()`, silently overriding `keeperfx.cfg` values (fixed later by "Fix some
keeperfx.cfg values getting defaulted"). Anything that reorders startup for a backend needs a test that
loads a config with non-default values and asserts they survive.

**Software fallbacks get deleted "because GL handles it".** Across parts 1–4 the CPU landview zoom, the
`copy_raw8_image_buffer` blitter, the `draw_keepersprite` raster fallback and the huge-sprite CPU path
were removed in favour of `RendererPresentImage`/`RendererSubmitKeeperSprite` calls. Once that happens the
software renderer is only as good as the abstraction. Phase C keeps the CPU path a first-class, tested
path: no PR may delete a software fallback without an equivalent path behind the same seam and a test.

**Design the submission API once.** `RendererSubmitKeeperSprite` changed shape in three consecutive
commits (dst rect → plus source position → a `SpriteScale` struct), each rippling through every draw
helper in `engine_render.c`. A frame-graph/IR boundary
([04-architecture-and-ir-boundary.md](04-architecture-and-ir-boundary.md)) should fix the *unit of
submission* (what a keeper sprite, a tile, a lens request is) before call sites are converted
([06-call-site-consolidation.md](06-call-site-consolidation.md)).

**Instrumentation must be provably behaviour-neutral.** The profiling commit regrouped `update_things()`
into timed zones and, in doing so, moved the dead-creature list to update *after* effect generators — a
simulation-order change (RNG consumption, replay determinism) with no mention in the commit. Rule for
Phase C perf work: profiling zones wrap calls, they never reorder them; a replay/desync run
(`-packetsave`/`-packetload`, `ftest`) is part of the acceptance for any commit touching update loops.

**"Fixes" can be reverts of the branch's own regressions.** Two later commits only repair breakage the GL
merge introduced (config defaulting, the fade default). When reviewing a range, classify those as
"regression of the rejected work — not applicable" instead of porting them; check the fork really lacks
the bug before dropping.

**Hook naming churn is a review cost.** `LbScreenIsLocked` → `RendererCanDraw`/`RendererIsFrameOpen`,
`RendererLockFramebuffer` → `RendererBeginFrame`, direct `lbDisplay.*` reads → `RendererScreenWidth()`
across ~35 files. Pick the vocabulary in the seam design (R6) before touching call sites, and do the
rename as a mechanical commit of its own so behaviour changes stay reviewable.

**Merge mechanics this taught us** (recorded in
[`upstream-merge-workflow.md`](../../../Architecture/upstream-merge-workflow.md)): merge each renderer
commit alone with `-s ours` and hand-port only demonstrable core fixes; never merge the whole range at once.

## Relationship to the reviewed branch, restated

`origin/feature/opengl-renderer` is **not being merged**, in whole or in part, for its 3D
world-view renderer. [`docs/merge-checks/opengl-renderer-review.md`](../../../merge-checks/opengl-renderer-review.md)
has the full review, including which of its incidental non-renderer bug fixes *were* worth taking
(and were — see that document and the corresponding small fixes already applied to
`local_camera.c`, `gui_parchment.c`, `bflib_math.c`, `bflib_crash.c`, `console_cmd.c`, and a
confirmed-dead bucket-kind cleanup in `engine_render.c`). This file is the record of what survives
from its *design*, for Phase C's own from-scratch implementation against this fork's layering,
pixel-format, and graphics-API decisions.
