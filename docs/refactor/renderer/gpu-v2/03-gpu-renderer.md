# Stage 3 — GPU acceleration (single source) — superseded

> **Superseded (2026-09-13c).** This document (previously the single source of truth for the whole
> GPU-acceleration stage, 866 lines) has been split into per-concern sub-plans so implementation
> can proceed one reviewable slice at a time. Start at
> [`00-overview.md`](00-overview.md) — it lists every sub-plan file and carries this file's shared
> context forward. This file is kept only so old links (from
> [`docs/merge-checks/opengl-renderer-review.md`](../../../merge-checks/opengl-renderer-review.md)
> and elsewhere) don't dead-end; it is not being updated further and its content has been fully
> distributed into:
>
> - [`00-overview.md`](00-overview.md) — status, shared context ("what stage 4/the in-game-GUI
>   project already built"), Phase A, `RendererType`/backend selection, non-goals
> - [`01-phase-b-2d-compositing.md`](01-phase-b-2d-compositing.md) — Phase B (B1/B2/B3, cursor)
> - [`02-graphics-api-choice.md`](02-graphics-api-choice.md) — **new**: OpenGL vs. Vulkan vs.
>   SDL_GPU for Phase C, recommending SDL_GPU
> - [`03-pixel-format-and-texture-cache.md`](03-pixel-format-and-texture-cache.md) — the
>   RGBA-not-palette-index decision and the shared sprite/texture cache
> - [`04-architecture-and-ir-boundary.md`](04-architecture-and-ir-boundary.md) — the layering split
>   and the `WorldFrame` IR
> - [`05-reviewed-branch-lessons.md`](05-reviewed-branch-lessons.md) — what to take/not take from
>   `origin/feature/opengl-renderer`
> - [`06-call-site-consolidation.md`](06-call-site-consolidation.md) — **new**: shrinking the ~21
>   `RendererPresentFrame()` call sites ahead of Phase C.0
> - [`07-phased-delivery.md`](07-phased-delivery.md) — the Pre-C.0/C.0–C.5 table
> - [`08-risks.md`](08-risks.md) — R1–R15
> - [`09-verification.md`](09-verification.md) — the cross-phase verification baseline

This file's prior content (Phase A/B/C design, risk list R1–R13, verification section) is preserved
in full in git history (see the commit that introduced this split) and in the sub-plans above —
nothing was dropped, only relocated and, in two places (graphics API, call-site count), expanded.
