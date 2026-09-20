# Call-site consolidation (prerequisite for Phase C)

See [00-overview.md](00-overview.md) for stage status and shared context. This is new work, not
carried over from the original single-file plan — it exists because Phase C's new GPU-submission
façade (R6) makes something the plan already knew about, but deferred, no longer optional.

## Why this is in scope now

[08-risks.md](08-risks.md)'s R1 lists ~20 `RendererPresentFrame()` call sites across `kfx_apploop`,
`kfx_net`, `kfx_frontend`, `kfx_platform`, and `kfx_render`, every one of which triggers the full
`ImGuiContextNewFrame → RendererRunImGuiFrameCallback → ImGuiContextRender` sequence today.
[../05-imgui-linkage-consolidation.md](../05-imgui-linkage-consolidation.md) — the stage sequenced
directly ahead of Phase B to move all ImGui *knowledge* into `kfx_frontend` — explicitly does
**not** reduce this count, and says so plainly: "Consolidating those call sites themselves...is a
**separate, optional cleanup** — worth doing for R1's audit burden, not required for the linkage
goal, and not in this stage's scope."

Phase C is what removes the word "optional." Once `RendererGpu3D` exists, every one of these ~20
sites is a place that has to correctly handle:

- **The reentrancy guard (R1)**, which today only suppresses ImGui submission on nested presents —
  Phase C's GPU command-buffer submission needs an equivalent answer ("what happens if a present
  fires while a previous frame's GPU work hasn't been consumed yet"), and that answer has to hold
  at all ~20 sites, not just the two the current risk write-up calls out as "obvious."
- **The main-thread invariant (R2)** — SDL's renderer API, and (per
  [02-graphics-api-choice.md](02-graphics-api-choice.md)) SDL_GPU's command-buffer submission, are
  effectively main-thread-only in this codebase's usage today. Every call site is a place that
  invariant must continue to hold.
- **A future render thread (R12; explicitly deferred to C.5, not assumed now)** — if it's ever
  pursued, it needs to know which of these ~20 sites can tolerate the main thread not blocking on
  GPU submission and which can't. That's an unanswerable question today because the sites aren't
  even categorized, let alone unified.

Twenty-odd individually-audited call sites was tolerable when the only thing riding on them was
"does ImGui get suppressed correctly on a nested call." It stops being tolerable once a GPU
submission façade's correctness depends on the same set — every new obligation Phase C adds has to
be independently verified at each site, and a site added later (a 22nd, a 23rd) has no forcing
function to remember the contract at all.

## What's actually there today (verified, not estimated)

```
kfx_apploop/game_session_loop.cpp   ×8   (lines 277, 624, 773, 828, 857, 895, 1023, 1028)
kfx_render/vidmode.c                ×2   (685, 864)
kfx_net/net_exchange_gameplay.c     ×1   (153)
kfx_net/packets_misc.c              ×1   (370)
kfx_frontend/front_network.c        ×2   (232, 623)
kfx_frontend/front_fmvids.c         ×2   (81, 110)
kfx_frontend/front_landview.c       ×1   (1098)
kfx_frontend/front_simple.c         ×2   (179, 333)
kfx_platform/bflib_video.c          ×1   (225)
kfx_platform/bflib_fmvids.cpp       ×1   (510)
                                     ──
                                     21 real call sites
```
(Excludes the definition itself, comments, and test-file references.) This matches R1's "~20"
closely enough that the risk write-up doesn't need correcting, but the exact list matters for
scoping this phase, so it's recorded here rather than re-derived each time.

## What "consolidation" should and shouldn't mean

**It should not mean textually merging call sites across unrelated subsystems.** Smacker frame
stepping, net-resync progress, loading-screen progress, and palette-fade stepping are genuinely
different callers with genuinely different control flow — collapsing them into one physical call
site would mean restructuring those loops themselves, which is a much larger and riskier refactor
than this prerequisite phase should take on, and isn't what Phase C actually needs.

**It should mean replacing one untyped, undifferentiated entry point with a small, closed set of
named ones**, so that Phase C's new obligations get audited and enforced *per entry point*, not
per call site:

- **`RendererPresentGameFrame()`** — the real per-tick loop-body present. This is where
  `game_session_loop.cpp`'s 8 sites live, and it is the one place in this list where a genuine
  *physical* reduction is plausible: per
  [../05-imgui-linkage-consolidation.md](../05-imgui-linkage-consolidation.md)'s own estimate, "the
  real loop bodies and a couple of edge-case redraws...could plausibly collapse to 2–3." Do that
  collapse here, as part of this phase, since it's the highest-value and most contained piece of
  actual call-count reduction available.
- **`RendererPresentStepFrame()`** — everything that presents one increment of visible progress
  without a full game-loop tick behind it: Smacker frame stepping (`bflib_fmvids.cpp`,
  `front_fmvids.c` ×2), net resync progress (`net_exchange_gameplay.c`, `packets_misc.c`), loading
  screens (`front_simple.c` ×2), landview transitions (`front_landview.c`), frontend network wait
  screens (`front_network.c` ×2), and whatever `vidmode.c`'s ×2 and `bflib_video.c`'s ×1 turn out to
  be on inspection (palette-fade stepping and mode-change refresh, respectively, per R1's existing
  description — confirm at implementation time). These 13 stay as separate call sites in separate
  files — that's correct, they're separate callers — but they all route through one named function
  whose reentrancy/threading contract is written and verified **once**.

This turns "~21 places to audit for every new GPU-submission obligation" into "2 functions to get
right, called from ~21 places" — the actual leverage this stage's GPU work needs, without a
loop-restructuring refactor across `kfx_net`/`kfx_frontend` whose only purpose would be shrinking a
call count that doesn't, by itself, reduce risk.

## Sequencing

This phase lands **after**
[../05-imgui-linkage-consolidation.md](../05-imgui-linkage-consolidation.md) (which it depends on —
same "single choke point in `kfx_platform`" foundation) and **before** Phase C.0
([07-phased-delivery.md](07-phased-delivery.md)). It is a pure refactor: no behaviour change, no
visible change, same "screenshot-identical" bar
[../05-imgui-linkage-consolidation.md](../05-imgui-linkage-consolidation.md) already set for itself.
Doing it after Phase C.0 would mean touching the same ~21 call sites twice, once to consolidate and
once to add GPU-submission awareness — doing it once, first, is strictly less total work as well as
lower risk.

## Verification

- `KFX_OS=linux ./build-cmake.sh` + mingw cross-compile, both variants.
- `python3 scripts/check_layering.py --strict`.
- Full Catch2 suite green, unchanged count.
- Screenshot-identical across every affected path: a full game-loop session, a Smacker cutscene, a
  net resync (pause/unpause with a second client if feasible, or the existing resync test harness),
  a loading screen, a landview transition, a palette fade. This refactor must change nothing
  visible — the same bar [../05-imgui-linkage-consolidation.md](../05-imgui-linkage-consolidation.md)
  holds itself to.
- `grep -rn "RendererPresentFrame(" src/` shows exactly the two new named entry points as its only
  remaining callers of the raw underlying present, confirming no site was missed.
- See R15 in [08-risks.md](08-risks.md) for the specific regression risk this phase itself carries,
  and why it needs its own soak before Phase C.0 starts.
