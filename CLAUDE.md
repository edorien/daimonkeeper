# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

KeeperFX — a free, open-source reimplementation of Bullfrog's *Dungeon Keeper* (C/C++). Originally a decompilation project; the codebase has since been fully rewritten. Requires the original game's data files (not in this repo) to actually run.

## Build

**CMake (`CMakeLists.txt` + `src/kfx_*/CMakeLists.txt`, glob-based) is the only build definition that compiles the game**, for every target — native Linux, Windows via MSVC/clang-cl (vcpkg), and Windows via mingw-w64 cross-compile (`build/cmake/toolchains/mingw32.cmake`). This is what `.github/workflows/*.yml` actually runs to produce `keeperfx`/`keeperfx_hvlog`. `file(GLOB ...)` per library means a new file under `src/kfx_<name>/src/` is picked up automatically — no file list to maintain.

The historical `Makefile` (`mingw32-make standard`/`heavylog`, hand-maintained `OBJS =` list) no longer builds the game in CI and isn't documented/wired to fetch its own SDL3 mingw dev headers (`sdl/include`, `sdl/lib`) — treat it as unmaintained for compiling source. CI does still shell out to it for the **asset/data pipeline only**: `make pkg-languages`/`pkg-gfx`/`pkg-enginegfx` (regenerate .dat files from .po/.pot and PNGs) and `make pkg-assemble` (stage config/campaign/level data for packaging) — none of these touch `OBJS` or compile any `.c`/`.cpp`. Final packaging is CMake/CPack (`cmake --build out --target package`), not `make package`. `Makefile` itself stays at the repo root (where `make` looks by default); everything it `include`s (`version.mk`, `prebuilds.mk`, `package.mk`, `pkg_gfx.mk`, `pkg_lang.mk`, `pkg_sfx.mk`, the still-active `tool_*.mk` files) lives under `build/make/` — mirroring `build/cmake/`'s CMake modules. Anything referencing one of these by path (`CMakeLists.txt`'s own `version.mk` read, `.github/workflows/*.yml`) must use the `build/make/` path too.

**The CMake build fetches its own third-party dependencies — don't assume a bare environment needs manual `-dev` package installs first.** `build/cmake/modules/Dependencies.cmake`: Windows/mingw uses prebuilt `kfx-deps` static-lib tarballs (`kfx_fetch()`); native Linux tries system `pkg-config` for each dependency (SDL3, openal, spng, minizip, luajit, miniupnpc, natpmp) and falls back to `FetchContent`-from-source (or `ExternalProject_Add` for ffmpeg and luajit specifically, since neither has a plain CMake build) when a system package isn't found — ffmpeg in particular is *always* built from source (see the portability rationale below), never taken from a distro package. A machine with only a C/C++ toolchain, `cmake`, `ninja`, and `pkg-config` — no SDL3/ffmpeg/openal/luajit/etc. `-dev` packages — can still run `KFX_OS=linux ./build-cmake.sh` end-to-end; it just does more compiling on first run while those fallbacks build. Don't reach for `apt install` of a long dependency list, or assume a build can't be verified, before actually trying the configure step.

### CMake (local dev and CI)

```bash
./build-cmake.sh                     # Windows target, cross-compiled (needs mingw-w64 i686 toolchain)
KFX_OS=linux ./build-cmake.sh        # native Linux ELF build
./build-cmake.sh keeperfx_hvlog      # heavy-log (verbose debug logging) variant
USE_DOCKER=1 ./build-cmake.sh        # build inside an Ubuntu 24.04 container matching CI
BUILD_DIR=out/foo ./build-cmake.sh   # override the build tree (default: out/<KFX_OS>/, git-ignored)

# what build-cmake.sh's Windows path wraps, and what CI runs directly:
cmake -S . -B out/windows -G Ninja -DCMAKE_TOOLCHAIN_FILE=build/cmake/toolchains/mingw32.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build out/windows --target keeperfx keeperfx_hvlog
```

Build tree: `out/<KFX_OS>/keeperfx` (or `.exe` on Windows) — `out/linux/` and `out/windows/` are separate trees (a `CMakeCache.txt` bakes in its compiler/toolchain, so they can't share one). `keeperfx` and `keeperfx_hvlog` are the same code, differing only in `BFDEBUG_LEVEL` (0 vs 10) — every `src/kfx_*` library is compiled twice, once per variant.

`build-cmake.sh` also runs `cmake --install ... --component runtime` after the build, copying the binary plus any runtime libs it needs (SDL3 shared libs, when built from source rather than found on the system) to `dist/<KFX_OS>/` (`dist/windows/` or `dist/linux/`, git-ignored) — a stable, ready-to-run location independent of `$BUILD_DIR`. Driven by the same `install()` rules `Packaging.cmake`/`Dependencies.cmake` use for CPack (both tag their runtime-relevant rules `COMPONENT runtime`), so it stays in sync automatically; the `--component runtime` filter is what keeps this to just the binary + its libs instead of also pulling in every `install()` rule the fetched SDL3 subprojects register for themselves (headers, cmake config, docs, ...). On Linux the binary's `INSTALL_RPATH` is `$ORIGIN` so the copy in `dist/linux/` finds its sibling `.so` files without `LD_LIBRARY_PATH`.

For a **full local package** (binary + libs + game data — configs, campaigns, levels, language/sound `.dat` files, docs), not just the binary, use `build-package.sh` instead — it generalizes what CI's release workflows do (`.github/workflows/build-*.yml`) for either platform:

```bash
./build-package.sh                              # Windows, full package
KFX_OS=linux ./build-package.sh                 # Linux, full package
BUILD_NUMBER=1234 PACKAGE_SUFFIX=Alpha ./build-package.sh
```

It runs `make pkg-enginegfx`, the CMake configure+build (both variants), `make pkg-assemble` (stages the rest of the game data into `pkg/`), then two `cmake --install ... --component <c>` calls — `runtime` and `gamedata` (the `pkg/`-staging `install(CODE ...)` block in `Packaging.cmake`, tagged the same way as the SDL3 libs) — into `dist/<KFX_OS>/`. Needs network access (clones `dkfans/FXGraphics` for `pkg-enginegfx`, plus the usual first-run dependency fetches).

Both `dist/linux/` and `dist/windows/` are meant to be genuinely portable — copyable to a machine that never ran the build — not just a build-tree convenience copy. Windows gets there almost for free (`-static stdc++ winpthread` plus static `.a` deps for everything except the 3 SDL3 DLLs). Linux needed a deliberate fix: `Dependencies.cmake`'s native-Linux branch always builds ffmpeg from source with `--disable-everything`/`--disable-autodetect` and only `smacker`/`smackaud` enabled (via `ExternalProject_Add`, not `FetchContent` — ffmpeg's build is its own `./configure`+`make`, not CMake) rather than the usual "system pkg-config first" pattern the other deps use. A distro ffmpeg package is *always* the wrong shape here regardless of whether it's present: it's built with every optional codec/protocol/font-rendering feature on, which drags in 100+ transitive shared libraries (X11, cairo, pango, Kerberos, video codecs nothing here uses, ...) that don't get bundled — `bflib_fmvids.cpp` only ever decodes KeeperFX's own `.smk` (Smacker) cutscenes, so building just that support statically closes the gap entirely. Verified via `ldd`: `dist/linux/keeperfx`'s only non-bundled, non-libc dependencies are `libssl`/`libcrypto`/`libzstd` (curl's TLS/compression, near-universal on modern distros) — no ffmpeg-related library appears at all.

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

