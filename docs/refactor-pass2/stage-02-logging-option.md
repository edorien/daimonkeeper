# S02 — "Logging" option instead of the heavy-log build

**Status:** done 2026-09-28 (`bc2d4102e`..`7aadf7b89`, docs in the following commit) · **Work items:** W18 · **Depends on:** — (S01's
`KFX_UNWIRED` macro adapts to it) · **Risk:** low–medium · **Layout
change:** none · **Estimate:** 1–1.5 weeks

The full design and its evidence are in
[`00-analysis.md` §10](00-analysis.md#10-a-logging-option-instead-of-the-heavy-log-build-w18).
This document is the execution plan.

## Goal

There is one executable, `keeperfx`. How much it logs is a game option
(`LOG_LEVEL`, applied live) with four values:

| Value | Writes |
| --- | --- |
| Off | nothing, except crash reports |
| Normal (default) | today's standard log: errors, warnings and info lines |
| Debug | today's heavy log, minus GPU validation |
| Debug max | everything, including the ~740 debug lines no build prints today |

GPU validation becomes its own restart-only option (`GPU_DEBUG`).

## Steps

1. **Runtime level, with no behaviour change yet.**
   - In `kfx_platform/bflib_basics`, add `enum LogLevel`,
     `kfx_log_level` (default Normal) and `kfx_debug_threshold`
     (Normal → 0, Debug → 10, Debug max → 20), with `set_log_level()`.
   - In `globals.h`, rewrite `SYNCDBG`/`WARNDBG`/`ERRORDBG`/`NAVIDBG`/
     `NETDBG`/`SCRIPTDBG` as `do { if ((lv) < KFX_DEBUG_CEILING &&
     KFX_UNLIKELY(kfx_debug_threshold > (lv))) … } while (0)`, keeping
     their names and signatures.
   - `BFDEBUG_LEVEL` stays defined, but only as the ceiling. The two
     existing builds keep working: the normal build has ceiling 0, so every
     debug site still compiles away; the heavy-log build defaults the
     threshold to 10.
   - In `FUNCTESTING` builds the ftest harness (`ftest.c` setup) sets the
     level itself, to Normal by default or Debug per test, so ftests never
     depend on the tester's `keeperfx.cfg`.
2. **Convert the 58 `#if BFDEBUG_LEVEL` blocks**, file by file:
   - `vidmode.c`/`vidmode_data.cpp`/`vidmode.h` (font test data),
     `frontend.cpp` (font test screen, Shift+F toggle): always compiled,
     gated by the level at runtime; the font test entry is gated at startup.
   - `gui_topmsg.c:117` (on-screen error stats): runtime.
   - `thing_data.c`, `thing_creature.c:3874`, `config_keeperfx.c`,
     `config_campaigns.c`, `lvl_script_commands.c` (3 blocks),
     `main_game.c`, `game_session_loop.cpp` (stats dumps, play-time),
     `custom_sprites.c`, `bflib_dernc.c`, `bflib_mouse.cpp`,
     `cursor_tag.c`: plain `if (kfx_debug_threshold > N)`, or unconditional
     code where the block only declared a counter.
   - `RendererProfile.{h,cpp}` and `RendererGpu3D.cpp:783` (RPROF): always
     compiled, each macro gated on `kfx_debug_threshold > 0`.
   - `RendererGpu3D.cpp:45` (GPU debug mode): read the new `GPU_DEBUG`
     option instead (step 5).
   - `bflib_crash.c:156` (map-file name) and `bflib_basics.c:485` (log
     header): print the level instead of "heavylog"/"standard"; one `.map`.

   Rule to document next to the macros: **no debug-only block may write
   simulation state.** All 58 blocks satisfy it today.
3. **The "Off" level.**
   - `LbLog()` returns early when the level is Off, using the existing
     `TbLog.Suspended` path, so none of the ~3,300 always-on call sites
     change.
   - **Only crash reports are written when Off** (maintainer decision,
     2026-09-27). The crash parachute in `bflib_crash.c` (signal and
     exception handlers, stack trace) bypasses the level and opens the log
     lazily on a crash.
   - `error_dialog_fatal`: when logging is Off, its message box drops "See
     keeperfx.log for details" and says that logging is off (set Logging to
     Normal and retry for details). No log entry is written.
   - Functional tests need no exception: the ftest harness sets its own
     level in `FUNCTESTING` builds (step 1), so `FTest:` lines are always
     written there.
   - The on-screen debug message list (`write_log_to_array_for_live_viewing`)
     follows the level.
