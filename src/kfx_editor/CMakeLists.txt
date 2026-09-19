# docs/refactor/editor/00-overview.md/01-entry-and-editor-session.md --
# the in-game level editor's own internal OBJECT library. Ranked highest
# of all src/kfx_*/ libraries (scripts/check_layering.py's LIBRARY_ORDER,
# immediately before app_entry) -- it may #include anything below it;
# nothing below #includes it back. Same glob-based, compiled-twice
# (std/hvlog) shape as every sibling src/kfx_*/CMakeLists.txt (see e.g.
# src/kfx_script/CMakeLists.txt).

file(GLOB KFX_EDITOR_SOURCES_C "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
file(GLOB KFX_EDITOR_SOURCES_CXX "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
set(KFX_EDITOR_SOURCES ${KFX_EDITOR_SOURCES_C} ${KFX_EDITOR_SOURCES_CXX})

add_library(kfx_editor STATIC ${KFX_EDITOR_SOURCES})
target_link_libraries(kfx_editor PRIVATE kfx_common_opts kfx_bfdebug_std imgui_color_text_edit)
target_include_directories(kfx_editor PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_library(kfx_editor_hvlog STATIC ${KFX_EDITOR_SOURCES})
target_link_libraries(kfx_editor_hvlog PRIVATE kfx_common_opts kfx_bfdebug_hvlog imgui_color_text_edit)
target_include_directories(kfx_editor_hvlog PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

list(APPEND KFX_OBJECT_LIBS_STD kfx_editor)
set(KFX_OBJECT_LIBS_STD ${KFX_OBJECT_LIBS_STD} PARENT_SCOPE)
list(APPEND KFX_OBJECT_LIBS_HVLOG kfx_editor_hvlog)
set(KFX_OBJECT_LIBS_HVLOG ${KFX_OBJECT_LIBS_HVLOG} PARENT_SCOPE)

# docs/refactor/testing/stage-01-framework-and-scaffold.md: opt-in Catch2
# unit tests, gated the same way as the FetchContent(Catch2) block itself.
if(KFX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
