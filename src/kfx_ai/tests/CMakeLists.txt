# kfx_ai_utest: the computer-player AI's tests, moved from kfx_sim_utest in
# refactor pass 2 (S14). Same shape as kfx_sim_utest, with kfx_ai on top.
file(GLOB KFX_AI_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(kfx_ai_utest ${KFX_AI_TEST_SOURCES})
target_link_libraries(kfx_ai_utest PRIVATE
    kfx_ai
    kfx_sim
    kfx_pathfinding
    kfx_config
    kfx_platform
    kfx_log_opts
    kfx_common_opts
    centitoml
    tinyfiledialogs
    Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(kfx_ai_utest TEST_PREFIX "kfx_ai.")
