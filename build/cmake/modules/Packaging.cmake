# ---------------------------------------------------------------------------
# Packaging.cmake — CPack configuration for KeeperFX distribution packages
# ---------------------------------------------------------------------------
#
# Produces the same kind of release archive as `make package`, but takes the
# keeperfx binary from the CMake build and the game data from the make data
# pipeline. Usage:
#
#   1. Configure (pass the build number / suffix used for the filename):
#        cmake -S . -B out -G Ninja -DCMAKE_TOOLCHAIN_FILE=build/cmake/toolchains/mingw32.cmake \
#              -DCMAKE_BUILD_TYPE=RelWithDebInfo \
#              -DBUILD_NUMBER=1234 -DPACKAGE_SUFFIX=Alpha
#   2. Build the binary:
#        cmake --build build --target keeperfx
#   3. Assemble the game data (gfx/lang/sfx pipeline) into pkg/:
#        make BUILD_NUMBER=1234 PACKAGE_SUFFIX=Alpha pkg-assemble
#   4. Create the archive:
#        cmake --build build --target package
#
#
# Output: pkg/<slug>-<maj>.<min>.<rel>.<build>-kfx<compat>[-<suffix>].7z
#   e.g. pkg/daimonkeeper-1.0.0.1234-kfx1.4-Alpha.7z (the slug and both
#   versions come from build/make/version.mk)
# ---------------------------------------------------------------------------

set(CPACK_PACKAGE_NAME      "${PRODUCT_SLUG}")
set(CPACK_PACKAGE_VENDOR    "dAImon Keeper contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "dAImon Keeper - based on KeeperFX; requires the original Dungeon Keeper data files")
set(CPACK_PACKAGE_VERSION   "${VER_MAJOR}.${VER_MINOR}.${VER_RELEASE}.${BUILD_NUMBER}")
set(CPACK_PACKAGE_VERSION_MAJOR "${VER_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${VER_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${VER_RELEASE}")

# Archive filename <slug>-<maj>.<min>.<rel>.<build>-kfx<compat>[-<suffix>]: our
# version, and the KeeperFX release whose content it supports.
set(_kfx_pkg_base "${PRODUCT_SLUG}-${VER_MAJOR}.${VER_MINOR}.${VER_RELEASE}.${BUILD_NUMBER}-kfx${KFX_COMPAT_MAJOR}.${KFX_COMPAT_MINOR}")
if(PACKAGE_SUFFIX AND NOT "${PACKAGE_SUFFIX}" STREQUAL "")
    set(CPACK_PACKAGE_FILE_NAME "${_kfx_pkg_base}-${PACKAGE_SUFFIX}")
else()
    set(CPACK_PACKAGE_FILE_NAME "${_kfx_pkg_base}")
endif()

# Place generated archives in the source-tree pkg/ directory
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_SOURCE_DIR}/pkg")

# Flat archive layout (no top-level directory prefix)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_COMPONENT_INCLUDE_TOPLEVEL_DIRECTORY OFF)

