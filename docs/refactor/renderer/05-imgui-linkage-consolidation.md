# Stage 5 — Consolidate ImGui linkage into one library

Status: **landed.** Pure refactor — no behaviour change, no visible change. ImGui
context/backend ownership moved from `kfx_platform/{include,src}/gui/ImGuiContext.{h,cpp}` to
`kfx_frontend/{include,src}/gui/FrontendImGui.{h,cpp}`, behind the new
`RendererImGuiCallbacks` struct (`renderer/RendererManager.h`) -- named that way, not
`RendererOverlayCallbacks` as originally sketched below, to avoid colliding with the
pre-existing, unrelated `RenderOverlayCallbacks` (`kfx_config/include/render_overlay.h`,
architecture.md §5.1). `kfx_imgui.h` (the `kfximgui::` int64_t/double-vs-int/float wrapper
templates) moved alongside it, from `kfx_platform/include/` to `kfx_frontend/include/`, for the
same reason -- it was already only ever consumed by `kfx_frontend`/`kfx_editor`, never
`kfx_platform` itself. A few implementation details below turned out to differ from what
shipped; search this file for "Landed as:" notes at each such point.
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
| `bflib_mspointer.cpp` (×2), `RendererSoftware::PresentFrame()` | `RendererScreenOwned()`/`ImGuiContextScreenOwned` (added after this table was first written) | `renderer_imgui_callbacks->screen_owned`, via the unchanged `RendererScreenOwned()` facade |

**Landed as:** the code had grown an `ImGuiContextScreenOwned`/`RendererScreenOwnedFn` pair (docs/
refactor/renderer/05-imgui-owned-menu-backdrop.md Phase C) between this table's original draft and
implementation — not listed above. It folds into the same struct as `screen_owned`, queried by both
`RendererSoftware::PresentFrame()` (the legacy-blit skip) and `bflib_mspointer.cpp` (the legacy-cursor
skip) — it can't be one of the "disappears entirely, called directly" cases below despite
`frontend_imgui_screen_active()`/`ingame_parchment_active()` both being `kfx_frontend` functions,
because those two `kfx_platform` call sites need the query from outside `FrontendImGui.cpp`'s own
per-frame block.

Two couplings that are *already* upward callbacks (registered from `main.cpp`) but consumed inside
`kfx_platform`'s `ImGuiContext.cpp` today — these **disappear entirely**, because after the move
`FrontendImGui.cpp` is in `kfx_frontend` and can call the real functions directly:

- `RendererSetCursorImageCallback` / `ImGuiCursorImageFn` → `FrontendImGui.cpp` calls
  `FeStyleGetCursorImage()` directly (`frontgui_style.cpp`, same library).
- `RendererSetMousePositionCallback` / `ImGuiMousePositionFn` → `FrontendImGui.cpp` calls
  `GetMouseX()`/`GetMouseY()` directly. **Landed as:** these live in `kfx_frontend/include/
  kjm_input.h`, not `bflib_mouse.h`/`kfx_platform` as this paragraph originally assumed — same
  library either way, so the "no callback needed" conclusion still holds, just for a same-library
  rather than downward-legal reason.

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

### New: `RendererImGuiCallbacks` in `renderer/RendererManager.h`

Mirrors the existing `RendererDrawCallbacks` pattern exactly (`RendererManager.h:149-153`,
`RendererManager.cpp:21-27`) — a `struct` of function pointers, an `extern const
RendererImGuiCallbacks *renderer_imgui_callbacks;` defaulting to an all-noop static, and a
`set_renderer_imgui_callbacks()` setter. SDL types are fine in this header (`kfx_platform` owns
SDL); no ImGui type appears.

**Landed as:** named `RendererImGuiCallbacks` / `renderer_imgui_callbacks`, not
`RendererOverlayCallbacks` / `renderer_overlay` as sketched here — `kfx_config/include/
render_overlay.h` already declares an unrelated `RenderOverlayCallbacks` struct + `render_overlay`
extern (architecture.md §5.1, `kfx_render` → frontend debug-overlay/parchment/panel-sprite draws),
and the two names differ only by "Renderer" vs "Render" — too easy to typo one for the other.
`screen_owned` is also added as a struct member (see the coupling-surface table above); it isn't in
the snippet below because this section predates that predicate existing at all.

```c
struct RendererImGuiCallbacks {
    // lifecycle -- from RendererSoftware
    TbBool (*ensure)(struct SDL_Window *window, struct SDL_Renderer *renderer);
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
    TbBool (*screen_owned)(void);
    // debug
    void (*set_demo_visible)(TbBool visible);
};
```

