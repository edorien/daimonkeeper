# Stage 11 of docs/refactor/: kfx_script is physically extracted out of
# src/ in one pass (see docs/refactor/stage-11-kfx-script.md), mirroring
# kfx_platform/kfx_config's single-pass approach (stages 3/4) rather than
# kfx_sim/kfx_render/kfx_net/kfx_game/kfx_frontend's incremental-cluster
# one -- unlike those, this cluster needs no struct-Game field migration
# (kfx_script is allowed to depend on everything below it; "no upward
# includes" isn't the right test for this layer per the stage doc), so
# there's no reason to stage it. This file globs src/kfx_script/src/
# directly, same shape as every other library's CMakeLists.txt.

file(GLOB KFX_SCRIPT_SOURCES_C "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
file(GLOB KFX_SCRIPT_SOURCES_CXX "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
set(KFX_SCRIPT_SOURCES ${KFX_SCRIPT_SOURCES_C} ${KFX_SCRIPT_SOURCES_CXX})

add_library(kfx_script STATIC ${KFX_SCRIPT_SOURCES})
target_link_libraries(kfx_script PRIVATE kfx_common_opts kfx_log_opts)
target_include_directories(kfx_script PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

list(APPEND KFX_OBJECT_LIBS kfx_script)
set(KFX_OBJECT_LIBS ${KFX_OBJECT_LIBS} PARENT_SCOPE)

# docs/refactor/testing/stage-01-framework-and-scaffold.md: opt-in Catch2
# unit tests, gated the same way as the FetchContent(Catch2) block itself.
if(KFX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
