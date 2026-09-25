# docs/refactor/testing/stage-01-framework-and-scaffold.md /
# stage-04 (kfx_config, first library in the stage-02 §5 rollout order
# after kfx_platform): links the whole kfx_config OBJECT library directly,
# per stage-01's "Progress" recommendation to try that shape first now
# that docs/refactor/todo/check-layering-symbol-level-blind-spot.md's
# raw-declaration escape hatches are fixed.
#
# centitoml (an OBJECT library, Dependencies.cmake) doesn't reliably
# survive two INTERFACE-library hops into a final link -- the same
# reason root CMakeLists.txt links it directly onto keeperfx/
# keeperfx_hvlog rather than trusting kfx_common_opts's INTERFACE
# chain alone. config_translation.c/value_util.c call toml_parse()
# directly, so this target needs the same explicit link.
# kfx_config_test_paths.h.in -> kfx_config_test_paths.h: gives
# value_util_test.cpp's fixture-file tests an absolute path to
# fixtures/, same idea as kfx_script_test_paths.h.in / ver_defs.h.in.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/kfx_config_test_paths.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/kfx_config_test_paths.h"
    @ONLY)

file(GLOB KFX_CONFIG_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(kfx_config_utest ${KFX_CONFIG_TEST_SOURCES})
target_link_libraries(kfx_config_utest PRIVATE
    kfx_config
    kfx_platform
    kfx_bfdebug_std
    kfx_common_opts
    centitoml
    tinyfiledialogs # same OBJECT-library explicit-link gap as centitoml --
                     # kfx_platform now links bflib_filedialogs.cpp, which
                     # needs tinyfiledialogs's compiled object.
    Catch2::Catch2WithMain)
target_include_directories(kfx_config_utest PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

include(CTest)
include(Catch)
catch_discover_tests(kfx_config_utest TEST_PREFIX "kfx_config.")
