cmake_minimum_required(VERSION 3.20)

# Do not allow building in root
if (CMAKE_BINARY_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(FATAL_ERROR "Do not build in the root. Create a bin directory and remove ${CMAKE_SOURCE_DIR}/CMakeCache.txt")
endif ()

project(keeperfx C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

find_package(Git REQUIRED)
execute_process(COMMAND "${GIT_EXECUTABLE}" describe --always OUTPUT_VARIABLE COMMIT_ID OUTPUT_STRIP_TRAILING_WHITESPACE)

file(STRINGS "${CMAKE_SOURCE_DIR}/build/make/version.mk" _kfx_version_lines)
foreach(_line IN LISTS _kfx_version_lines)
    if(_line MATCHES "^(VER_MAJOR|VER_MINOR|VER_RELEASE|VER_BUILD)=([0-9]+)")
        set(${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
    endif()
endforeach()
foreach(_v VER_MAJOR VER_MINOR VER_RELEASE VER_BUILD)
    if(NOT DEFINED ${_v})
        message(FATAL_ERROR "version.mk is missing ${_v}")
    endif()
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/build/make/version.mk")

if(NOT DEFINED BUILD_NUMBER)
    set(BUILD_NUMBER "${VER_BUILD}")
endif()
set(PACKAGE_SUFFIX ""  CACHE STRING "Distribution suffix (e.g. Alpha, Release)")
set(VER_STRING     "${VER_MAJOR}.${VER_MINOR}.${VER_RELEASE}.${BUILD_NUMBER} ${PACKAGE_SUFFIX}")
set(GIT_REVISION   "${COMMIT_ID}")

set(KEEPERFX_VER_DEFS_H_IN ${CMAKE_SOURCE_DIR}/ver_defs.h.in)
set(KEEPERFX_VER_DEFS_H_OUT ${CMAKE_SOURCE_DIR}/ver_defs.h)
configure_file(${KEEPERFX_VER_DEFS_H_IN} ${KEEPERFX_VER_DEFS_H_OUT})

# build/cmake/modules/: brought in from upstream's SDL3 migration (merge of
# origin/master's SDL2->SDL3 work, see git log). Dependencies.cmake defines
# the kfx_sdl3 INTERFACE target (SDL3/SDL3_image/SDL3_mixer, prebuilt on
# Windows, pkg-config-or-FetchContent on Linux) plus every other external
# dependency (curl, luajit, ffmpeg, openal, ...) and the kfx_link_dependencies()
# function used below. BuildTargets.cmake (the rest of that module set) is
# NOT included here -- it assumes upstream's flat src/*.c(pp) GLOB_RECURSE
# single-target build, which this repo's src/kfx_*/ OBJECT-library split
# (below) replaces; only the dependency-acquisition half of upstream's
# module set applies here.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/build/cmake/modules)
include(Helpers)
include(Platforms)
include(Dependencies)

# Non-recursive: main.cpp is the only source file directly under src/ --
# every kfx_* library now lives in its own src/kfx_*/ subdirectory
# (unified alongside main.cpp/ftests/ rather than a separate top-level
# libs/ tree), and each owns its own file(GLOB ...) in its own
# CMakeLists.txt (add_subdirectory()'d below). A recursive glob here
# would double-pick-up every one of those files into the executable
# targets directly, alongside their real OBJECT-library compilation --
# silently duplicating every translation unit and failing at link time
# with "multiple definition" for every single symbol.
file(GLOB KEEPERFX_SOURCES_C "src/*.c")
file(GLOB KEEPERFX_SOURCES_CXX "src/*.cpp")
# src/ftests/ is the one subdirectory under src/ that isn't a kfx_*
# library (functional-test scaffolding, deliberately kept out of any
# library) -- it needs its own scoped recursive glob since it nests a
# tests/ subdirectory of its own. Built as its own kfx_ftests OBJECT
# library, gated behind the KFX_FUNCTESTING option below, rather than
# appended directly into KEEPERFX_SOURCES_C/CXX: several src/kfx_*/
# libraries call into it (ftest_srand()/ftest_update()) under #ifdef
# FUNCTESTING, so it needs to be linked wherever those libraries are --
# see KFX_FUNCTESTING's own comment for why that means keeperfx/
# keeperfx_hvlog only, never alongside KFX_BUILD_TESTS.
file(GLOB_RECURSE KEEPERFX_FTESTS_C "src/ftests/*.c")
file(GLOB_RECURSE KEEPERFX_FTESTS_CXX "src/ftests/*.cpp")
# The platform-exclusive OS-integration files (windows.cpp/steam_api.cpp/
# cdrom.cpp/linux.cpp) moved to src/kfx_platform/src/ in stage 3 -- see
# src/kfx_platform/CMakeLists.txt for their exclusion logic now. The new
# src/kfx_platform/src/platform/ classes from upstream's SDL3
# migration moved there too.

message(STATUS "We are using ${CMAKE_CXX_COMPILER_ID}")

# ---------------------------------------------------------------------------
# docs/refactor/ split src/ into ~9 kfx_* libraries by ownership (see
# docs/refactor/stage-01-object-libraries.md for how this started, as an
# OBJECT-library grouping proven against real files before anything
# physically moved). Every kfx_* library is now its own physically-extracted
# directory under src/ (alongside main.cpp/ftests/, not a separate top-level
# libs/ tree), each with a CMakeLists.txt that globs its own
# sources directly -- there's no per-file stem list left in this file to
# keep in sync with anything. What's left in KFX_SOURCES_REMAINING below is
# exactly src/main.cpp and the src/ftests/*.c functional-test scaffolding,
# both deliberately never migrated into a library.
#
# keeperfx and keeperfx_hvlog need genuinely different BFDEBUG_LEVEL
# compiled into nearly every translation unit (globals.h's SYNCDBG/
# WARNDBG/etc macros key off it, and globals.h is included almost
# everywhere) -- not just linked differently. OBJECT library sources are
# compiled once according to that library's own settings, so sharing one
# set of kfx_* libraries between both executables would silently drop
# keeperfx_hvlog's enhanced debug logging across the whole codebase. Each
# library is therefore built twice, once per BFDEBUG_LEVEL -- exactly the
# same total compile work the old single GLOB_RECURSE approach already
# did (every source file was already compiled twice, once per
# add_executable call).
# ---------------------------------------------------------------------------

# Kept identical to Makefile's WARNFLAGS (line 448) -- this list had drifted
# out of sync (missing -Wno-format-truncation/-Wno-maybe-uninitialized),
# which only surfaces as build failures on GCC 13, i.e. exactly the
# compiler CI's g++-mingw-w64-i686 install actually provides.
set(WARNFLAGS -Wall -W -Wshadow -Wno-sign-compare -Wno-unused-parameter -Wno-maybe-uninitialized -Wno-strict-aliasing -Wno-unknown-pragmas -Wno-format-truncation -Werror)
set(GNU_COMPILER_FLAG -march=x86-64 -fno-omit-frame-pointer -fmessage-length=0)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wimplicit")

# Compile settings shared by every kfx_* library and by keeperfx/
# keeperfx_hvlog's own directly-owned sources (KFX_SOURCES_REMAINING) --
# an INTERFACE library so there's exactly one place to change a flag,
# instead of ~20 target_compile_options calls drifting apart. Also carries
# every external dependency (SDL3 + the rest of Dependencies.cmake's
# targets) for the same reason: OBJECT library sources need these include
# directories to compile at all, not just at final-link time. Individual
# *_static/PkgConfig::* targets propagate their own
# INTERFACE_INCLUDE_DIRECTORIES/COMPILE_DEFINITIONS (e.g. curl_static's
# CURL_STATICLIB) through this same INTERFACE chain -- linking them here
# doesn't duplicate anything at the final executable link, since OBJECT
# libraries don't perform a real link step; kfx_link_dependencies() below
# is what actually links them onto keeperfx/keeperfx_hvlog.
add_library(kfx_common_opts INTERFACE)
target_compile_options(kfx_common_opts INTERFACE ${WARNFLAGS} ${GNU_COMPILER_FLAG})
target_link_libraries(kfx_common_opts INTERFACE kfx_sdl3)
# Individual kfx_* OBJECT libraries #include headers from these dependencies
# directly (lua.h, AL/al.h, curl/curl.h, toml.h, json.h, minizip/unzip.h,
# enet6/enet.h, astronomy.h, ...), not just the final keeperfx/keeperfx_hvlog
# executables -- so their INTERFACE_INCLUDE_DIRECTORIES need to reach every
# OBJECT library's own compilation via this same INTERFACE chain, mirroring
# kfx_link_dependencies() (Dependencies.cmake) but with INTERFACE instead of
# PRIVATE linkage since kfx_common_opts has no compiled sources of its own.
if(WIN32)
    target_link_libraries(kfx_common_opts INTERFACE kfx_win32_static_deps)
else()
    target_link_libraries(kfx_common_opts INTERFACE
        kfx_ffmpeg kfx_openal kfx_luajit
        kfx_spng kfx_minizip PkgConfig::ZLIB
        astronomy_static centijson_static enet6_static kfx_curl
        kfx_miniupnpc kfx_natpmp)
endif()
target_link_libraries(kfx_common_opts INTERFACE centitoml)
# tinyfiledialogs (Dependencies.cmake) -- same OBJECT-library INTERFACE-
# propagation story as centitoml above: kfx_platform's bflib_filedialogs.cpp
# #includes tinyfiledialogs.h directly.
target_link_libraries(kfx_common_opts INTERFACE tinyfiledialogs)
# imgui (Dependencies.cmake, Phase A of docs/refactor/renderer/
# 04-imgui-gui-foundation.md) -- same OBJECT-library INTERFACE-propagation
# story as centitoml above: kfx_platform's ImGuiContext.cpp #includes
# imgui.h/imgui_impl_sdl3.h/imgui_impl_sdlrenderer3.h directly, so its
# include dirs need to reach that compilation via this same INTERFACE
# chain. The compiled object itself is linked directly onto the final
# executables below, for the same reason centitoml's is (see that
# target_link_libraries(keeperfx PRIVATE centitoml) comment).
target_link_libraries(kfx_common_opts INTERFACE imgui)
# kfx_platform (stage 3) physically moved out of directly under src/ into
# its own include/ directory; every other kfx_* component and both
# executables still need to find its headers (e.g. #include
# "bflib_basics.h"), and src/kfx_platform/src/*.c* files still have a
# handful of acknowledged residual upward includes back into src/ (see
# docs/refactor/stage-02-decouple-bflib.md) that stage 2 didn't fully
# resolve -- so this is deliberately bidirectional rather than a one-way
# include dir on the kfx_platform target itself, which sibling OBJECT
# libraries (only linked together at the final executable, not to each
# other) wouldn't inherit.
target_include_directories(kfx_common_opts INTERFACE
    "${CMAKE_SOURCE_DIR}/src/kfx_platform/include"
    "${CMAKE_SOURCE_DIR}/src/kfx_config/include"
    "${CMAKE_SOURCE_DIR}/src/kfx_pathfinding/include"
    "${CMAKE_SOURCE_DIR}/src/kfx_sim/include"
    "${CMAKE_SOURCE_DIR}/src/kfx_render/include"
    "${CMAKE_SOURCE_DIR}/src/kfx_net/include"
    "${CMAKE_SOURCE_DIR}/src/kfx_game/include"
    "${CMAKE_SOURCE_DIR}/src/kfx_frontend/include"
    "${CMAKE_SOURCE_DIR}/src/kfx_script/include"
    "${CMAKE_SOURCE_DIR}/src/kfx_apploop/include"
    "${CMAKE_SOURCE_DIR}/src/kfx_editor/include"
    "${CMAKE_SOURCE_DIR}/src"
    # ver_defs.h is configure_file()'d to the repo root (see above), not
    # under src/ -- this was a pre-existing latent gap (predates the
    # libs/->src/ move) that a stray leftover src/ver_defs.h from the
    # Makefile build happened to mask until now.
    "${CMAKE_SOURCE_DIR}")
if(UNIX AND NOT APPLE)
    # Same suppression linux.mk already carries for this GCC/glibc combo
    # (not needed by the mingw-w64 cross-compiler, hence kept Linux-only):
    # abs(long) vs abs(int) call sites throughout ariadne/bflib_sound.
    target_compile_options(kfx_common_opts INTERFACE $<$<COMPILE_LANGUAGE:C>:-Wno-absolute-value>)
endif()

# The two BFDEBUG_LEVEL variants (see block comment above).
add_library(kfx_bfdebug_std INTERFACE)
target_compile_definitions(kfx_bfdebug_std INTERFACE BFDEBUG_LEVEL=0)
add_library(kfx_bfdebug_hvlog INTERFACE)
target_compile_definitions(kfx_bfdebug_hvlog INTERFACE BFDEBUG_LEVEL=10)

# docs/refactor/testing/: opt-in Catch2 unit-test harness, one test binary
# per src/kfx_*/ library (see stage-01-framework-and-scaffold.md for the
# full design). Native Linux only for now -- there's no Windows CI runner
# or Wine step to execute a mingw-cross-compiled test binary, so this
# isn't wired into the mingw32 toolchain path. Off by default: contributors
# who never pass -DKFX_BUILD_TESTS=ON see no new targets, no FetchContent,
# no new required tools.
option(KFX_BUILD_TESTS "Build Catch2 unit tests for each kfx_* library (native Linux only, see docs/refactor/testing/stage-01-framework-and-scaffold.md)" OFF)
if(KFX_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1 # pin; bump deliberately, not floating
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
    enable_testing()
endif()

# Compiles in the src/ftests/ in-game functional-test harness (every
# #ifdef FUNCTESTING block, spread across src/ftests/ itself plus
# kfx_platform/kfx_config/kfx_sim/kfx_game/kfx_frontend/kfx_apploop/main.cpp)
# into keeperfx/keeperfx_hvlog. The legacy Makefile's FTEST_DEBUG=1 is the
# only place that ever defined this; it is undefined here otherwise, so
# without this option every #ifdef FUNCTESTING block -- and the -ftests
# launch flag it gates -- compiles to nothing. Running the resulting
# binary with -ftests still needs the real, proprietary Dungeon Keeper
# data files (see src/ftests/README.md); this option only controls
# whether the harness is compiled in at all.
#
# Declared (and its KFX_BUILD_TESTS conflict resolved) before
# KFX_TEST_COVERAGE below, which reads this option's value to decide
# whether to instrument keeperfx/kfx_ftests for gcov -- option() calls
# must run before anything reads the variable they define.
option(KFX_FUNCTESTING "Compile in the src/ftests/ in-game functional-test harness (-ftests launch flag; needs real game data files to run, not just build)" OFF)
# Deliberately incompatible with KFX_BUILD_TESTS, not just untested together:
# turning FUNCTESTING on makes kfx_sim/kfx_game/kfx_frontend/kfx_apploop
# call real ftest_srand()/ftest_update() symbols (defined in kfx_ftests
# below) under #ifdef FUNCTESTING. Every kfx_*_utest binary reuses those
# same OBJECT-library .o files as keeperfx (that's the point of OBJECT
# libraries), so it would need those symbols resolved too -- but
# kfx_ftests' own sources (ftest_util.c, ftest_bug_*.c) reach all the way
# up to kfx_frontend/kfx_net (message_add_fmt, kfx_net_state,
# zoom_to_parchment_map, ...), and CMake OBJECT libraries do not forward
# each other's compiled objects transitively (confirmed with an isolated
# repro: PUBLIC target_link_libraries between two OBJECT libraries
# propagates usage requirements like include dirs, never the objects
# themselves) -- only a target that lists the *entire* dependency ladder
# directly, the way kfx_apploop_utest already does, can actually resolve
# them. Retrofitting every narrower *_utest (kfx_sim_utest, kfx_render_utest,
# ...) to also link the full ladder would undo their deliberately minimal,
# individually-justified link lists (see their own CMakeLists.txt comments)
# for a combination (functional-test harness + isolated unit tests) nobody
# actually needs at the same time: -ftests exercises the real compiled game
# against real game data, unit tests deliberately avoid needing either.
if(KFX_FUNCTESTING AND KFX_BUILD_TESTS)
    message(FATAL_ERROR "KFX_FUNCTESTING and KFX_BUILD_TESTS cannot both be ON: "
        "the *_utest binaries reuse kfx_sim/kfx_game/kfx_frontend/kfx_apploop's "
        "OBJECT-library output, which would need ftest_srand()/ftest_update() "
        "resolved (see the comment above this check) without pulling in every "
        "*_utest's full dependency ladder. Build them in separate trees instead.")
endif()
if(KFX_FUNCTESTING)
    target_compile_definitions(kfx_common_opts INTERFACE FUNCTESTING=1)

    # main.cpp and src/ftests/* itself are the only consumers now that
    # KFX_BUILD_TESTS is guaranteed OFF whenever this is ON (see above) --
    # a plain OBJECT library linked only into keeperfx/keeperfx_hvlog below.
    add_library(kfx_ftests OBJECT ${KEEPERFX_FTESTS_C} ${KEEPERFX_FTESTS_CXX})
    target_link_libraries(kfx_ftests PRIVATE kfx_common_opts)
endif()

# docs/refactor/testing/comprehensive/stage-05-coverage-tooling.md: opt-in
# gcov/lcov coverage report generation, layered on top of KFX_BUILD_TESTS
# or KFX_FUNCTESTING (mutually exclusive with each other, so exactly one
# of the two -- or neither -- is ever the actual consumer). Fetches lcov
# 1.16 (not the latest 2.x -- that needs non-core Perl modules; 1.16 needs
# none, verified) via kfx_fetch() (Dependencies.cmake), the same
# download-a-tarball-once helper every other prebuilt dependency in this
# file uses -- no `apt install lcov`/`pip install gcovr` needed.
option(KFX_TEST_COVERAGE "Instrument kfx_* libraries for gcov/lcov coverage (native Linux, requires -DKFX_BUILD_TESTS=ON or -DKFX_FUNCTESTING=ON)" OFF)
if((KFX_BUILD_TESTS OR KFX_FUNCTESTING) AND KFX_TEST_COVERAGE)
    target_compile_options(kfx_common_opts INTERFACE --coverage -O0 -g)
    target_link_options(kfx_common_opts INTERFACE --coverage)

    kfx_fetch(lcov "https://github.com/linux-test-project/lcov/releases/download/v1.16/lcov-1.16.tar.gz")
    # Derived from D (Dependencies.cmake), not a separately hardcoded path:
    # kfx_fetch(<dir> ...) always extracts to "${D}/<dir>", so this has to
    # track whatever D actually is rather than assume CMAKE_BINARY_DIR/deps.
    set(KFX_LCOV_BIN "${D}/lcov/lcov-1.16/bin/lcov")
    set(KFX_GENHTML_BIN "${D}/lcov/lcov-1.16/bin/genhtml")

    # geninfo (what `lcov --capture` invokes) checks each source line
    # against a regex -- lcov_excl_line, defaulting to the literal string
    # "LCOV_EXCL_LINE" -- and drops any matching line from the report.
    # --rc lcov_excl_line=<pattern> overrides that regex for this
    # invocation; kept as an alternation so per-line "// LCOV_EXCL_LINE"
    # comments still work alongside the pattern-based match. The logging
    # macros below (globals.h) are pure diagnostics with no branching
    # logic of their own, so their call sites contribute uninteresting
    # noise to the coverage percentage -- matched by macro name rather
    # than requiring an exclusion comment on every call site.
    set(KFX_LCOV_EXCL_LINE_PATTERN "LCOV_EXCL_LINE|\\b(ERRORLOG|WARNLOG|WARNMSG|SYNCDBG|JUSTMSG|SCRPTWRNLOG|SCRPTERRLOG)\\s*\\(")
endif()

set(KFX_SOURCES_REMAINING ${KEEPERFX_SOURCES_C} ${KEEPERFX_SOURCES_CXX})
set(KFX_OBJECT_LIBS_STD "")
set(KFX_OBJECT_LIBS_HVLOG "")

# Every kfx_* component is now a real, physically-extracted library target
# -- see src/kfx_*/CMakeLists.txt -- so nothing here subtracts from
# KFX_SOURCES_REMAINING anymore; it stays exactly what the glob above found
# (main.cpp + src/ftests/*, both intentionally kept out of any library).
# Stage 1 through 11's kfx_add_component() function, which incrementally
# carved libraries out of KFX_SOURCES_REMAINING by regex-matching stem
# lists before their physical src/kfx_*/ directory existed, is gone now
# that every component has one -- each src/kfx_*/CMakeLists.txt appends
# itself to KFX_OBJECT_LIBS_STD/_HVLOG directly, so all of them must come
# before those lists are consumed below.
add_subdirectory(src/kfx_platform)
add_subdirectory(src/kfx_config)
add_subdirectory(src/kfx_pathfinding)
add_subdirectory(src/kfx_sim)
add_subdirectory(src/kfx_render)
add_subdirectory(src/kfx_net)
add_subdirectory(src/kfx_game)
add_subdirectory(src/kfx_frontend)
add_subdirectory(src/kfx_script)
add_subdirectory(src/kfx_apploop)
add_subdirectory(src/kfx_editor)

# docs/refactor/testing/comprehensive/stage-05-coverage-tooling.md: the
# whole local coverage workflow as one target -- `ctest` then
# `cmake --build <dir> --target coverage`. Placed after every
# add_subdirectory(src/kfx_*) call above since it DEPENDS on their
# *_utest targets by name -- all eleven now exist (kfx_apploop/kfx_script
# landed in stage-06/07, kfx_editor's own suite added later; see
# docs/Architecture/testing-harness.md).
if(KFX_BUILD_TESTS AND KFX_TEST_COVERAGE)
    add_custom_target(coverage
        COMMAND ${CMAKE_COMMAND} -E env perl "${KFX_LCOV_BIN}"
            --capture --directory "${CMAKE_BINARY_DIR}"
            --output-file "${CMAKE_BINARY_DIR}/coverage.raw.info"
            --gcov-tool gcov --quiet
            --rc "lcov_excl_line=${KFX_LCOV_EXCL_LINE_PATTERN}"
        COMMAND perl "${KFX_LCOV_BIN}"
            --extract "${CMAKE_BINARY_DIR}/coverage.raw.info"
            "${CMAKE_SOURCE_DIR}/src/kfx_*/src/*"
            --output-file "${CMAKE_BINARY_DIR}/coverage.info" --quiet
        COMMAND perl "${KFX_GENHTML_BIN}"
            "${CMAKE_BINARY_DIR}/coverage.info"
            --output-directory "${CMAKE_BINARY_DIR}/coverage-html" --quiet
        DEPENDS
            kfx_platform_utest kfx_config_utest kfx_pathfinding_utest
            kfx_sim_utest kfx_render_utest kfx_net_utest kfx_game_utest
            kfx_frontend_utest kfx_script_utest kfx_apploop_utest
            kfx_editor_utest
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        COMMENT "Capturing coverage and rendering HTML report (coverage-html/index.html)"
        # Without VERBATIM, the Ninja/Makefile generators emit the
        # --extract pattern below unescaped on the generated shell command
        # line, so the shell glob-expands "src/kfx_*/src/*" against the
        # real filesystem before lcov's own perl-based matcher ever sees
        # it -- silently turning one literal wildcard pattern into a list
        # of already-resolved, wildcard-free path arguments (confirmed by
        # reading the generated build.ninja directly: the COMMAND line had
        # no quotes at all around this argument). That shell-level glob
        # only expands one path segment per "*", so it can never reach the
        # files living one or two directories below src/kfx_<name>/src/
        # (currently src/kfx_platform/src/{platform,renderer,
        # renderer/software}/, moved there by the "Flatten kfx/platform
        # and kfx/renderer" commit) -- they were silently missing from
        # every coverage.info/coverage-html report even though several
        # have real test coverage. VERBATIM makes CMake itself escape
        # every command argument for the target shell, so the pattern
        # reaches lcov as the single literal string it's written as here,
        # letting lcov's own transform_pattern() (which maps each "*" to a
        # regex ".*" -- already recursive, matches "/" too) do the actual
        # matching as originally intended.
        VERBATIM)
endif()

# The ftest-driven equivalent of the coverage target above: since
# KFX_FUNCTESTING and KFX_BUILD_TESTS are mutually exclusive (see
# KFX_FUNCTESTING's own comment), this never collides with the `coverage`
# target defined there -- exactly one of the two ever exists in a given
# configure. Runs every registered non-long-running src/ftests/ test
# (wildcard -ftests, no name) via -headless (bflib_video.c/main.cpp: SDL's
# "dummy" video driver + SoundDisabled, so this needs no real display or
# audio device) and -exitonfailedtest (nonzero exit on failure, aborting
# the target the same way a failing `ctest` would).
#
# Needs the real, proprietary Dungeon Keeper data files to get past config
# loading and actually run a level (see src/ftests/README.md) -- KFX_FTEST_DATA_DIR
# points at a real install (not distributed with this repo, hence no
# default other than a local staging convention); StageFtestData.cmake
# copies just the subset the registered tests need (~90MB, determined
# empirically via strace against a real run -- not all of a real
# install's ~1.5GB is needed) into this build tree, so keeperfx runs from
# CMAKE_BINARY_DIR like every other COMMAND here already does. Verified
# against a real install: 6 of 7 registered short tests passed
# (example_template_test, bug_imp_tp_attack_door__claim/prisoner/deadbody,
# bug_imp_goldseam_dig, bug_pathing_stair_treasury); the 7th
# (bug_invisible_units_cant_select) never completed within 3 minutes on
# its own, well past its other-test siblings' each-under-15-seconds pace --
# it drives mouse-cursor/thing-under-hand selection
# (ftest_util_center_cursor_over_dungeon_view(), player->thing_under_hand),
# which plausibly never resolves against a real display's absence under
# -headless. Not excluded from the wildcard run below (yet) -- flagged
# here rather than silently worked around.
option(KFX_FTEST_DATA_DIR "Path to a real KeeperFX game-data install (proprietary, not distributed with this repo) to stage into the build tree for the KFX_FUNCTESTING+KFX_TEST_COVERAGE `coverage` target -- see src/ftests/README.md" "")
if(KFX_FUNCTESTING AND KFX_TEST_COVERAGE)
    add_custom_target(coverage
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${KFX_FTEST_DATA_DIR}"
            "-DDEST_DIR=${CMAKE_BINARY_DIR}"
            "-DKEEPERFX_CFG=${CMAKE_SOURCE_DIR}/config/keeperfx.cfg"
            -P "${CMAKE_SOURCE_DIR}/build/cmake/modules/StageFtestData.cmake"
        COMMAND "$<TARGET_FILE:keeperfx>" -ftests -headless -exitonfailedtest
        COMMAND ${CMAKE_COMMAND} -E env perl "${KFX_LCOV_BIN}"
            --capture --directory "${CMAKE_BINARY_DIR}"
            --output-file "${CMAKE_BINARY_DIR}/coverage.raw.info"
            --gcov-tool gcov --quiet
            --rc "lcov_excl_line=${KFX_LCOV_EXCL_LINE_PATTERN}"
        COMMAND perl "${KFX_LCOV_BIN}"
            --extract "${CMAKE_BINARY_DIR}/coverage.raw.info"
            "${CMAKE_SOURCE_DIR}/src/kfx_*/src/*"
            --output-file "${CMAKE_BINARY_DIR}/coverage.info" --quiet
        COMMAND perl "${KFX_GENHTML_BIN}"
            "${CMAKE_BINARY_DIR}/coverage.info"
            --output-directory "${CMAKE_BINARY_DIR}/coverage-html" --quiet
        DEPENDS keeperfx
        # Every COMMAND above uses fully-qualified paths (KFX_LCOV_BIN,
        # ${CMAKE_BINARY_DIR}/..., ${CMAKE_SOURCE_DIR}/...), including
        # keeperfx itself now that StageFtestData.cmake stages its data
        # into CMAKE_BINARY_DIR -- so, unlike an earlier version of this
        # target, cwd genuinely doesn't matter for any of them; kept
        # explicit anyway for clarity.
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        COMMENT "Staging ftest data, running src/ftests/ headlessly, then capturing coverage and rendering HTML report (coverage-html/index.html)"
        # See the sibling coverage target's own comment above for why
        # VERBATIM is required on the --extract pattern.
        VERBATIM)
endif()

# Each src/kfx_*/CMakeLists.txt globs its own library directory directly --
# see docs/refactor/stage-0{6,7,8,9,10,11}-*.md for the per-stage history
# of how each one got there incrementally (kfx_sim/kfx_render one prefix
# cluster at a time, kfx_net/kfx_game/kfx_frontend the same but with their
# CMakeLists.txt created early to give their *_state.h/.c a home ahead of
# the first cluster, kfx_platform/kfx_config/kfx_script in a single pass).
# Physically unified under src/ (rather than a separate top-level libs/
# tree) after the SDL3 merge, so a single "everything the app builds from
# lives under src/" root matches upstream's own layout too.

message(STATUS "kfx: ${KFX_OBJECT_LIBS_STD} grouped; ${KFX_SOURCES_REMAINING} left ungrouped (app entry point + test scaffolding)")

# Add two executable targets: keeperfx and keeperfx_hvlog. Each owns
# whatever's left in KFX_SOURCES_REMAINING directly (main.cpp, ftests/*)
# and links in every kfx_* OBJECT library's matching BFDEBUG_LEVEL variant.
#
# Order matters: the default linker on both platforms (ld.bfd -- confirmed
# on both native Linux and mingw-w64) resolves static-library symbols in a
# single left-to-right pass. kfx_common_opts carries kfx_sdl3 -- if it's
# listed before the libraries that need SDL3 symbols, the linker would
# already have moved past it by the time the .o files referencing them
# are reached. Libraries needing symbols must come before the
# static/shared libraries providing them; kfx_link_dependencies() (called
# below, after these) carries the rest of Dependencies.cmake's targets
# for exactly this reason.
#
# Every kfx_* library is now a real STATIC archive (docs/refactor/todo/
# remove-symbol-level-layering-residuals.md's "Next steps"), not an
# OBJECT library -- so unlike before, *this list's own internal order*
# now matters too: a real archive only gets its needed members pulled in
# to satisfy symbols already undefined at that point in the scan, whereas
# OBJECT .o files were unconditionally included regardless of position.
# KFX_OBJECT_LIBS_STD/_HVLOG is built lowest-ranked-first (kfx_platform,
# kfx_config, ... kfx_apploop -- each add_subdirectory() appends itself in
# that call order); references only ever flow from a higher-ranked
# library down to a lower-ranked one (check_layering.py/
# check_layering_symbols.py --strict both report zero violations), so the
# link line needs the *reverse*: highest-ranked first, most-fundamental
# last. Every kfx_*_utest's own hand-written link order already lists
# libraries this same way (confirmed by inspection, not assumed) -- this
# just reverses the one list built in construction order instead of
# hand-ordering it again.
set(KFX_STATIC_LIBS_STD ${KFX_OBJECT_LIBS_STD})
list(REVERSE KFX_STATIC_LIBS_STD)
set(KFX_STATIC_LIBS_HVLOG ${KFX_OBJECT_LIBS_HVLOG})
list(REVERSE KFX_STATIC_LIBS_HVLOG)

# Window icon: generated as part of kfx_platform (src/kfx_platform/
# CMakeLists.txt), the only consumer of kfx_window_icon_png -- not here,
# so kfx_platform doesn't reach up to app_entry-level generated code for
# it (check_layering_symbols.py --strict).

add_executable(keeperfx ${KFX_SOURCES_REMAINING})
target_link_libraries(keeperfx PRIVATE ${KFX_STATIC_LIBS_STD} kfx_bfdebug_std kfx_common_opts)

add_executable(keeperfx_hvlog ${KFX_SOURCES_REMAINING})
target_link_libraries(keeperfx_hvlog PRIVATE ${KFX_STATIC_LIBS_HVLOG} kfx_bfdebug_hvlog kfx_common_opts)

if(KFX_FUNCTESTING)
    target_link_libraries(keeperfx PRIVATE kfx_ftests)
    target_link_libraries(keeperfx_hvlog PRIVATE kfx_ftests)
endif()

# kfx_link_dependencies() (Dependencies.cmake) links every external
# dependency other than SDL3 (already on kfx_common_opts above) onto the
# final executables -- curl/luajit/ffmpeg/openal/enet6/astronomy/spng/
# zlib/minizip/centijson/miniupnpc/libnatpmp, plus (Windows) the
# RESCAN-grouped static-archive linking their circular refs need.
kfx_link_dependencies(keeperfx)
kfx_link_dependencies(keeperfx_hvlog)

# centitoml (Dependencies.cmake) is itself an OBJECT library, wrapped
# inside kfx_sdl3/kfx_link_dependencies' INTERFACE indirection. Its
# compiled object doesn't reliably make it through two levels of
# INTERFACE-library indirection into the final link (its include
# directories propagate fine for compilation -- kfx_config's toml_parse()
# callers compile clean -- but the object file itself doesn't), so link it
# directly here too, the same way the pre-Stage-1 CMakeLists.txt always did.
target_link_libraries(keeperfx PRIVATE centitoml)
target_link_libraries(keeperfx_hvlog PRIVATE centitoml)

# tinyfiledialogs: same two-levels-of-INTERFACE-indirection object-linking
# gap as centitoml just above.
target_link_libraries(keeperfx PRIVATE tinyfiledialogs)
target_link_libraries(keeperfx_hvlog PRIVATE tinyfiledialogs)

# imgui: same two-levels-of-INTERFACE-indirection object-linking gap as
# centitoml just above -- link its compiled object directly onto the final
# executables too.
target_link_libraries(keeperfx PRIVATE imgui)
target_link_libraries(keeperfx_hvlog PRIVATE imgui)

if(WIN32)
    target_sources(keeperfx PRIVATE "res/keeperfx_stdres.rc")
    target_sources(keeperfx_hvlog PRIVATE "res/keeperfx_stdres.rc")
endif()

if(WIN32)
    # The default bfd linker in MinGW is extremely slow. LLVM linker (LLD) is
    # much much faster -- but isn't guaranteed to be installed alongside
    # g++-mingw-w64-i686 (it isn't, in a plain apt install), so this is a
    # speed optimization, not a hard requirement.
    find_program(KFX_LLD_LINKER NAMES ld.lld i686-w64-mingw32-ld.lld)
    if(KFX_LLD_LINKER)
        set_property(TARGET keeperfx PROPERTY LINKER_TYPE LLD)
        set_property(TARGET keeperfx_hvlog PROPERTY LINKER_TYPE LLD)
    endif()

    set(GNU_LINK_FLAG -mwindows -Wl,--enable-auto-import)
    target_link_options(keeperfx PRIVATE ${GNU_LINK_FLAG} -Wl,-Map,keeperfx.map)
    target_link_options(keeperfx_hvlog PRIVATE ${GNU_LINK_FLAG} -Wl,-Map,keeperfx_hvlog.map)
    target_link_libraries (keeperfx PUBLIC -static stdc++ winpthread -dynamic)
    target_link_libraries (keeperfx_hvlog PUBLIC -static stdc++ winpthread -dynamic)

    # System libraries -- kept identical to and in the same relative
    # position as Makefile's LINKLIB tail (`-lwinmm -lmingw32 -limagehlp
    # -lws2_32 -ldbghelp -lbcrypt -lole32 -luuid`, line ~391). This has to
    # be listed *after* everything else that might need winsock symbols
    # (e.g. libcurl_static's recvfrom/gethostname): ld.bfd is single-pass,
    # and enet_static already links ws2_32 too, but only for enet's own
    # needs at the point it's processed -- libcurl's later, different
    # winsock symbol needs go unresolved unless ws2_32 is scanned again
    # after it. bcrypt is also linked by Dependencies.cmake's openal_static
    # block; relinking it here is harmless.
    target_link_libraries(keeperfx PRIVATE winmm mingw32 imagehlp ws2_32 dbghelp bcrypt ole32 uuid)
    target_link_libraries(keeperfx_hvlog PRIVATE winmm mingw32 imagehlp ws2_32 dbghelp bcrypt ole32 uuid)

    # Ship the Windows exes without their debug symbols (#5318): objcopy splits
    # them into <exe>.debug next to the binary (CI uploads those as a separate
    # "-symbols" artifact). No-op for MSVC/non-Windows, see Helpers.cmake.
    apply_keeperfx_debug_split(keeperfx)
    apply_keeperfx_debug_split(keeperfx_hvlog)
elseif(UNIX AND NOT APPLE)
    target_link_options(keeperfx PRIVATE -rdynamic -Wl,-Map,keeperfx.map)
    target_link_options(keeperfx_hvlog PRIVATE -rdynamic -Wl,-Map,keeperfx_hvlog.map)
    target_link_libraries(keeperfx PRIVATE m dl)
    target_link_libraries(keeperfx_hvlog PRIVATE m dl)
    # Any SDL3-family libs built from source (Dependencies.cmake's Linux
    # fallback when the system doesn't have libsdl3-dev) get installed
    # alongside the binary -- see that file's install(DIRECTORY ...) calls.
    # Let the *installed* copy find them there instead of needing
    # LD_LIBRARY_PATH, without touching the build tree's own absolute RPATH
    # into _deps/ (CMake only applies INSTALL_RPATH at `cmake --install`
    # time; running straight out of the build dir is unaffected).
    set_target_properties(keeperfx keeperfx_hvlog PROPERTIES
        INSTALL_RPATH "$ORIGIN")

    # Same debug-symbol split as the Windows exes: without it the ELF carries
    # ~90 MB of DWARF. Symbols land in <binary>.debug next to it.
    apply_keeperfx_debug_split(keeperfx)
    apply_keeperfx_debug_split(keeperfx_hvlog)
endif()

# tools/ builds a host-native icon-conversion utility (png2ico) used to
# generate the Windows resource icon; unrelated to keeperfx/keeperfx_hvlog
# linking, so its position here doesn't matter.
add_subdirectory(tools)

# CPack-based release packaging (build/cmake/modules/Packaging.cmake, from
# upstream's SDL3 migration) -- produces the same kind of release archive
# as `make package`, taking the keeperfx binary from this CMake build and
# the game data from `make pkg-assemble` (see Packaging.cmake's header
# comment for the exact command sequence). Replaces the old
# Makefile-passthrough `package` custom target, which built its own
# separate copies of the binaries via Make's own object directory instead
# of reusing this CMake build tree's keeperfx/keeperfx_hvlog -- CPack's
# install()-based approach also correctly bundles SDL3's runtime DLLs on
# Windows (see Dependencies.cmake's install(FILES ...) calls), which the
# old target had no equivalent for.
include(Packaging)

# Optional self-contained Linux/Steam Deck bundle (-DKFX_DECK_BUNDLE=ON).
include(DeckBundle)
