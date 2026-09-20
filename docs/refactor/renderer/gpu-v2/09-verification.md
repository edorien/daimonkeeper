# Verification

See [00-overview.md](00-overview.md) for stage status and shared context.

- **Phase A:** done — nothing to verify beyond the stage-2 regression pass that already covered it.
- **Pre-C.0 (call-site consolidation):** see
  [06-call-site-consolidation.md](06-call-site-consolidation.md)'s own verification section in full.
- **Phase B1:** frame-time measurement (around `PresentFrame` and `frontend_copy_background`)
  before/after; screenshot-identical frontend output (B1 changes nothing visible).
- **Phase B2:** a scheduled screenshot must contain the ImGui overlay (main menu, an Options tab,
  a migrated list screen) — it does not on the current build. Movie capture: either the feature is
  gone (verify the menu entry / `-recordmovie` path reports so cleanly, no silent flag set), or the
  re-implemented capture produces a playable file. Screenshot format/row-order correct on both
  toolchains. Screenshot taken with vsync on does not corrupt or stall beyond one frame.
- **Phase B3:** verified as part of the in-game-GUI-as-ImGui project itself — nothing to add here.
- **Cursor:** exactly one cursor visible at all times, at the tracked position, in the frontend, in
  the ImGui in-game HUD, and with `GUI_ICON_PACK=CLASSIC` selected; a B2 screenshot shows one
  cursor in every case.
- **Reentrancy / unusual present sites (R1):** trigger a screenshot during a palette fade, during
  Smacker cutscene playback, and during a multiplayer pause/unpause resync — no crash, no
  half-composited capture, no double-applied pending state.
- **Renderer recreate (R3):** on platforms where a fullscreen transition recreates the `SDL_Window`,
  toggle fullscreen on a screen that owns a dynamic texture (land preview) and confirm no
  use-after-free.
- **Graphics-API spike (C.0, R14):** the SDL_GPU smoke test
  ([02-graphics-api-choice.md](02-graphics-api-choice.md)) runs clean on both the mingw/Windows and
  native-Linux CI targets before any world-view geometry code is written against the API.
- **Phase C:** see the per-phase exit criteria in [07-phased-delivery.md](07-phased-delivery.md) —
  each phase (Pre-C.0 through C.4) has its own layering/build/visual-parity/frame-time gate; C.5 is
  intentionally unscoped pending real profiling data from C.1–C.4.
- `KFX_OS=linux ./build-cmake.sh` + mingw cross-compile (both variants) after each phase.
- `python3 scripts/check_layering.py --strict` — no new cross-library `#include` should be needed;
  the ImGui-adjacent façades on `RendererManager.h` (Phase B) and the new geometry-submission
  façade (Phase C, R6) are the extension points.
- Test on both the mingw/Windows and native-Linux SDL3 targets — `SDL_Renderer`'s (and, from C.0
  onward, SDL_GPU's) backing API differs by platform. Stage 4 has been exercising both through
  `imgui_impl_sdlrenderer3`, so a platform-specific regression here is more likely to show up as a
  *capture* (`SDL_RenderReadPixels` format/flip) or *interop* (SDL_Renderer/SDL_GPU device sharing,
  [02](02-graphics-api-choice.md)) difference than a compositing one.
