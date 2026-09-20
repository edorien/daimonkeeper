# Review: upstream `feature/opengl-renderer` branch

**Reviewed:** 2026-09-13
**Target:** `origin/feature/opengl-renderer` @ `01205c3af` (31 commits, single author `Cerwym`)
**Base:** `c375b20a4` — this is exactly our fork's current merge-base with `origin/master` (last merge: `79ae8a0e7`), so the branch is a clean 3-commit-ahead-of-us upstream lineage, not a stale fork.
**Not yet merged upstream** — `feature/opengl-renderer` is still a separate PR branch off `origin/master`; `master` itself only has the "Preliminary OpenGL renderer backend (#5251)" commit (`37b5d8788`), which this branch then adds 30 more commits on top of.

Diff stat: **189 files changed, +21090 / −3035**. Breakdown:
- `src/kfx/renderer/*` + `src/kfx/ui/*` (new C++ renderer/UI code): 101 files, +15107/−239
- `deps/glad/*` (vendored GL loader): 3 files, +3645/0
- Everything else (legacy flat `src/*.c`/`.cpp` hook points, build system, config, IDE files): 85 files, +2338/−2796

## Verdict up front

**Do not bulk-merge this branch.** The OpenGL rendering *technique* itself is sound — see below, the "8-bit palette forced onto the GPU" framing undersells what's actually there. The problem is architectural fit and maturity, not the core idea:

1. It was written against the **pre-refactor flat `src/` tree** (upstream never adopted our `kfx_platform`/`kfx_render`/`kfx_sim`/... split), so essentially none of the 189 changed files apply to a path that exists in our tree today.
2. The GL renderer's core class (`GLWorldViewRenderer`, 2661 lines, the largest single file) directly `#include`s and reads globals from what would be `kfx_sim`, `kfx_render`, and `kfx_game` in our layering — a direct violation of the "`kfx_platform` depends on nothing but external libs" rule our own `IRenderer`/`RendererManager` seam was built to enforce.
3. Upstream's own config comment calls it **"experimental — opt-in for testing only"**; it shipped as a single-author, single-week branch with zero automated test coverage and a visible tail of stability/perf fixes discovered after the fact.

The **small hook-point patches** scattered through ~20 legacy gameplay files are a different story: narrow, well-commented, mostly safe, and worth taking regardless of what happens with the GL renderer itself — a few are standalone bug fixes with no GL dependency at all.

Full reasoning and a staged path forward follow.

---

## 1. The palette-on-GPU technique itself: sound, not a hack

The concern that prompted this review — "forcing the 8-bit palette onto a GPU" — describes a real and completely standard technique for porting palette-based engines to OpenGL, not a shortcut. Checked in `src/kfx/renderer/opengl/GLShaders.h` and confirmed consistently in every atlas/texture setup site:

- Sprite/tile atlases are uploaded as `GL_R8` textures holding **raw palette indices** (0–255), not pre-expanded RGBA.
- A `256×1 RGBA8` palette texture is sampled once per fragment: `texture(u_palette, vec2(idx, 0.5))`.
- Player-colour/ghost/damage remaps go through a second `256×64 R8` "fade table" LUT (`u_fade_table`), matching the original engine's `pixmap.fade_tables` semantics exactly (`UI_REMAP_FRAGMENT_SHADER`, `WORLD_FRAGMENT_SHADER`).
- Every atlas/palette/fade-table texture is explicitly set to `GpuTextureFilter::Nearest` (checked across `GLTileAtlas.cpp`, `GLSpriteAtlas.cpp`, `GLImagePresentPass.cpp`, `GLUIRenderer.cpp`, `GLWorldViewRenderer.cpp` — every site but one). Interpolating a palette-index texture would produce garbage indices; the author clearly knew this and avoided it everywhere except:
- `GLMapFadePass.cpp` uses `Linear` filtering — but only for the already-RGB parchment cross-fade blend, which is the one place linear filtering is actually correct.

