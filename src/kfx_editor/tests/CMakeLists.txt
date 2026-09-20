# docs/refactor/testing/00-overview.md: kfx_editor is rank 9, the
# highest-ranked src/kfx_*/ library (immediately below app_entry) -- allowed
# to depend on everything below it, so this links the same full ladder
# kfx_apploop_utest does (src/kfx_apploop/tests/CMakeLists.txt), plus
# kfx_editor itself. Explicit centitoml/imgui links for the same
# transitive-symbol-visibility reason as every other *_utest so far.
#
# Per the "try the plain linked-library shape first" rule: no stub library
# is linked here unless a real build says otherwise.
file(GLOB KFX_EDITOR_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(kfx_editor_utest ${KFX_EDITOR_TEST_SOURCES})
target_link_libraries(kfx_editor_utest PRIVATE
    kfx_editor
    kfx_apploop
    kfx_script
    kfx_frontend
    kfx_game
    kfx_net
    kfx_render
    kfx_sim
    kfx_pathfinding
    kfx_config
    kfx_platform
    kfx_bfdebug_std
    kfx_common_opts
    kfx_luajit
    centitoml
    tinyfiledialogs # same OBJECT-library explicit-link gap as centitoml --
                     # kfx_platform now links bflib_filedialogs.cpp, which
                     # needs tinyfiledialogs's compiled object.
    imgui # kfx_platform's ImGuiContext.cpp (and, transitively, kfx_editor's
          # own ImGui-driven toolbox code) reference imgui symbols -- same
          # two-levels-of-INTERFACE-indirection object-linking gap as
          # centitoml above.
    Catch2::Catch2WithMain)

# editor_script_syntax_test.cpp drives the same TextEditor::Language the
# script editor uses, so it needs the vendored widget's header.
target_include_directories(kfx_editor_utest PRIVATE "${CMAKE_SOURCE_DIR}/deps/ImGuiColorTextEdit")

include(CTest)
include(Catch)
catch_discover_tests(kfx_editor_utest TEST_PREFIX "kfx_editor.")

# editor_lua_support_test.cpp scans the shipped Lua stub files.
target_compile_definitions(kfx_editor_utest PRIVATE KFX_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
