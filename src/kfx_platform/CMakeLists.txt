# Stage 3 of docs/refactor/: kfx_platform is the first library physically
# extracted out of src/ (see docs/refactor/stage-03-extract-kfx-platform.md).
# Unlike the other kfx_* components (still grouped in-place inside src/ via
# CMakeLists.txt's kfx_add_component() regex partition), this one owns a
# real directory with its own include/ -- so it globs its own sources
# directly instead of being carved out of KEEPERFX_SOURCES_C/_CXX.
#
# Every kfx_* library is a real STATIC library, not an OBJECT one
# (docs/refactor/todo/remove-symbol-level-layering-residuals.md's "Next
# steps" -- proved out first on just this library before converting the
# rest). Still appended to KFX_OBJECT_LIBS_STD/_HVLOG in the parent scope
# the same way as before (the variable name is now a slight misnomer,
# kept to avoid a repo-wide rename) -- the root CMakeLists.txt reverses
# that list before linking, since a real archive only gets its needed
# members pulled in to satisfy symbols already undefined at that point in
# ld.bfd's single left-to-right pass (unlike OBJECT .o files, which are
# unconditionally included regardless of position). Every kfx_*_utest's
# own hand-written link order already happened to list libraries
# highest-rank-first for the same reason, confirmed by inspection before
# relying on it.

file(GLOB KFX_PLATFORM_SOURCES_C "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
file(GLOB KFX_PLATFORM_SOURCES_CXX "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
# platform/ (brought in from upstream's SDL3 migration -- see git log)
# holds the IPlatform/IWindowSystem OOP abstraction that replaced windows.cpp/
# linux.cpp/cdrom.cpp/steam_api.cpp; not picked up by the plain GLOB above
# since it's one directory deeper.
file(GLOB KFX_PLATFORM_SOURCES_CXX_SUB "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/*.cpp")
list(APPEND KFX_PLATFORM_SOURCES_CXX ${KFX_PLATFORM_SOURCES_CXX_SUB})
# renderer/ (also from upstream's SDL3 migration) is the IRenderer seam
# (RendererManager facade + RendererSoftware backend) that bflib_video.c
# calls into for palette/framebuffer/screenshot/present-frame -- a
# low-level SDL rendering backend with no engine-specific dependencies, so
# it lives alongside kfx_platform's other bflib_video.c-adjacent code
# rather than in kfx_render (which is the higher-ranked 3D engine).
file(GLOB KFX_PLATFORM_SOURCES_CXX_RENDERER "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/*.cpp")
list(APPEND KFX_PLATFORM_SOURCES_CXX ${KFX_PLATFORM_SOURCES_CXX_RENDERER})
# renderer/software/ -- the CPU software-renderer backend's raster
# primitives (bflib_render*.c/bflib_vidraw*.c/SwDrawTarget.c), moved here
# from a flat src/ layout by upstream's software-rendering refactor (see
# git log). One directory deeper than the *.cpp glob above and all .c
# rather than .cpp, so it needs its own glob too.
file(GLOB KFX_PLATFORM_SOURCES_C_RENDERER_SW "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/software/*.c")
list(APPEND KFX_PLATFORM_SOURCES_C ${KFX_PLATFORM_SOURCES_C_RENDERER_SW})

# Platform-exclusive exceptions -- PlatformWindows.cpp/PlatformLinux.cpp
# are each only buildable/relevant for their own OS (matches upstream's
# BuildTargets.cmake filter logic).
if(WIN32)
    list(REMOVE_ITEM KFX_PLATFORM_SOURCES_CXX "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/PlatformLinux.cpp")
else()
    list(REMOVE_ITEM KFX_PLATFORM_SOURCES_CXX "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/PlatformWindows.cpp")
endif()

# Window icon (non-Windows only -- the Windows executable gets its icon
# from a .rc resource instead). Embeds res/keeperfx_icon256-24bpp.png as a
# C byte array at configure time; platform/WindowSystemSDL.cpp loads it via
# SDL3_image and applies it with SDL_SetWindowIcon. Generated here (not at
# the top-level executable target) so kfx_platform -- the lowest-ranked
# library -- doesn't reach up to app_entry-level generated code for a
# symbol it's the only consumer of (check_layering_symbols.py --strict).
if(NOT WIN32)
    set(KFX_ICON_PNG "${CMAKE_SOURCE_DIR}/res/keeperfx_icon256-24bpp.png")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${KFX_ICON_PNG}")
    file(READ "${KFX_ICON_PNG}" _icon_hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _icon_bytes "${_icon_hex}")
    set(KFX_ICON_C "${CMAKE_CURRENT_BINARY_DIR}/generated/window_icon.c")
    file(WRITE "${KFX_ICON_C}.in"
        "const unsigned char kfx_window_icon_png[] = {${_icon_bytes}};\n"
        "const unsigned int kfx_window_icon_png_size = sizeof(kfx_window_icon_png);\n")
    configure_file("${KFX_ICON_C}.in" "${KFX_ICON_C}" COPYONLY)
    list(APPEND KFX_PLATFORM_SOURCES_C "${KFX_ICON_C}")
endif()

set(KFX_PLATFORM_SOURCES ${KFX_PLATFORM_SOURCES_C} ${KFX_PLATFORM_SOURCES_CXX})

add_library(kfx_platform STATIC ${KFX_PLATFORM_SOURCES})
target_link_libraries(kfx_platform PRIVATE kfx_common_opts kfx_bfdebug_std)
target_include_directories(kfx_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_library(kfx_platform_hvlog STATIC ${KFX_PLATFORM_SOURCES})
target_link_libraries(kfx_platform_hvlog PRIVATE kfx_common_opts kfx_bfdebug_hvlog)
target_include_directories(kfx_platform_hvlog PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

list(APPEND KFX_OBJECT_LIBS_STD kfx_platform)
set(KFX_OBJECT_LIBS_STD ${KFX_OBJECT_LIBS_STD} PARENT_SCOPE)
list(APPEND KFX_OBJECT_LIBS_HVLOG kfx_platform_hvlog)
set(KFX_OBJECT_LIBS_HVLOG ${KFX_OBJECT_LIBS_HVLOG} PARENT_SCOPE)

# docs/refactor/testing/stage-01-framework-and-scaffold.md: opt-in Catch2
# unit tests, gated the same way as the FetchContent(Catch2) block itself.
if(KFX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