This is exactly the technique used by most serious indexed-colour engine ports (DOSBox-adjacent projects, several Build-engine and Bullfrog-engine source ports). It preserves the original engine's exact palette fades/remaps instead of approximating them in true colour, at the cost of an extra texture fetch per fragment. **The approach is not the risk here** — the surrounding integration is.

Two things worth independent verification before trusting this further (not done as part of this review, flagged for follow-up):
- No mipmapping is generated for these atlases (`glTexParameteri` calls show only min/mag filter, no `GL_GENERATE_MIPMAP` or explicit mip uploads) — correct for a technique like this (mipmapping an index texture is meaningless without a custom downsample), but means the renderer has no minification story if a texture atlas is ever displayed smaller than native size. Given DK's fixed low-res sprites this is probably fine, but wasn't verified against every distance/zoom path.
- Performance was clearly a live issue during development — see §5.

## 2. Architectural fit: this predates, and doesn't match, our own renderer-seam refactor

This is the load-bearing finding. Our fork already has its own backend-abstraction seam, built independently:

```
src/kfx_platform/include/renderer/IRenderer.h        (RENDERER_SOFTWARE only — no GL entry yet)
src/kfx_platform/include/renderer/RendererManager.h  (C-callable facade)
src/kfx_platform/include/renderer/RendererSoftware.h (the only backend today)
```

Per `docs/Architecture/architecture.md` §2.1, this seam deliberately lives at the **bottom** of the dependency ladder (`kfx_platform` depends on nothing but external libs) specifically so gameplay code can call into rendering through `extern "C"` entry points without an upward `#include`.

Tracing the git history: this seam originated as upstream PR **#5122** ("IRenderer seam + VSync setting"), landing as flat `src/kfx/platform/`, `src/kfx/renderer/`, `src/kfx/lense/`. Our fork merged that, then did its own large refactor (`594f4d861` "split src/ into layered kfx_* libraries", `f73e750d5` "flatten kfx/platform and kfx/renderer to platform/ and renderer/") that relocated and renamed those files into `kfx_platform`/`kfx_render`. **Upstream never did this** — `feature/opengl-renderer` continues building directly on the original flat `src/kfx/renderer/` from #5122, un-split.