- **`src/ftests/`** — in-game functional tests (CUnit-based scaffolding), for reproducing bugs / exercising gameplay logic against a real running game. Enabled via the `FUNCTESTING` build define. Run with `-ftests` (optionally `-ftests <test_name>` for a single test) as a game launch argument; `-exitonfailedtest` makes the process exit with code 0/-1 on success/failure, for automation. Results are logged to `keeperfx.log`, lines prefixed `FTest:`. New tests: copy `src/ftests/tests/ftest_template.{h,c}`, rename, implement actions, register in `src/ftests/ftest_list.c`. Full guide: [src/ftests/README.md](src/ftests/README.md).
- **`tests/`** — standalone CUnit test programs (`tst_main`, `tst_enet_client`, `tst_enet_server`, `001_test`), built via `mingw32-make tests`.
- **`src/kfx_*/tests/`** — Catch2 unit tests, one binary per `kfx_*` library (`KFX_BUILD_TESTS=ON`, native Linux only). Full guide: [docs/refactor/testing/00-overview.md](docs/refactor/testing/00-overview.md).

When merging new commits from upstream (`origin`, dkfans/keeperfx) into this fork's refactored tree, use the coverage-first procedure in [docs/Architecture/upstream-merge-workflow.md](docs/Architecture/upstream-merge-workflow.md) — upstream has no test harness of its own, so this fork's `src/ftests/` and Catch2 suite are what catch a merge-introduced regression.