# 7Z for the Windows/MinGW target; TGZ otherwise.
if(WIN32 OR MINGW OR CMAKE_CROSSCOMPILING OR CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(CPACK_GENERATOR "7Z")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

# Package only the "runtime"/"gamedata" components defined below -- the same
# two build-cmake.sh/build-package.sh already request via `cmake --install
# ... --component runtime|gamedata`. Without this, CPack's archive generators
# default to a monolithic (non-component) install: CMAKE_INSTALL_COMPONENT is
# left unset for that run, which satisfies every generated install()
# COMPONENT rule's "OR NOT CMAKE_INSTALL_COMPONENT" fallback branch -- not
# just this project's own runtime/gamedata rules, but also every third-party
# FetchContent dependency's (zlib, spng, openal-soft, curl, ...) own
# unrequested install() rules, tagged into the implicit "Unspecified"
# component by their own CMakeLists.txt. Those often resolve to an absolute
# DESTINATION baked in at configure time (e.g. zlib's install() rules land at
# literal /usr/local/lib, /usr/local/include, ...) rather than one relative
# to CPACK_INSTALL_PREFIX, so a monolithic install tries to write straight to
# the host's system directories -- and fails outright wherever that requires
# privileges the build doesn't have. Switching to component-mode packaging
# runs the install once per named component, with CMAKE_INSTALL_COMPONENT
# always set to one of them, so the "Unspecified" rules' fallback branch never
# matches and they're never invoked; ALL_COMPONENTS_IN_ONE keeps the existing
# single flat archive instead of splitting into one archive per component.
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
set(CPACK_COMPONENTS_ALL runtime gamedata mcp)

# --- Install rules ---------------------------------------------------------

# COMPONENT runtime tags exactly what build-cmake.sh's `cmake --install
# ... --component runtime` convenience copy (dist/<platform>/) should pull
# in. Without a --component filter, `cmake --install` runs *every*
# install() rule in the project graph, including ones SDL3/SDL3_image/
# SDL3_mixer's own CMakeLists.txt register when FetchContent pulls them in
# as subdirectories (headers, cmake config, pkgconfig, docs, wayland
# protocol XML) -- none of which are tagged "runtime", so --component
# runtime skips them.
install(TARGETS keeperfx RUNTIME DESTINATION . COMPONENT runtime)
install(FILES "${CMAKE_BINARY_DIR}/${PRODUCT_SLUG}.map" DESTINATION . OPTIONAL COMPONENT runtime)

# Linux desktop integration: the icons plus a script that writes a per-user
# .desktop entry pointing at wherever this folder ends up (the game runs from
# its own folder, so the entry needs the absolute path -- no static .desktop).
if(NOT WIN32)
    file(GLOB _kfx_desktop_icons "${CMAKE_SOURCE_DIR}/res/${PRODUCT_SLUG}_icon[0-9][0-9][0-9].png")
    install(FILES ${_kfx_desktop_icons} DESTINATION icons COMPONENT runtime)
    install(PROGRAMS "${CMAKE_SOURCE_DIR}/res/linux/install-desktop-entry.sh" DESTINATION . COMPONENT runtime)
endif()

# The game data assembled by "make pkg-assemble" (configs, campaigns, levels,
# language/sound .dat files, SDL3 runtime DLLs, docs). Evaluated at pack time so
# pkg/ is read then, not at configure time. Skips any archive left in pkg/.
# COMPONENT gamedata -- same reasoning as COMPONENT runtime above: lets
# build-package.sh request "runtime" + "gamedata" explicitly and skip
# third-party subprojects' own untagged install() rules, instead of an
# unfiltered `cmake --install` that would pull those in too.
# The LLM/agent bridge (docs/refactor/AI/LLM/, scripts/llm_bridge/README.md): plain stdlib Python, the same on every
# platform, so one rule serves both dist/windows/mcp/ and dist/linux/mcp/. COMPONENT mcp -- its own component, not folded
# into runtime or gamedata, for the same reason those two are split: build-cmake-linux.sh/build-package.sh request each
# component by name, and an unnamed "Unspecified" rule would also catch every third-party subproject's own install()
# rules (see the COMPONENT runtime/gamedata comments above). Excludes the offline test modules (test_*.py) and any
# __pycache__ -- dev-only, not something a person running the bridge against their own game needs -- and any
# experience.sqlite (the agent's game records and lessons, which the bridge keeps next to itself): a developer's own
# must never ship, nor overwrite the one in an install the bridge has been learning in.
install(DIRECTORY "${CMAKE_SOURCE_DIR}/scripts/llm_bridge/"
    DESTINATION mcp
    COMPONENT mcp
    PATTERN "test_*" EXCLUDE
    REGEX "__pycache__" EXCLUDE
    REGEX "experience\\.sqlite" EXCLUDE
)

# pkg/ and dist/<os>/ are never wiped between runs, so names earlier builds staged and no
# longer do -- KeeperFX's keeperfx.cfg/keeperfx_readme.txt from before the rename, the
# reverted fxdata-<slug>/ data folder -- are skipped here and removed from the install
# folder, instead of shipping next to their replacements.
install(CODE "
    set(_pkg_src \"${CMAKE_SOURCE_DIR}/pkg\")
    set(_pkg_obsolete \"^(keeperfx\\\\.cfg|keeperfx_readme\\\\.txt|fxdata-[^/]+/.*)\$\")
    file(REMOVE_RECURSE
        \"\${CMAKE_INSTALL_PREFIX}/keeperfx.cfg\"
        \"\${CMAKE_INSTALL_PREFIX}/keeperfx_readme.txt\"
        \"\${CMAKE_INSTALL_PREFIX}/fxdata-${PRODUCT_SLUG}\")
    if(EXISTS \"\${_pkg_src}\")
        file(GLOB_RECURSE _pkg_files
            LIST_DIRECTORIES false
            RELATIVE \"\${_pkg_src}\"
            \"\${_pkg_src}/*\")
        foreach(_f IN LISTS _pkg_files)
            if(_f MATCHES \"\${_pkg_obsolete}\")
                continue()
            endif()
            if(NOT _f MATCHES \"(keeperfx|${PRODUCT_SLUG}).*\\\\.(7z|tar\\\\.gz|tgz)\$\")
                get_filename_component(_dir \"\${_f}\" DIRECTORY)
                file(MAKE_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}/\${_dir}\")
                file(COPY \"\${_pkg_src}/\${_f}\"
                    DESTINATION \"\${CMAKE_INSTALL_PREFIX}/\${_dir}\")
            endif()
        endforeach()
    endif()
" COMPONENT gamedata)

include(CPack)