The two lineages have since diverged on the shared interface itself:
- `IRenderer::LockFramebuffer()`/`UnlockFramebuffer()` in our tree became `RendererBeginFrame()`/`RendererEndFrame()` upstream (used in `scrcapt.c`'s diff).
- Upstream's `IRenderer.h` grew 65 new lines (cursor layers, world-view renderer accessors, frame-graph executor hooks) our version doesn't have.
- `RendererManager.h`/`.cpp` grew by 232/513 lines respectively upstream — this is not a small delta to reconcile.

More importantly, **the GL implementation itself doesn't respect the boundary that made this seam worth having**. `GLWorldViewRenderer.cpp` (2661 lines, the single biggest new file) directly `#include`s:

```cpp
#include "player_data.h"        // kfx_sim — get_my_player(), player_room_colours[]
#include "creature_graphics.h"  // kfx_sim — creature_table[]
#include "engine_buckets.h"     // kfx_render — BucketKind structs, buckets[]
#include "engine_render.h"      // kfx_render — BUCKETS_COUNT, orient tables
#include "engine_textures.h"    // kfx_render — TEXTURE_BLOCKS_COUNT, block_ptrs[]
#include "vidmode.h"            // kfx_render — pixmap.fade_tables, pixmap.ghost
#include "local_camera.h"       // kfx_render
#include "game_legacy.h"        // kfx_game — game.lish.subtile_lightness
```

and reads several of those globals directly in the render body (`creature_table[i]`, `player_room_colours[color_idx]`, `game.lish.subtile_lightness`, `pixmap.fade_tables`), not just at construction time.

There **is** a partial IR/command-buffer abstraction (`src/kfx/renderer/ir/{WorldCommands,UICommands,TextCommands}.h`, `IRCommandBuffer.h`, `IRenderTaskProducer.h`, `RenderTaskProducerRegistry`) that looks like exactly the right shape — a retained scene description the game/render layer produces and the backend (GL or software) consumes, which is how this *should* cross our layering boundary. But it's used inconsistently: only one function in the whole file (`GPURenderNow(const WorldCommandBuffers&)`) actually consumes it; the rest of the 2661 lines still reach straight into gameplay globals. This reads as a mid-flight refactor the author hadn't finished, not a deliberate design choice.

**Practical consequence:** if `GLWorldViewRenderer` and friends were dropped into `kfx_platform` as-is (where `IRenderer`/`RendererManager` live), `check_layering.py --strict` would fail hard — this is a `kfx_platform → kfx_sim`/`kfx_render`/`kfx_game` violation on a scale far beyond anything in our current accepted-violations list (architecture.md §8.2). Putting it in a new top-of-stack library instead would sidestep the layering check but defeats the purpose of the seam (the whole point of `IRenderer` living at the bottom is that `main.cpp::setup_game()` can wire in *any* backend without the backend needing special placement).

## 3. What's actually portable, and what needs a rewrite

| Area | Files (upstream paths) | Assessment |
|---|---|---|
| Shared seam interfaces | `IRenderer.h`, `RendererManager.{h,cpp}`, `RendererSoftware.{h,cpp}`, `ITextRenderer.*`, `IUIRenderer.*` | **Diverged on both sides** since common ancestor #5122. Needs a manual 3-way reconciliation (path move + content merge), not a copy. Do this first — everything else depends on it. |
| GL backend core | `src/kfx/renderer/opengl/*` (GLWorldViewRenderer, GLUIRenderer, GLResourceMapper, GLShaders, GLSpriteAtlas, GLTileAtlas, GLImagePresentPass, GLMapFadePass, GLZoomBoxTilesPass, GLTextRenderer, GLCursorLayer) | Net-new, ~11.5k lines. The *shader and resource-mapper* layers (`GLResourceMapper.cpp`, `GLShaders.h`, `GpuResourceDesc.h`/`GpuResourceHandle.h`) are backend-only and portable close to as-is into `kfx_platform`. The *scene-assembly* layers (`GLWorldViewRenderer`, `GLUIRenderer`) are not — they need to be rebuilt to consume the IR command buffers exclusively instead of reaching into `kfx_sim`/`kfx_render`/`kfx_game` globals, or the layering violation needs to be an explicit, team-approved exception. |
| IR/command-buffer scaffolding | `src/kfx/renderer/ir/*`, `IRCommandBuffer.h`, `IRenderTaskProducer.h`, `RenderTaskProducerRegistry.*` | This is the right shape for our layering and worth keeping/finishing rather than discarding — it's the piece that would let a GL backend live in `kfx_platform` legitimately. Currently incomplete (see §2). |
| Render thread | `RenderThreadManager.{h,cpp}`, `RendererThread.{h,cpp}`, `ASSERT_GAME_THREAD`/`ASSERT_RENDER_THREAD` | New architecture: GL rendering runs on a dedicated thread, synchronized via a signal/wait pair (`Signal()`/`WaitForCompletion()`). Nothing in our fork does cross-thread rendering today. This needs its own focused correctness review (lifetime of GL context across threads, what happens on `Stop()` mid-frame, whether `g_on_render_thread` assertions are load-bearing or advisory) before it's trusted — not something to wave through as part of a bulk merge. |
| Legacy hook-point patches | `thing_creature.c`, `thing_effects.c`, `power_hand.c`, `player_data.c`, `player_instances.c`, `packets.c`, `local_camera.c`, `vidfade.c`, `vidmode.c`, `scrcapt.c`, `spritesheet.cpp`, `main_game.c`, `gui_parchment.c`, `frontmenu_ingame_map.c`, `front_landview.c`, `bflib_mspointer.cpp`, `bflib_sprfnt.c` | **Low risk, worth cherry-picking regardless of the GL decision.** See §4. |
| Build system | `Dependencies.cmake`, `Platforms.cmake` | Same lineage/style as ours (`kfx_fetch`, `VCPKG_TOOLCHAIN` conditionals already exist in both). Low-risk merge: add `deps/glad` target, `find_package(OpenGL)`, the OpenAL/vcpkg thread-model fix. |
| Build system | `BuildTargets.cmake` | **Doesn't apply.** Our fork's top-level `BuildTargets.cmake` no longer holds a flat `KEEPERFX_SOURCES_CXX` list — that logic moved into each `src/kfx_*/CMakeLists.txt`'s own `file(GLOB ...)`. The LLD-linker detection fix and the vcpkg-DLL-copy-next-to-exe fix are both worth having, but need to be re-homed into wherever our fork now defines the `keeperfx`/`keeperfx_hvlog` targets themselves. |
| `keeperfx_vs2010.*` removal | — | Moot — we don't have these files anymore. |
| Vendored `deps/glad` | `deps/glad/{include,src}` | Public-domain GL loader, ~3600 lines of generated code, lives outside `src/` (won't be picked up by any `file(GLOB)`). Low risk, matches our existing `deps/centitoml` pattern. |

## 4. The legacy hook-point patches are the safest part of this branch

Sampled all of the touched pre-existing gameplay/video files (~2338 net lines across 85 files, excluding `src/kfx/*` and `deps/glad`). Pattern throughout: small, targeted "notify the active renderer" calls, consistently commented with the *why*, not the *what*:

```c
// Redirect this call's sprite submissions into the dedicated
// swipe-overlay buffer instead of the general UI one -- see
// IRenderer::BeginOverlayCapture()'s own comment for why (lets GL
// composite the swipe sprite inside the lens-distortion bracket,
// matching develop, instead of flat on top of the finished frame).
// No-op on software, which draws immediately either way.
RendererBeginOverlayCapture(OVERLAY_CAPTURE_SWIPE);
```

Notably, several of these are **standalone bug fixes with no dependency on the GL work at all**, caught incidentally while wiring the new renderer:

- `local_camera.c`: `update_local_cameras()` was snapshotting `previous_local_cameras` *after* `process_camera_action()` had already mutated `destination_local_cameras` — meaning camera interpolation had nothing valid to ease from. Fixed by moving the `memcpy` before the mutating call. This is a real, pre-existing correctness bug, unrelated to rendering backend.
- `gui_parchment.c`: the low-res parchment load path was reusing `poly_pool` (a shared scratch buffer) as its destination — a latent aliasing hazard if anything else touches `poly_pool` between load and use. Fixed by giving it a dedicated `lores_parchment` buffer. Also fixed: `parchment_loaded` was being latched to `1` even when `LbFileLoadAt()` failed, permanently serving whatever stale data was in the buffer instead of allowing a retry.
- `spritesheet.cpp`: `free_spritesheet()` didn't invalidate `IUIRenderer::ResolveSprite()`'s pointer-keyed cache — a freed sheet's memory could be reused by the next `load_spritesheet()` call, silently aliasing the new sheet's sprites onto the old one's stale cached atlas pixels. Fixed by clearing the cache on every free.

None of these three need OpenGL to matter; they're correctness fixes to the *existing* software-rendering-era code, found as a side effect of the author stress-testing render paths while building the GL backend. **These are worth pulling into our fork independently of any GL merge decision** (re-applied against our fork's `src/kfx_render/src/local_camera.c`, `src/kfx_frontend/` parchment equivalent, and `src/kfx_render/src/vidmode.c`/spritesheet equivalents rather than the flat upstream paths — the underlying files exist in both trees under different paths, so this is a hand-port, not a `git cherry-pick`).

The remaining hook-point changes (`RendererSetScreenTint`, `RendererPreserveFadeCache`, `RendererClearKeeperSpriteAtlas`, `RendererUpdateSlabTexture`, `RendererSubmitKeeperHandSprite`, `RendererClearSpriteHandleCache`) are pure GL-enablement — safe no-ops under `RENDERER_SOFTWARE` (verified: each is guarded or has a documented software-mode no-op), but only useful once a GL backend exists to consume them. No reason to take these ahead of the GL work itself.

## 5. Maturity and risk signals

- **Single author, single branch, ~1 week of active iteration** (`2026-09-05` sanity commit, then a burst `2026-09-08` → `2026-09-12`), all commits by one contributor (`Cerwym`). Not yet reviewed or merged into upstream `master` — still an open PR branch.
- Upstream's own shipped config comment: `; Renderer backend: SOFTWARE (default, stable) or OPENGL (experimental -- opt-in for testing only).` Upstream itself doesn't consider this production-ready.
- The commit log itself is a stability narrative, not just a feature build-out: `fix: worldview renders again`, `fix: camera rotation`, `fix(renderer): possession-view holes and screen-stride bug`, `fix(renderer): stop parchment overhead map from blacking out the background`, `perf(selector): stop killing the game with 30x the IR quads` (a real, previously-shipped perf bug — the commit touched `engine_render.c` *and* `GLWorldViewRenderer.cpp` together to fix it), `fix(renderer): fix build breakage from the master merge`. This is normal for a solo WIP branch, but means treating the tip commit as "done" would be a mistake — it's a snapshot of ongoing debugging, not a finished feature.
- **Zero automated test coverage added** — no Catch2 unit tests, no `src/ftests/` functional tests, despite this being by far the largest and riskiest change in the branch's history. This alone would fail our fork's own merge bar: `docs/Architecture/upstream-merge-workflow.md` explicitly calls for a coverage-first approach precisely because upstream has no test harness of its own and our `src/ftests/`/Catch2 suite is what would catch a merge-introduced regression here. A renderer swap with this much surface area (world view, UI, text, cursor, minimap, parchment, lens effects, screenshot/movie capture) and no tests is exactly the scenario that workflow doc was written for.
- Thread-safety of `RenderThreadManager` is unverified by this review (see §3) and is new complexity our fork doesn't currently carry at all.

## 6. Other friction to be aware of

- Our working tree currently has **unrelated in-flight changes** to `src/kfx_frontend/src/frontend.cpp` and `src/kfx_frontend/src/frontgui_screens.cpp` (per `git status` — uncommitted), plus a new `src/kfx_editor/` in-game level-editor project. Both areas overlap with what a future GL-renderer merge would also need to touch (frontend screens, UI submission paths). Worth sequencing deliberately rather than layering three concurrent renderer/UI-surface efforts on top of each other.
- Our fork's `IRenderer.h` doesn't have a `RENDERER_OPENGL` enum value yet — adding one, plus the `GetUIRenderer()`/`GetTextRenderer()` sub-renderer accessors upstream's grown, is a small, uncontroversial first step regardless of how much of the GL implementation itself gets adopted.

## 6a. Addendum (2026-09-13): incidental fixes and dead code taken from this branch

Per the recommendation in §4/§7, the following were hand-ported onto this fork's tree — verified
with a full `KFX_OS=linux` build, `check_layering.py --strict`, and the `kfx_platform`/`kfx_render`/
`kfx_game`/`kfx_frontend` Catch2 suites (all green) — independently of any decision on the GPU
renderer itself:

- **`local_camera.c` (`4e82d9a61`, "fix: camera rotation")** — `update_local_cameras()` was
  snapshotting `previous_local_cameras` *after* `process_camera_action()` had already mutated
  `destination_local_cameras`, leaving camera interpolation nothing valid to ease from. Reordered.
- **`gui_parchment.c` (`e1f25657e`'s underlying insight, adapted — see below)** — the low-res
  parchment background load was reusing `poly_pool` (the render engine's shared bucket-allocation
  scratch pool, reused every frame) as permanent storage for a one-time file load; given its own
  `lores_parchment` buffer instead. Also: `parchment_loaded` was latched to `1` even when
  `LbFileLoadAt()` failed, permanently serving stale/uninitialized data instead of allowing a
  retry — fixed to latch only on success. Note: upstream's actual `e1f25657e` fixes a *different*
  bug (parchment overhead-map margin blackout) that doesn't apply here — this fork's
  `draw_overhead_map()` was already independently rewritten and never had that failure mode (see
  §3 note on `gui_parchment.c`'s divergence). The `poly_pool`-aliasing bug ported above was found
  by inspecting the same file for the same class of problem, not by porting that specific commit.
- **`spritesheet.cpp` sprite-handle cache invalidation (from `9ecfb6d50`) — reviewed, not
  applicable.** This fork has no `IUIRenderer::ResolveSprite()`-style pointer-keyed sprite cache
  yet (that's upstream's GL-era addition), so there's nothing to invalidate today. Recorded as a
  requirement for whenever this fork builds the equivalent cache (Phase B's planned
  sprite→GPU-texture cache, [`../refactor/renderer/gpu-v2/03-pixel-format-and-texture-cache.md`](../refactor/renderer/gpu-v2/03-pixel-format-and-texture-cache.md)) —
  see [`gpu-v2/08-risks.md`](../refactor/renderer/gpu-v2/08-risks.md)'s R10.
- **Dead-code / warning cleanup (`7c90a67bf`)** — `bflib_math.c`'s `bitScanReverse()` MSVC branch
  used `DWORD` (pulling in `<windows.h>` just for the typedef); replaced with `unsigned long`.
  `bflib_crash.c` dropped a redundant `#include <imagehlp.h>` (superseded by the `<dbghelp.h>`
  already included next to it). `console_cmd.c` had four `const int suggested_key_max = 2048;`
  used as an array bound — not a compile-time constant in C, making the array a VLA; changed to
  `enum { suggested_key_max = 2048 };`.
- **Confirmed-dead render bucket kinds removed from `engine_render.c` (`6cbc5d597`'s dead-code
  half)** — `QK_PolygonSimple`, `QK_PolyMode0`, `QK_PolyMode4`, `QK_TrigMode2`, `QK_PolyMode5`,
  `QK_TrigMode3`, `QK_TrigMode6`, `QK_RotableSprite`, `QK_BasicPolygon` and their matching
  `BucketKind*` structs, union members, and switch-case bodies. Verified dead by inspection before
  removal: each symbol appeared only at its `enum`/struct declaration and its own switch-case
  label — no call site anywhere in the file ever constructed a bucket item with any of these
  kinds (`get_bucket_item(..., QK_PolygonSimple, ...)` etc. never occurs), and the file's own
  pre-existing `// Possibly unused` comments on each case already flagged the same suspicion. This
  fork's copy carries the identical dead structs (same decompilation lineage), so upstream's
  finding transferred directly. **Not taken**: the reviewed commit's actual motivating bug (a
  systemic `RendererScreenWidth()`/`RendererScreenHeight()` stride mix-up across
  `bflib_fmvids.cpp`/`bflib_sprfnt.c`/`engine_redraw.c`) doesn't apply — this fork has no such
  accessor pair — but the mix-up itself is recorded as a naming-hygiene lesson in
  [`../refactor/renderer/gpu-v2/05-reviewed-branch-lessons.md`](../refactor/renderer/gpu-v2/05-reviewed-branch-lessons.md)
  for whenever Phase C adds its own stride/dimension accessors.

**Reviewed, renderer-touching, and deliberately *not* applied** (per the instruction to discuss
rather than silently take anything past a clear bugfix):

- **`a13588c78`, "replace direct global screen/mouse accessors with RendererManager calls"** — a
  ~35-file refactor routing legacy `LbScreenWidth()`-style globals through upstream's evolved
  `RendererManager`. Not a bugfix; it's groundwork entangled with upstream's own diverged seam
  additions (`RendererScreenWidth`/`Height`, the IR `WorldCommands` shape, several
  `GLWorldViewRenderer`/`GLUIRenderer` call sites) that don't exist in this fork's `RendererManager`.
  Porting it would mean first reconciling the seam headers (§2/§7 of the main review), which is
  its own scoped task, not a side effect of a dead-code pass.
- **`b681bffb1`, "refactor(hdr): clean compisitor"** — introduces an `IGLHdrPolicy`
  Windows-HDR-compositor abstraction (`GLHdrPolicyWin`/`GLHdrPolicyNull`). Entirely in service of
  the GL renderer's own HDR output path; no equivalent concept exists in this fork and none is
  needed until (if) a GPU 3D path is built. No action.
- **`99cefeb28`, "remove confirmed-dead UI replay/fallback code"** — dead code within upstream's
  *own* GL-era `IUIRenderer`/`RendererSettings` additions (a replay/fallback path made obsolete by
  the branch's own later commits). None of the removed code exists in this fork at all — not
  applicable, nothing to remove.
- **`ee8418062`/`83da5cadf`/`85a9232e1`/`5bd559107` (build-system commits: CMake preset rework,
  glad vendoring, OpenAL/vcpkg fixes)** — per direction, build fixes weren't ported given this
  fork's CMake build has itself moved on independently (per-library `file(GLOB)` targets vs
  upstream's flat `KEEPERFX_SOURCES_CXX` list; see §3's build-system row). Any of these worth
  having would need re-homing into the current CMake structure, not a mechanical port.
- **`ca30ac2db`, "comment out dead fronttest"` + the rest of `7c90a67bf`'s debug font-test-screen
  removal** — reviewed, not applied. This fork's `frontend.cpp` has the same
  `fronttestfont_draw`/`fronttestfont_input` functions (debug-only, `BFDEBUG_LEVEL > 0`) but also a
  wider `FeSt_FONT_TEST` state-machine surface (name lookup, a debug keybinding trigger) that
  upstream's commits didn't touch either. Removing just the two functions cleanly, the way
  upstream did, would leave the state reachable with dead draw/input handlers; doing it properly
  means also retiring `FeSt_FONT_TEST` itself, which is more than a mechanical dead-code deletion
  and wasn't verified safe within this review's scope. Left as a flagged, independently-actionable
  cleanup rather than a partial edit to a large, heavily-shared file.

## 7. Recommended path forward

Not a decision for this review to make unilaterally, but in priority order if the team wants to move on this:

1. **Take the three incidental bug fixes now** (§4: `local_camera.c` interpolation snapshot order, `gui_parchment.c` buffer aliasing + failed-load latch, `spritesheet.cpp` sprite-handle cache invalidation), hand-ported onto our split files. Zero GL dependency, real correctness value, near-zero risk.
2. **Reconcile the shared seam headers** (`IRenderer.h`, `RendererManager.h/.cpp`) between our version and upstream's, so future work (ours or a future upstream pull) speaks the same vocabulary — before anything else touches these files.
3. **Treat `src/kfx/renderer/opengl/*` as a reference implementation, not a drop-in.** The shader/resource-mapper layer is close to portable as-is; the scene-assembly layer (`GLWorldViewRenderer`/`GLUIRenderer`) needs to be re-derived against the IR command-buffer abstraction as the *only* crossing point into `kfx_sim`/`kfx_render`/`kfx_game` state, finishing what upstream's own `ir/` scaffolding started. This is a substantial rewrite, not a port — budget it as such.
4. **Build test coverage alongside, not after.** Given the coverage-first upstream-merge-workflow precedent, this should gain `src/ftests/` coverage for the render-affecting gameplay paths (creature swipe overlay, power-hand cursor, palette fades, parchment/minimap) before or during the port, not as a follow-up.
5. **Review `RenderThreadManager` in isolation** before trusting it — it's a genuinely new concurrency model for this codebase and deserves scrutiny independent of the rendering work it enables.
6. Defer the `keeperfx_vs2010.*`/build-system cleanup items — pick up `Dependencies.cmake`/`Platforms.cmake`-equivalent changes (glad target, OpenGL find_package, OpenAL/vcpkg thread-model fix) opportunistically once the above is underway; they're low-risk but not urgent on their own.
