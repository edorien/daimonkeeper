# docs/refactor/testing/stage-04d-kfx-render.md: links the whole
# kfx_render OBJECT library, same shape as kfx_sim_utest (see
# ../../kfx_sim/tests/CMakeLists.txt). Explicit centitoml link for the
# same transitive-kfx_config reason.
#
# kfx_render_utest transitively links kfx_sim, which used to need the
# get_packet/set_packet_action/etc. accepted-residual stubs
# (kfx_sim/tests/kfx_packet_test_stubs) -- removed once those functions'
# real implementations moved down into kfx_sim itself (packet_data.c,
# docs/refactor/todo/remove-symbol-level-layering-residuals.md), so
# kfx_sim provides them for real now. Also doesn't need
# kfx_sim_test_stubs.cpp's former creature_table_add[] stub for the same
# reason -- creature_table_add[] moved into kfx_sim's own
# creature_graphics.c too.
file(GLOB KFX_RENDER_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(kfx_render_utest ${KFX_RENDER_TEST_SOURCES})
target_link_libraries(kfx_render_utest PRIVATE
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
    Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(kfx_render_utest TEST_PREFIX "kfx_render.")