Retire `RendererImGuiFrameFn` / `RendererSetImGuiFrameCallback` / `RendererRunImGuiFrameCallback`
and the four `RendererSet{CursorImage,MousePosition,ScreenOwned}Callback` /
`RendererSetImGuiDemoVisible` entry points — all folded into this one struct + setter (`ensure` is
`TbBool`-returning, not `void`, per the note on `PresentFrame()`'s gating below).

### `PresentFrame()` after the change

```
// backdrop already blitted + SDL_RenderTexture'd
static bool s_presenting_imgui_frame = false;   // reentrancy guard, unchanged name/intent
if (!s_presenting_imgui_frame && renderer_imgui_callbacks->ensure(lbWindow, m_renderer)) {
    s_presenting_imgui_frame = true;
    renderer_imgui_callbacks->begin_frame();
    renderer_imgui_callbacks->submit();
    renderer_imgui_callbacks->render();
    s_presenting_imgui_frame = false;
}
SDL_RenderPresent(m_renderer);
```

**Landed as:** `ensure`'s `TbBool` return value **is** still consulted (this paragraph originally
said to drop it) — `RendererImGuiEnabled()` referenced just above doesn't exist any more (retired
2026-09-12 per this stage's own overview doc, well before this stage landed), so there is no other
gate available, and this is meant to be a pure no-behaviour-change refactor: today's code only ever
calls `submit()`/`RendererRunImGuiFrameCallback()` when `ImGuiContextEnsure()` actually succeeded
(context created, backends initialised), and `FrontendImGuiFrame()` submits real `ImGui::` widget
calls that are only safe with a live context. Always calling `begin_frame()`/`submit()`/`render()`
regardless of `ensure`'s result — even though each no-ops internally on `!s_active` — would still
call `submit()` (and therefore real `ImGui::` widget code) with no context active, which
`ImGui::Begin()`-family calls do not tolerate. The default no-op `RendererImGuiCallbacks` (active
before `main.cpp::setup_game()` registers the real one, and in every `*_utest` binary) has `ensure`
return `0` unconditionally, so the whole block is skipped rather than attempting a real context
against a null/absent window — same "inert until `setup_game()` wires it" outcome the "no return
value consulted" framing was going for, just via the gate staying in place instead of being
removed.

### `main.cpp` wiring

Four of the five scattered `RendererSet*` registration calls (`RendererSetImGuiFrameCallback`,
`RendererSetMousePositionCallback`, `RendererSetCursorImageCallback`,
`RendererSetScreenOwnedCallback`) collapse to one:

```c
set_renderer_imgui_callbacks(&renderer_imgui_callbacks_impl);   // in setup_game(), next to renderer_draw_callbacks_impl
```

where `renderer_imgui_callbacks_impl` is a `static const RendererImGuiCallbacks` whose members are
mostly `FrontendImGui*` function pointers directly (no wrapper needed — same shape as
`renderer_draw_callbacks_impl`), except `submit`, which stays `&app_imgui_frame` (a small
`main.cpp`-local wrapper, unchanged from before this stage): `kfx_editor` ranks above
`kfx_apploop`/`kfx_frontend`, so `FrontendImGuiFrame()` itself can't also call `editor_frame()` —
only `main.cpp`, the composition root, can see both.

**Landed as:** the fifth call, `RendererSetImGuiDemoVisible((start_params.debug_flags &
DFlg_ImGuiDemo) != 0)`, does **not** fold into the struct literal above — it's not a callback
*registration* like the other four, it's a state-*setting* call (pushing the resolved `-imguidemo`
flag through the newly-registered `set_demo_visible` member), so it stays a separate statement
right after `set_renderer_imgui_callbacks()`, same as it always was.

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

