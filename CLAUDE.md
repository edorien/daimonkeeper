# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

dAImon Keeper — a free, open-source reimplementation of Bullfrog's *Dungeon Keeper* (C/C++), derived from KeeperFX (dkfans/keeperfx, the `origin` remote) and still merging from it. Originally a decompilation project; the codebase has since been fully rewritten. Requires the original game's data files (not in this repo) to actually run. It plays KeeperFX campaigns/map packs but is a separate product: see "Product identity and KeeperFX compatibility" below.

## Build

**CMake (`CMakeLists.txt` + `src/kfx_*/CMakeLists.txt`, glob-based) is the only build definition that compiles the game**, for every target — native Linux, Windows via MSVC/clang-cl (vcpkg), and Windows via mingw-w64 cross-compile (`build/cmake/toolchains/mingw32.cmake`). This is what `.github/workflows/*.yml` actually runs to produce the game (target `keeperfx`, output file `daimonkeeper`). `file(GLOB ...)` per library means a new file under `src/kfx_<name>/src/` is picked up automatically — no file list to maintain.

The historical `Makefile` (`mingw32-make standard`/`heavylog`, hand-maintained `OBJS =` list) no longer builds the game in CI and isn't documented/wired to fetch its own SDL3 mingw dev headers (`sdl/include`, `sdl/lib`) — treat it as unmaintained for compiling source. CI does still shell out to it for the **asset/data pipeline only**: `make pkg-languages`/`pkg-gfx`/`pkg-enginegfx` (regenerate .dat files from .po/.pot and PNGs) and `make pkg-assemble` (stage config/campaign/level data for packaging) — none of these touch `OBJS` or compile any `.c`/`.cpp`. Final packaging is CMake/CPack (`cmake --build out --target package`), not `make package`. `Makefile` itself stays at the repo root (where `make` looks by default); everything it `include`s (`version.mk`, `prebuilds.mk`, `package.mk`, `pkg_gfx.mk`, `pkg_lang.mk`, `pkg_sfx.mk`, the still-active `tool_*.mk` files) lives under `build/make/` — mirroring `build/cmake/`'s CMake modules. Anything referencing one of these by path (`CMakeLists.txt`'s own `version.mk` read, `.github/workflows/*.yml`) must use the `build/make/` path too.

**The CMake build fetches its own third-party dependencies — don't assume a bare environment needs manual `-dev` package installs first.** `build/cmake/modules/Dependencies.cmake`: Windows/mingw uses prebuilt `kfx-deps` static-lib tarballs (`kfx_fetch()`); native Linux tries system `pkg-config` for each dependency (SDL3, openal, spng, minizip, luajit, miniupnpc, natpmp) and falls back to `FetchContent`-from-source (or `ExternalProject_Add` for ffmpeg and luajit specifically, since neither has a plain CMake build) when a system package isn't found — ffmpeg in particular is *always* built from source (see the portability rationale below), never taken from a distro package. A machine with only a C/C++ toolchain, `cmake`, `ninja`, and `pkg-config` — no SDL3/ffmpeg/openal/luajit/etc. `-dev` packages — can still run `./build-cmake-linux.sh` end-to-end; it just does more compiling on first run while those fallbacks build. Don't reach for `apt install` of a long dependency list, or assume a build can't be verified, before actually trying the configure step.

### CMake (local dev and CI)

`build-cmake-linux.sh` is the single-binary entry point for **both** platforms, despite its name — `KFX_OS` selects the target and defaults to `linux`:

```bash
./build-cmake-linux.sh                                  # native Linux ELF build (default)
KFX_OS=windows ./build-cmake-linux.sh                   # Windows target, cross-compiled (needs mingw-w64 i686 toolchain)
USE_DOCKER=1 ./build-cmake-linux.sh                     # build inside an Ubuntu 24.04 container matching CI (combine with KFX_OS)
BUILD_DIR=out/foo ./build-cmake-linux.sh                # override the build tree (default: out/<KFX_OS>/, git-ignored)

# what build-cmake-linux.sh's Windows path wraps, and what CI's release workflows run directly:
cmake -S . -B out/windows -G Ninja -DCMAKE_TOOLCHAIN_FILE=build/cmake/toolchains/mingw32.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build out/windows --target keeperfx
```

The script takes one target per run (first argument, default `keeperfx`) and must be run from the repo root (the configure step uses the current directory as the source tree).

`extraBuildScripts/` holds the pre-move scripts:

- `build-cmake.sh` — legacy, superseded by `build-cmake-linux.sh` (defaults to `KFX_OS=windows`, doesn't install the `mcp` component). Run it from the repo root as `extraBuildScripts/build-cmake.sh`.
- `build-package-windows.sh` / `build-package-windows.bat` — **native Windows** build + package (no cross-compile): the Windows half of `build-package.sh` run inside MSYS2's MINGW32 shell, producing the CPack `.7z` in `pkg/` plus `dist/windows/` (installs the same `runtime`, `gamedata` and `mcp` components). Both switch to the repo root themselves, so they run from any directory: `extraBuildScripts/build-package-windows.sh` from a MINGW32 shell, or `extraBuildScripts\build-package-windows.bat` from a plain Command Prompt (bootstraps MSYS2; set `MSYS2_ROOT` if it isn't at `C:\msys64`).

Build tree: `out/<KFX_OS>/daimonkeeper` (or `.exe` on Windows; the CMake target is still `keeperfx`, `OUTPUT_NAME` is the product slug) — `out/linux/` and `out/windows/` are separate trees (a `CMakeCache.txt` bakes in its compiler/toolchain, so they can't share one). There is one executable: how much it logs is the `LOG_LEVEL` option (`OFF`/`NORMAL`/`DEBUG`/`DEBUGMAX` in `daimonkeeper.cfg` or the options screen, applied live), not a build variant — the old `keeperfx_hvlog` heavy-log build is gone (docs/refactor-pass2/stage-02-logging-option.md). Functional-test runs (`-ftests`) always log at `NORMAL`, whatever `daimonkeeper.cfg` says.

`build-cmake-linux.sh` also runs `cmake --install ... --component runtime` after the build, copying the binary plus any runtime libs it needs (SDL3 shared libs, when built from source rather than found on the system) to `dist/<KFX_OS>/` (`dist/windows/` or `dist/linux/`, git-ignored) — a stable, ready-to-run location independent of `$BUILD_DIR`. Driven by the same `install()` rules `Packaging.cmake`/`Dependencies.cmake` use for CPack (both tag their runtime-relevant rules `COMPONENT runtime`), so it stays in sync automatically; the `--component runtime` filter is what keeps this to just the binary + its libs instead of also pulling in every `install()` rule the fetched SDL3 subprojects register for themselves (headers, cmake config, docs, ...). On Linux the binary's `INSTALL_RPATH` is `$ORIGIN` so the copy in `dist/linux/` finds its sibling `.so` files without `LD_LIBRARY_PATH`. A second, separate `--component mcp` install copies `scripts/llm_bridge/` (the LLM/agent MCP bridge, `docs/refactor/AI/LLM/`) to `dist/<KFX_OS>/mcp/` — plain stdlib Python, identical either way, so it runs unconditionally regardless of `$KFX_OS`.

For a **full local package** (binary + libs + game data — configs, campaigns, levels, language/sound `.dat` files, docs), not just the binary, use `build-package.sh` instead — it generalizes what CI's release, unit-test and coverage workflows do (`.github/workflows/build-*.yml`). It does **not** read `KFX_OS`: by default one run does three passes — the Windows package, the Linux package, and a Catch2 coverage build+test+report — and each is skipped with a `SKIP_*` variable:

```bash
./build-package.sh                              # Windows + Linux packages, plus unit-test coverage report
SKIP_WINDOWS=1 ./build-package.sh               # Linux package (+ coverage) only
SKIP_LINUX=1 ./build-package.sh                 # Windows package (+ coverage) only
SKIP_COVERAGE=1 ./build-package.sh              # both packages, no coverage pass
BUILD_NUMBER=1234 PACKAGE_SUFFIX=Alpha ./build-package.sh
KFX_FTEST_DATA_DIR=/path/to/keeperfx/install ./build-package.sh   # also ftest coverage, merged report
```

It runs `make pkg-enginegfx` and `make pkg-assemble` once (stages the shared game data into `pkg/`), then per platform the CMake configure+build (both variants) into `out/<os>/` and three `cmake --install ... --component <c>` calls — `runtime`, `gamedata` (the `pkg/`-staging `install(CODE ...)` block in `Packaging.cmake`, tagged the same way as the SDL3 libs) and `mcp` (`scripts/llm_bridge/`) — into `dist/<os>/`. If `i686-w64-mingw32-gcc` isn't on `PATH` the Windows pass is skipped with a warning rather than aborting. The coverage pass writes `out/coverage/coverage-html/index.html`; with `KFX_FTEST_DATA_DIR` set it also builds `out/coverage-ftest/` and merges both into `out/coverage-merged/`. Needs network access (clones `dkfans/FXGraphics` for `pkg-enginegfx`, plus the usual first-run dependency fetches, once per build tree).

For **coverage only** (no packages, no `make pkg-*`, no `dist/` output), `build-coverage-core.sh` runs the unit-test coverage, the ftest coverage and the merge, pre-pointed at the git-ignored local game data in `core_files/` (it exits early if that doesn't contain `daimonkeeper.cfg` or `keeperfx.cfg`, and `data/`):

```bash
./build-coverage-core.sh                              # unit + ftest coverage, merged report
CORE_FILES=/other/install ./build-coverage-core.sh    # different game-data directory
SKIP_UNIT=1 ./build-coverage-core.sh                  # reuse out/coverage/, redo ftest + merge
SKIP_FTEST=1 ./build-coverage-core.sh                 # unit-test coverage only
```

Both `dist/linux/` and `dist/windows/` are meant to be genuinely portable — copyable to a machine that never ran the build — not just a build-tree convenience copy. Windows gets there almost for free (`-static stdc++ winpthread` plus static `.a` deps for everything except the 3 SDL3 DLLs). Linux needed a deliberate fix: `Dependencies.cmake`'s native-Linux branch always builds ffmpeg from source with `--disable-everything`/`--disable-autodetect` and only `smacker`/`smackaud` enabled (via `ExternalProject_Add`, not `FetchContent` — ffmpeg's build is its own `./configure`+`make`, not CMake) rather than the usual "system pkg-config first" pattern the other deps use. A distro ffmpeg package is *always* the wrong shape here regardless of whether it's present: it's built with every optional codec/protocol/font-rendering feature on, which drags in 100+ transitive shared libraries (X11, cairo, pango, Kerberos, video codecs nothing here uses, ...) that don't get bundled — `bflib_fmvids.cpp` only ever decodes KeeperFX's own `.smk` (Smacker) cutscenes, so building just that support statically closes the gap entirely. Verified via `ldd`: `dist/linux/daimonkeeper`'s only non-bundled, non-libc dependencies are `libssl`/`libcrypto`/`libzstd` (curl's TLS/compression, near-universal on modern distros) — no ffmpeg-related library appears at all.

### Make (asset/data pipeline and packaging only — see note above)

```bash
mingw32-make package       # 7z release package via package.mk (standalone; not the CMake/CPack package target)
mingw32-make pkg-languages # regenerate .dat files from .po/.pot
mingw32-make pkg-gfx       # regenerate gfx .dat/.tab/.raw/.pal from PNGs (needs libPNG + separate gfx source)
mingw32-make pkg-assemble  # stage config/campaign/level data (what CI runs before cmake --build ... --target package)
mingw32-make clean
mingw32-make tests         # builds the CUnit test binary in tests/
mingw32-make cppcheck      # static analysis
```

Add `DEBUG=1` to any target for a build with debug symbols. Must be run from a real shell (`sh`/bash via MSYS on Windows) — not `cmd.exe`.

### Layering check (CI-blocking)

```bash
python3 scripts/check_layering.py            # human-readable report
python3 scripts/check_layering.py --strict   # exit 1 on any violation not in ACCEPTED_VIOLATIONS — this is what CI runs
```

Verifies no `src/kfx_*/` library `#include`s a header from a library ranked above it (see Architecture below). Run this after any change that adds or moves an `#include` across a `src/kfx_*/` boundary.

## Tests

Two separate test mechanisms:

- **`src/ftests/`** — in-game functional tests (CUnit-based scaffolding), for reproducing bugs / exercising gameplay logic against a real running game. Enabled via the `FUNCTESTING` build define. Run with `-ftests` (optionally `-ftests <test_name>` for a single test) as a game launch argument; `-exitonfailedtest` makes the process exit with code 0/-1 on success/failure, for automation. Results are logged to `daimonkeeper.log`, lines prefixed `FTest:`. New tests: copy `src/ftests/tests/ftest_template.{h,c}`, rename, implement actions, register in `src/ftests/ftest_list.c`. Full guide: [src/ftests/README.md](src/ftests/README.md).
- **`tests/`** — standalone CUnit test programs (`tst_main`, `tst_enet_client`, `tst_enet_server`, `001_test`), built via `mingw32-make tests`.
- **`src/kfx_*/tests/`** — Catch2 unit tests, one binary per `kfx_*` library (`KFX_BUILD_TESTS=ON`, native Linux only). Full guide: [docs/refactor/testing/00-overview.md](docs/refactor/testing/00-overview.md).

When merging new commits from upstream (`origin`, dkfans/keeperfx) into this fork's refactored tree, use the coverage-first procedure in [docs/Architecture/upstream-merge-workflow.md](docs/Architecture/upstream-merge-workflow.md) — upstream has no test harness of its own, so this fork's `src/ftests/` and Catch2 suite are what catch a merge-introduced regression.

## Architecture

**Read [docs/Architecture/architecture.md](docs/Architecture/architecture.md) first** — it is the authoritative, current description of the codebase structure, kept up to date. The rest of this section is a summary; defer to that document on any conflict.

`src/` was refactored (see `docs/refactor/`, historical record only — don't expect it to track current code) from one flat 266-file directory into internal CMake `OBJECT` libraries with a **strict, one-directional, acyclic dependency graph**, enforced in CI by `scripts/check_layering.py --strict`:

```
kfx_platform → kfx_config → kfx_content → kfx_model → kfx_pathfinding → kfx_sim → kfx_ai → kfx_render → kfx_net → kfx_game → kfx_frontend → kfx_script → kfx_apploop → kfx_editor → app_entry (main.cpp)
```

(`kfx_script` and `kfx_apploop` are special-ranked: allowed to depend on anything below, nothing depends on them. `kfx_editor` — the in-game level editor, see `docs/refactor/editor/` and architecture.md §2.9a — sits above `kfx_apploop`, just below `app_entry`.) Each `src/kfx_<name>/` directory *is* its CMake OBJECT library — the physical file location determines build-target membership, there's no separate hand-maintained file list.

**Never let a lower-ranked library `#include` a higher-ranked one.** When a lower layer genuinely needs to call into a higher one (state read, UI action, sound, Lua event), use a *port* instead: an entry in a `.def` file under `kfx_config/include/ports/` (or `kfx_platform/include/ports/` for platform code), called through its generated `PREFIX_name()` wrapper (`ui_turn_off_menu()`, `script_lua_on_...()`), implemented and tabled by the higher *provider* library in its `*_port_impl.c(pp)`, and installed once by `src/main.cpp::wire_ports()` with `set_*_port()` (it runs first thing in `LbBullfrogMain()`, before any config parsing). `main.cpp` is the deliberate exception — the one file allowed to `#include` every layer, because it's the composition root where the ports are installed. Full port catalog and the (small, documented) list of accepted irreducible violations: architecture.md §5 and §8.2.

World state lives in per-library `extern` state structs (`kfx_sim_state`, `kfx_net_state`, `kfx_game_state`, `kfx_config_state`, `kfx_render_state`, `kfx_frontend_state`) — not in the old `struct Game`, which is now a near-empty serialization placeholder. These structs, and Ariadne's navigation mesh (`ariadne_saved_state.h`), are `memcpy`'d wholesale as raw blobs in two places (network resync, save games); they are zeroed once at start-up, and between levels `clear_game()` clears only chosen parts. Any field change or move is a layout change: bump that struct's version and size in `kfx_config/include/state_versions.h` (a `_Static_assert` fails the build until you do). Older saves are then refused cleanly, with no migrations (architecture.md §6.2). Sim state kept anywhere else (a global, a `static`) is lost on a save/load or resync: the `sim_state_continuity` ftests catch that.

Domain model basics: the map is a `Slab` → `Subtile` → `Column` → `Cube` hierarchy; every entity in the world is a `struct Thing` (`kfx_sim`), creatures additionally carry a `struct CreatureControl`. Full detail: [docs/data_structure.md](docs/data_structure.md).

## Product identity and KeeperFX compatibility

This tree ships as **dAImon Keeper** (slug `daimonkeeper`), not KeeperFX. The rules that keep the two apart while staying content-compatible:

- **One source for names and versions**: `build/make/version.mk` holds `PRODUCT_SLUG`, the product version (`VER_*`, 1.0.0) and `KFX_COMPAT_MAJOR/MINOR` (1.4 — the KeeperFX release whose content this plays). CMake, the Makefile and the code (via the generated `ver_defs.h` and `src/kfx_platform/include/version.h`: `PRODUCT_NAME`, `PRODUCT_SLUG`, `PRODUCT_MAGIC`, `PRODUCT_VERSION_LABEL` "dAImon Keeper 1.0.0 — KFX 1.4") all read it. Use those macros for anything user-facing, never string literals. `KFX_COMPAT_*` moves only when an upstream merge brings in everything of a KeeperFX *release* (`scripts/kfx_parity.py --upstream <tag> --fail-on-missing` reports nothing missing).
- **Own files**: executable `daimonkeeper`, base config `daimonkeeper.cfg` (seeded once from an existing `keeperfx.cfg`, which is never written), log `daimonkeeper.log`, otherwise KeeperFX's folder names (`fxdata/`, `save/`, `replays/`) -- the two games' `fxdata/` contents differ, so each needs its own install folder; users share data between them with symlinks, not by relocating folders in the code. Saves and replays carry a `PROD` chunk (`PRODUCT_MAGIC` 'DMKR'); files without it (KeeperFX's) are refused by `validate_save_chunks()`. LAN discovery strings are product-specific; matchmaking is off by default and refuses KeeperFX's server.
- **The content surface does not change**: script command names, config keys/values, Lua API names, folder layout, `mods/<mod>/keeperfx.cfg` overrides. Add, never repurpose.
- **Internal identifiers stay**: `kfx_*` libraries, `keeperfx_*` symbols, the `keeperfx` CMake target, file-header credits. Renaming them only makes upstream merges harder.
- **Compatibility checks**: unknown script commands/names, config keys/values, Lua calls to missing functions and limit overflows are collected in `kfx_config`'s compat report (`compat_report.h`) during a level load, logged as `COMPAT:` lines and shown to the player before play (`game_compat_review.c`, `frontgui_ingame.cpp`). The level/campaign lists mark levels whose scripts use unknown commands (`script_preflight_*` in `lvl_script.c`, `frontgui_compat_badges.cpp`). The editor's "Force KeeperFX" save refuses maps KeeperFX 1.4 can't load (`editor_kfx_compat.cpp`, list generated into `kfx_compat_reference.inc`).
- **Generated files to refresh**: `scripts/gen_kfx_compat_reference.py` (after upstream merges, when `KFX_COMPAT_*` or `config/fxdata` changes), `scripts/gen_third_party_notices.py` (when a dependency changes); both have `--check`. `NOTICE` records the upstream commit merged up to. The upstream-merge workflow doc lists when to run each.
- Artwork (icon, start-up screens, README banner) is generated from `res/branding/` (see its README).

## Conventions

- New file → just the correct `src/kfx_<name>/src/` directory (see Build above); CMake's glob picks it up, nothing else to update.
- Respect the dependency ladder; run `check_layering.py` before assuming a cross-library `#include` is fine.
- World state belongs in the owning library's state struct, not `struct Game`.
- New cross-layer call needed → add one line to the provider's port `.def` (`KFX_PORT_VOID`/`KFX_PORT_RET`/…, with its unwired default) and the real function to the provider's table in its `*_port_impl.c(pp)`; callers use the `PREFIX_name()` wrapper. A whole new port also needs its generated header and defaults file (copy an existing one) and a `set_*_port()` + `KFX_TABLE_COMPLETE` line in `main.cpp` (architecture.md §5.2).
- Functional tests go in `src/ftests/`, registered in `ftest_list.c`.