4. **Startup buffering.**
   - Between `LbErrorLogSetup` (`main.cpp`, early in `LbBullfrogMain`) and
     `load_configuration()`, write log lines into an in-memory buffer.
   - Once the level is known, flush the buffer to the file (Normal and
     above) or drop it (Off).
   - If configuration loading fails, always flush.
5. **The settings rows** go in `config_settingschema.c`, following the
   `SCREENSHOT` enum row:
   - `LOG_LEVEL`: `SOptT_Enum`, `SCat_Game`, `SApply_Live`,
     `enum_table = {OFF, NORMAL, DEBUG, DEBUGMAX}` (labels "Off",
     "Normal", "Debug", "Debug max").
   - `GPU_DEBUG`: `SOptT_Bool`, `SCat_Graphics`, `SApply_NeedsRestart`,
     `frontend_only`.
   - Add both keys to `config_keeperfx.c`'s `conf_commands[]` so
     `keeperfx.cfg` round-trips.
6. **Buffered writes for Debug and Debug max.**
   - Replace the per-line `fflush` in `LbLog` with a flush on
     `ERRORLOG`/`WARNLOG`, once per frame (a call from `keeper_screen_swap`
     or `gameplay_loop_logic`), on level change, in the crash handler, and
     at exit.
   - Normal keeps per-line flushing: it is low volume, and bug reports rely
     on the last line being on disk.
7. **HTTP API.** `get_log_tail` (`kfx_script/src/api.c`) flushes first, and
   adds `"log_level"` to its reply.
8. **Benchmark gate** (before step 9). Replay a fixed recording
   (`-packetload`, `-frameskip`, fixed seed). Compare the current `keeperfx`
   with the new binary at Normal, over 3 runs each. Accept a sim-turn time
   within noise (≤ 1%). If it's worse, lower `KFX_DEBUG_CEILING` or demote
   the hottest sites. Also measure Debug max once, for log size per minute
   and frame time, and record both in this document.
9. **Retire the second executable.**
   - `CMakeLists.txt`: remove `kfx_bfdebug_std`/`kfx_bfdebug_hvlog`, the
     `*_hvlog` object libraries in each `src/kfx_*/CMakeLists.txt`,
     `KFX_STATIC_LIBS_HVLOG`, the `keeperfx_hvlog` target and its `.map`
     link option.
   - `build/cmake/modules/Packaging.cmake`: install `keeperfx_hvlog` as a
     **copy** of `keeperfx` for the transition (see below).
   - `.github/workflows/build-{prototype,alpha-patch-unsigned,release-patch-unsigned}.yml`,
     `build-cmake.sh`, `build-cmake-linux.sh`, `build-package.sh`: drop the
     second target.
   - Transition shim: at startup, if the executable's own file name
     contains `hvlog`, use Debug for this session without writing
     `keeperfx.cfg`.
10. **Docs.** Update `README.md`, `CLAUDE.md` (the build section), and
    `architecture.md` §7.1 and §12.3.

## As built (2026-09-28)

