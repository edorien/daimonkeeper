# docs/refactor/testing/stage-04b-kfx-pathfinding.md: links the whole
# kfx_pathfinding OBJECT library, same shape as kfx_config_utest (see
# ../../kfx_config/tests/CMakeLists.txt) -- kfx_pathfinding is clean
# against both check_layering.py and check_layering_symbols.py, so no
# per-symbol workaround is needed here either. It transitively links
# kfx_config, though, so it needs the same explicit centitoml link
# kfx_config_utest does -- see that target's CMakeLists.txt comment for
# why kfx_common_opts's INTERFACE link to centitoml isn't enough on its
# own (an OBJECT library two INTERFACE hops deep doesn't reliably survive
# into the final link).
file(GLOB KFX_PATHFINDING_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(kfx_pathfinding_utest ${KFX_PATHFINDING_TEST_SOURCES})
target_link_libraries(kfx_pathfinding_utest PRIVATE
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
catch_discover_tests(kfx_pathfinding_utest TEST_PREFIX "kfx_pathfinding.")