## Architecture

**Read [docs/Architecture/architecture.md](docs/Architecture/architecture.md) first** — it is the authoritative, current description of the codebase structure, kept up to date. The rest of this section is a summary; defer to that document on any conflict.

`src/` was refactored (see `docs/refactor/`, historical record only — don't expect it to track current code) from one flat 266-file directory into internal CMake `OBJECT` libraries with a **strict, one-directional, acyclic dependency graph**, enforced in CI by `scripts/check_layering.py --strict`:

```
kfx_platform → kfx_config → kfx_sim → kfx_render → kfx_net → kfx_game → kfx_frontend → kfx_script → kfx_apploop → app_entry (main.cpp)
```

(`kfx_script` and `kfx_apploop` are special-ranked: allowed to depend on anything below, nothing depends on them. `kfx_editor` — the in-game level editor, see `docs/refactor/editor/` and architecture.md §2.9a — sits above `kfx_apploop`, just below `app_entry`.) Each `src/kfx_<name>/` directory *is* its CMake OBJECT library — the physical file location determines build-target membership, there's no separate hand-maintained file list.

**Never let a lower-ranked library `#include` a higher-ranked one.** When a lower layer genuinely needs to call into a higher one (state read, UI action, sound, Lua event), use the callback-struct pattern instead: declare a `*Callbacks` struct (almost always in `kfx_config/include/`), implement it in the higher layer, wire it up once in `src/main.cpp::setup_game()` via `set_*_callbacks()`. `main.cpp` is the deliberate exception — the one file allowed to `#include` every layer, because it's the composition root where cross-layer wiring is isolated. Full callback-struct catalog and the (small, documented) list of accepted irreducible violations: architecture.md §5 and §8.2.

World state lives in per-library `extern` state structs (`kfx_sim_state`, `kfx_net_state`, `kfx_game_state`, `kfx_config_state`, `kfx_render_state`, `kfx_frontend_state`) — not in the old `struct Game`, which is now a near-empty serialization placeholder. These structs are `memcpy`'d wholesale as raw blobs in three places (network resync, save games, level reset) — you can move fields *between* the structs freely, but changing a struct's on-disk/on-wire layout needs a migration story (architecture.md §6.2).

Domain model basics: the map is a `Slab` → `Subtile` → `Column` → `Cube` hierarchy; every entity in the world is a `struct Thing` (`kfx_sim`), creatures additionally carry a `struct CreatureControl`. Full detail: [docs/data_structure.md](docs/data_structure.md).

## Conventions

- New file → just the correct `src/kfx_<name>/src/` directory (see Build above); CMake's glob picks it up, nothing else to update.
- Respect the dependency ladder; run `check_layering.py` before assuming a cross-library `#include` is fine.
- World state belongs in the owning library's state struct, not `struct Game`.
- New cross-layer call needed → declare the callback struct in `kfx_config`, implement the wrapper in `main.cpp`, register in `setup_game()`.
- Functional tests go in `src/ftests/`, registered in `ftest_list.c`.
