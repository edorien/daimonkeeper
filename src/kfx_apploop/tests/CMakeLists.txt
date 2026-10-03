# docs/refactor/testing/comprehensive/stage-07-kfx-apploop-game-process.md:
# links the whole kfx_apploop OBJECT library plus its full real dependency
# ladder -- kfx_apploop is rank 8, allowed to depend on everything below
# (architecture.md §2.9), so this links the same wide ladder
# kfx_script_utest does, plus kfx_apploop and kfx_script themselves (this
# library's own dependency ladder includes kfx_script). Explicit
# centitoml link for the same transitive-kfx_config reason as every
# *_utest so far.
#
# check_layering_symbols.py reports zero accepted residuals for
# kfx_apploop; per the "try the plain linked-library shape first" rule,
# no stub library is linked here either, unless a real build says
# otherwise.
file(GLOB KFX_APPLOOP_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(kfx_apploop_utest ${KFX_APPLOOP_TEST_SOURCES})
target_link_libraries(kfx_apploop_utest PRIVATE
    kfx_apploop
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

include(CTest)
include(Catch)
catch_discover_tests(kfx_apploop_utest TEST_PREFIX "kfx_apploop.")
