# Stage 8 of docs/refactor/: kfx_net is physically extracted out of src/
# incrementally, one cluster at a time (see
# docs/refactor/stage-08-kfx-net.md), mirroring stage 6/7's kfx_sim/
# kfx_render approach. Created ahead of the physical-extraction clusters
# (stage 8.4) to give kfx_net_state.h/.c a real home for the struct Game
# field-group migration (stage 8.3) -- the root CMakeLists.txt's flat-src
# `kfx_net` component was renamed to `kfx_net_remaining` to free up the
# name for this real library, same as kfx_sim_remaining/
# kfx_render_remaining before it. This file globs src/kfx_net/src/
# directly, exactly like kfx_sim/kfx_render's own CMakeLists.txt -- so
# later clusters just add more files here via `git mv` and shrink their
# stem list out of kfx_net_remaining; no changes needed in this file.

file(GLOB KFX_NET_SOURCES_C "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
file(GLOB KFX_NET_SOURCES_CXX "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
set(KFX_NET_SOURCES ${KFX_NET_SOURCES_C} ${KFX_NET_SOURCES_CXX})

add_library(kfx_net STATIC ${KFX_NET_SOURCES})
target_link_libraries(kfx_net PRIVATE kfx_common_opts kfx_log_opts)
target_include_directories(kfx_net PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

list(APPEND KFX_OBJECT_LIBS kfx_net)
set(KFX_OBJECT_LIBS ${KFX_OBJECT_LIBS} PARENT_SCOPE)

# docs/refactor/testing/stage-01-framework-and-scaffold.md: opt-in Catch2
# unit tests, gated the same way as the FetchContent(Catch2) block itself.
if(KFX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
