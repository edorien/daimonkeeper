# KeeperFX — Test & Coverage Harness

This document is the current, authoritative description of KeeperFX's
**unit-test harness** — the Catch2-based `KFX_BUILD_TESTS` machinery — and
its coverage tooling, plus (§7) the coverage tooling built on top of
`src/ftests/`'s in-game functional-test harness (`KFX_FUNCTESTING`). Like
[`architecture.md`](architecture.md), it describes what's true *now*;
`docs/refactor/testing/` is the historical narrative of how this was
built, stage by stage, and shouldn't be trusted to track current code
(see that directory's own framing, and `architecture.md` §14's document
map).

Two independent test mechanisms exist in this codebase:

- **`src/ftests/`** — in-game functional tests, exercised against a real
  running game via `-ftests`. Authoring a test, available test names, and
  the CLI flags (`-exitonfailedtest`, `-includelongtests`, `-headless`)
  are covered in [`src/ftests/README.md`](../../src/ftests/README.md) and
  `architecture.md` §9; this document's §7 covers only how its coverage
  data gets produced and folded into a report, alongside the same
  `KFX_TEST_COVERAGE`-driven tooling §6 covers for the unit-test harness.
- **The unit-test harness** (`KFX_BUILD_TESTS`) — the rest of this
  document.

---

## 1. Shape

One Catch2 v3.7.1 test binary per `src/kfx_*/` library:
`kfx_platform_utest`, `kfx_config_utest`, `kfx_pathfinding_utest`,
`kfx_sim_utest`, `kfx_render_utest`, `kfx_net_utest`, `kfx_game_utest`,
`kfx_frontend_utest`, `kfx_script_utest`, `kfx_apploop_utest` — all ten
`kfx_*` libraries have one; none are still missing. Test sources live in
each library's own `src/kfx_<name>/tests/` directory, globbed by that
directory's own `CMakeLists.txt` (same glob-per-library convention the
main build uses — see `architecture.md` §7.1). Registered with CTest via
`catch_discover_tests(..., TEST_PREFIX "kfx_<name>.")`, so `ctest -R
kfx_sim` matches every test in that binary and every test's full name is
namespaced (e.g. `kfx_sim.player_has_lost is false for an invalid
player`).

Gated by two off-by-default CMake options, native Linux only (no Windows
CI runner/Wine step exists to execute a mingw-cross-compiled test
binary):

```cmake
option(KFX_BUILD_TESTS "Build Catch2 unit tests for each kfx_* library (native Linux only)" OFF)
option(KFX_TEST_COVERAGE "Instrument kfx_* libraries for gcov/lcov coverage (requires -DKFX_BUILD_TESTS=ON)" OFF)
```

A contributor who never passes `-DKFX_BUILD_TESTS=ON` sees no new
targets, no Catch2 `FetchContent`, no new required tools — the default
build is unaffected.

## 2. Building and running

```bash
cmake -S . -B out/linux -G Ninja -DKFX_OS=linux -DKFX_BUILD_TESTS=ON
cmake --build out/linux --target \
  kfx_platform_utest kfx_config_utest kfx_pathfinding_utest kfx_sim_utest \
  kfx_render_utest kfx_net_utest kfx_game_utest kfx_frontend_utest \
  kfx_script_utest kfx_apploop_utest -j"$(nproc)"

ctest --test-dir out/linux --output-on-failure       # everything
ctest --test-dir out/linux -R kfx_sim                # one library
./out/linux/src/kfx_sim/tests/kfx_sim_utest -s        # a binary directly, verbose
./out/linux/src/kfx_sim/tests/kfx_sim_utest "[room_data]"  # a Catch2 tag filter
```

Each `*_utest` binary is also directly runnable with any Catch2 CLI flag
(`--order rand`, `-s` for successful-assertion output, a free-text
substring to filter `TEST_CASE` names, a `[tag]` filter matching the
`"[kfx_<name>][<file>]"` tags every `TEST_CASE` in this codebase carries).

## 3. Per-library link shape

Every `*_utest` target links the **real** `kfx_*` OBJECT library it
tests, plus every real library below it on the dependency ladder
(`architecture.md` §1) — not a mocked-out version. This was a deliberate
early finding (`docs/refactor/testing/stage-01-framework-and-scaffold.md`):
once `check_layering_symbols.py`'s accepted residuals were fixed
(`docs/refactor/todo/check-layering-symbol-level-blind-spot.md`), linking
the whole real dependency chain "just works" for most libraries, and only
a handful of genuine residual gaps need a stub.

| Target | Links (besides itself + everything below it) | Stub libraries needed |
|---|---|---|
| `kfx_platform_utest` | — | — |
| `kfx_config_utest` | `centitoml` (explicit — see §5) | — |
| `kfx_pathfinding_utest` | `centitoml` | — |
| `kfx_sim_utest` | `centitoml` | none (formerly `kfx_packet_test_stubs`, `kfx_sim_test_stubs.cpp` — removed, see §4) |
| `kfx_render_utest` | `centitoml` | none (formerly `kfx_packet_test_stubs` — removed, see §4) |
| `kfx_net_utest` | `centitoml` | `kfx_game_state_test_stubs`, `kfx_frontend_state_test_stub` |
| `kfx_game_utest` | `centitoml` | `kfx_frontend_state_test_stub` only (not `kfx_game_state_test_stubs` — `kfx_game` itself provides the real symbols) |
| `kfx_frontend_utest` | `centitoml` | none (linking `kfx_net`+`kfx_game`+`kfx_frontend` for real resolves everything) |
| `kfx_script_utest` | `centitoml`, `kfx_luajit` | none |
| `kfx_apploop_utest` | `centitoml`, `kfx_luajit` | none |

