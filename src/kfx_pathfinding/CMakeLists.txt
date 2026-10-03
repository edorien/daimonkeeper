# docs/refactor/stage-06a-ariadne-pathfinding-interface.md: kfx_pathfinding
# is the ariadne wallhug/pathfinding code extracted out of src/kfx_sim,
# mirroring src/kfx_config/CMakeLists.txt's approach exactly (see the
# comment there).

file(GLOB KFX_PATHFINDING_SOURCES_C "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
file(GLOB KFX_PATHFINDING_SOURCES_CXX "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
set(KFX_PATHFINDING_SOURCES ${KFX_PATHFINDING_SOURCES_C} ${KFX_PATHFINDING_SOURCES_CXX})

add_library(kfx_pathfinding STATIC ${KFX_PATHFINDING_SOURCES})
target_link_libraries(kfx_pathfinding PRIVATE kfx_common_opts kfx_log_opts)
target_include_directories(kfx_pathfinding PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

list(APPEND KFX_OBJECT_LIBS kfx_pathfinding)
set(KFX_OBJECT_LIBS ${KFX_OBJECT_LIBS} PARENT_SCOPE)

# docs/refactor/testing/stage-01-framework-and-scaffold.md: opt-in Catch2
# unit tests, gated the same way as the FetchContent(Catch2) block itself.
if(KFX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
