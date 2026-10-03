# Stage 6 of docs/refactor/: kfx_sim is physically extracted out of src/
# incrementally, one prefix cluster at a time (see
# docs/refactor/stage-06-kfx-sim.md's sequencing section), unlike stages 3/4
# which moved their whole library in one pass. This file globs
# src/kfx_sim/src/ directly, exactly like kfx_platform/kfx_config's own
# CMakeLists.txt -- so later clusters just add more files here via `git mv`
# and shrink their stem list out of the root CMakeLists.txt's
# kfx_add_component(kfx_sim "...") call; no changes needed in this file.

file(GLOB KFX_SIM_SOURCES_C "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
file(GLOB KFX_SIM_SOURCES_CXX "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
set(KFX_SIM_SOURCES ${KFX_SIM_SOURCES_C} ${KFX_SIM_SOURCES_CXX})

add_library(kfx_sim STATIC ${KFX_SIM_SOURCES})
target_link_libraries(kfx_sim PRIVATE kfx_common_opts kfx_log_opts)
target_include_directories(kfx_sim PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

list(APPEND KFX_OBJECT_LIBS kfx_sim)
set(KFX_OBJECT_LIBS ${KFX_OBJECT_LIBS} PARENT_SCOPE)

# docs/refactor/testing/stage-01-framework-and-scaffold.md: opt-in Catch2
# unit tests, gated the same way as the FetchContent(Catch2) block itself.
if(KFX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