- `CMakeLists.txt` — remove `target_link_libraries(kfx_common_opts INTERFACE imgui)`.
- `src/kfx_frontend/CMakeLists.txt` — add `target_link_libraries(kfx_frontend[_hvlog] PRIVATE
  imgui)`, plus a `src/gui/*.cpp` glob (mirroring `kfx_platform`'s own per-subdirectory glob
  pattern) so `gui/FrontendImGui.cpp` gets picked up. **Landed as:** the plain
  `target_link_libraries` line was enough — no `$<TARGET_OBJECTS:imgui>` workaround needed.
  That workaround is for object files disappearing through *multiple* levels of `INTERFACE`
  indirection (`centitoml`'s own comment: "doesn't reliably make it through two levels..."); this
  is one direct `OBJECT`-library dependency on a `STATIC` library, which bundles fine, and
  `kfx_editor` was already proving the same thing indirectly (via `imgui_color_text_edit`'s
  `PUBLIC` link to `imgui`) before this stage touched anything. The final executables' own direct
  `imgui` link (next bullet) is the actual safety net either way.
- `CMakeLists.txt` — keep `target_link_libraries(keeperfx[_hvlog] PRIVATE imgui)` (final
  executables still need the object; same reason `centitoml` is linked there).
- Delete the `imgui` link line from the 5 unrelated `src/kfx_*/tests/CMakeLists.txt`
  (`kfx_sim`, `kfx_game`, `kfx_net`, `kfx_config`, `kfx_render` — none of these link the real
  `kfx_frontend` library, only a lightweight `kfx_frontend_state_test_stub`) — keep it in
  `kfx_frontend/tests/`, `kfx_script_utest`, `kfx_apploop_utest` and `kfx_editor_utest` (all four
  link `kfx_frontend`'s real objects).
- `kfx_platform/tests/CMakeLists.txt` — remove the `imgui` line.
- `src/kfx_platform/include/kfx_imgui.h` → `src/kfx_frontend/include/kfx_imgui.h` (`git mv`): this
  header (the `kfximgui::` int64_t/double-vs-int/float wrapper templates) was never consumed by
  anything in `kfx_platform` itself, only `kfx_frontend`/`kfx_editor` — physically living under
  `kfx_platform/include/` was a pure organizational artifact of every `kfx_*/include/` directory
  being globally propagated to every target (`CMakeLists.txt`'s `target_include_directories
  (kfx_common_opts INTERFACE ...)` block, unrelated to the per-library link graph
  `check_layering.py` actually polices), not a real dependency — but it would still have shown up
  as a false hit in this stage's own `grep -rlE 'imgui|ImGui' src/kfx_platform` verification below.
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

**Landed as:** kept (and extended) in `kfx_platform/tests/RendererManager_test.cpp` instead of
moving — it's exercising `RendererManager.h`'s own facade + default no-op struct, which is still
entirely `kfx_platform`-local and needs no ImGui symbol either way; a `kfx_frontend_utest` copy
would just be redundant. Added a `set_renderer_imgui_callbacks(&fake)`-based case (mirroring
`RendererDrawCallbacks`'s own fake-struct test right above it) and a separate
`FrontendImGui_test.cpp` in `kfx_frontend/tests/` exercising `FrontendImGui*`'s own
no-active-context safety.

- Full suite must stay green; this is a no-behaviour-change refactor.

(The two bullets this replaced described `kfx_frontend_utest`-side coverage for the struct wiring
itself and drew an analogy to `net_resync_test.cpp`'s unrelated `RenderOverlayCallbacks` fake — see
the "Landed as" note above for what actually shipped instead.)

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

- `KFX_OS=linux ./build-cmake-linux.sh` (the script this repo actually ships under that name now;
  `./build-cmake.sh` elsewhere in this doc/CLAUDE.md is a stale pre-rename name) + mingw
  cross-compile, both variants — the link-graph change is the main risk, so a clean link on both
  toolchains is the primary signal.
- `python3 scripts/check_layering.py --strict`.
- Full Catch2 suite green, unchanged count (plus the new coverage under Tests above).
- `grep -rlE 'imgui|ImGui' src/kfx_platform src/kfx_sim src/kfx_render src/kfx_net src/kfx_config
  src/kfx_game` → comment-only hits (no `#include`, no symbol).
- **Screenshot-identical** frontend across every migrated screen (main menu, all Options tabs,
  every list/select screen, credits) + the `imgui_demo` (`-imguidemo`) and style-sheet
  (`-imguistyle`) debug windows — this refactor must change nothing visible.
- Interactive smoke: open Options, change a setting, resize the window, toggle fullscreen (renderer
  teardown/rebuild path — constraint 1), play a cutscene (nested present + `renderer_destroying`
  ordering). **Landed as:** the `-classicmenu` mention here is stale — it was already retired
  (2026-09-12, `docs/refactor/ingame-gui/00-overview.md` §1/§8, predating this stage landing) by the
  time this stage started; there is no noop-frontend-path smoke check left to run.

## Cross-references to update once implemented

- [04-imgui-gui-foundation.md](04-imgui-gui-foundation.md) §3.1 — **done**: the "`kfx_platform` owns
  the ImGui context" decision is superseded; ownership moved to `kfx_frontend`
  (`gui/FrontendImGui.{h,cpp}`) and why is recorded there.
- [gpu-v2/08-risks.md](gpu-v2/08-risks.md) R1/R6 — **done**: noted the linkage is consolidated;
  Phase B's capture and HUD work is now single-library.
- [gpu-v2/06-call-site-consolidation.md](gpu-v2/06-call-site-consolidation.md) — this stage is its
  named prerequisite; link back once 06 itself lands too (not yet).
- [architecture.md](../../Architecture/architecture.md) §5 callback catalogue — **done**: added
  `RendererImGuiCallbacks` (not `RendererOverlayCallbacks` — see the naming note above).
- [00-overview.md](00-overview.md) — **done**: this stage's status/sequence entries updated.
- [gpu-v2/00-overview.md](gpu-v2/00-overview.md) — **done**: step 1 of the proposed implementation
  sequence marked landed.