Every target also links `kfx_log_opts` (the logging compile settings;
there is one build variant since refactor pass 2's S02) and `Catch2::Catch2WithMain`
(see §4).
`centitoml` needs an explicit link on every target that transitively
reaches `kfx_config` (i.e. all of them): it's an OBJECT library wrapped
two `INTERFACE`-library hops deep inside `kfx_common_opts`, which doesn't
reliably survive into a final link — the same reason root
`CMakeLists.txt` links it directly onto `keeperfx`
rather than trusting the `INTERFACE` chain alone.

## 4. Shared test-support code

Every `*_utest` target links plain `Catch2::Catch2WithMain` directly — no
shared shim needed. That wasn't always true: a `kfx_test_main` library used
to sit here, forwarding `kfxmain()` into `Catch::Session().run(argc, argv)`,
because `kfx_platform`'s `PlatformLinux.cpp`/`PlatformWindows.cpp` used to
physically own the process `main()`/`WinMain()`, and every `*_utest`
transitively linking `kfx_platform` couldn't also use
`Catch2::Catch2WithMain` (its own `main` would conflict). Fixed by
`docs/refactor/todo/remove-kfxmain-symbol-residual.md`: the native entry
point moved down into `app_entry` (`src/native_entry.cpp`, `architecture.md`
§2.10), so `kfx_platform` no longer defines `main()` at all.

Two stub libraries paper over `check_layering_symbols.py`'s accepted
residuals (`architecture.md` §8.2) — cases where a *type* is declared in
a lower-ranked library by design, but the functions operating on it are
really implemented in a higher-ranked one, so a test binary that doesn't
happen to link that higher library needs a stand-in:

- **`kfx_game_state_test_stubs`** / **`kfx_frontend_state_test_stub`**
  (`src/kfx_net/tests/`) — fake the `game`/`kfx_game_state` and
  `kfx_frontend_state` globals `net_resync.cpp`'s raw-blob resync
  (`architecture.md` §6.2) reaches into. Two separate libraries, not one,
  because each stops being needed on a different schedule as more
  libraries get linked for real (`kfx_game_utest` still needs the
  frontend one but not the game one).

(A third and fourth stub — `kfx_packet_test_stubs` for
`get_packet`/`get_packet_direct`/`set_packet_action`/
`set_players_packet_action`, and `kfx_sim_test_stubs.cpp` for
`creature_table_add[]` — used to live here too. Both were removed once
those symbols' real implementations moved down into `kfx_sim` itself
(`packet_data.c`/`creature_graphics.c`), see
`docs/refactor/todo/remove-symbol-level-layering-residuals.md`.)

**Fixture-file path baking**: `kfx_config_utest` and `kfx_script_utest`
each `configure_file()` a `*_test_paths.h.in` → generated header (the
same `@ONLY`-substitution pattern the project's own `ver_defs.h.in`
uses), baking an absolute, `CMAKE_SOURCE_DIR`/`CMAKE_CURRENT_SOURCE_DIR`-
derived path into a macro at configure time — `KFX_CONFIG_TEST_FIXTURES_DIR`
(this test directory's `fixtures/` subdir, for `value_util_test.cpp`'s
`load_toml_file()` tests) and `KFX_REPO_CONFIG_DIR` (the repo's real
`config/` dir, for `lua_base_test.cpp`'s `open_lua_script()` fixture) —
so tests are runnable from any working directory, not just the build
tree.

## 5. Testability patterns

Three shapes cover essentially every test in this codebase (see
`docs/refactor/testing/stage-02-testability-and-fakes.md` for the
original design rationale):

- **Pattern A — reset a state struct.** Most world state lives in a
  per-library `extern` struct (`kfx_sim_state`, `kfx_config_state`,
  `kfx_net_state`, …, `architecture.md` §6.1). A test fixture
  `std::memset`s the relevant struct to zero (or calls the module's own
  reset function, where one exists — `light_initialise()`,
  `triangulation_init_triangles()`, `edge_points_clean()`), sets the
  handful of fields the function under test reads, and asserts.
- **Pattern B — fake a callback provider.** Where production code calls
  through one of `kfx_config`'s callback structs (`architecture.md` §5),
  a test registers a fake implementation (`set_game_callbacks(&fake)`,
  `set_config_reload_callbacks(&fake)`, …) and restores the default
  no-op table (`set_*_callbacks(nullptr)`) in the fixture's destructor.
  Necessary whenever the *default* no-op stub would make the interesting
  branch unreachable (e.g. `get_level_strings()`'s default returns NULL,
  so `get_string()` can't resolve a level string without a fake).
- **Fixture files** — a few tests need real file content, not just
  in-memory state (`value_util_test.cpp`'s `tests/fixtures/sample.toml`;
  `lua_base_test.cpp`/`lua_classes_test.cpp` against the real
  `config/fxdata/lua/` tree, since it's KeeperFX's own scripting layer
  and fully in-repo).

A recurring, minor testability gap found repeatedly while extending this
harness: a function used only within its own translation unit sometimes
has **no header declaration at all** (only a same-file forward
declaration, or none). Since a test lives in a different translation
unit, it can't call such a function without one. The fix applied every
time this came up — never a same-file `extern` in the test, always a
real header addition — was to add the missing prototype to that library's
own header (e.g. `lvl_script_conditions.h`, `dungeon_stats.h`,
`magic_powers.h`, `room_data.h`, `light_data.h`, `ariadne_navitree.h`),
matching the file's existing declaration style. This is a legitimate,
minimal production change (the function already had external linkage; it
was just never advertised), not a test-only workaround.

## 6. Coverage tooling

Layered on top of `KFX_BUILD_TESTS`, gated by `KFX_TEST_COVERAGE`:
`gcov` (bundled with GCC) plus `lcov` **1.16** — deliberately not the
latest 2.x, which needs non-core Perl modules (`DateTime`,
`Capture::Tiny`); 1.16 needs only core Perl, verified by grepping the
fetched tarball's `use` statements. Not `gcovr` either — it needs a pip
dependency chain (Jinja2, …) with no clean fetch equivalent. `lcov` is
fetched via `Dependencies.cmake`'s existing `kfx_fetch()` helper (the
same download-a-tarball-once mechanism every other prebuilt dependency in
this project uses) — **no `apt install lcov` / `pip install gcovr`
required**, consistent with this project's "the CMake build fetches its
own dependencies" convention (see root `CLAUDE.md`).

```bash
cmake -S . -B out/coverage -G Ninja -DKFX_OS=linux \
  -DKFX_BUILD_TESTS=ON -DKFX_TEST_COVERAGE=ON
cmake --build out/coverage --target \
  kfx_platform_utest kfx_config_utest kfx_pathfinding_utest kfx_sim_utest \
  kfx_render_utest kfx_net_utest kfx_game_utest kfx_frontend_utest \
  kfx_script_utest kfx_apploop_utest -j"$(nproc)"

ctest --test-dir out/coverage --output-on-failure   # must run first --
                                                     # `coverage` builds the
                                                     # binaries, ctest is
                                                     # what actually produces
                                                     # .gcda profiling data
cmake --build out/coverage --target coverage        # capture -> extract -> render
# report: out/coverage/coverage-html/index.html
```

The `coverage` custom target (root `CMakeLists.txt`) chains three `lcov`/
`genhtml` calls: `--capture` (whole build tree) → `--extract
".../src/kfx_*/src/*"` (drops every `_deps/` third-party file, `centitoml`,
and the test binaries themselves — confirmed via `grep '^SF:'
coverage.info` listing only real `src/kfx_*/src/*.c(pp)` production
files) → `genhtml` (renders `coverage-html/`). Its `DEPENDS` list
covers all ten `*_utest` targets, so `cmake --build out/coverage --target
coverage` alone (after `ctest` has produced `.gcda` data) builds
everything it needs — no need to list all ten targets by hand first. The
target carries `VERBATIM` — required for the `--extract` pattern above
to survive as a literal string; without it, CMake emits the pattern
unquoted on the generated shell command line and the shell glob-expands
it against the real filesystem before lcov ever sees the `*` (see the
Known Gaps entry for the bug this caused and how it was found).

**Rebuilding after a source change**: gcov can throw `libgcov profiling
error: overwriting an existing profile data with a different checksum`
against stale `.gcda` files left from a prior instrumented build of the
same source. Harmless, but clear them before re-running: `find
out/coverage -name "*.gcda" -delete`. CI never hits this (`coverage`
always starts from a fresh `out/coverage` tree).

**gcov's line-coverage denominator excludes comments, blank lines, and
any line that emits no executable code** (declarations, closing braces,
…) — it's not counting raw file line count. Confirmed directly: `bflib_math.c`
is 1,361 lines total but only 172 appear as `DA:` (instrumented) entries
in `coverage.info`.

**Current numbers** (regenerated 2026-08-30, 892 tests): **5.96% line
coverage** (7,489 / 125,589), **16.48% function coverage** (1,257 / 7,629).
The jump from 662→735 tests is a real, deliberate deep pass on
`kfx_platform` (§9 below) — this round moved past the earlier "no
further functions reachable without significant harness changes"
verdict by actually building fixtures (a `RendererDrawCallbacks` fake,
`bflib_sound.c`'s un-static'd `MaxNoSounds`/`SampleList`) rather than
stopping at the first wall. Before that pass, this same 662-test figure
moved twice in one day for tooling reasons, not test changes — see the
Known Gaps entries below for the full before/after record of each:
first a stale build tree (leftover `.o`/`.gcno` at pre-flatten paths,
from before the "Flatten kfx/platform and kfx/renderer" commit) was
found inflating the previously-reported denominator without affecting
the real numerator; then, independently,
the `coverage` target's `lcov --extract` step turned out to be silently
excluding 16 real source files two directories below
`src/kfx_platform/src/` (a CMake `VERBATIM` bug, now fixed), which added
both real coverage *and* real total lines back in once corrected. No
coverage floor is enforced — deliberately deferred
(`docs/refactor/testing/comprehensive/stage-05-coverage-tooling.md` §5)
until there's a more mature baseline to set one against. The report is
informational, uploaded as a CI artifact on every PR, not merge-blocking.

## 7. ftest-driven coverage (KFX_FUNCTESTING)

A second, independent source of coverage data: instead of Catch2 unit
tests exercising individual functions in isolation, this runs the real
compiled game through `src/ftests/`'s registered functional tests
(`architecture.md` §9, `src/ftests/README.md`) and captures whatever real
gameplay simulation touches. Complementary to §1–§6 above, not a
replacement — it reaches code paths (the actual per-turn `update()`
dispatch chain, real map/creature/room state transitions) that isolated
unit tests don't naturally exercise, at the cost of only covering
whatever the registered test scenarios happen to play through.

### 7.1 Why this needs its own option, and can't combine with KFX_BUILD_TESTS

`KFX_FUNCTESTING` (root `CMakeLists.txt`) compiles `src/ftests/*` into its
own `kfx_ftests` OBJECT library and defines `FUNCTESTING=1` across every
`kfx_*` library via `kfx_common_opts` — needed because
`kfx_sim`/`kfx_game`/`kfx_frontend`/`kfx_apploop` call `ftest_srand()`/
`ftest_update()` directly under `#ifdef FUNCTESTING`. Off by default
(nothing under `src/ftests/` compiles otherwise — confirmed by grep: the
legacy Makefile's `FTEST_DEBUG=1` was the only place that ever defined
this, and it isn't part of the CMake build CI actually runs).

It's **mutually exclusive with `KFX_BUILD_TESTS`** (a hard `FATAL_ERROR`
at configure time, not just an untested combination): every `*_utest`
binary reuses the exact same `kfx_sim`/`kfx_game`/… OBJECT-library `.o`
files `keeperfx` does, so turning `FUNCTESTING=1` on for one turns it on
for all of them — they'd all need `ftest_srand()`/`ftest_update()`
resolved. Linking `kfx_ftests` in to provide those symbols doesn't work
either: `kfx_ftests`' own sources reach all the way up to `kfx_frontend`/
`kfx_net` (`message_add_fmt`, `kfx_net_state`, …), and CMake OBJECT
libraries don't forward each other's compiled objects transitively
(confirmed with an isolated repro — `target_link_libraries` between two
OBJECT libraries propagates usage requirements like include dirs, never
the objects themselves) — only a target listing the *entire* dependency
ladder directly, the way `kfx_apploop_utest` already does, can resolve
them. Retrofitting every narrower `*_utest` to link the full ladder would
undo their deliberately minimal, individually-justified link shapes (§3
above) for a combination nobody actually needs at the same time:
`-ftests` exercises the real compiled game against real game data, unit
tests deliberately avoid needing either. Build them in separate trees.

### 7.2 Headless execution

`src/ftests/` originally only ran inside a fully-windowed, audio-enabled
game session — unusable for CI/sandboxed coverage runs (no display, no
audio device) and wasteful even locally (real rendering/audio work
neither the ftest scenarios nor coverage capture care about). The
`-headless` flag (`main.cpp`, alongside `-nosound`) fixes this two ways:

- **Video**: sets `VideoDisabled` (`bflib_video.h`/`.c`), checked in
  `PlatformLinux`/`PlatformWindows::VideoInit()` to force SDL's `"dummy"`
  video driver via `SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy")` before
  `SDL_Init(SDL_INIT_VIDEO)` — a real (if never-shown) window/surface
  still gets created, so the rest of the engine's window/renderer code
  runs completely unmodified. Verified directly (a standalone SDL3 probe,
  then the real game binary): `SDL_Init`/`SDL_CreateWindow`/
  `SDL_GetWindowSurface`/`SDL_UpdateWindowSurface` all succeed against the
  dummy driver, including with `DISPLAY`/`WAYLAND_DISPLAY` both unset.
  `daimonkeeper.log` logs `SDL video driver: dummy` (`LbScreenInitialize`,
  `bflib_video.c`) to confirm it took effect.
- A dummy display reports zero real display modes, which broke
  `LbHwCheckIsModeAvailable()` (`bflib_video.c`) for *every* requested
  resolution, including the 320×200 failsafe — `setup_game` failed
  outright before this was found (only caught by testing against a real
  install, not something a config-load-only test would surface). Fixed
  via an existing, previously-unused hook: `VideoDisabled` now also makes
  `PlatformManager_ForcesAllModesAvailable()` return true, the same
  "trust the requested mode" escape hatch `IPlatform::
  ForcesAllModesAvailable()` already existed for.
- **Audio**: `VideoDisabled`'s sibling, `SoundDisabled`, already existed
  (`-nosound`/`-s`) — `init_sound()` (`sounds.c`) returns before touching
  OpenAL/SDL audio at all when set. `-headless` just also sets it.

### 7.3 Real game data: KFX_FTEST_DATA_DIR

Getting past config loading and actually loading a level needs the real,
proprietary Dungeon Keeper data files this repo can't distribute (root
`CLAUDE.md`). `KFX_FTEST_DATA_DIR` points at a real install (e.g. a local
`core_files/` directory — gitignored, not part of this repo);
`build/cmake/modules/StageFtestData.cmake` copies only the subset the
*registered, non-long-running* tests actually need into the build tree
the game (`daimonkeeper`, target `keeperfx`) runs from, determined empirically with `strace` against a real
run rather than guessed:

| Staged | Size | Why |
|---|--:|---|
| `data/`, `fxdata/`, `creatrs/` | ~50MB | Sprites/palettes/fonts, engine Lua scripts, creature stat configs — needed unconditionally by any session |
| `campgns/keeporig`, `campgns/keeporig_lnd`, `levels/deepdngn` (full) | ~32MB | The campaign/level the registered short tests actually load |
| Every other `campgns/`/`levels/`/`multiplayer/` top-level `.cfg`/`.txt` | ~200KB | Every session scans all installed campaigns/mappacks at startup regardless of which loads — missing/incomplete ones just log a harmless startup warning |
| `mods/load_order.cfg` + the mods it references in `[after_base]` | ~7MB | Only what `load_order.cfg` itself enables — a real install's full `mods/` can be much larger (one unrelated mod alone was 55MB) |
| `save/` (empty, created) | — | Write-only game output, not input data |

Not staged at all — confirmed unused for these tests: `sound/`, `music/`
(`SoundDisabled` skips audio file access entirely), `unearth/`,
`scrshots/`, `ldata/`, and every `mods/` subdirectory `load_order.cfg`
doesn't reference. Total: ~90MB, vs. ~1.5GB for a full real install.
**Update the hardcoded `keeporig`/`keeporig_lnd`/`deepdngn` list in
`StageFtestData.cmake` if a newly-registered test uses a different
campaign or level.**

The script also clears any `.gcda` already in the destination tree before
staging — see §6's stale-checksum note above; without this, a build tree
reused across multiple ftest-coverage runs (or one that previously ran
the unit-test coverage workflow) silently accumulates old counts into the
new run for every file whose compiled object happens to match
byte-for-byte, inflating the reported numbers without it being obvious
why. Confirmed the hard way: a run against a tree with stale `.gcda` from
an earlier session reported 35.0%/53.0% line/function coverage; the same
run against a freshly-cleared tree reported 31.8%/42.5% — the *lower*
number is the correct, isolated ftest-only figure, not a regression.

### 7.4 Building and running

Uses `out/coverage-ftest`, not `out/coverage` — §6's `out/coverage` is
already the unit-test coverage tree's conventional name, and the two
can't share one tree (§7.1: `KFX_BUILD_TESTS`/`KFX_FUNCTESTING` are
mutually exclusive in a single configure).

```bash
cmake -S . -B out/coverage-ftest -G Ninja -DKFX_OS=linux \
  -DKFX_BUILD_TESTS=OFF -DKFX_FUNCTESTING=ON -DKFX_TEST_COVERAGE=ON \
  -DKFX_FTEST_DATA_DIR=/path/to/a/real/game-data/install
cmake --build out/coverage-ftest --target coverage
# report: out/coverage-ftest/coverage-html/index.html
```

Unlike the unit-test path, this is a single target — no separate `ctest`
step, since there's nothing to discover; `coverage` itself stages the
data, runs `keeperfx -ftests -headless -exitonfailedtest` (wildcard, no
test name: runs every `tests_list` entry, `long_running_tests_list`
entries excluded unless `-includelongtests` is added), then captures/
extracts/renders via the same `lcov`/`genhtml` calls §6 describes.

**gcov only flushes `.gcda` on a clean process exit.** If the `-ftests`
run is killed (a CI timeout, a hung test, Ctrl-C) before it finishes, the
capture step runs against *zero* coverage data for the whole run, not a
partial result — confirmed directly (a SIGTERM-killed run produced no
`.gcda` at all; the next clean single-test run produced 288). This is why
a test that stalls under `-headless` is a correctness problem for this
target, not just a slowness annoyance (§7.5).

### 7.5 Known gap: GUI-dependent tests don't run headlessly

`bug_invisible_units_cant_select` (`src/ftests/ftest_list.c`) is commented
out of `tests_list` rather than registered: it drives mouse-cursor/
thing-under-hand selection
(`ftest_util_center_cursor_over_dungeon_view()`, `player->thing_under_hand`),
which never reliably resolves without a real display to pick against.
Confirmed against a real install: the other 6 registered short tests pass
in well under a minute combined, while this one alone ran past 3 minutes
without completing a single one of its 10 repeat iterations. Per §7.4,
a stall here would cost the *entire* run's coverage data, not just this
one test's — hence commented out rather than left to intermittently wedge
the target. Re-enabling it needs either a fix that doesn't depend on real
rendering, or a genuinely long timeout budget accepted as a known cost.

### 7.6 Merging with the unit-test report

