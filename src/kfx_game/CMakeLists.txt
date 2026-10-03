# Stage 9 of docs/refactor/: kfx_game is physically extracted out of
# src/ incrementally, one cluster at a time (see
# docs/refactor/stage-09-kfx-game.md), mirroring stage 6/7/8's
# kfx_sim/kfx_render/kfx_net approach. Created ahead of the
# physical-extraction clusters (stage 9.4) to give kfx_game_state.h/.c a
# real home for the struct Game field-group migration (stage 9.3) -- the
# root CMakeLists.txt's flat-src `kfx_game` component was renamed to
# `kfx_game_remaining` to free up the name for this real library, same
# as kfx_net_remaining/kfx_sim_remaining/kfx_render_remaining before it.
# This file globs src/kfx_game/src/ directly, exactly like kfx_net's own
# CMakeLists.txt -- so later clusters just add more files here via
# `git mv` and shrink their stem list out of kfx_game_remaining; no
# changes needed in this file.

file(GLOB KFX_GAME_SOURCES_C "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
file(GLOB KFX_GAME_SOURCES_CXX "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
set(KFX_GAME_SOURCES ${KFX_GAME_SOURCES_C} ${KFX_GAME_SOURCES_CXX})

add_library(kfx_game STATIC ${KFX_GAME_SOURCES})
target_link_libraries(kfx_game PRIVATE kfx_common_opts kfx_log_opts)
target_include_directories(kfx_game PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

list(APPEND KFX_OBJECT_LIBS kfx_game)
set(KFX_OBJECT_LIBS ${KFX_OBJECT_LIBS} PARENT_SCOPE)

# docs/refactor/testing/stage-01-framework-and-scaffold.md: opt-in Catch2
# unit tests, gated the same way as the FetchContent(Catch2) block itself.
if(KFX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
