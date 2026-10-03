# docs/refactor/testing/stage-04e-kfx-net.md: links the whole kfx_net
# OBJECT library plus its real dependency ladder. Explicit centitoml link
# for the same transitive-kfx_config reason as every *_utest so far.
#
# kfx_net's one accepted residual is net_resync.cpp's raw-blob resync
# (architecture.md §6.2): it #includes and memcpy()s `game`/
# `kfx_game_state`/`kfx_frontend_state` wholesale, all really owned by
# kfx_game/kfx_frontend. Split into two shared STATIC libraries (not one)
# because they stop being needed on different schedules -- see
# docs/refactor/testing/stage-04f-kfx-game.md for kfx_game_utest, the
# first target that needs one but not the other. Defined here, the first
# place either is needed, so later *_utest targets can link just the
# piece(s) they still need.
add_library(kfx_game_state_test_stubs STATIC kfx_game_state_test_stubs.cpp)
target_link_libraries(kfx_game_state_test_stubs PRIVATE kfx_common_opts)
target_include_directories(kfx_game_state_test_stubs PRIVATE
    "${CMAKE_SOURCE_DIR}/src/kfx_game/include")

add_library(kfx_frontend_state_test_stub STATIC kfx_frontend_state_test_stub.cpp)
target_link_libraries(kfx_frontend_state_test_stub PRIVATE kfx_common_opts)
target_include_directories(kfx_frontend_state_test_stub PRIVATE
    "${CMAKE_SOURCE_DIR}/src/kfx_frontend/include")

file(GLOB KFX_NET_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(REMOVE_ITEM KFX_NET_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/kfx_game_state_test_stubs.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kfx_frontend_state_test_stub.cpp")

# (kfx_packet_test_stubs, formerly linked by kfx_sim_utest/
# kfx_render_utest for get_packet/set_packet_action/etc., no longer
# exists -- those functions' real implementations moved down into
# kfx_sim's own packet_data.c, docs/refactor/todo/
# remove-symbol-level-layering-residuals.md, so kfx_sim provides them for
# real wherever it's linked, this target included.)
add_executable(kfx_net_utest ${KFX_NET_TEST_SOURCES})
target_link_libraries(kfx_net_utest PRIVATE
    kfx_net
    kfx_render
    kfx_ai
    kfx_sim
    kfx_pathfinding
    kfx_config
    kfx_platform
    kfx_log_opts
    kfx_common_opts
    centitoml
    tinyfiledialogs # same OBJECT-library explicit-link gap as centitoml --
                     # kfx_platform now links bflib_filedialogs.cpp, which
                     # needs tinyfiledialogs's compiled object.
    kfx_game_state_test_stubs
    kfx_frontend_state_test_stub
    Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(kfx_net_utest TEST_PREFIX "kfx_net.")