The two coverage sources are genuinely complementary — real gameplay
simulation and isolated unit tests exercise mostly different lines — so
`scripts/merge_coverage.sh` combines both trees' already-generated
`coverage.info` (via `lcov --add-tracefile`, which merges hit-counts
per source line rather than picking one input over the other) into one
report, without needing a third build tree or a CMake target of its own
(it only reads two already-built trees' output, and the two source
configurations can't coexist in one configure to begin with — §7.1):

```bash
scripts/merge_coverage.sh out/coverage out/coverage-ftest out/coverage-merged
# report: out/coverage-merged/coverage-html/index.html
```

Defaults to exactly those three paths (`out/coverage` = unit-test tree,
`out/coverage-ftest` = ftest tree, per §7.4's naming), so a bare
`scripts/merge_coverage.sh` works once both trees have already had
`cmake --build <tree> --target coverage` run in them. It looks for a
fetched `lcov`/`genhtml` under either tree's `deps/lcov/` (same pinned
1.16 either way), so only one of the two strictly needs
`-DKFX_TEST_COVERAGE=ON` to have actually fetched it — though both need
`KFX_TEST_COVERAGE` on to have produced a `coverage.info` at all.

### 7.7 Current numbers

Regenerated 2026-08-31 against a real KeeperFX install, 15 registered
short tests (the original 6, the seven `creature_*` tests described
below, plus `creature_prison_capture` and `creature_torture_ownership`
from the room-owner-vs-creature-owner-differs round further down):

| Source | Line coverage | Function coverage |
|---|--:|--:|
| Unit-test harness alone (§9's 892-test figure has since grown; this run: 833 discovered tests) | 6.34% (7,956 / 125,585) | 16.91% (1,290 / 7,629) |
| ftest harness alone, from a freshly `.gcda`-cleared tree (§7.3's contamination note) | ~33-35% | ~44-53% |
| **Merged** (`scripts/merge_coverage.sh`) | **36.2%** (44,356 / 122,699) | **52.7%** (4,021 / 7,630) |

The merged total is higher than either source alone by more than either
source's own gap to zero would suggest if the two mostly overlapped —
confirming they're hitting substantially different code, not mostly the
same lines twice. Thirteen short gameplay scenarios reaching higher function
coverage than 833 targeted unit tests is a real signal about where each
approach's strengths lie: unit tests reach deliberately broad,
deep-but-narrow slices (one function's edge cases in isolation); real
gameplay simulation reaches the actual per-turn dispatch chains those
unit tests don't naturally traverse together. Neither replaces the
other — §7's opening paragraph. (Combat-adjacent files' exact percentages
fluctuate a few points run to run — AI pathing/engagement timing isn't
perfectly deterministic — so treat single-file numbers below as
directional, not exact reproducible figures.)

**Stale-exclusion correction (2026-08-31, same day):** the 35.3%/52.7%
figure directly above this was measured against an `out/coverage`
(unit-test tree) `coverage.info` captured *before* the
`KFX_LCOV_EXCL_LINE_PATTERN` exclusion (this file's own §6, `CMakeLists.txt`)
existed in the working tree. `lcov --add-tracefile` unions the line sets
of everything it merges, so a diagnostic-log line the *ftest* tracefile
correctly excluded still got pulled back into the merged total via the
stale unit tracefile, which had captured it as a real (mostly-uncovered)
line. Re-running `ctest`+`cmake --build out/coverage --target coverage`
fresh against the current exclusion pattern before merging again dropped
the shared denominator by 2,918 lines (125,617→122,699) and raised the
merged line percentage from 35.3% to 36.2% with the numerator barely
moved (44,392→44,356) — i.e. the codebase's *real* effective coverage was
already the higher number; the lower one was measuring stale noise, not
missing tests. Re-running also surfaced an unrelated pre-existing issue:
one `kfx_sim` `TEST_CASE` has a name long enough that `ctest`'s
exact-name Catch2 filter invocation for it produces no output at all
(confirmed: the same literal name passed directly to the test binary also
silently produces nothing, while a short wildcard matching the same
`TEST_CASE` passes normally) — flagged separately, not fixed here, since
it predates and is unrelated to today's ftest work.

**`creature_combat_power_hand`** (`src/ftests/tests/ftest_creature_combat_power_hand.c`)
was the first of four tests added directly off this table's per-file
breakdown: `creature_states_combt.c` was the single largest proportional
gap in kfx_sim (10.3% line coverage before, despite `kfx_sim/tests/
creature_states_combt_test.cpp`'s unit tests already covering the
battle-list bookkeeping — the harder line-of-sight/combat-target-selection
logic was explicitly left as a "hard tail" there). Spawns a keeper
creature and a nerfed (1hp) hero creature next to the player's dungeon
heart on map00011 ("Hearth"), lets the dungeon AI's combat state machine
resolve the fight, then exercises `PwrK_HAND` pickup/drop via
`magic_use_available_power_on_thing()` directly — the headless-safe
equivalent of what `bug_invisible_units_cant_select` (§7.5) tried to do
through mouse-cursor picking. Result: `creature_states_combt.c`
10.3%→~40-48%, `power_hand.c` 15.0%→23.0%.

First attempt carved a fresh `SlbT_CLAIMED` arena at an arbitrary,
disconnected map location (the pattern most of this directory's other
tests use) — `replace_slab_from_script()`'s claim logic turned out to
silently resolve that to `SlbT_PATH` owned by `PLAYER_NEUTRAL` instead of
the requested owner/kind, evidently depending on adjacency to
already-owned territory rather than being a context-free paint. Fixed by
spawning next to the player's real, already-`CLAIMED` dungeon heart
(`dungeon->dnheart_idx`) instead of carving new ground — a pattern the
three tests below reuse directly (spawn 2 slabs one side of the heart,
build the target room 3 slabs the other side).

**`creature_temple_prayer`**/**`creature_lair_healing`**/**`creature_garden_eating`**
(`src/ftests/tests/ftest_creature_{temple_prayer,lair_healing,garden_eating}.c`)
target three more `creature_states_*.c`/room files
(`creature_states_pray.c` was at 0% before; `creature_states_lair.c`,
`room_garden.c` both low) by driving each room's real "creature has a
need, AI sends it to the room" flow rather than the player-assigned-job
flow combat/power-hand used:

- **Lair** (health-gated): `creature_requires_healing()`
  (`creature_states_lair.c`) checks only `thing->health` against a config
  threshold — set the spawned creature's health to 1 directly (same
  technique as nerfing the combat test's enemy) and the AI queues healing
  sleep on its own. Passed on the first real run.
- **Garden** (hunger + food-capacity-gated): `process_creature_needs_to_eat()`
  additionally requires actual food in the room, which a freshly-carved
  room doesn't have (real gameplay grows it over many turns via
  `room_grow_food()`) — `room_create_new_food_at()` (`room_garden.c`)
  creates it directly instead of waiting. Also passed first-run once that
  was in place.
- **Temple** (anger-gated, the hard one): `Job_TEMPLE_PRAY` needed
  `set_creature_assigned_job()` *and* the room needed
  `set_room_available(PLAYER0, RoK_TEMPLE, 1, 1)` — map00011's own script
  leaves TEMPLE only researchable, not buildable
  (`ROOM_AVAILABLE(ALL_PLAYERS,TEMPLE,1,0)`); carving the room directly
  bypasses the UI's placement gate, but `creature_can_do_job_for_player()`
  still respects the level's `can_build` flag separately. Even then, the
  opportunistic check (`creature_states.c`'s `anger_process_creature_anger`)
  only re-fires every 128 turns per creature and doesn't always land on
  the first attempt — needed an 800-turn budget to find where it actually
  resolved (turn 356) before trimming to a 600-turn budget with margin.

All three also call `internal_set_thing_state(creature, CrSt_CreatureDoingNothing)`
right after setup: a freshly-spawned creature can start in
`CrSt_MoveToPosition` (its own post-spawn settling), and the opportunistic
need-check that drives all of temple/lair/garden only evaluates from the
idle state.

**Bug found and fixed along the way**: `creature_model_id()`
(`config_creature.c`) returned one past the `model[]` slot it actually
found a name at — `creature_model_id("ORC")` resolved to
`FLOATING_SPIRIT`'s model index instead of ORC's. A pre-existing unit
test (`config_creature_test.cpp`) had already documented this as a
"real, currently-dormant quirk (nothing in this codebase calls
creature_model_id)"; `ftest_creature_temple_prayer.c` became the first
real caller and hit it immediately (spawned a `FLOATING_SPIRIT`, which
promptly got deleted for being created inside a wall — that creature type
apparently isn't meant to spawn like a normal ground creature). Fixed to
return the bare loop index, matching every other 1-based accessor's
convention; the unit test updated to assert the corrected value rather
than the bug.

**`creature_training`**/**`creature_guard_post`**/**`creature_barracks`**
(`src/ftests/tests/ftest_creature_{training,guard_post,barracks}.c`)
target three more near-zero `creature_states_*.c` files
(`_train.c` 3.2%, `_guard.c`/`_barck.c` both 0.0%) via a *third*, simpler
dispatch path distinct from both combat/power-hand's player-action calls
and temple's anger-motive chain: `creature_doing_nothing()`
(`creature_states.c`, the `CrSt_CreatureDoingNothing` state handler
itself) checks `cctrl->job_assigned` directly and calls
`attempt_job_preference()` -- same 128-turn-per-creature cooldown pattern
as temple, but with no anger/annoy-config gate and no room-availability
wrinkle (map00011's script already leaves TRAINING/GUARD_POST/BARRACKS
fully buildable, unlike TEMPLE). All three passed on the first real run
once this mechanism was identified — carve room, `set_creature_assigned_job()`
with the job's `get_id(creaturejob_desc, "...")` value, force
`CrSt_CreatureDoingNothing`, wait up to 600 turns. Result:
`creature_states_train.c` 3.2%→27.1%, `creature_states_guard.c` 0%→28.1%,
`creature_states_barck.c` 0%→33.3%.

Not yet attempted: `creature_states_scavn.c` (2.7%, `SCAVENGER` is
`can_build=0` on this level like TEMPLE *and* needs a live enemy creature
to convert, not just an idle keeper one) and `creature_states_hero.c`
(9.2%, hero-side AI rather than a player room-job flow this recipe
targets). `creature_states_tresr.c` is only 5 lines total, not worth a
dedicated test. `_mood.c`/`_rsrch.c` were already at 20%+ before this
round, from earlier unit-test and ftest work.

**`creature_prison_capture`** (`src/ftests/tests/ftest_creature_prison_capture.c`)
and **`creature_torture_ownership`** (`src/ftests/tests/ftest_creature_torture_ownership.c`)
target `_prisn.c` (26.5% line / 46.2% function) and `_tortr.c` (33.1%
line / 46.7% function) specifically for their room-owner-vs-creature-owner
ownership-differs behaviour — capturing an enemy, and an enemy actually
being tortured rather than just self-training. `_wrshp.c` (28.0% line /
60.0% function, risen incidentally from the tests above rather than a
dedicated one) turned out to have no equivalent behaviour at all — see
below. (Percentages here are post-stale-exclusion-fix, see this
section's own note above.)

`creature_prison_capture` went through two dead ends before landing on
its final approach:

- **First attempt: `PwrK_HAND` pickup + drop.** The natural-looking way
  to "capture an enemy" is the player's hand: `magic_use_available_power_on_thing(PwrK_HAND, ...)`
  to pick up an unconscious hero, `dump_first_held_thing_on_map()` to
  drop it in the prison. This fails outright:
  `can_cast_power_on_thing()` (`magic_powers.c`) checks the power's
  config `Castability` flags, and `config/fxdata/magic.cfg`'s `POWER_HAND`
  entry (`ANYWHERE OWNED_CRTRS CUSTODY_CRTRS ALL_FOOD ALL_GOLD
  OWNED_OBJECTS_PICKUP`) has neither `UNCONSC_CRTRS` nor any
  enemy-targeting flag — a player's hand plainly cannot grab an
  unconscious enemy in this engine, by design. The real capture mechanic
  is creature-AI-driven instead: an imp/special-digger's own task stack
  (`add_unclaimed_unconscious_bodies_to_imp_stack()`, `spdigger_stack.c`)
  notices unconscious enemies nearby and drags them to a prison room
  itself, eventually reaching `thing_creature.c`'s
  `controlled_creature_drop_thing()` — the same function a
  direct-control player drags-and-drops through.
- **Final approach: call `controlled_creature_drop_thing()` directly.**
  Spawning a real imp and waiting for its task-stack AI to path-find and
  drag a body would be far slower and less deterministic than every
  other test in this round (temple/lair/garden's philosophy of forcing
  preconditions directly rather than waiting on organic AI). Instead: a
  spawned PLAYER0-owned "guard" creature stands in the room and plays the
  role of the dropper (`controlled_creature_drop_thing()` only reads its
  `->owner`/`->mappos`/`CreatureControl`, nothing dragged/held-state
  specific), dropping an unconscious `PLAYER_GOOD`-owned captive placed
  directly at the room's centre subtile (the "dropped into the centre of
  a 3x3 room" scenario shape).
  This surfaced a genuine transient-state pitfall: right after the call,
  `active_state` reads `CrSt_CreatureArrivedAtPrison` as expected, but
  that state's handler (`creature_arrived_at_prison()`,
  `creature_states_prisn.c`) only runs on the *next* game turn and
  immediately advances the creature to `CrSt_CreatureInPrison` via
  `add_creature_to_work_room()` — from which the room's own idle
  "shuffle to a new spot" behaviour (`process_prison_visuals()`) can
  transiently bounce `active_state` back through `CrSt_MoveToPosition`
  every so often, entirely healthy captivity behaviour that looks
  identical to a failed capture if you only check one snapshot of
  `active_state`. Fixed by waiting for the one signal stable across that
  churn instead: `cctrl->work_room_id` being set to the prison room
  (cleared only if the creature actually leaves the room, e.g. death or
  jailbreak). The ownership-differs behaviour itself is asserted
  directly: `room_initially_valid_as_type_for_thing()` — which
  `creature_arrived_at_prison()` must pass for `work_room_id` to get set
  at all — only accepts a room whose owner differs from the creature's
  own owner when `enemies_may_work_in_room(room->kind)` is true (backed
  by `CAPTIVITY`'s job config allowing `ENEMY_CREATURES`); the test calls
  it a second time itself to pin down that this is *why* the differently-owned
  captive was let in, not an accident. (`jailbreak_possible()` also keys
  off this same ownership mismatch, but additionally requires the
  captive's own player to hold map territory adjacent to the prison —
  never true for `PLAYER_GOOD` on a single-player level like map00011 —
  so it isn't a reliable thing to assert here.)

`creature_torture_ownership` targets `process_torture_function()`
(`creature_states_tortr.c`)'s own ownership-differs branch directly:
"Other torture functions are available only when torturing enemies" —
`if (room->owner == creatng->owner) return CrCkRet_Available;` before
`update_torture_points()` is ever reached. Rather than simulate a drop or
wait out the job-assignment dispatch (crossing into RNG-driven
break/convert/death rolls that only trigger hundreds of turns after
`accumulated_torture_points` passes a threshold), the test calls
`at_torture_room()` directly on two creatures already standing in the
same room — one owned by the room's owner, one not — then calls
`process_torture_function()` once on each and compares
`cctrl->tortured.accumulated_torture_points` before and after: unchanged
for the same-owner creature (proves the early return fired), increased
for the differently-owned one (proves `update_torture_points()` ran).
Deterministic, no dependence on the later RNG rolls, and passed first
try once written this way.

**Workshop has no equivalent ownership-differs behaviour, confirmed
rather than assumed.** `at_workshop_room()`'s validation
(`room_initially_valid_as_type_for_thing(room, get_room_role_for_job(Job_MANUFACTURE), creatng)`)
is the identical generic check Prison and Torture use, but
`config/fxdata/creature.cfg`'s `MANUFACTURE` job config
(`Assign = OWNED_CREATURES HUMAN_DROP COMPUTER_DROP AREA_WITHIN_ROOM WHOLE_AREA`)
has no `ENEMY_CREATURES`/`ENEMY_DIGGERS` flag, so
`enemies_may_work_in_room(RoK_WORKSHOP)` is always false — an enemy
creature simply cannot pass validation into a workshop at all, matching
the design intent (no "make my enemies build my traps" mechanic exists).
No dedicated test was written for workshop as a result; its coverage
rose only incidentally from the tests above.

## 8. CI integration

`.github/workflows/build-prototype.yml`, three relevant jobs (all
`ubuntu-24.04`, all reuse the same apt package list `build-prototype-linux`
needs, so `-DKFX_BUILD_TESTS=ON` doesn't fall back to slow
`FetchContent`-from-source builds of SDL3/openal/luajit/etc. in CI —
`kfx_common_opts` needs the full dependency chain regardless of which
single library is under test):

- **`check-layering`** — `python3 scripts/check_layering.py --strict`.
  Merge-blocking. No C/C++ toolchain needed.
- **`unit-tests`** — configures `-DKFX_BUILD_TESTS=ON`, builds all ten
  `*_utest` targets, runs `ctest --output-on-failure`. Merge-blocking.
- **`coverage`** — separate build tree (`out/coverage`, not `out` —
  coverage-instrumented objects behave differently under fast-iteration
  tooling than plain ones), `-DKFX_TEST_COVERAGE=ON` on top, runs
  `ctest`, then `cmake --build out/coverage --target coverage`, uploads
  `coverage-html/**` via `actions/upload-artifact@v4`. Informational
  only, does not gate merging.

`check_layering_symbols.py --strict` (the `nm`-based post-build audit
catching raw-`extern` forward-declarations `check_layering.py`'s
`#include`-only check misses — `architecture.md` §8.2, §8.1) is not
currently a separate CI job; it's run manually alongside every test
addition as a verification step (see `docs/refactor/testing/`'s stage
docs for the convention).

§7's ftest-driven coverage is **not** part of CI — it needs the real,
proprietary Dungeon Keeper data files (`KFX_FTEST_DATA_DIR`) this repo
can't distribute and CI can't provide, so it's a local-only workflow
(§7.4) for now.

## 9. Current test inventory

As of 2026-08-30, **892 tests** across all ten libraries (`ctest -N`
count; each `*_utest` binary's own Catch2 summary matches):

| Library | Tests | Notable coverage |
|---|--:|---|
| `kfx_sim` | 160 | Five natural clusters (map/thing/creature/room/player, `docs/refactor/testing/comprehensive/stage-08b-kfx-sim-clusters.md`) plus `dungeon_stats.c`/`magic_powers.c`/`power_specials.c` outside those clusters; `roomspace.c`'s `box_placement_mode`/`drag_placement_mode` (the latter driven by the shared `get_packet` stub's `control_flags` field, not just its call path); `creature_graphics.c`'s `keepersprite_*` dispatch functions (the `creature_table_add` accepted residual); four of 17 `creature_states_*.c` files now covered (`_tresr`, `_mood`'s anger/mood bookkeeping, `_gardn`'s hunger accessors, `_rsrch`'s pure `struct Dungeon`-only research-queue functions); `map_locations.c`'s `TbMapLocation` bit-packing accessors, `room_jobs.c`'s `worker_needed_in_dungeons_room_role` (`RoRoF_Research` branch), `map_ceiling.c`'s `ceiling_set_info`, `power_process.c`'s `players_disease_can_infect_target_players_creatures` |
| `kfx_config` | 186 | Old-style config tokenizer, campaign level membership/navigation (incl. `kfx_config`'s own first pattern-B test), the TOML fixture-file loader; `config_campaigns.c`'s struct-management functions; every `*Callbacks`-registration file's default no-op table exhaustively exercised (eight files, incl. `sim_feedback.c`'s 113-field table and `config.c`'s own 93-field `ConfigReloadCallbacks`, both missed by the original sweep); `config_slabsets.c`'s `load_columns_config_file`/`load_slabset_config_file`/`clear_slabsets` (the latter two needed a real `ConfigReloadCallbacks` fake with actual backing arrays); nine per-loader-schema TOML/`.cfg` fixture worked examples (`config_textures.c`, `config_powerhands.c`, `config_players.c`, `config_cubes.c`, `config_lenses.c`, `config_crtrstates.c`, `config_objects.c`, `config_slabsets.c`, plus `config_terrain.c`/`config_trapdoor.c`/`config_rules.c`/`config_settings.c` reached via direct `kfx_config_state` writes rather than a real fixture file — six the first coverage for `config.h`'s second, older `NamedField`/`parse_named_field_blocks` loader family, 9 files, previously zero coverage); `config_translation.c`'s TOML-backed alias-lookup table; `highscores.c`'s `add_high_score_entry`'s sorted-array insertion algorithm; `config_terrain.c`'s rich set of pure slab/room-kind predicates; `config_trapdoor.c`'s `create_manufacture_array_from_trapdoor_data`; `config_rules.c`'s sacrifice-recipe bookkeeping; `config_settings.c`'s `setup_default_settings`; `config_strings.c`'s pure buffer/lookup functions |
| `kfx_platform` | 248 | Everything the 175-test baseline had, plus a deep pass (2026-08-30) explicitly building fixtures/exposing statics rather than stopping at the first `static`/no-getter wall: `renderer/RendererManager.cpp`'s no-active-renderer guard surface (`RendererInit`'s unknown-type failure, palette/framebuffer/screenshot guards, the full draw-colour/draw-flags accessor set, `RendererDrawSlabBackground`'s `RendererDrawCallbacks` pattern-B fake); `bflib_cpu.c`'s pure bit-field decoders (`cpu_get_type`/`_family`/`_model`/`_stepping`, hand-verified against real Kaby-Lake- and family-15-shaped signatures) plus a light `cpu_detect()` smoke test; `bflib_netsession.c`'s `net_copy_name_string`; `moonphase.c`'s `calculate_moon_phase` (the process-start-zero "new moon" default, plus a real-astronomy-library smoke test); `bflib_sound.c` — the 3D positional-audio bookkeeping layer turned out almost entirely pure (distance/volume/pan/pitch math, emitter/sample array bookkeeping), sharply narrower than the file-level "real OpenAL device" verdict the original pass gave it — un-static'd `MaxNoSounds`/`SampleList` (extern in `bflib_sound.h`, mirroring `kfx_config`'s `campaign` precedent) so tests can construct "sample already playing" scenarios via direct field writes instead of the real `start_emitter_playing()`/`play_sample()`, landing ~30 functions while still declining every branch that reaches `stop_sample`/`play_sample`/`SetSample*`/`GetCurrentSoundMasterVolume` (`bflib_sndlib.cpp`'s real OpenAL wrapper, the actual boundary); `renderer/software/bflib_vidraw.c`'s `LbSpriteSetScaling*Array`/`LbSpriteClearScaling*Array` (pure fixed-point scan-conversion math into a caller-owned array, no screen buffer — first coverage this 2,117-line file has ever had) and `bflib_mspointer.cpp`'s thin `LbCursorSpriteSetScaling*` wrappers around them. Twelve functions across `bflib_sound.c`/`bflib_vidraw.c`/`bflib_mspointer.cpp` had real external linkage but no header declaration at all; all added. Per-library: 6.1%→9.5% line coverage, 12.7%→19.5% function coverage. |
| `kfx_pathfinding` | 87 | Every small self-contained `ariadne_*.c` file's accessors/allocators/tag-bookkeeping/region-connectivity (as before), plus a real, reusable, grid-backed `PathfindingWorldCallbacks` fake (`pathfinding_fake_world.h`) that finally unlocked the "big three": `ariadne_wallhug.c` (2,178 lines, zero coverage before this round) now covered via its 5 real-external-linkage entry points (`dig_to_position`, `get_hug_side_options`, `get_next_position_and_angle_required_to_tunnel_creature_to`, `initialise_wallhugging_path_from_to`, `slab_wall_hug_route`), driven through several scenarios (open path, walled-off direct route, multiple navstate machine transitions) to transitively exercise every one of the file's `static` helpers (`hug_round`, `creature_cannot_move_directly_to_with_collide`, `get_starting_angle_and_side_of_hug*`, etc.); `ariadne_update.c`'s `init_navigation()`/`update_navigation_triangulation()` now drive the real Delaunay triangulation algorithm end-to-end over a small uniform open-floor grid (68.4% line coverage, up from a handful of pure-bookkeeping functions), transitively exercising real (non-fixture) triangulation machinery in `ariadne_navitree.c`/`ariadne_tringls.c`/`ariadne_points.c`/`ariadne_edge.c`/`ariadne_findcache.c` for the first time; `ariadne.c`'s route-lifecycle entry points (`ariadne_initialise_creature_route_f`, `creature_follow_route_to_using_gates`, `ariadne_count_waypoints_on_creature_route_to_target_f`, `ariadne_invalidate_creature_route`, `angle_to_quadrant`) driven across a genuinely-triangulated map, taking it from 2.5% to 25.3% line coverage. Per-library: 10.3%→40.4% line coverage, 32.2%→77.2% function coverage. A pre-existing test (`ariadne_update_test.cpp`'s `init_navigation` test) turned out to have an order-dependent assertion — comparing `count_Triangles` against a "before" snapshot that isn't a reliable baseline once another test triangulates the identical map shape first — caught by the routine 3x `--order rand` stability check and fixed to assert an absolute structural bound instead. `ariadne.c`'s remaining ~75% (gate-navigator internals not yet exercised by `creature_follow_route_to_using_gates`, `pointed_at8`, `nearest_search_f`, `navigation_points_connected`, `path_init8_wide_f`'s standalone entry) and most of `ariadne_wallhug.c`'s ~500-line navstate state machine remain the next targets |
| `kfx_render` | 40 | `light_data.c`'s shading-signal drain (its accessors and allocators moved with the light registry to `kfx_sim` in refactor pass 2, S11), `engine_camera.c` zoom math, `engine_redraw.c`'s `update_mouse_light()` (the `kfx_render -> kfx_net` side of the `get_packet_direct` accepted residual); the `LensEffect` C++ class hierarchy — base-class accessors (`GetType`/`GetName`/`SetEnabled`/`IsEnabled`) plus `Mist`/`Overlay`/`Displacement`/`Flyeye`/`LuaLensEffect`'s constructor/`Draw`/`Cleanup` lifecycle on a never-`Setup()` instance, direct instantiation with no fixture as `stage-08-comprehensive-library-passes.md` had flagged |
| `kfx_net` | 41 | `net_checksums.c` (`get_thing_checksum`, and now `checksums_different`'s host-vs-client comparison, including the `PlaF_CompCtrl`/inactive-network-slot skip branches and the "missing checksum packet" case), `packets.c` field accessors, `net_resync.cpp`'s `boing` round-trip and `animate_resync_progress_bar`, `packets_misc.c`'s full player-packet accessor set (`get_packet`/`get_packet_direct`/`set_players_packet_action`/`get_players_packet_action`/`set_players_packet_control`/`unset_players_packet_control`/`set_players_packet_position`), `packets_input.c`'s `is_mouse_on_map`/`remember_cursor_subtile`, `net_input_lag.c`'s network-not-active short-circuit guards |
| `kfx_game` | 25 | `game_legacy.c`, `lvl_script_conditions.c` (now including `pop_condition`/`get_`/`set_script_current_condition`), `game_merge.c`'s moon-phase visibility; `game_heap.c`'s `setup_heap_manager` failure path and `he_alloc`; `game_saves.c`'s inter-level creature-transfer bookkeeping (`add_transfered_creature`/`get_transferred_creature`/`clear_transfered_creatures`); `sounds.c`'s `reset_ambient_sound_thing_idx`/`ambient_sound_stop`'s invalid-thing early return |
| `kfx_frontend` | 28 | `gui_topmsg.c` (now including `erstat_check`'s turn-gate/cursor-advance logic and `is_onscreen_msg_visible`/`show_onscreen_msg`), `gui_vscroll.c` (built the `save_game_catalogue[]` fixture); `kfx_frontend_state.c` (full save/load/reset/size raw-blob lifecycle, incl. a real file round trip); `gui_frontmenu.c`'s pure `active_menus[]` scans (`get_active_menu`/`first_monopoly_menu`/`menu_id_to_number`/`point_is_over_gui_menu`) — still the thinnest library by design (stage-04g found most files GUI-widget/save-catalogue coupled) |
| `kfx_script` | 23 | Bare-`lua_State` helpers, the real `open_lua_script()` init chain, `classes/Pos3d.lua`; `lua_utils.c`'s `try_get_c_method`/`try_get_from_methods` (the per-class `__index` metamethod helpers, real Lua stack manipulation); `api.c`'s event-subscription bookkeeping (`api_subscribe_event`/`api_unsubscribe_event`/`api_is_subscribed_to_event`/`api_clear_all_subscriptions`) and `get_max_flags` |
| `kfx_apploop` | 8 | `display_should_be_updated_this_turn` (pattern A + a fake `LbTimerClock` + pattern B combined); `find_frame_rate`/`packet_load_find_frame_rate`'s accumulate-then-fire threshold crossing (1000ms/5000ms), each a single combined `TEST_CASE` after a two-`TEST_CASE` version was found order-dependent within one raw-binary process (both functions' local statics have no reset accessor); `keeper_screen_swap` (safe here since `RendererPresentFrame()` no-ops on the default-null `s_active_renderer`); `keeper_wait_for_next_turn`'s branch that returns without touching the wall clock at all (`frame_skip < 0` and no wait-sleep mode) |

Two libraries' rows include coverage deliberately targeted at
`check_layering.py --strict`'s two currently-*unaccepted* violations
(`ariadne_regions.c`, `power_specials.c`) — both root-caused to a
wrong-header choice with a trivial, zero-behavior-change fix, written up
in
[`docs/refactor/todo/two-remaining-layering-violations.md`](../refactor/todo/two-remaining-layering-violations.md).

Per-library detail, what's tested and what's deliberately deferred (and
why), is tracked in
[`docs/refactor/testing/comprehensive/stage-08-comprehensive-library-passes.md`](../refactor/testing/comprehensive/stage-08-comprehensive-library-passes.md)
and
[`stage-08b-kfx-sim-clusters.md`](../refactor/testing/comprehensive/stage-08b-kfx-sim-clusters.md)
— kept current as tests are added, unlike the numbered `stage-0N-*.md`
narrative documents, which are historical snapshots of the day they were
written.

## 10. Layering-residual test coverage

Per an explicit request to focus coverage around
`check_layering.py`/`check_layering_symbols.py`'s violations — both to
de-risk an eventual fix where one exists, and to protect the by-design
residuals' surrounding code where it doesn't. Checked precisely (grepping
each test file for the actual symbol/function name, not assumed from
which production file a test happens to also cover) rather than claimed
in the abstract — an earlier draft of this section overclaimed full
coverage before that check.

**`#include`-level (`check_layering.py`)**:

| Violation | Status | Test coverage |
|---|---|---|
| `ariadne_regions.c` → `player_data.h` | **fixed** (2026-08-29, `07784c1f7`) — swapped for `kfx_config_state.h`'s own `PLAYERS_COUNT` copy | `ariadne_regions_test.cpp` (10 tests) |
| `power_specials.c` → `api.h` | **fixed** (2026-08-29, `07784c1f7`) — redundant include deleted, `script_hooks.h` was already included directly | `power_specials_test.cpp` (6 tests) |
| `console_cmd.c` → `game_session_loop.h` | was accepted, found **fully dead**, then **removed** (2026-08-29, `07784c1f7`) — its `ACCEPTED_VIOLATIONS` entry is gone too | none needed — no live code exercised it |
| `net_resync.cpp` → `kfx_frontend_state.h`/`kfx_game_state.h`/`game_legacy.h` | **fixed** (`docs/refactor/todo/remove-remaining-layering-violations.md`) — `net_resync.cpp` now goes through callback-based opaque blobs (`resync_export_game_state`/`resync_import_game_state`/etc., wired in `main.cpp::setup_game()`) instead of `#include`-ing those headers directly | `net_resync_test.cpp` (4 tests) — covers other functions *in the same file*, not the blob-resync functions themselves |
| `bflib_enet.cpp` → `net_main.h` | **fixed** (`docs/refactor/todo/remove-remaining-layering-violations.md`) — `struct NetSP`/`NetUserId`/etc. moved down into `src/kfx_platform/include/bflib_netsp.h`, so `bflib_enet.cpp` no longer needs `kfx_net`'s `net_main.h` at all | `src/ftests/tests/ftest_net_enet_loopback_*` — a real two-process ENet loopback functional test, not a Catch2 unit test (see `docs/refactor/todo/ftest-fake-multiplayer.md`) |

`check_layering.py --strict` now reports **zero un-accepted violations**,
and `ACCEPTED_VIOLATIONS` itself is empty — every `#include`-level
residual this table ever tracked has been fixed, not just accepted. The
first three fixes above were root-caused and test-covered in one session
(recorded below and in
[`docs/refactor/todo/two-remaining-layering-violations.md`](../refactor/todo/two-remaining-layering-violations.md))
but deliberately left unapplied pending review; a separate concurrent
session (`07784c1f7`, same day) picked them up, applied all three edits,
and reverified against a clean two-variant build plus both layering
checkers. The last two (`net_resync.cpp`, `bflib_enet.cpp`) were fixed in
a later session, see
[`remove-remaining-layering-violations.md`](../refactor/todo/remove-remaining-layering-violations.md).

**Symbol-level (`check_layering_symbols.py`)** — 9 distinct symbol names
across 15 accepted call sites:

| Symbol | Call sites | Test coverage |
|---|---|---|
| `get_packet_direct` | `engine_redraw.c`, `roomspace.c`, `roomspace_prediction.c` | `engine_redraw_test.cpp`, `roomspace_test.cpp`, plus the accessor itself directly in `packets_misc_test.cpp` — `roomspace_prediction.c`'s own call site is still untested (its `get_packet`/`set_packet_action` calls live inside one large orchestration function with `static`, module-private state — declined, see Known gaps) |
| `creature_table_add` | `creature_graphics.c` | `creature_graphics_test.cpp` |
| `kfxmain` | **fixed** (`docs/refactor/todo/remove-kfxmain-symbol-residual.md`) — `PlatformLinux.cpp`/`PlatformWindows.cpp` no longer own the process entry point; moved to `app_entry`'s `src/native_entry.cpp` | n/a — no longer a cross-layer call at all |
| `get_packet` | `roomspace.c`, `roomspace_prediction.c` | the accessor itself now covered directly (`packets_misc_test.cpp`); neither production call site is |
| `set_packet_action` | `roomspace.c`, `roomspace_prediction.c` | the accessor itself covered (`packets_test.cpp`, pattern A on a bare `struct Packet`); neither production call site is |
| `set_players_packet_action` | `creature_instances.c`, `roomspace.c`, `thing_creature.c` | the accessor itself now covered directly (`packets_misc_test.cpp`, along with its siblings `get_players_packet_action`/`set_players_packet_control`/`unset_players_packet_control`); none of the three production call sites are |
| `game` / `kfx_game_state` / `kfx_frontend_state` | `net_resync.cpp` (the raw-blob resync itself) | none — see the `#include`-level row above |

## 11. Known gaps (as of this writing)

- 13 of 17 `creature_states_*.c` files (`kfx_sim`) still have no test.
  Four now do: `_tresr` (the original, smallest), `_mood` (the anger/mood
  bookkeeping cluster — `creature_can_get_angry`, `anger_calculate_
  creature_is_angry`, `anger_free_for_anger_increase/decrease`, `anger_
  is_creature_angry/livid`, `anger_get_creature_anger_type`), `_gardn`
  (`creature_able_to_eat`, `hunger_is_creature_hungry`), and `_rsrch`
  (`get_next_research_item`, `has_new_rooms_to_research` — both take a
  bare `struct Dungeon *`, no `Thing`/`CreatureControl` needed at all).
  In every case, only the pure/near-pure accessor functions were
  covered — the actual `at_*_room()`/`*ing()` state-machine entry points
  in all 17 files (including these four) still need a fuller
  `Thing`+`CreatureControl`+room/job context, not attempted anywhere yet.
- `kfx_pathfinding`'s "big three" (`ariadne.c`, `ariadne_update.c`,
  `ariadne_wallhug.c`, 3,313/1,773/2,178 lines): the `PathfindingWorldCallbacks`
  fake (51-entry interface, `architecture.md` §2.2a) this needed got built
  (2026-08-30, `pathfinding_fake_world.h`, a reusable grid-backed fake with
  real controllable backing storage for map/slab/thing state), taking the
  library from 10.3%/32.2% to 40.4%/77.2% line/function coverage — see §9's
  `kfx_pathfinding` row for what's covered now. Still open: most of
  `ariadne.c`'s gate-navigator branch (`gate_navigator_init8`/
  `route_through_gates`/`gate_route_to_coords` and friends — only reached
  indirectly via `creature_follow_route_to_using_gates`'s internal dispatch
  so far, not directly exercised across varied gate topologies),
  `pointed_at8`/`nearest_search_f`/`navigation_points_connected`/standalone
  `path_init8_wide_f` calls, and most of `ariadne_wallhug.c`'s ~500-line
  `get_next_position_and_angle_required_to_tunnel_creature_to` navstate
  state machine (a representative sample of states/transitions is covered,
  not exhaustive branch coverage).
- `kfx_net`'s `packets_input.c`/`packets_cheats.c` — mostly process_*-shaped
  dispatch, not the field-packing shape `packets.c` turned out to be.
  Two of its non-dispatch helpers are now covered directly
  (`is_mouse_on_map`, `remember_cursor_subtile` — `packets_misc_test.cpp`,
  both needed a "add the missing header declaration" fix, added to
  `packets.h`); the `process_dungeon_control_packet_*`/`packets_process_cheats`
  dispatch functions themselves remain untested.
- `roomspace_prediction.c`'s `get_packet`/`set_packet_action` call sites
  (inside `update_local_dig_tag_prediction`) were investigated and
  declined: unlike every other "missing declaration" fix in this plan
  (always targeting already-externally-linked symbols), the function's
  own state (`local_dig_tag_prediction`, `local_dig_roomspace_prediction`,
  `local_dig_render_roomspace_active`) is `static` — module-private —
  so testing it would mean exposing genuinely private internals, a
  different and more invasive kind of change than the rest of this plan.
- The two `check_layering.py --strict` violations are now **fixed**
  (2026-08-29, `07784c1f7`, a separate concurrent session applying the
  fix this plan had root-caused and verified but deliberately left
  unapplied) — see
  [`two-remaining-layering-violations.md`](../refactor/todo/two-remaining-layering-violations.md)
  for the full before/after record.
- **`kfx_platform`** (the lowest-ranked library) was pushed hard in a
  dedicated pass (2026-08-29): 41 → 175 tests, 2.3% → 6.1% line coverage,
  5.2% → 12.7% function coverage (per-library, `kfx_platform/src/index.html`).
  Every file with real pure/near-pure logic reachable without a new fake-
  provider mechanism now has direct coverage (`kfx_memory.c`,
  `bflib_coroutine.c`, and `spritesheet.cpp` all at 100%/85%+). What's
  left in this library is uniformly gated behind a *live* external
  dependency, not just "needs a fixture": `bflib_render_trig.c`/
  `bflib_vidraw*.c`/`bflib_render_gpoly.c`/`bflib_sprfnt.c` (real
  rendering-surface + clip-window state machine — `setup_vecs()` does
  accept a plain malloc'd pixel buffer with no SDL involved, so this
  *could* be pattern-A tested, but understanding the clip-window globals
  well enough to do it right is a dedicated sub-effort of its own, same
  category as the pathfinding "big three" above); `bflib_sndlib.cpp`/
  `bflib_sound.c`/`sound_manager.cpp` (real OpenAL device);
  `bflib_enet.cpp`/`bflib_netsp.cpp` (real sockets); `bflib_inputctrl.cpp`/
  `bflib_mouse.cpp`/`bflib_input_joyst.cpp` (real SDL event pump);
  `bflib_fmvids.cpp` (real Smacker video decode); `bflib_crash.c` (signal
  handlers — actively unsafe to exercise in a unit test); `bflib_cpu.c`
  (real CPUID); `moonphase.c` (wraps the real `astronomy` library through
  a `static` variable with no setter — would need a new accessor, not
  just a test). This is the point flagged as "no further functions/routes
  reachable without significant harness changes" for this library.
- Two real production quirks were found while writing these tests
  (documented here, not fixed, same restraint as the layering-violation
  finds above):
  - `bflib_fileio.c`'s `LbFileOpen(..., Lb_FILE_MODE_NEW)` is broken for
    any path starting with `/`: `create_directory_for_file()` finds the
    *leading* slash as its first `strchr` match, computes an empty
    directory-to-create string, and `mkdir("")` fails with `ENOENT` —
    aborting the whole open before `fopen()` is ever reached (confirmed
    directly with a standalone repro, not just read). Dormant in
    practice because every real `Lb_FILE_MODE_NEW` caller in this
    codebase uses relative paths.
  - `bflib_filelst.c`'s `LbDataLoadSetModifyFilenameFunction()` reads
    like a "set and return the previous hook" setter but doesn't track a
    previous value at all — it just echoes `newfunc` straight back
    (confirmed by a test that assumed the old-value contract and failed).
- **`kfx_platform` revisited (2026-08-30, deep pass with fixtures)**: the
  bullet above's "no further functions/routes reachable without
  significant harness changes" verdict turned out to be too pessimistic
  for four of its own listed items, once actually re-read function body
  by function body rather than judged from the file's overall name/
  purpose: 175 → 248 tests, 6.1% → 9.5% line coverage, 12.7% → 19.5%
  function coverage. `bflib_sound.c` was filed under "real OpenAL
  device" wholesale, but turned out to be almost entirely pure 3D-audio
  bookkeeping (distance/volume/pan/pitch math, emitter/sample array
  management) with the real OpenAL calls concentrated in a handful of
  specific branches (`stop_sample`/`play_sample`/`SetSample*`/
  `GetCurrentSoundMasterVolume`, all still declined) — reachable at all
  only because `MaxNoSounds`/`SampleList` were un-static'd (extern in
  `bflib_sound.h`) so tests could construct "sample already playing"
  scenarios via direct field writes instead of the real playback path,
  the `campaign`-in-`kfx_config` pattern applied here for the first time
  in this library. `bflib_vidraw.c`'s `LbSpriteSetScaling*Array`/
  `LbSpriteClearScaling*Array` (pure fixed-point scan-conversion math
  into a caller-owned array, no screen buffer) turned out separable from
  the rest of that 2,117-line file's real-framebuffer code, its first
  coverage ever; `bflib_mspointer.cpp`'s `LbCursorSpriteSetScaling*`
  wrappers followed for free. `renderer/RendererManager.cpp` — not even
  mentioned in the original pass, added to this library after it, but
  the same "no active renderer" guard shape as `RendererPresentFrame`
  (already exercised indirectly via `kfx_apploop`'s `keeper_screen_swap`
  test) — turned out to guard almost its *entire* public surface, safely
  callable end to end with `s_active_renderer` at its `nullptr` default;
  its `RendererDrawCallbacks` seam (default a no-op stub) got its first
  pattern-B fake. `bflib_cpu.c`'s bit-field decoders, `bflib_netsession.c`,
  and `moonphase.c`'s process-start-zero default were also picked up —
  smaller, but genuinely zero-dependency functions the original pass
  hadn't reached. What's *actually* left after this second look is
  narrower and more confidently real: `bflib_render_trig.c` (4,631
  lines)/`bflib_render_gpoly.c`/`bflib_vidraw_spr_*.c`/`bflib_sprfnt.c`'s
  remaining real-framebuffer drawing code, `bflib_vidsurface.c` (calls
  `SDL_CreateSurface`/`SDL_BlitSurface` directly), `platform/*.cpp` (real
  OS window creation), `renderer/RendererSoftware.cpp`/
  `ITextRenderer.cpp`/`IUIRenderer.cpp` (the real backend implementation
  `RendererManager.cpp` dispatches to), `bflib_inputctrl.cpp`/
  `bflib_mouse.cpp`/`bflib_input_joyst.cpp`/`bflib_mshandler.cpp` (real
  SDL event pump), `bflib_enet.cpp`/`bflib_netsp.cpp` (real sockets),
  `bflib_sndlib.cpp`/`sound_manager.cpp` (the real OpenAL device
  `bflib_sound.c` now cleanly stops short of), `bflib_fmvids.cpp` (real
  Smacker decode), `bflib_crash.c` (signal handlers, unsafe to exercise).
  This time the "no further functions/routes reachable without
  significant harness changes" verdict is against actually-verified
  real dependencies (`SDL_CreateSurface`, real window/event-pump/socket/
  OpenAL calls confirmed by reading each file), not inferred from a
  file's name or its file-level doc comment.
- **`kfx_config`** (next up the ladder after `kfx_platform`, same "focus
  on the lowest-ranked library" request) went 45 → 104 tests, 3.3% →
  8.7% line coverage, 5.0% → 32.0% function coverage (per-library,
  `kfx_config/src/index.html`). The single biggest lever turned out to
  be this library's seven still-untested `*Callbacks`-registration files
  (`net_callbacks.c` 52 stub functions, `game_callbacks.c` 46,
  `pathfinding_world.c` 54, `script_hooks.c` 25, `render_overlay.c` 28,
  `dungeon_availability.c` 10, `sprite_lookup.c` 7 — ~220 tiny functions
  total): every one is `static`, only reachable through its default
  table's function-pointer fields, and every noop body ignores its
  pointer arguments, so one exhaustive "call every field, assert the
  documented no-op default" test per file drove several of them straight
  to 100%/100% line+function coverage at once. `config_campaigns.c`
  (0/804 lines beforehand, despite this table's earlier text implying
  campaign coverage existed — that coverage was actually `is_bonus_level`
  and friends in `config.c`, a different file) got its struct-management
  functions covered (level/campaign array append/grow/clear/swap/sort/
  lookup); `config_textures.c` got a worked example of the "per-loader
  TOML-fixture" pattern the general gap below describes.
  What remains in `kfx_config` is overwhelmingly the large per-`config_*.c`
  TOML loaders (`config_crtrmodel.c` 1,635 lines, `config_creature.c`
  1,320, `config_magic.c` 563, `config_sounds.c` 477, `config_trapdoor.c`
  271, `config_rules.c` 266, `config_settings.c` 260, `config_terrain.c`
  227, `config_effects.c` 167, `config_compp.c` 130, `config_slabsets.c`
  96, `highscores.c` 94, `config_objects.c` 86, `config_crtrstates.c` 66,
  `config_spritecolors.c` 64, `config_lenses.c` 55, `config_cubes.c` 40)
  — each needs its own TOML fixture file and real schema understanding,
  a dedicated per-loader sub-effort each rather than a shared mechanism,
  same category as the pathfinding "big three"/kfx_platform's live-
  external-dependency files above. Not a hard "significant harness
  change" blocker like those, though — `value_util.c`'s `load_toml_file()`
  and the fixture-file convention (`kfx_config_test_paths.h.in`) already
  make each one tractable; it's a volume problem, not a capability gap.
- **`kfx_config` revisited (2026-08-30, deep pass, five sub-rounds)**:
  104 → 186 tests, 8.8% → 19.8% line coverage, 32.2% → 63.5% function
  coverage. First sub-round (104→109): `sim_feedback.c` turned out to
  be an eighth `*Callbacks`-registration file in the exact same shape
  as the seven the original pass already swept (113 tiny `static` noop
  functions) — simply missed the first time, landed with one exhaustive
  test; `config_powerhands.c` got a second worked example of the
  "per-loader TOML-fixture" pattern (`config_textures.c` was the
  first). Second sub-round (109→138), after an explicit redirect to
  keep pushing the `config_*.c` volume rather than stop at one or two
  examples: found a **ninth** missed `*Callbacks` file,
  `config.c`'s own `ConfigReloadCallbacks` — 93 fields, the largest in
  this library, declared and defaulted right in `config.c` rather than
  its own dedicated file the way the other eight are, which is likely
  why both sweeps missed it; landed in one test case, 72 assertions.
  Discovered and confirmed via an actual test failure (not read-only
  inspection) that `config_cubes.c`'s `Name` field is spelled with the
  wrong case (`"Name"`, not `"NAME"`) for `config.c`'s
  `set_defaults()`'s case-sensitive auto-registration check, so
  `cube_desc[]` is silently never populated and `cube_code_name()`
  always returns `"INVALID"` — currently dormant, since nothing outside
  `config_cubes.c` calls that function. Landed three worked examples of
  a **second, previously-uncovered loader family**:
  `NamedField`/`parse_named_field_blocks` (`config.h`, 9 files —
  `config_cubes.c`, `config_lenses.c`, `config_crtrstates.c`,
  `config_objects.c`, `config_compp.c`, `config_terrain.c`, `config.c`
  itself, `config_trapdoor.c`, `config_magic.c` — none had any coverage
  before), each fixture modelled on the corresponding real file under
  `config/fxdata/`. `config_translation.c`'s TOML-backed alias-lookup
  table (exact-language-match, alias-fallback, and out-of-range-id
  branches) and `highscores.c`'s `add_high_score_entry()` (a genuinely
  nontrivial sorted-array insertion/duplicate-overwrite algorithm on
  the module-level `campaign` global, hand-traced and verified against
  two clean scenarios: insert into an empty table, and reject when full
  with no duplicate levels) round out the second sub-round. Third
  sub-round (138→146): `config_objects.c` — a fourth `NamedField`
  worked example, plus `get_required_room_capacity_for_object()`'s
  gold/food/crate/power room-role branches (the `RoRoF_LairStorage`/
  `RoRoF_DeadStorage` cases, which additionally need
  `config_creature.c`'s `creature_stats_get()`, weren't attempted) and
  `crate_thing_to_workshop_item_class`/`_model` exercised directly
  against `config_reload_callbacks`'s already-verified default no-op
  stubs, no new fake needed. `config_effects.c` was surveyed and found
  to mix *both* loader families in one file (raw TOML dict traversal
  for the main `effect%d`/`effectelement%d` blocks, `NamedField` for a
  nested `effectGenerator` sub-config) — a third shape, not attempted
  this round. Fourth sub-round (146→153): `config_slabsets.c` split
  cleanly in two. `load_columns_config_file()` writes straight into
  `kfx_config_state`, no fake needed. `load_slabset_config_file()`/
  `clear_slabsets()` are different: they reach into
  `config_reload_callbacks`'s `get_slabset_array`/`get_slabobjs_array`/
  `get_slabobjs_idx_array`/`get_slabset_num_ptr`/`get_slabobjs_num_ptr`,
  all of which default to `NULL` — the first `*Callbacks` reach in this
  library where the safe default itself isn't safe to call, so a real
  fake with real backing arrays was required (same shape as
  `ariadne_test.cpp`'s `PathfindingWorldCallbacks` fake, but for
  `ConfigReloadCallbacks`). While building the fixture, a dotted TOML
  table header (`[slab0.S]`) with no explicit `[slab0]` parent failed
  to parse under this codebase's bundled TOML implementation — caught
  by an actual load failure, corrected by matching the real
  `config/fxdata/slabset.toml`'s own structure (which does declare the
  parent explicitly). Fifth sub-round (153→186), per an explicit "keep
  going until significant line coverage" request: a shift in technique
  for four files (`config_terrain.c`, `config_trapdoor.c`,
  `config_rules.c`, `config_settings.c`) — instead of a real fixture
  file through each loader's `load_func`, most of the value in these
  files turned out to be *pure predicate/accessor functions* reachable
  by writing `kfx_config_state` fields directly (pattern A), sidestepping
  the loader's own fixture-syntax cost entirely. `config_terrain.c`
  alone yielded a dozen slab/room-kind predicates (some pure `SlabKind`-
  enum comparisons needing no config data at all).
  `create_manufacture_array_from_trapdoor_data()` (`config_trapdoor.c`)
  is a genuinely nontrivial aggregation algorithm, fully covered.
  `config_rules.c` turned up a real bug, confirmed by an actual failing
  assertion, not assumed: `sac_compare_fn` (the `qsort` comparator
  `add_sacrifice_victim` uses to keep a recipe's victim list sorted)
  returns a plain `a < b` boolean (0 or 1), never negative — violating
  `qsort`'s three-way contract, so the resulting sort order is
  implementation-defined, not the ascending order the code's own intent
  suggests. The test was rewritten to check victim presence/count
  rather than bake in a specific (UB-dependent) final order.
  `config_settings.c`'s `load_settings()`/`save_settings()` were
  investigated and declined: unlike every other loader in this library,
  they take no `fname` parameter — they hardcode
  `prepare_file_path(FGrp_Save, "settings.toml")`, a real path in the
  actual save directory, with no way to redirect them at a fixture
  without either a real file-I/O side effect or changing the function's
  signature; `setup_default_settings()`/`get_max_i_can_see_from_settings()`
  (pure) were covered instead. ~9 `config_*.c` loaders remain untouched
  (`config_crtrmodel.c` 3,039 lines, `config_creature.c` 2,627,
  `config_magic.c` 1,514, `config_sounds.c` 1,297, `config_keeperfx.c`
  1,244, `config_compp.c` 465, `config_effects.c` 391,
  `config_mods.c`/`highscores.c`'s file-I/O paths) — still a volume
  problem, now with two proven, worked loader patterns plus the
  direct-state-write shortcut for predicate-heavy files.
- **`kfx_pathfinding`** (third rung up the ladder, same approach again)
  went 41 → 58 tests, 6.9% → 9.7% line coverage, 21.3% → 28.7% function
  coverage (per-library, `kfx_pathfinding/src/index.html`). Unlike
  `kfx_platform`/`kfx_config`, this library was already reasonably well
  covered on the small self-contained `ariadne_*.c` files going in — this
  pass mostly closed remaining gaps *within* those same files rather than
  landing a new one wholesale: `ariadne_findcache.c`'s `triangulation_init_cache`/
  `triangle_brute_find8_near` (the latter had no header declaration,
  added); `ariadne_tringls.c`'s `get_triangle_point`/`triangle_tip_equals`/
  `edgelen_set`/`triangle_find_first_used` (the last one reusing
  `triangulation_init_triangles` as a deterministic fixture, the same
  dual production-API/test-setup role `ariadne_edge_test.cpp`'s
  `edge_points_clean()` plays); `ariadne_navitree.c`'s `navitree_add`/
  `delaunay_init` (`ix_delaunay` had no header declaration, added — and
  a prior test-file comment claiming `delaunay_init` needed a real
  triangulated fixture like its Delaunay-walk siblings turned out to be
  wrong on inspection, corrected); `ariadne_naviheap.c`'s actual min-heap
  priority ordering (`naviheap_top`/`naviheap_remove` popping in
  ascending `tree_val` order — previously only capacity/empty-state was
  tested, deliberately deferred pending `ariadne_navitree.c`'s own
  fixture, which now exists); `ariadne_points.c`'s `point_dispose`;
  `ariadne_regions.c`'s `region_set`/`region_unset`/`region_unlock`
  (their own `Regions[]` bookkeeping stays unobservable — no accessor —
  but `region_set`'s write to the *triangle's own* region id, via the
  already-covered `set_triangle_region_id`, is). `triangle_find8`/
  `point_find` (the actual point-location triangulation walk, via
  `triangle_divide_areas_s8differ`'s barycentric-sign math) were assessed
  and declined — reproducing `ariadne_regions_test.cpp`'s "hand-verified
  minimal fixture" discipline for point-in-triangle geometry instead of
  region-BFS connectivity is a bigger, separate undertaking. The "big
  three" (`ariadne.c` 1,773 lines/`ariadne_update.c` 936/`ariadne_wallhug.c`
  1,375) remain the one real "significant harness change" item in this
  library: a `PathfindingWorldCallbacks` fake (51 entries,
  `architecture.md` §2.2a) driving real pathfinding logic, not just the
  interface's own default-table registration (`kfx_config`'s
  `pathfinding_world_test.cpp` already covers that side).
- **`kfx_pathfinding` revisited (2026-08-30, deep pass, two sub-rounds)**:
  58 → 62 tests, 9.7% → 10.3% line coverage, 28.7% → 32.2% function
  coverage. First sub-round (58→60): `ariadne.c` (the largest of the
  "big three", 3,313 lines) got its first-ever coverage:
  `thing_nav_block_sizexy`/`thing_nav_sizexy` each call exactly one
  callback (`thing_get_clipbox_size`) and index a private lookup table
  with clamping, narrow enough to reach with a minimal single-field
  `PathfindingWorldCallbacks` fake (copy the default table, override
  just that one field) rather than the full 51-entry functional map
  fake the rest of the file needs. A third candidate,
  `tag_open_closed_init()`, looked identical from a plain text search
  but turned out to live inside a
  `/* TODO PATHFINDING Enable when needed ... */` comment block — dead,
  disabled code, never compiled — caught by an actual link failure
  ("undefined reference"), not assumed from reading. Second sub-round
  (60→62), after the user pointed out more was reachable in both this
  library and `kfx_config`: `ariadne_update.c` (second-largest of the
  "big three", 1,773 lines) got its first-ever coverage too — five
  small functions
  (`ariadne_set_navigation_map_size`/`ariadne_reset_navigation_map`/
  the `map_changed_for_navigation` flag trio) turned out to be pure
  `kfx_pathfinding_state` bookkeeping with no map/callback dependency
  at all, unlike everything else in the file (which drives the real
  triangulation algorithm through `pathfinding_world`'s map callbacks).
  `ariadne_wallhug.c`'s `set_hugging_pos_using_blocked_flags` was
  assessed and declined: narrow in callback count (two), but its
  coordinate math writes through `struct Coord3d`'s packed
  `val`/`stl.pos` union in a way that depends on this platform's exact
  struct layout/padding — hand-verifying the expected output would mean
  either trusting an unconfirmed layout assumption or reverse-deriving
  the "expected" value from the same binary being tested, neither of
  which is a real independent check. The rest of `ariadne.c`/
  `ariadne_update.c`/all of `ariadne_wallhug.c` still needs the full
  functional fake, a much larger undertaking these narrow wins don't
  shortcut.
- **`kfx_pathfinding` revisited again (2026-08-30, the full functional
  fake)**: 62 → 87 tests, **10.3% → 40.4% line coverage, 32.2% → 77.2%
  function coverage**. Built the full-scope fake the two prior
  sub-rounds explicitly deferred: `pathfinding_fake_world.h`, a
  reusable grid-backed `PathfindingWorldCallbacks` fake with real
  controllable backing storage. Technique: `struct Map`/`SlabMap`/
  `Navigation`/`Ariadne`/`Thing` are all opaque to ariadne — only ever
  round-tripped as pointers, never dereferenced by production code — so
  the fake is free to invent its own backing storage (a flat per-subtile
  `Cell` grid: map flags, slab kind, owner, floor height, "unsafe", a
  single `walkable` bool driving `hug_can_move_on`/`is_valid_hug_subtile`/
  `thing_in_wall_at` consistently) addressed by encoding a grid index
  directly into the "pointer" value, plus a `FakeThing` struct reached
  via `reinterpret_cast` through `struct Thing*`. `GridWorldFixture`
  resets to an all-open walkable grid and installs the fake;
  `TriangulatedWorldFixture` extends it with a small uniform map already
  run through `init_navigation()`'s real triangulation.
  `ariadne_wallhug.c` (2,178 lines, zero tests before this round) is now
  covered via its 5 real-external-linkage entry points — confirmed via
  `nm` on the built `.o`, not assumed: several non-`static`-looking
  definitions (`get_hugging_blocked_flags`, etc.) keep internal linkage
  because their forward declarations earlier in the file are `static`.
  Driving `slab_wall_hug_route` and
  `get_next_position_and_angle_required_to_tunnel_creature_to` through
  several scenarios transitively exercises every one of the file's
  `static` helpers rather than just the 5 exported entries. One test
  (`get_hug_side_options`) was rewritten after an empirical check
  disproved an initial assumption that it always converges to the exact
  destination subtile on an open grid — it doesn't, confirmed by an
  actual failing run — the test now asserts the function's real,
  always-true structural contract instead. `ariadne_update.c`'s
  `init_navigation()`/`update_navigation_triangulation()` now drive the
  real Delaunay triangulation algorithm end-to-end (68.4% line
  coverage), transitively exercising real triangulation machinery in
  `ariadne_navitree.c`/`ariadne_tringls.c`/`ariadne_points.c`/
  `ariadne_edge.c`/`ariadne_findcache.c` for the first time from
  genuinely-computed triangles rather than hand-poked fixtures.
  `ariadne.c`'s route-lifecycle entry points
  (`ariadne_initialise_creature_route_f`,
  `creature_follow_route_to_using_gates`,
  `ariadne_count_waypoints_on_creature_route_to_target_f`,
  `ariadne_invalidate_creature_route`, `angle_to_quadrant`) driven
  across a genuinely-triangulated map took it from 2.5% to 25.3% line
  coverage. `ariadne_prepare_creature_route_to_target_f` always calls
  `path_init8_wide_f` with `subroute=-2`, which takes the
  `ma_triangle_route`/tree-route branch rather than
  `gate_navigator_init8`/`route_through_gates` — so the route-init tests
  cover the tree-route machinery
  (`ma_triangle_route`/`triangle_route_do_fwd`/`_bak`/`calc_intersection`/
  `route_to_path`/`nav_same_component`/`regions_connected`/etc.) while
  the gate-navigator branch remains mostly unexercised (see the §11
  Known Gaps entry). A pre-existing test
  (`ariadne_update_test.cpp`'s `init_navigation` test) turned out to
  have an order-dependent assertion — `CHECK(count_Triangles >
  count_triangles_before)` isn't a reliable bound once another test
  (this round's own `RouteFixture`, also using the same
  `TriangulatedWorldFixture` shape) has already triangulated the
  identical map earlier in a randomized run, since re-triangulating an
  identical-shaped map is idempotent, not cumulative — caught by the
  routine 3x `--order rand --rng-seed {1,2,3}` stability check (failed
  at seed 1, passed at 2/3), fixed by asserting against
  `triangulation_init_triangles`'s own just-seeded baseline (2) instead
  of a "before this call" snapshot.
- **`kfx_sim`** (fourth rung up the ladder) went 138 → 160 tests, 1.31%
  → 1.45% line coverage, 3.18% → 3.50% function coverage (per-library,
  `kfx_sim/src/index.html`). The smallest percentage move of any library
  pushed so far, and expectedly so: at 49,302 lines across ~90 files,
  `kfx_sim` is by far the largest library (more than 3× `kfx_platform`,
  5× `kfx_config`, 10× `kfx_pathfinding` combined) and the bulk of it is
  genuine `Thing`+`CreatureControl`+`Room`+`Dungeon` simulation logic —
  the same "needs a fuller world-state fixture" territory the 13
  untested `creature_states_*.c` files and the AI `player_comp*.c`
  files (`player_computer.c`/`player_comptask.c`/`player_compchecks.c`/
  `player_compprocs.c`/`player_compevents.c`, ~4,800 lines combined, all
  still at 0%) already represent. This pass found and landed the pure/
  near-pure functions reachable without that fixture work:
  `map_locations.c`'s `TbMapLocation` bit-packing accessors (pure
  bitfield math, `get_coord_encoded_location`/`get_map_location_type`/
  `_longval`/`_plyrval`, plus `get_map_location_code_name`'s
  `MLoc_HEROGATE`/`MLoc_PLAYERSHEART` branches — the latter through
  `kfx_config`'s already-populated `player_desc[]` table, no fixture
  needed); `room_jobs.c`'s `worker_needed_in_dungeons_room_role`
  (`RoRoF_Research` branch only — the sibling branches reach into
  `player_computer.c`'s `get_dungeon_money_less_cost`, which itself calls
  a `kfx_config` power-cost lookup, not attempted); `map_ceiling.c`'s
  `ceiling_set_info` (found, by testing rather than assuming, that a
  very negative `height_min` reaches the "distance too large" rejection
  branch and still leaves `kfx_sim_state.ceiling_dist` written before
  the early return — an earlier draft of this test wrongly assumed that
  branch was unreachable through the public validation path); `power_
  process.c`'s `players_disease_can_infect_target_players_creatures`
  (found, again by testing, that its `allies_share_disease` branch is
  only reachable when the one-directional alliance flag it re-checks is
  already known to be set, so that branch always resolves to "can't
  infect" regardless of the flag's value — not a bug fixed here, just
  the function's actual behavior, pinned down). Given the library's
  scale, closing a meaningful fraction of it — starting with the
  `creature_states_*.c`/`player_comp*.c` fixture work — is realistically
  a multi-session effort of its own, not a single follow-up pass like
  the other three libraries got.
- **`kfx_render`** (fifth rung up the ladder) went 25 → 40 tests, 2.1% →
  3.1% line coverage, 5.4% → 11.1% function coverage (per-library,
  `kfx_render/src/index.html`) — function coverage more than doubled,
  the biggest relative jump of any library pushed so far, by landing the
  `LensEffect` C++ class hierarchy stage-08's kfx_render row had already
  flagged as "a natural fit... no fixture needed": `MistEffect`/
  `OverlayEffect`/`DisplacementEffect`/`FlyeyeEffect`'s constructors,
  `Draw()`, and `Cleanup()` are all safe to exercise on a never-`Setup()`
  instance (confirmed by reading every one of their bodies first — each
  guards its teardown logic behind `m_current_lens >= 0` or an
  internally nullptr-checked helper, and each `Draw()` takes the same
  early-return path before ever touching its `LensRenderContext*`
  argument, so `nullptr` is safe to pass); `LensEffect` base-class
  accessors (`GetType`/`GetName`/`SetEnabled`/`IsEnabled`) and
  `LoadAssetWithFallback`'s validation early-returns; `LuaLensEffect`'s
  full lifecycle with a `NULL` `lua_State` (skips `RegisterBufferFunctions`
  at construction and every other Lua-touching branch) plus its pure
  `SetConfig`/`SetParameter`/`GetParameter` config surface.
  `LuaLensEffect`'s actual Lua-exposed pixel accessors (`LuaGetPixel`/
  `LuaSetPixel`/`LuaCopyPixel`) are `private`, only reachable by
  registering them into a real `lua_State` and running a script that
  passes a userdata block shaped like the `.cpp` file's own unexported
  `LuaBufferInfo` struct — replicating that layout in a test would be a
  fragile shortcut, not a real fixture, so not attempted. `PaletteEffect`
  (the sixth effect subclass)'s `Setup()`/`Cleanup()` reach into real
  player/palette state (`get_my_player()`, `lenses_conf`,
  `PaletteSetPlayerPalette`) and weren't attempted either. The rest of
  this library — `engine_render.c` (5,618 lines, the 3D rendering
  engine), `custom_sprites.c`, `engine_arrays.c`, `vidmode.c`,
  `cursor_tag.c` (real map/subtile state), and the remaining
  `LensManager.cpp`/`lens_api.c` singleton-backed render-target
  plumbing — all need a live rendering surface or real map/player world
  state, `kfx_platform`'s `bflib_vidraw.c`-style "significant harness
  change" territory rather than a quick follow-up.
- **`kfx_net`** (sixth rung up the ladder) went 33 → 41 tests, 3.3% →
  4.0% line coverage, 6.4% → 8.6% function coverage (per-library,
  `kfx_net/src/index.html`). `net_checksums.c`'s `checksums_different()`
  landed (host-vs-client checksum comparison, including the
  `PlaF_CompCtrl`/inactive-network-slot skip branches and the "missing
  checksum packet" case) — found `get_host_player_id()` is hardcoded to
  return `0`, which made the fixture simpler than expected (host is
  always `kfx_sim_state.players[0]`, no need to fake it). `net_input_lag.c`'s
  two `network_is_active()`-gated early returns (`input_lag_skips_
  processing`/`input_lag_needs_lookahead`) also landed — everything else
  in that file mixes module-private state with zero accessors
  (`input_lag_target`/`input_lag_increase_turns`, ...) and real
  wall-clock reads (`LbTimerClock()`), so it wasn't pursued further:
  even the parts that only touch private state can't be asserted on
  without either a new accessor (out of scope) or draining that state
  through side-effecting calls in a way that would make the test
  order-dependent across Catch2's randomized run order. Most of the rest
  of this library — `net_matchmaking.c`, `net_game.c`,
  `net_exchange_common.c`/`_gameplay.c`, `net_lobby.c`, `net_lan.c`,
  `net_portforward.cpp`, `net_holepunch.c`, `net_main.c` — is genuine
  ENet/socket networking, needing either a real network stack or heavy
  mocking; `packets_cheats.c` is three giant `Thing`/`Room`/`Dungeon`-
  state dispatch functions, the same "needs a fuller world-state
  fixture" territory `kfx_sim`'s AI cluster represents.
- **`kfx_game`** (seventh rung up the ladder) went 13 → 25 tests, 0.3% →
  0.8% line coverage, 0.6% → 2.5% function coverage (per-library,
  `kfx_game/src/index.html`) — both roughly quadrupled off a very small
  starting base. `game_saves.c`'s inter-level creature-transfer
  bookkeeping (`add_transfered_creature`/`get_transferred_creature`/
  `clear_transfered_creatures`) landed cleanly: `get_dungeon()`/
  `dungeon_invalid()` index `kfx_sim_state.dungeon[]` directly with no
  allocation-flag gate (unlike players), so any in-range `PlayerNumber`
  resolves to a valid dungeon with zero extra fixture work.
  `is_primitive_save_version()` (same file) was considered and declined:
  its threshold is a pointer-difference between two unrelated globals
  (`game`/`kfx_sim_state.loaded_level_number`), an inherently unstable
  value tied to link layout rather than a real invariant, so there's
  nothing meaningful to assert against. `game_heap.c`'s
  `setup_heap_manager()` failure path is real, not faked — it opens an
  actual `creature.jty` data file that doesn't exist in the unit-test
  environment, so `LbFileOpen(..., Lb_FILE_MODE_READ_ONLY)` genuinely
  returns `NULL`; `reset_heap_manager()` was declined since it calls
  `LbFileClose()` unconditionally on a handle that starts `NULL` in this
  binary, and `fclose(NULL)` is undefined behavior per the C standard
  (relying on glibc's specific non-crashing handling of it would be
  exactly the kind of implementation-defined assumption this plan avoids
  elsewhere). `lvl_script_conditions.c`'s `pop_condition`/`get_`/
  `set_script_current_condition` landed too, though `pop_condition`'s
  "pop from a non-empty stack" branch is unreachable from a test: its
  backing `condition_stack[]`/`condition_stack_pos` are module-private
  with no accessor, and the only thing that ever pushes onto that stack
  is `command_add_condition()`, real script-parsing state not attempted
  here. The overwhelming majority of this library remains untouched and
  needs real script/save/audio state: `lvl_script_commands.c` (3,741
  lines, the largest single file in the whole codebase),
  `console_cmd.c` (1,990), `lvl_script_commands_old.c` (842),
  `lvl_script.c` (673), `game_saves.c`'s actual save/load chunk I/O,
  `lvl_script_value.c`, `main_game.c`, `sounds.c`'s real OpenAL/S3D
  emitter functions, `lvl_script_lib.c`, and `game_loop.c` — all
  `kfx_sim`-AI-cluster-style or `kfx_platform`-audio-style "significant
  harness change" territory, not a quick follow-up.
- **`kfx_frontend`** (eighth rung up the ladder — the library
  `stage-08-comprehensive-library-passes.md` called "genuinely the
  hardest ... for this exercise") went 9 → 28 tests, 0.2% → 0.6% line
  coverage, 0.6% → 1.7% function coverage (per-library,
  `kfx_frontend/src/index.html`) — roughly tripled off a very small
  base, more than doubling the library's test count. Two clean, whole-
  file wins: `kfx_frontend_state.c` (the raw-blob save/load/reset
  lifecycle registered on `GameCallbacks` — `save_frontend_state`/
  `load_frontend_state` reuse `kfx_platform`'s real-scratch-file
  discipline for an actual round trip, not a fake) and `gui_frontmenu.c`'s
  pure `active_menus[]` scans (`get_active_menu`/`first_monopoly_menu`/
  `menu_id_to_number`/`point_is_over_gui_menu`, all bounds-checked reads
  over `kfx_platform`'s own `struct GuiMenu` array — a *complete* array
  type, unlike some other extern arrays hit earlier in this plan, so
  `sizeof()`/direct indexing both just work). `gui_topmsg.c`'s
  `erstat_check` (the periodic error-statistics flush, gated on
  `get_gameturn() & 0x07 == 0`) also landed, reusing `kfx_platform`'s
  `GetGameTurnFunc` pattern-B fixture rather than depending on real
  wall-clock/game-turn state. Three missing header declarations found
  and added along the way (`point_is_over_gui_menu`, `erstat_check` plus
  its three backing globals). Everything else in this library is
  genuinely GUI-widget-array/screen-coordinate/real-menu-state coupled
  as `stage-04g` found originally — `frontend.cpp` (2,026 lines),
  `front_input.c` (1,943), `frontmenu_ingame_tabs.c` (1,856),
  `front_landview.c`, `frontmenu_ingame_map.c`, `gui_frontbtns.c`,
  `gui_draw.c`, `gui_parchment.c`, and the rest — none of it reduces to
  a bare-struct pattern-A test the way `kfx_frontend_state.c`/
  `gui_frontmenu.c` did.
- **`kfx_script`** (ninth rung up the ladder) went 8 → 23 tests, 4.6% →
  6.1% line coverage, 6.4% → 8.5% function coverage (per-library,
  `kfx_script/src/index.html`) — nearly tripled the test count.
  `lua_utils.c`'s `try_get_c_method`/`try_get_from_methods` (the shared
  per-class `__index` metamethod helpers every `lua_api_*.c` file's
  method table goes through) landed with a bare `luaL_newstate()`
  fixture — real Lua stack manipulation, not a fake, reusing
  `lua_params_test.cpp`'s existing "bare state, no `open_lua_script()`
  init chain" discipline. `api.c`'s event-subscription bookkeeping
  (`api_subscribe_event`/`api_unsubscribe_event`/
  `api_is_subscribed_to_event`/`api_clear_all_subscriptions`) turned out
  to be pure array management with zero socket involvement despite
  living in the external HTTP API server file, plus `get_max_flags`
  (a pure count of `kfx_game`'s real, statically-populated `flag_desc[]`
  table). Eight functions in `api.c` had no header declaration at all
  despite real external linkage — all added to `api.h`, the usual "add
  the missing declaration" fix, though only the event-subscription five
  plus `get_max_flags` got tests; the var-subscription three
  (`api_is_subscribed_to_var`/`api_subscribe_var`/`api_unsubscribe_var`)
  additionally reach into `kfx_game`'s `get_condition_value()` (real
  `Dungeon`/`Thing`/`PlayerInfo` state depending on the variable type)
  and weren't pursued this pass — a reasonable next increment. The rest
  of this library is the `lua_api_*.c` family's actual Lua-exposed
  bindings (`lua_api.c` at 1,325 lines, `lua_api_things.c`,
  `lua_api_lens.c`, ...) plus `lua_triggers.c`/`lua_cfg_funcs.c` — all
  need either a real triangulated/Thing/Room fixture behind the Lua call
  or the full `open_lua_script()` init chain already exercised in
  `lua_base_test.cpp`, not a quick follow-up.
- **`kfx_apploop`** (tenth and top rung of the ladder) went 4 → 8 tests,
  4.5% → 8.3% line coverage, 15% → 25% function coverage (per-file —
  this library is a single source file, `game_session_loop.cpp`).
  `find_frame_rate()`/`packet_load_find_frame_rate()` each keep a
  function-local static accumulator/timestamp pair with no reset
  accessor; both were already reached indirectly by the pre-existing
  `display_should_be_updated_this_turn` tests (via the fixture's fixed
  `LbTimerClock` fake returning a constant 0), but only their "still
  accumulating" branch, since the clock never advances — landed the
  "1000ms/5000ms elapsed, compute a time_delta" branch of each by making
  the fake clock settable. A first attempt split each function's
  coverage across two separate `TEST_CASE`s (one for "accumulating",
  one for "fires") and failed under `--order rand`: within one raw-binary
  process, whichever `TEST_CASE` happened to run second could find the
  static already sitting at the exact value the first one's "prime" step
  used as its own trigger threshold, silently skipping its prime and
  inheriting the first test's leftover accumulator instead of a known
  baseline — confirmed by the actual failure (`6656 == 5120` expected),
  not assumed. Fixed by combining each function's full behavior into one
  `TEST_CASE`, verified stable across three more `--order rand` seeds.
  `keeper_screen_swap()` turned out to be safely callable directly, not
  a "real rendering surface" function as its name suggests: it calls
  `RendererPresentFrame()`, which no-ops on kfx_platform's own
  `s_active_renderer` static defaulting to `nullptr` (never set in this
  test binary, no `RendererSetActive()` call). `keeper_wait_for_next_turn()`
  got one branch — `frame_skip < 0` and no `GNFldD_WaitSleepMode` returns
  `false` before ever touching `get_time_tick_ns()` — the only branch
  reachable without a real wall-clock read: unlike `LbTimerClock`,
  `get_time_tick_ns()` reads `std::chrono::high_resolution_clock` directly
  through a macro, not a registered function pointer, so there's no
  existing fake-provider seam for it. The rest of this library —
  `update()` (the per-turn dispatcher, calls into every other layer),
  `game_loop()`/`wait_at_frontend()`/`keeper_gameplay_loop()` (the
  top-level orchestration loops, real coroutines/network/rendering calls
  throughout) — is this ladder's own composition root by design (see
  `game_session_loop.h`'s file comment) and doesn't reduce to a
  pattern-A/B unit test without either a real wall-clock seam or a much
  larger integration-style harness; genuinely out of scope for this kind
  of pass.
- **Coverage-tooling gap, found while investigating the tenth-rung
  (`kfx_apploop`) overall-repo denominator drop, and fixed the same
  day**: the `coverage` target's `lcov --extract
  ".../src/kfx_*/src/*"` pattern (`CMakeLists.txt`) silently excluded
  every file living one or two directories deeper than
  `src/kfx_<name>/src/` — 16 files, all under
  `src/kfx_platform/src/{platform,renderer,renderer/software}/` (moved
  there by the "Flatten kfx/platform and kfx/renderer to platform/ and
  renderer/" commit), several of which (`bflib_render.c`,
  `bflib_vidraw.c`, ...) already have real test coverage from earlier
  rounds. Root cause was **not** the pattern's shape — lcov's own
  `transform_pattern()` (`lcov` source, `deps/lcov/lcov-1.16/bin/lcov`)
  already maps each `*` to a regex `.*`, which matches `/` fine, so a
  single trailing `*` was always capable of reaching arbitrary depth.
  The real bug was `add_custom_target(coverage ...)` missing
  `VERBATIM`: without it, CMake emitted the `--extract` pattern
  *unquoted* on the generated Ninja command line (confirmed by reading
  `build.ninja` directly), so the shell glob-expanded
  `src/kfx_*/src/*` against the real filesystem — one path segment per
  `*` — before lcov's own matcher ever saw a `*` character; each
  already-resolved, wildcard-free result was then compared as a literal
  string, which could never match anything two directories down. Fixed
  by adding `VERBATIM` to the target, which makes CMake itself escape
  every argument for the target shell, letting lcov do the (recursive,
  already-correct) matching it was designed for. Confirmed fixed: the
  previously-invisible 16 files now appear in `coverage.info`
  (262 → 277 `SF:` entries); corrected overall totals: **3.1% line
  coverage (3,949 / 125,589)**, **9.4% function coverage (715 / 7,629)**
  — both the numerator and denominator went *up* from the
  already-corrected 662-test figure in §6, since these files carry real,
  previously-uncounted coverage on top of previously-uncounted total
  lines.
- No coverage floor / CI gate — deliberately deferred, §6 above.
- No branch-coverage data (`lcov`'s `--summary` reports "no data found"
  for branches; line/function coverage only).

## 12. Document map

| Question | Where to look |
|---|---|
| What's true about the harness right now | this document |
| How the harness was designed, stage by stage (historical) | [`docs/refactor/testing/`](../refactor/testing/) |
| Per-library test status, what's tested vs. deferred | [`docs/refactor/testing/comprehensive/stage-08-comprehensive-library-passes.md`](../refactor/testing/comprehensive/stage-08-comprehensive-library-passes.md), [`stage-08b-kfx-sim-clusters.md`](../refactor/testing/comprehensive/stage-08b-kfx-sim-clusters.md) |
| Library dependency ladder, callback-struct pattern, layering enforcement | [`architecture.md`](architecture.md) |
| How to write a functional (`src/ftests/`) test | [`src/ftests/README.md`](../../src/ftests/README.md) |
| How ftest-driven coverage is produced, headless mode, staging real game data, merging with the unit-test report | §7 above |
