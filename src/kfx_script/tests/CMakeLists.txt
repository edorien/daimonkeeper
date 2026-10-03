# docs/refactor/testing/comprehensive/stage-06-kfx-script-luajit.md: links
# the whole kfx_script OBJECT library plus its full real dependency ladder
# -- kfx_script is rank 7 (deliberately wide, architecture.md §2.8), so
# this is the widest *_utest link list yet, matching kfx_frontend_utest's
# shape plus kfx_script itself. Explicit centitoml link for the same
# transitive-kfx_config reason as every *_utest so far.
#
# kfx_luajit (Dependencies.cmake) is reused as-is -- LuaJIT is already
# fetched/built for the main program (pkg-config-first, FetchContent/
# ExternalProject fallback otherwise), nothing script-testing-specific to
# add there.
#
# check_layering_symbols.py reports zero accepted residuals for
# kfx_script itself, and every symbol kfx_net/kfx_render/kfx_sim's own
# residuals could need is already resolved for real by linking
# kfx_frontend+kfx_game+kfx_net+kfx_render+kfx_sim together (confirmed by
# kfx_frontend_utest needing no stub library at all, stage-04g) -- so, per
# the "try the plain linked-library shape first" rule, no stub library is
# linked here either, unless a real build says otherwise.
# kfx_script_test_paths.h.in -> kfx_script_test_paths.h: gives the Tier 2
# fixture (lua_base_test.cpp) an absolute path to this repo's config/
# directory, same idea as ver_defs.h.in's version-info generation.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/kfx_script_test_paths.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/kfx_script_test_paths.h"
    @ONLY)

file(GLOB KFX_SCRIPT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(kfx_script_utest ${KFX_SCRIPT_TEST_SOURCES})
target_link_libraries(kfx_script_utest PRIVATE
    kfx_script
    kfx_frontend
    kfx_game
    kfx_net
    kfx_render
    kfx_ai
    kfx_sim
    kfx_pathfinding
    kfx_config
    kfx_platform
    kfx_log_opts
    kfx_common_opts
    kfx_luajit
    centitoml
    tinyfiledialogs # same OBJECT-library explicit-link gap as centitoml --
                     # kfx_platform now links bflib_filedialogs.cpp, which
                     # needs tinyfiledialogs's compiled object.
    imgui # kfx_frontend's own gui/FrontendImGui.cpp/frontgui_*.cpp
          # reference imgui symbols (this target links kfx_frontend for
          # real) -- same two-levels-of-INTERFACE-indirection object-linking
          # gap as centitoml above.
    Catch2::Catch2WithMain)
target_include_directories(kfx_script_utest PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

include(CTest)
include(Catch)
catch_discover_tests(kfx_script_utest TEST_PREFIX "kfx_script.")
