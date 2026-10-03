# kfx_content's Catch2 tests (the cfgc_* tests, moved from kfx_config's
# test binary in refactor pass 2, S04). Same shape as kfx_pathfinding_utest:
# links the library and everything it sits on, plus the explicit
# centitoml/tinyfiledialogs links every *_utest needs (see
# ../../kfx_config/tests/CMakeLists.txt for why).
# kfx_content_test_paths.h.in -> kfx_content_test_paths.h: the repo root and
# this directory's fixtures/, for the corpus and snapshot tests.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/kfx_content_test_paths.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/kfx_content_test_paths.h"
    @ONLY)

file(GLOB KFX_CONTENT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(kfx_content_utest ${KFX_CONTENT_TEST_SOURCES})
target_link_libraries(kfx_content_utest PRIVATE
    kfx_content
    kfx_config
    kfx_platform
    kfx_log_opts
    kfx_common_opts
    centitoml
    tinyfiledialogs
    Catch2::Catch2WithMain)
target_include_directories(kfx_content_utest PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

include(CTest)
include(Catch)
catch_discover_tests(kfx_content_utest TEST_PREFIX "kfx_content.")
