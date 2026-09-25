# docs/refactor/testing/stage-04f-kfx-game.md: links the whole kfx_game
# OBJECT library plus its real dependency ladder. Explicit centitoml link
# for the same transitive-kfx_config reason as every *_utest so far.
#
# kfx_game_utest transitively links kfx_net, which needs net_resync.cpp's
# accepted-residual stubs (see ../../kfx_net/tests/CMakeLists.txt) -- but
# only kfx_frontend_state_test_stub, not kfx_game_state_test_stubs:
# `game`/`kfx_game_state` are real here, provided by kfx_game itself
# (game_legacy.c/kfx_game_state.c) -- linking the stub too would be a
# duplicate-definition error.
file(GLOB KFX_GAME_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(kfx_game_utest ${KFX_GAME_TEST_SOURCES})
target_link_libraries(kfx_game_utest PRIVATE
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
    kfx_frontend_state_test_stub
    Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(kfx_game_utest TEST_PREFIX "kfx_game.")
