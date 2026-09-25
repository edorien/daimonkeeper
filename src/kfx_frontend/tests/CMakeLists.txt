# docs/refactor/testing/stage-04g-kfx-frontend.md: links the whole
# kfx_frontend OBJECT library plus its real dependency ladder. Explicit
# centitoml link for the same transitive-kfx_config reason as every
# *_utest so far.
#
# Needs none of the accepted-residual stub libraries earlier stages
# introduced (kfx_game_state_test_stubs, kfx_frontend_state_test_stub --
# kfx_packet_test_stubs no longer exists at all, see kfx_net/tests/'s own
# CMakeLists.txt comment): linking kfx_net, kfx_game, and kfx_frontend all
# for real here resolves every symbol those stubs exist to fake --
# confirmed by the build succeeding without them, not assumed.
file(GLOB KFX_FRONTEND_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(kfx_frontend_utest ${KFX_FRONTEND_TEST_SOURCES})
target_link_libraries(kfx_frontend_utest PRIVATE
    kfx_frontend
    kfx_game
    kfx_net
    kfx_render
    kfx_sim
    kfx_pathfinding
    kfx_config
    kfx_platform
    kfx_bfdebug_std
    kfx_common_opts
    centitoml
    tinyfiledialogs # same OBJECT-library explicit-link gap as centitoml --
                     # kfx_platform now links bflib_filedialogs.cpp, which
                     # needs tinyfiledialogs's compiled object.
    imgui # same two-levels-of-INTERFACE-indirection object-linking gap as
          # centitoml above -- kfx_frontend (gui/FrontendImGui.cpp/
          # frontgui_style.cpp/frontgui_widgets.cpp/frontgui_stylesheet_test.cpp)
          # needs imgui's compiled object.
    Catch2::Catch2WithMain)

# Repo root, for tests that run against the shipped game data in core_files/ (skipped when absent).
target_compile_definitions(kfx_frontend_utest PRIVATE KFX_TEST_REPO_ROOT="${CMAKE_SOURCE_DIR}")

include(CTest)
include(Catch)
catch_discover_tests(kfx_frontend_utest TEST_PREFIX "kfx_frontend.")