One commit per step (step 8 is a measurement, recorded below; step 10's docs
went into step 9's commit and the docs commit after it). Where the build
differs from the plan above:

- **`#if (BFDEBUG_LEVEL > N)` maps to `if (KFX_DEBUG_ON(N))`**, the exact
  condition under which `*DBG(N, …)` prints (`KFX_DEBUG_ON` is in globals.h,
  next to the macros and the "no sim state in debug code" rule).
- **Session pinning instead of a harness hook.** `set_log_level_pinned()`
  (kfx_platform) sets the level for the session and makes keeperfx.cfg's
  `LOG_LEVEL` a no-op (`set_log_level_from_config()`); an options-screen
  change still applies. `main.cpp` pins Normal right after command-line
  parsing for an `-ftests` run, before the config is read, so the startup
  lines are at the pinned level too. There is no per-test Debug level.
- **Enum labels are the raw names** (`OFF`, `NORMAL`, `DEBUG`, `DEBUGMAX`), as
  on every other enum row of the options screen (`SOFTWARE`/`VULKAN`, …): the
  schema has no separate display labels.
- **Off and crashes:** `LbLogForceOn()`, called first by each crash handler,
  makes the rest of the process's logging write. SIGTERM goes through the
  POSIX crash handler too, so a `kill`/`timeout` at Off leaves a crash report.
  Windows' vectored exception handler logs first-chance exceptions only at
  Normal and above.
- **Per-frame flush:** `LbLogFlush()` runs in `RendererPresentFrame()`
  (kfx_platform), which covers menus as well as gameplay.
- **No transition copy (decided 2026-09-28).** Step 9 first shipped a
  `keeperfx_hvlog` copy of the installed binary and pinned Debug for any
  executable named `*hvlog*`. Both were dropped the same day: the external
  launcher is mostly obsolete, its options having moved into the in-game
  options menu. The HeavyLog CMake build presets and the VS launch entry went
  too. Only functional-test runs pin a level now.
- **Found on the way:** `LbFileMakeFullPath` drops the last character of the
  directory, which is why callers pass `"/"`; the unit test passes a
  trailing separator.

### Measurements

- **Benchmark gate (step 8).** Thread-CPU time spent in `update()`
  (temporary instrumentation in scratch worktrees), `bug_pathing_stair_treasury`
  (800 turns), five alternating runs each, on a loaded machine:

  | Binary | Median µs per turn |
  | --- | ---: |
  | today's `keeperfx` (S01 tip, ceiling 0) | 102.5 |
  | this stage at Normal (ceiling 20, every `*DBG` compiled in) | 100.5 |

  −2%, within noise (per-pair changes −3.2% … +3.5%), so the ceiling stays 20.
- **Debug max, once:** the same test wrote 34 MB (713,000 lines) in 45 s
  (~46 MB a minute); `update()` took ~900 µs per turn, the renderer held
  ~29 fps, and the test passed. Usable for short reproductions; the per-thing
  traces at levels 17–19 dominate.
- **Buffered writes (step 6):** the old heavy-log build drew 622 frames for
  `example_template_test`'s turns; with buffering the same turns draw 840.
- **Functional tests:** all 70 short ftests pass at Normal (`keeperfx`) and at
  Debug (the same binary named `keeperfx_hvlog`, while that name still
  pinned Debug), one process per test, and
  each log's header shows the level it ran at; the Debug run wrote 174 MB of
  logs in all. `harness_setup_failure` (not in the short list) passes through
  its own script.
- **Log parity:** `example_template_test`'s keeperfx.log from this binary at
  Normal is identical to the old `keeperfx`'s, and at Debug (the `*hvlog*`
  name) to the old `keeperfx_hvlog`'s, apart from the header line and
  per-frame/timing lines.

## Verification

- Steps 1–2 change nothing observable: compare `keeperfx.log` from a fixed
  replay at Normal (new binary) and from the old standard binary, ignoring
  timestamps.
- Heavy-log parity: a fixed replay at Debug (new) against the old
  `keeperfx_hvlog`. Same lines, except the header and GPU validation.
- Ftests at Normal and at Debug.
- A manual run at Off: no file is created in a normal session, and a forced
  crash (debug console) still writes the stack trace.
- The settings screen shows the row, the change applies without a restart,
  and the value survives a restart.

## Risks

- **Performance** in hot per-thing loops: that's what the step-8 gate is
  for.
- **Tools that parse `keeperfx.log`:** the LLM bridge, ftest scripts, and
  the user's own live-testing habit of reading `SYNCLOG` lines. All of these
  keep working at Normal and above, and none relies on the old header text.
- **The external launcher:** it probably offers a heavy-log option that
  starts `keeperfx_hvlog`. The shim keeps that working. Check before
  dropping the copy in a later release.

## Upstream-merge notes

- Upstream keeps `BFDEBUG_LEVEL` and the `*DBG` macro names, and both
  survive, so merged code compiles unchanged.
- A merged `#if (BFDEBUG_LEVEL > N)` block compiles against the ceiling,
  which is harmless. Convert it to a runtime check in the merge's review
  step.
- Upstream CI and packaging changes to `keeperfx_hvlog` must be dropped
  during merges.
