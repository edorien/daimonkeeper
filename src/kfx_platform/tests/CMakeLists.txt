# docs/refactor/testing/stage-01-framework-and-scaffold.md: one Catch2 test
# binary per src/kfx_*/ library. Only ever add_subdirectory()'d when
# KFX_BUILD_TESTS is ON (see ../CMakeLists.txt), so Catch2 and CTest are
# already available by the time this file is processed.
#
# Used to need a kfx_test_main shim library here: kfx_platform's
# PlatformLinux.cpp/PlatformWindows.cpp used to physically own the process
# main()/WinMain(), so every *_utest transitively linking kfx_platform
# couldn't use Catch2::Catch2WithMain (its own `main` would conflict).
# Fixed by docs/refactor/todo/remove-kfxmain-symbol-residual.md: the native
# entry point moved to src/native_entry.cpp (app_entry), so kfx_platform no
# longer defines main() at all -- every *_utest target links plain
# Catch2::Catch2WithMain now.
file(GLOB KFX_PLATFORM_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(kfx_platform_utest ${KFX_PLATFORM_TEST_SOURCES})
target_link_libraries(kfx_platform_utest PRIVATE
    kfx_platform
    kfx_log_opts
    kfx_common_opts
    # No imgui link here any more -- docs/refactor/renderer/
    # 05-imgui-linkage-consolidation.md moved ImGui context/backend
    # ownership (gui/ImGuiContext.cpp) out of kfx_platform into
    # kfx_frontend's gui/FrontendImGui.cpp; nothing left in kfx_platform
    # references an imgui symbol.
    tinyfiledialogs # same OBJECT-library explicit-link gap as centitoml
                    # elsewhere (root CMakeLists.txt) -- kfx_platform now links
                    # bflib_filedialogs.cpp, which needs tinyfiledialogs's
                    # compiled object.
    Catch2::Catch2WithMain)

# Manual GPU verification program (see its header comment) -- deliberately not
# registered with ctest: it needs a real Vulkan device and a window.
add_executable(gpu3d_verify gpu/gpu3d_verify.cpp)
target_link_libraries(gpu3d_verify PRIVATE kfx_platform kfx_log_opts kfx_common_opts tinyfiledialogs)

include(CTest)
include(Catch)
# TEST_PREFIX: catch_discover_tests() otherwise names each ctest entry after
# the bare Catch2 TEST_CASE description alone, with no per-library
# namespacing -- `ctest -R kfx_platform` (the workflow stage-01 documents)
# needs this prefix present to match anything.
catch_discover_tests(kfx_platform_utest TEST_PREFIX "kfx_platform.")
