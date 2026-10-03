# kfx_content -- the engine-decoupled config content layer (cfgc_*): reads,
# edits, validates and writes config documents for the content editors
# (kfx_editor) without touching live game state. Split out of kfx_config in
# refactor pass 2 (docs/refactor-pass2/stage-04-content-library.md), so
# kfx_config is back to the game's own config loaders plus the callback
# interfaces. Ranked directly above kfx_config: it only includes kfx_config
# and kfx_platform headers.

file(GLOB KFX_CONTENT_SOURCES_C "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
file(GLOB KFX_CONTENT_SOURCES_CXX "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
set(KFX_CONTENT_SOURCES ${KFX_CONTENT_SOURCES_C} ${KFX_CONTENT_SOURCES_CXX})

add_library(kfx_content STATIC ${KFX_CONTENT_SOURCES})
target_link_libraries(kfx_content PRIVATE kfx_common_opts kfx_log_opts)
target_include_directories(kfx_content PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

list(APPEND KFX_OBJECT_LIBS kfx_content)
set(KFX_OBJECT_LIBS ${KFX_OBJECT_LIBS} PARENT_SCOPE)

# docs/refactor/testing/stage-01-framework-and-scaffold.md: opt-in Catch2
# unit tests, gated the same way as the FetchContent(Catch2) block itself.
if(KFX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
