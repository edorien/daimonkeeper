# Stage 5 — Consolidate ImGui linkage into one library

Status: **planning, not started.** Pure refactor — no behaviour change, no visible change.
Prerequisite for [gpu-v2/00-overview.md](gpu-v2/00-overview.md) Phase B (risk
[**R1**/**R6**](gpu-v2/08-risks.md)): the capture
path, the in-game HUD submission, and the Phase C geometry façade all get simpler if exactly one
library talks to ImGui. Cleans up a [04-imgui-gui-foundation.md](04-imgui-gui-foundation.md)
architecture decision (§3.1) that Phases A–G outgrew.

See [00-overview.md](00-overview.md) for context.

---

## Problem

Dear ImGui is currently compiled and linked into **two** places:

1. **`kfx_platform`** — one file, `src/kfx_platform/src/gui/ImGuiContext.cpp`, the only translation
   unit outside `kfx_frontend` that `#include <imgui.h>` (verified 2026-09-06: the *only* one). It
   owns the context + `ImGui_ImplSDL3_*` + `ImGui_ImplSDLRenderer3_*` lifecycle, the SDL event
   feed, `NewFrame`/`Render`, the demo toggle, the cursor-over-ImGui draw, and the dynamic-texture
   helpers.
2. **`kfx_frontend`** — `frontgui_style.cpp`, `frontgui_widgets.cpp`, `frontgui_screens.cpp`,
   `frontgui_stylesheet_test.cpp` and every `frontgui_*_frame()` screen. These call `ImGui::`
   directly and unavoidably (stage 4 §"a screen calls `ImGui::` directly" — accepted).

Because `ImGuiContext.cpp` lives in `kfx_platform` — the lowest-ranked library, which everything
depends on — the `imgui` OBJECT library's include dirs are propagated to **every** target via
`target_link_libraries(kfx_common_opts INTERFACE imgui)` (`CMakeLists.txt:164`), and its compiled
object has to be force-linked onto **all ten `*_utest` binaries** plus both executables
(`src/kfx_*/tests/CMakeLists.txt` — 8 of them carry an `imgui` link line with a comment explaining
the object-linking gap; `src/kfx_sim/tests/`, `src/kfx_game/tests/`, `src/kfx_net/tests/`,
`src/kfx_config/tests/`, `src/kfx_render/tests/`, `src/kfx_script/tests/`, `src/kfx_apploop/tests/`,
plus `kfx_platform/tests/` and `kfx_frontend/tests/`).

Consequences:
- A GUI toolkit is a link dependency of `kfx_sim`'s and `kfx_pathfinding`'s unit tests, which have
  nothing to do with rendering.
- Every new `imgui`-symbol reference from `kfx_frontend` risks a fresh "undefined symbol in
  `kfx_foo_utest`" break (stage 4 hit this repeatedly — see its Phase A/B notes).
- [gpu-v2/01-phase-b-2d-compositing.md](gpu-v2/01-phase-b-2d-compositing.md)'s work (capture read-back, in-game HUD submit)
  would have to be split across the `kfx_platform`/`kfx_frontend` boundary, threaded through
  callbacks, because the compositing call site (`RendererSoftware::PresentFrame`) is in
  `kfx_platform` but the ImGui knowledge is in `kfx_frontend`.

## Goal

**ImGui is compiled into, and linked from, exactly one `src/kfx_*` library: `kfx_frontend`.**
`kfx_platform` references no ImGui symbol and no ImGui header. The only non-`kfx_frontend` targets
that link the `imgui` object are the final executables and the three `*_utest` binaries that
already reuse `kfx_frontend`'s objects (`kfx_frontend_utest`, `kfx_script_utest`,
`kfx_apploop_utest`).

### Why `kfx_frontend`, not `kfx_platform`

The screens can't move down — they need campaign/save/net/config-schema state that lives at or
above `kfx_frontend`'s rank (7). They call raw `ImGui::` and stage 4 deliberately chose not to
wrap every call. So the screens are a fixed point: whatever library they're in must link ImGui.
Consolidation therefore moves the *context/backend ownership* **up** to join them, rather than the
other way round.

This also shrinks the blast radius from "everything" to "the 3 libraries above `kfx_frontend`",
because only `kfx_script`, `kfx_apploop` and `app_entry` depend on `kfx_frontend`.

---

## The coupling surface to sever

Every place `kfx_platform` currently reaches ImGui (all via the opaque `gui/ImGuiContext.h`
wrapper — none include `imgui.h` themselves):

| Site | Uses today | Becomes |
|---|---|---|
| `RendererSoftware::PresentFrame()` | `ImGuiContextEnsure`, `ImGuiContextNewFrame`, `ImGuiContextRender` | `renderer_overlay->ensure/begin_frame/render` |
| `RendererSoftware::destroy_present_target()` | `ImGuiContextShutdown` (before `SDL_DestroyRenderer`) | `renderer_overlay->renderer_destroying` |
| `bflib_inputctrl.cpp` poll loop | `ImGuiContextIsActive`, `ImGuiContextProcessEvent` | `renderer_overlay->is_active/process_event` |
| `bflib_mspointer.cpp` (×2) | `ImGuiContextWantCaptureMouse` | `renderer_overlay->want_capture_mouse` |
| `RendererManager.cpp` façades | `ImGuiContextSetDemoVisible`, `ImGuiContextCreateTexture`/`UpdateTexture`/`DestroyTexture` | see below |
| `RendererManager.h` | `#include "gui/ImGuiContext.h"` for `ImGuiCursorImage`/`ImGuiCursorImageFn` types | deleted (callback removed) |

Two couplings that are *already* upward callbacks (registered from `main.cpp`) but consumed inside
`kfx_platform`'s `ImGuiContext.cpp` today — these **disappear entirely**, because after the move
`FrontendImGui.cpp` is in `kfx_frontend` and can call the real functions directly:

- `RendererSetCursorImageCallback` / `ImGuiCursorImageFn` → `FrontendImGui.cpp` calls
  `FeStyleGetCursorImage()` directly (`frontgui_style.cpp`, same library).
- `RendererSetMousePositionCallback` / `ImGuiMousePositionFn` → `FrontendImGui.cpp` calls
  `GetMouseX()`/`GetMouseY()` directly (`bflib_mouse.h`, `kfx_platform`, downward-legal).

The dynamic-texture helpers (`RendererCreateDynamicTexture` etc.) are **pure SDL** — no ImGui
symbol — so they stay in `kfx_platform` unchanged. Their handles are `SDL_Texture*` cast to
`void*`; `kfx_frontend` casts them to `ImTextureID` at the `ImGui::Image` call site, exactly as
today.

`RendererImGuiEnabled()` / `RendererSetImGuiEnabled()` is a plain `bool` gate — stays in
`kfx_platform`, now guarding whether the `renderer_overlay->*` calls fire at all.

---

## Target design

### New: `kfx_frontend/src/gui/FrontendImGui.{h,cpp}`

The moved-and-renamed `ImGuiContext.{h,cpp}` — verbatim logic, now in `kfx_frontend`. Owns the
context, both backends, `NewFrame`/`Render`, the demo/style-sheet debug windows, the
cursor-over-ImGui draw (calling `FeStyleGetCursorImage` directly), and the per-frame mouse-position
feed (calling `GetMouseX/Y` directly). Its per-frame `begin`/`render` entry points are what the
callback struct points at.

`kfx_frontend`'s existing `FrontendImGuiFrame()` (`frontgui_screens.cpp`, the current
`RendererImGuiFrameFn`) stays as the *submission* body and becomes the struct's `submit` member.

### New: `RendererOverlayCallbacks` in `renderer/RendererManager.h`

Mirrors the existing `RendererDrawCallbacks` pattern exactly (`RendererManager.h:149-153`,
`RendererManager.cpp:21-27`) — a `struct` of function pointers, an `extern const
RendererOverlayCallbacks *renderer_overlay;` defaulting to an all-noop static, and a
`set_renderer_overlay_callbacks()` setter. SDL types are fine in this header (`kfx_platform` owns
SDL); no ImGui type appears.

```c
struct RendererOverlayCallbacks {
    // lifecycle -- from RendererSoftware
    void (*ensure)(struct SDL_Window *window, struct SDL_Renderer *renderer);
    void (*renderer_destroying)(void);   // MUST run before SDL_DestroyRenderer
    // per-frame -- from RendererSoftware::PresentFrame, bracketing submit()
    void (*begin_frame)(void);
    void (*submit)(void);                // was RendererImGuiFrameFn
    void (*render)(void);
    // input -- from bflib_inputctrl / bflib_mspointer
    void (*process_event)(const union SDL_Event *event);
    TbBool (*is_active)(void);
    TbBool (*want_capture_mouse)(void);
    TbBool (*want_capture_keyboard)(void);
    // debug
    void (*set_demo_visible)(TbBool visible);
};
```

Retire `RendererImGuiFrameFn` / `RendererSetImGuiFrameCallback` / `RendererRunImGuiFrameCallback`
and the four `RendererSet{CursorImage,MousePosition}Callback` / `RendererSetImGuiDemoVisible`
entry points — all folded into this one struct + setter.

### `PresentFrame()` after the change

```
// backdrop already blitted + SDL_RenderTexture'd
static bool s_presenting_overlay = false;                 // reentrancy guard, unchanged intent
if (!s_presenting_overlay && RendererImGuiEnabled()) {
    s_presenting_overlay = true;
    renderer_overlay->ensure(lbWindow, m_renderer);       // lazy, idempotent (frontend side)
    renderer_overlay->begin_frame();
    renderer_overlay->submit();
    renderer_overlay->render();
    s_presenting_overlay = false;
}
SDL_RenderPresent(m_renderer);
```

No `ImGuiContextEnsure` return value is consulted; the frontend side no-ops cleanly if the
window/renderer isn't ready. Same "null until `setup_game()` wires it" behaviour as today's
`RendererImGuiFrameFn`.

### `main.cpp` wiring

The four scattered `RendererSet*` calls (`main.cpp:1277,1283,1292,1300`) collapse to one:

```c
set_renderer_overlay_callbacks(&renderer_overlay_impl);   // in setup_game(), next to renderer_draw_callbacks_impl
```

where `renderer_overlay_impl` is a `static const RendererOverlayCallbacks` whose members are thin
`extern "C"` wrappers around `FrontendImGui*` functions — the same shape as
`render_overlay_impl` (`main.cpp:1463`) and `renderer_draw_callbacks_impl` (`main.cpp:2187`).

---

## Ordering & correctness constraints

1. **`renderer_destroying()` before `SDL_DestroyRenderer(m_renderer)`.** The SDLRenderer3 backend
   holds references into the renderer; today's `destroy_present_target()` already calls
   `ImGuiContextShutdown()` first for exactly this reason. The callback must preserve that order.
2. **`ensure()` is called every frame, idempotent.** It must cheaply detect an unchanged
   window/renderer and return — the frontend side keeps the current `s_window == window &&
   s_renderer == renderer` fast-path.
3. **Reentrancy guard stays in `kfx_platform`**, around the whole `ensure/begin/submit/render`
   block — the ~21 nested `RendererPresentFrame()` call sites
   ([gpu-v2/08-risks.md](gpu-v2/08-risks.md) R1) are unaffected by this refactor and must stay
   that way.
4. **Event-forwarding gate.** `bflib_inputctrl.cpp` keeps its `ev.type != SDL_EVENT_MOUSE_MOTION`
   filter and its `is_active()` gate; only the function names change.
5. **Debug flags.** `-imguidemo` (`DFlg_ImGuiDemo`) routes through `set_demo_visible`;
   `-imguistyle` is already frontend-side (`FeStyleSheetFrame`) and needs no platform hook.

---

## CMake changes

- `CMakeLists.txt:164` — remove `target_link_libraries(kfx_common_opts INTERFACE imgui)`.
- `src/kfx_frontend/CMakeLists.txt` — add `target_link_libraries(kfx_frontend PRIVATE imgui)` and,
  if the STATIC-lib-vs-OBJECT-lib symbol-resolution gap bites the same way it does for `centitoml`,
  `target_sources(kfx_frontend PRIVATE $<TARGET_OBJECTS:imgui>)` or the equivalent the other
  libraries use. `PRIVATE` is correct: nothing above `kfx_frontend` includes `imgui.h`.
- `CMakeLists.txt:524-525` — keep `target_link_libraries(keeperfx[_hvlog] PRIVATE imgui)` (final
  executables still need the object; same reason `centitoml` is linked there).
- Delete the `imgui` link line from the 7 unrelated `src/kfx_*/tests/CMakeLists.txt`
  (`kfx_sim`, `kfx_game`, `kfx_net`, `kfx_config`, `kfx_render`, `kfx_script`, `kfx_apploop`) —
  keep it only in `kfx_frontend/tests/` (and `kfx_script_utest` / `kfx_apploop_utest` if they
  link `kfx_frontend`'s objects and hit the gap).
- `kfx_platform/tests/CMakeLists.txt` — remove the `imgui` line; move the two ImGui-related cases
  out of `RendererManager_test.cpp` (see Tests below).
- `deps/imgui` / `Dependencies.cmake` — unchanged.

## Layering check

`imgui` is third-party — `check_layering.py` doesn't police it either way. But the *intent* of the
check (no lower layer knowing a higher one's concerns) is served: after this, `kfx_platform` has
zero knowledge of any GUI toolkit, and `grep -rl 'imgui\|ImGui' src/kfx_platform` returns only
comment hits. Add that as a one-line assertion in the stage's verification, or a tiny CI grep.

## Tests

- `RendererManager_test.cpp` — the "enabled flag round-trips" case can stay (it's just the bool).
  The "`RendererSetImGuiDemoVisible` safe with no context" case moves to `kfx_frontend_utest`
  against `FrontendImGui`, or is dropped.
- New `kfx_frontend_utest` coverage: `set_renderer_overlay_callbacks(nullptr-members)` /
  default-noop safety, mirroring `render_overlay_test.cpp` and `RendererManager_test.cpp:122`'s
  `RendererDrawCallbacks` fake-struct pattern.
- `net_resync_test.cpp:125` already builds a fake `RenderOverlayCallbacks` — the new struct's fake
  will look the same.
- Full suite must stay green; this is a no-behaviour-change refactor.

---

## What this does *not* do — the ~21 present call sites

[gpu-v2/08-risks.md](gpu-v2/08-risks.md) R1 lists ~21 `RendererPresentFrame()` call sites across
`kfx_apploop`, `kfx_net`, `kfx_frontend`, `kfx_platform`, `kfx_render`. **None of them link or
reference ImGui** — they call the `kfx_platform` façade, which is exactly what this refactor keeps
as the single choke point. Consolidating those call sites themselves is a **separate phase**,
scoped in [gpu-v2/06-call-site-consolidation.md](gpu-v2/06-call-site-consolidation.md) — not
required for *this* stage's linkage goal, but no longer optional overall: Phase C's GPU-submission
façade needs it (see that document for why). It is sequenced directly after this stage and before
Phase C.0. Summary of that document's plan:
- The genuinely-necessary ones are the progress/stepping presents: Smacker frame stepping
  (`bflib_fmvids.cpp`, `front_fmvids.c`), net resync progress (`net_exchange_gameplay.c`,
  `packets_misc.c`), loading screens (`front_simple.c`), landview transitions (`front_landview.c`) —
  these route through one new `RendererPresentStepFrame()` entry point rather than being
  physically merged.
- `game_session_loop.cpp`'s 8 are the real loop bodies and a couple of edge-case redraws — those
  collapse to 2–3 via a new `RendererPresentGameFrame()` entry point, the one place a genuine
  physical reduction is worthwhile.

---

## Verification

- `KFX_OS=linux ./build-cmake.sh` + mingw cross-compile, both variants — the link-graph change is
  the main risk, so a clean link on both toolchains is the primary signal.
- `python3 scripts/check_layering.py --strict`.
- Full Catch2 suite green, unchanged count.
- `grep -rlE 'imgui|ImGui' src/kfx_platform src/kfx_sim src/kfx_render src/kfx_net src/kfx_config
  src/kfx_game` → comment-only hits (no `#include`, no symbol).
- **Screenshot-identical** frontend across every migrated screen (main menu, all Options tabs,
  every list/select screen, credits) + the `imgui_demo` (`-imguidemo`) and style-sheet
  (`-imguistyle`) debug windows — this refactor must change nothing visible.
- Interactive smoke: open Options, change a setting, resize the window, toggle fullscreen (renderer
  teardown/rebuild path — constraint 1), play a cutscene (nested present + `renderer_destroying`
  ordering), all with ImGui on; then `-classicmenu` once to confirm the noop path.

## Cross-references to update once implemented

- [04-imgui-gui-foundation.md](04-imgui-gui-foundation.md) §3.1 — the "`kfx_platform` owns the
  ImGui context" decision is superseded; record that ownership moved to `kfx_frontend` and why.
- [gpu-v2/08-risks.md](gpu-v2/08-risks.md) R1/R6 — note the linkage is consolidated; Phase B's
  capture and HUD work is now single-library.
- [gpu-v2/06-call-site-consolidation.md](gpu-v2/06-call-site-consolidation.md) — this stage is its
  named prerequisite; link back once both have landed.
- [architecture.md](../../Architecture/architecture.md) §5 callback catalogue — add
  `RendererOverlayCallbacks`.
- [00-overview.md](00-overview.md) — add this stage to the roadmap.
