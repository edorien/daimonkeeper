# Stage 7 of docs/refactor/: kfx_render is physically extracted out of
# src/ incrementally, one cluster at a time (see
# docs/refactor/stage-07-kfx-render.md), mirroring stage 6's kfx_sim
# approach. This file globs src/kfx_render/src/ directly, exactly like
# kfx_sim's own CMakeLists.txt -- so later clusters just add more files
# here via `git mv` and shrink their stem list out of the root
# CMakeLists.txt's kfx_add_component(kfx_render_remaining "...") call; no
# changes needed in this file.

file(GLOB KFX_RENDER_SOURCES_C "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
file(GLOB KFX_RENDER_SOURCES_CXX "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
set(KFX_RENDER_SOURCES ${KFX_RENDER_SOURCES_C} ${KFX_RENDER_SOURCES_CXX})

add_library(kfx_render STATIC ${KFX_RENDER_SOURCES})
target_link_libraries(kfx_render PRIVATE kfx_common_opts kfx_log_opts)
target_include_directories(kfx_render PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

list(APPEND KFX_OBJECT_LIBS kfx_render)
set(KFX_OBJECT_LIBS ${KFX_OBJECT_LIBS} PARENT_SCOPE)

# docs/refactor/testing/stage-01-framework-and-scaffold.md: opt-in Catch2
# unit tests, gated the same way as the FetchContent(Catch2) block itself.
if(KFX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
