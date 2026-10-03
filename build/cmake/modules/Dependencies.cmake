# Dependencies.cmake - third-party libraries, per platform.
#
# Every dependency except SDL3 is built from source here (FetchContent, or
# ExternalProject_Add for the handful with their own non-CMake build systems:
# ffmpeg's ./configure+make, LuaJIT's hand-written Makefile). SDL3 stays a
# prebuilt devel tarball from upstream libsdl-org releases -- that is not a
# KeeperFX-controlled artifact, unlike the rest, which used to be pinned
# prebuilt static-lib tarballs from the dkfans/kfx-deps release repo (see git
# history / build/make/*.mk for that older approach). The exact upstream
# commit/tag and CMake options each dependency uses below mirror
# dkfans/kfx-deps's own build recipe (its .github/workflows/build.yml), which
# already worked out these versions and flags for the mingw32/lin64 targets;
# this file just runs the same source builds in-tree instead of downloading
# their outputs.
#
# Targets are defined here; kfx_link_dependencies(<target>) links them onto
# the game executables (called from BuildTargets, once they exist).

include(FetchContent)
include(ExternalProject)
include(ProcessorCount)
ProcessorCount(KFX_NPROC)
if(KFX_NPROC EQUAL 0)
    set(KFX_NPROC 4)
endif()

# Silences the CMP0135 dev warning every URL-based FetchContent_Declare/
# ExternalProject_Add below would otherwise print (re-extracting an
# unchanged URL wouldn't re-trigger downstream rebuilds under the OLD
# policy default) -- NEW is what CMake itself recommends.
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# Where every dependency below downloads/builds, deliberately OUTSIDE
# CMAKE_BINARY_DIR (out/<platform>/): several are now compiled from source
# (ffmpeg, curl, openal, luajit included), so deleting/recreating
# out/<platform> -- a common "force a clean rebuild" step -- would
# otherwise force every one of them to re-download and rebuild from
# scratch too, rather than just the game's own object files. Lives under
# deps/ alongside this project's other vendored/fetched third-party
# content (deps/centitoml, deps/imgui); gitignored (.gitignore's
# /deps/.cache-mingw32/ and /deps/.cache-lin64/ entries -- mingw32/lin64 is
# the same platform-tag vocabulary the kfx-deps tarball names this file
# used to pull from already used). Windows and native Linux need separate
# subdirectories: they use different toolchains for the same
# dependencies, same reasoning out/linux/ and out/windows/ can't share a
# CMake build tree.
if(WIN32)
    set(KFX_DEP_CACHE_TAG mingw32)
else()
    set(KFX_DEP_CACHE_TAG lin64)
endif()
set(KFX_DEP_CACHE_DIR "${CMAKE_SOURCE_DIR}/deps/.cache-${KFX_DEP_CACHE_TAG}")
set(FETCHCONTENT_BASE_DIR "${KFX_DEP_CACHE_DIR}/_deps" CACHE PATH "" FORCE)

# kfx_populate(<name> <FetchContent_Populate args...>): wraps
# FetchContent_Populate()'s "inline content details" signature (the
# non-deprecated replacement for declaring via FetchContent_Declare() then
# calling bare FetchContent_Populate(<name>), removed by CMP0169) -- that
# signature does NOT honor FETCHCONTENT_BASE_DIR the way FetchContent_Declare
# + FetchContent_MakeAvailable/Populate(<name>) do; it defaults SOURCE_DIR/
# BINARY_DIR/SUBBUILD_DIR to under CMAKE_CURRENT_BINARY_DIR instead unless
# given explicitly. Passing them here keeps every dependency's source (and
# whatever binary dir it's populated with, even if the caller never uses
# it -- FetchContent_Populate creates it regardless) under KFX_DEP_CACHE_DIR
# regardless of which signature populates it -- a macro, not a function,
# since FetchContent_Populate sets <name>_SOURCE_DIR etc. in the caller's
# scope, which a function() would isolate away.
macro(kfx_populate name)
    FetchContent_Populate(${name}
        SOURCE_DIR   "${KFX_DEP_CACHE_DIR}/_deps/${name}-src"
        BINARY_DIR   "${KFX_DEP_CACHE_DIR}/_deps/${name}-build"
        SUBBUILD_DIR "${KFX_DEP_CACHE_DIR}/_deps/${name}-subbuild"
        ${ARGN})
endmacro()

# kfx_fetch()'s own output dir -- SDL3's prebuilt mingw devel tarballs
# (below) and CMakeLists.txt's lcov fetch both still use kfx_fetch(), so
# this stays general-purpose, not SDL3-specific; same reasoning as
# KFX_DEP_CACHE_DIR just above (survive an out/<platform> deletion).
set(D "${KFX_DEP_CACHE_DIR}/prebuilt")
set(KFX_CENTITOML_SRC "${CMAKE_SOURCE_DIR}/deps/centitoml")

# kfx_fetch(<dir> <url>): download + extract into <builddir>/deps/<dir>/ once.
# Only SDL3's prebuilt mingw devel tarballs still use this.
function(kfx_fetch dir url)
    set(_tgz "${D}/${dir}.tar.gz")
    set(_dest "${D}/${dir}")
    if(NOT EXISTS "${_dest}")
        file(MAKE_DIRECTORY "${_dest}")
        if(NOT EXISTS "${_tgz}")
            message(STATUS "Downloading dep: ${dir}  <-  ${url}")
            file(DOWNLOAD "${url}" "${_tgz}" SHOW_PROGRESS STATUS _st)
            list(GET _st 0 _code)
            if(NOT _code EQUAL 0)
                message(FATAL_ERROR "Failed to download ${url}: ${_st}")
            endif()
        endif()
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf "${_tgz}"
            WORKING_DIRECTORY "${_dest}"
            RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "Failed to extract ${_tgz}")
        endif()
    endif()
endfunction()

# --- centijson (both platforms, always from source): plain JSON library, no
# packaged distro version anywhere. Its own CMakeLists.txt requires CMake
# 3.30 (a project()-level cmake_minimum_required, which would abort configure
# entirely on CI's older apt-packaged CMake) -- sidestepped by not using it at
# all: FetchContent_Populate() only fetches the source, then we compile its 4
# .c files ourselves, same pattern as centitoml just below.
set(KFX_CENTIJSON_COMMIT 8c7a5fb42d9f55044d60592809288164adb4ca95)
kfx_populate(centijson_src
    URL "https://github.com/mity/centijson/archive/${KFX_CENTIJSON_COMMIT}.tar.gz")
add_library(centijson_static STATIC
    "${centijson_src_SOURCE_DIR}/src/json.c"
    "${centijson_src_SOURCE_DIR}/src/json-dom.c"
    "${centijson_src_SOURCE_DIR}/src/json-ptr.c"
    "${centijson_src_SOURCE_DIR}/src/value.c")
target_include_directories(centijson_static PUBLIC "${centijson_src_SOURCE_DIR}/src")

# --- astronomy (both platforms, always from source): cosinekitty/astronomy's
# C library is just one translation unit with no CMake (or any) build system
# of its own -- moonphase.c only calls into source/c/astronomy.c. Pinned to
# the commit dkfans/kfx-deps' own recipe patches (astronomy-356.patch fixes a
# missing-initializer warning, astronomy-357.patch guards the Windows-only
# GetSystemTimePreciseAsFileTime call, which is unavailable before Windows 8)
# apply against; both are still needed on current upstream master.
set(KFX_ASTRONOMY_COMMIT 865d3da7d8112bbc7911238052c6af4aaf877181)
set(_astro_patch_dir "${KFX_DEP_CACHE_DIR}/astronomy-patches")
file(MAKE_DIRECTORY "${_astro_patch_dir}")
file(DOWNLOAD "https://raw.githubusercontent.com/dkfans/kfx-deps/master/astronomy-356.patch" "${_astro_patch_dir}/356.patch")
file(DOWNLOAD "https://raw.githubusercontent.com/dkfans/kfx-deps/master/astronomy-357.patch" "${_astro_patch_dir}/357.patch")
kfx_populate(astronomy_src
    URL "https://github.com/cosinekitty/astronomy/archive/${KFX_ASTRONOMY_COMMIT}.tar.gz"
    PATCH_COMMAND sh -c "patch -p1 -N -i ${_astro_patch_dir}/356.patch; patch -p1 -N -i ${_astro_patch_dir}/357.patch"
    UPDATE_DISCONNECTED 1)
add_library(astronomy_static STATIC "${astronomy_src_SOURCE_DIR}/source/c/astronomy.c")
target_include_directories(astronomy_static PUBLIC "${astronomy_src_SOURCE_DIR}/source/c")

# --- enet6 (both platforms, always from source): SirLynix's IPv6-capable
# ENet fork. Has its own portable CMakeLists.txt (target `enet6`) -- no need
# for the Makefile/prebuilt-zip route dkfans/kfx-deps uses for its mingw
# artifacts, its CMake build cross-compiles the same way any other FetchContent
# dependency here does. `enet6_static` alias kept so kfx_link_dependencies()
# below and any other reference to the old imported-target name still works.
set(BUILD_SHARED_LIBS_SAVE ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
FetchContent_Declare(enet6_src
    URL "https://github.com/SirLynix/enet6/archive/refs/tags/v6.1.3.tar.gz")
FetchContent_MakeAvailable(enet6_src)
set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVE})
add_library(enet6_static ALIAS enet6)

add_library(centitoml OBJECT "${KFX_CENTITOML_SRC}/toml_api.c")
target_link_libraries(centitoml PUBLIC centijson_static)
target_include_directories(centitoml INTERFACE "${KFX_CENTITOML_SRC}")

# --- tinyfiledialogs (vendored source, same shape as centitoml above): a
# single .c/.h, no build system or GUI-toolkit link dependency of its own
# (deps/tinyfiledialogs/README.md) -- native OS file/folder pickers for the
# editor's Open/Save As dialogs (docs/refactor/editor/phase3/
# 03-slice4-file-dialogs.md), used in place of a free-text path field.
set(KFX_TINYFILEDIALOGS_SRC "${CMAKE_SOURCE_DIR}/deps/tinyfiledialogs")
add_library(tinyfiledialogs OBJECT "${KFX_TINYFILEDIALOGS_SRC}/tinyfiledialogs.c")
target_include_directories(tinyfiledialogs INTERFACE "${KFX_TINYFILEDIALOGS_SRC}")
if(WIN32)
    # Native common-dialog APIs (commdlg.h/shlobj.h) -- ole32 is already
    # part of kfx_win32_static_deps below, comdlg32/shell32 aren't yet.
    target_link_libraries(tinyfiledialogs INTERFACE comdlg32 shell32)
endif()

if(WIN32)
    # --- SDL3 (prebuilt MinGW dev tarballs; each wraps <name>-<ver>/i686-w64-mingw32/*)
    # SDL_net is intentionally absent: api.c now uses a native Winsock socket
    # layer (SDL3_net is not reliably packaged). See docs SDL3-MIGRATION notes.
    # Unlike everything else in this file, SDL3's tarballs come straight from
    # upstream libsdl-org releases, not a KeeperFX-controlled build -- there is
    # no "from source" conversion to make here.
    set(SDL3_VER      3.4.12)
    set(SDL3_MIX_VER  3.2.4)
    set(SDL3_IMG_VER  3.4.4)

    kfx_fetch(sdl3       "https://github.com/libsdl-org/SDL/releases/download/release-${SDL3_VER}/SDL3-devel-${SDL3_VER}-mingw.tar.gz")
    kfx_fetch(sdl3_mixer "https://github.com/libsdl-org/SDL_mixer/releases/download/release-${SDL3_MIX_VER}/SDL3_mixer-devel-${SDL3_MIX_VER}-mingw.tar.gz")
    kfx_fetch(sdl3_image "https://github.com/libsdl-org/SDL_image/releases/download/release-${SDL3_IMG_VER}/SDL3_image-devel-${SDL3_IMG_VER}-mingw.tar.gz")

    set(SDL3_PREFIX      "${D}/sdl3/SDL3-${SDL3_VER}/i686-w64-mingw32")
    set(SDL3_MIX_PREFIX  "${D}/sdl3_mixer/SDL3_mixer-${SDL3_MIX_VER}/i686-w64-mingw32")
    set(SDL3_IMG_PREFIX  "${D}/sdl3_image/SDL3_image-${SDL3_IMG_VER}/i686-w64-mingw32")

    add_library(kfx_sdl3 INTERFACE)
    # SDL3 headers live under include/SDL3/*.h; code uses <SDL3/SDL.h>,
    # <SDL3_mixer/SDL_mixer.h>, <SDL3_image/SDL_image.h>.
    target_include_directories(kfx_sdl3 INTERFACE
        "${SDL3_PREFIX}/include"
        "${SDL3_MIX_PREFIX}/include"
        "${SDL3_IMG_PREFIX}/include")
    target_link_libraries(kfx_sdl3 INTERFACE
        "${SDL3_PREFIX}/lib/libSDL3.dll.a"
        "${SDL3_MIX_PREFIX}/lib/libSDL3_mixer.dll.a"
        "${SDL3_IMG_PREFIX}/lib/libSDL3_image.dll.a")

    # SDL3 links dynamically, so ship its runtime DLLs (CPack picks these up).
    # COMPONENT runtime -- see Packaging.cmake's keeperfx install(TARGETS ...)
    # comment for why.
    install(FILES
        "${SDL3_PREFIX}/bin/SDL3.dll"
        "${SDL3_MIX_PREFIX}/bin/SDL3_mixer.dll"
        "${SDL3_IMG_PREFIX}/bin/SDL3_image.dll"
        DESTINATION . COMPONENT runtime)

    # --- zlib + minizip: zlib's own CMake build produces `zlib` (shared) and
    # `zlibstatic`. The OUTPUT_NAME-to-"z" rename its CMakeLists applies only
    # fires under `if(UNIX)`, which mingw's CMAKE_SYSTEM_NAME=Windows doesn't
    # satisfy -- force the same name here so the rest of this file (and the
    # link list below) can refer to one name on both platforms. zconf.h is
    # generated into the build dir by a configure_file() at configure time
    # (not a build step), so copying it next to zlib.h in the source dir
    # works immediately, same trick dkfans/kfx-deps' own CI uses.
    #
    # minizip has no CMake build of its own upstream (it's two files inside
    # zlib's source tree) -- compile ioapi.c/unzip.c directly, same as the
    # native-Linux fallback below. <minizip/unzip.h> resolves because
    # contrib/minizip *is* a directory literally named "minizip".
    set(KFX_ZLIB_VER 1.3.1)
    FetchContent_Declare(zlib_src
        URL "https://github.com/madler/zlib/releases/download/v${KFX_ZLIB_VER}/zlib-${KFX_ZLIB_VER}.tar.gz")
    FetchContent_MakeAvailable(zlib_src)
    set_target_properties(zlibstatic PROPERTIES OUTPUT_NAME z)
    target_include_directories(zlibstatic PUBLIC "${zlib_src_SOURCE_DIR}" "${zlib_src_BINARY_DIR}")
    file(COPY "${zlib_src_BINARY_DIR}/zconf.h" DESTINATION "${zlib_src_SOURCE_DIR}")
    add_library(zlib_static ALIAS zlibstatic)

    add_library(minizip_static STATIC
        "${zlib_src_SOURCE_DIR}/contrib/minizip/ioapi.c"
        "${zlib_src_SOURCE_DIR}/contrib/minizip/unzip.c")
    target_include_directories(minizip_static PUBLIC "${zlib_src_SOURCE_DIR}/contrib")
    target_link_libraries(minizip_static PUBLIC zlib_static)

    # These two dependents call find_package(ZLIB) internally rather than
    # taking a CMake target -- pre-seed the cache variables FindZLIB.cmake
    # looks for so it resolves to our from-source build instead of searching
    # (and finding nothing, cross-compiling). The path doesn't need to exist
    # yet: find_library()/find_path() trust an already-cached value without
    # re-checking, so this is safe even though libz.a isn't built until
    # later. add_dependencies() below makes sure it exists before it's
    # actually needed at link time.
    set(ZLIB_INCLUDE_DIR "${zlib_src_SOURCE_DIR}" CACHE PATH "" FORCE)
    set(ZLIB_LIBRARY "${zlib_src_BINARY_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}z${CMAKE_STATIC_LIBRARY_SUFFIX}" CACHE FILEPATH "" FORCE)

    # --- spng: same libspng source/options as the native-Linux fallback
    # below, minus the "is it already on the system" check (nothing is, when
    # cross-compiling) and with ENABLE_OPT off -- matches dkfans/kfx-deps'
    # own mingw32 recipe, avoiding libspng's x86 SIMD path on a 32-bit target.
    set(ENABLE_OPT     OFF CACHE BOOL "" FORCE)
    set(SPNG_SHARED    OFF CACHE BOOL "" FORCE)
    set(SPNG_STATIC    ON  CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(spng
        URL "https://github.com/randy408/libspng/archive/refs/tags/v0.7.4.tar.gz")
    FetchContent_MakeAvailable(spng)
    add_dependencies(spng_static zlibstatic)

    # --- openal-soft: same version/options as the native-Linux fallback
    # below, plus dkfans/kfx-deps' extra ALSOFT_BACKEND_* set that pins the
    # mingw build to DirectSound (the other backends either don't build under
    # mingw or don't apply to Windows).
    set(LIBTYPE                 STATIC CACHE STRING "" FORCE)
    set(ALSOFT_STATIC_WINPTHREAD ON  CACHE BOOL "" FORCE)
    set(ALSOFT_UTILS            OFF CACHE BOOL "" FORCE)
    set(ALSOFT_EXAMPLES         OFF CACHE BOOL "" FORCE)
    set(ALSOFT_TESTS            OFF CACHE BOOL "" FORCE)
    set(ALSOFT_INSTALL          OFF CACHE BOOL "" FORCE)
    set(ALSOFT_EAX              OFF CACHE BOOL "" FORCE)
    set(ALSOFT_BACKEND_WINMM    OFF CACHE BOOL "" FORCE)
    set(ALSOFT_BACKEND_DSOUND   ON  CACHE BOOL "" FORCE)
    set(ALSOFT_BACKEND_WASAPI   OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(openal_soft
        URL "https://github.com/kcat/openal-soft/releases/download/1.24.3/openal-soft-1.24.3.tar.bz2")
    FetchContent_MakeAvailable(openal_soft)

    # --- LuaJIT: its own hand-written Makefile (bootstraps minilua/buildvm,
    # generates lj_bcdef.h etc.), shelled out to via ExternalProject like the
    # native-Linux fallback below -- but cross-compiling needs the host
    # (minilua/buildvm, which run during the build to generate target code)
    # built 32-bit to match the target's pointer width, hence HOST_CC="gcc
    # -m32" (needs gcc-multilib) alongside the real i686-w64-mingw32 CC.
    kfx_populate(luajit_src
        URL "https://github.com/openresty/luajit2/archive/refs/tags/v2.1-20260724.tar.gz")
    ExternalProject_Add(luajit_build
        PREFIX            "${KFX_DEP_CACHE_DIR}/luajit-prefix"
        SOURCE_DIR        "${luajit_src_SOURCE_DIR}"
        CONFIGURE_COMMAND ""
        BUILD_IN_SOURCE   1
        BUILD_COMMAND     make -j${KFX_NPROC} amalg
                            "HOST_CC=gcc -m32"
                            CC=${CMAKE_C_COMPILER}
                            TARGET_SYS=Windows
                            BUILDMODE=static
        INSTALL_COMMAND   ""
        BUILD_BYPRODUCTS  "${luajit_src_SOURCE_DIR}/src/libluajit.a")
    add_library(luajit_static STATIC IMPORTED GLOBAL)
    set_target_properties(luajit_static PROPERTIES
        IMPORTED_LOCATION             "${luajit_src_SOURCE_DIR}/src/libluajit.a"
        INTERFACE_INCLUDE_DIRECTORIES "${luajit_src_SOURCE_DIR}/src")
    add_dependencies(luajit_static luajit_build)

    # --- miniupnpc / libnatpmp: both have portable CMakeLists.txt of their
    # own (SOURCE_SUBDIR points at miniupnpc/'s for the miniupnp monorepo);
    # each already links ws2_32/iphlpapi itself under `if(WIN32)`, same as
    # the native-Linux fallback below.
    set(UPNPC_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(UPNPC_BUILD_STATIC ON  CACHE BOOL "" FORCE)
    set(UPNPC_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
    set(UPNPC_BUILD_SAMPLE OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(miniupnp_src
        URL "https://github.com/miniupnp/miniupnp/archive/refs/tags/miniupnpc_2_3_3.tar.gz"
        SOURCE_SUBDIR miniupnpc)
    FetchContent_MakeAvailable(miniupnp_src)
    # <miniupnpc/miniupnpc.h> needs a directory literally named "miniupnpc" on
    # the include path -- added to a local wrapper target, not directly onto
    # libminiupnpc-static: that target gets install(EXPORT ...)'d by its own
    # CMakeLists.txt, and CMake refuses a build-directory path in an exported
    # target's INTERFACE_INCLUDE_DIRECTORIES.
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/miniupnpc_shim")
    file(CREATE_LINK "${miniupnp_src_SOURCE_DIR}/miniupnpc/include" "${CMAKE_BINARY_DIR}/miniupnpc_shim/miniupnpc" SYMBOLIC)
    add_library(kfx_miniupnpc_shim INTERFACE)
    target_include_directories(kfx_miniupnpc_shim INTERFACE "${CMAKE_BINARY_DIR}/miniupnpc_shim")

    kfx_populate(natpmp_src
        URL "https://github.com/miniupnp/libnatpmp/archive/134fc89e2781e154e40042641f4d8bcbe42579f1.tar.gz")
    set(BUILD_SHARED_LIBS OFF)
    add_subdirectory("${natpmp_src_SOURCE_DIR}" "${natpmp_src_BINARY_DIR}" EXCLUDE_FROM_ALL)
    # net_portforward.cpp #includes <natpmp/natpmp.h> on Windows specifically
    # (the old kfx-deps mingw32 tarball packaged it under include/natpmp/) --
    # natpmp's own source tree has natpmp.h at its root with no such
    # subdirectory, so symlink one in, same shim trick as miniupnpc above.
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/natpmp_shim")
    file(CREATE_LINK "${natpmp_src_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/natpmp_shim/natpmp" SYMBOLIC)
    add_library(kfx_natpmp_shim INTERFACE)
    target_include_directories(kfx_natpmp_shim INTERFACE "${CMAKE_BINARY_DIR}/natpmp_shim")

    # --- libcurl: CURL_USE_SCHANNEL uses Windows' native TLS stack, so no
    # OpenSSL build is needed here (matching this target's existing link
    # list of Windows system libs below: wldap32/crypt32/secur32/bcrypt are
    # what Schannel + WinLDAP need).
    set(BUILD_CURL_EXE     OFF CACHE BOOL   "" FORCE)
    set(BUILD_TESTING      OFF CACHE BOOL   "" FORCE)
    set(CURL_USE_SCHANNEL  ON  CACHE BOOL   "" FORCE)
    set(CURL_USE_LIBPSL    OFF CACHE BOOL   "" FORCE)
    set(BUILD_SHARED_LIBS  OFF CACHE BOOL   "" FORCE)
    FetchContent_Declare(curl_src
        URL "https://github.com/curl/curl/archive/refs/tags/curl-8_22_0.tar.gz")
    FetchContent_MakeAvailable(curl_src)
    add_dependencies(libcurl_static zlibstatic)
    target_compile_definitions(libcurl_static PUBLIC CURL_STATICLIB)

    # --- ffmpeg: same minimal --disable-everything build as the native-Linux
    # fallback below (see its comment for why: bflib_fmvids.cpp only ever
    # decodes KeeperFX's own bundled .smk cutscenes), cross-compiled via
    # --arch/--target-os/--cross-prefix instead of building for the host.
    set(FFMPEG_INSTALL_DIR "${KFX_DEP_CACHE_DIR}/ffmpeg-install")
    file(MAKE_DIRECTORY "${FFMPEG_INSTALL_DIR}/include")
    ExternalProject_Add(ffmpeg_build
        URL "https://ffmpeg.org/releases/ffmpeg-7.1.tar.xz"
        PREFIX "${KFX_DEP_CACHE_DIR}/ffmpeg"
        CONFIGURE_COMMAND <SOURCE_DIR>/configure
            --prefix=${FFMPEG_INSTALL_DIR}
            --disable-shared --enable-static
            --disable-programs --disable-doc --disable-avdevice --disable-postproc
            --disable-network --disable-x86asm
            --disable-avfilter --disable-swscale
            --disable-everything --disable-autodetect
            --enable-avformat --enable-avcodec --enable-avutil --enable-swresample
            --enable-demuxer=smacker
            --enable-decoder=smacker,smackaud
            --enable-protocol=file
            --arch=i686 --target-os=mingw32
            --cross-prefix=i686-w64-mingw32- --enable-cross-compile
        BUILD_COMMAND make -j${KFX_NPROC}
        INSTALL_COMMAND make install
        BUILD_BYPRODUCTS
            "${FFMPEG_INSTALL_DIR}/lib/libavformat.a"
            "${FFMPEG_INSTALL_DIR}/lib/libavcodec.a"
            "${FFMPEG_INSTALL_DIR}/lib/libavutil.a"
            "${FFMPEG_INSTALL_DIR}/lib/libswresample.a"
    )
    foreach(_lib avformat avcodec avutil swresample)
        add_library(kfx_ffmpeg_${_lib} STATIC IMPORTED GLOBAL)
        set_target_properties(kfx_ffmpeg_${_lib} PROPERTIES
            IMPORTED_LOCATION "${FFMPEG_INSTALL_DIR}/lib/lib${_lib}.a"
            INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INSTALL_DIR}/include")
        add_dependencies(kfx_ffmpeg_${_lib} ffmpeg_build)
    endforeach()

    # Static archives have circular refs (curl<->zlib, ffmpeg internals), so
    # link them in a group (RESCAN == --start-group/--end-group).
    set(_static
        kfx_ffmpeg_avformat kfx_ffmpeg_avcodec kfx_ffmpeg_swresample kfx_ffmpeg_avutil
        OpenAL::OpenAL astronomy_static enet6_static libminiupnpc-static natpmp
        libcurl_static spng_static centijson_static minizip_static zlib_static
        luajit_static)
    add_library(kfx_win32_static_deps INTERFACE)
    target_link_libraries(kfx_win32_static_deps INTERFACE
        "$<LINK_GROUP:RESCAN,${_static}>"
        kfx_miniupnpc_shim kfx_natpmp_shim
        ws2_32 winmm ole32 uuid iphlpapi wldap32 crypt32 secur32 bcrypt)

else()
    find_package(PkgConfig REQUIRED)

    add_library(kfx_sdl3 INTERFACE)
    pkg_check_modules(SDL3       IMPORTED_TARGET sdl3)
    pkg_check_modules(SDL3_image IMPORTED_TARGET sdl3-image)
    if(NOT SDL3_image_FOUND)
        pkg_check_modules(SDL3_image IMPORTED_TARGET SDL3_image)
    endif()
    pkg_check_modules(SDL3_mixer IMPORTED_TARGET sdl3-mixer)
    if(NOT SDL3_mixer_FOUND)
        pkg_check_modules(SDL3_mixer IMPORTED_TARGET SDL3_mixer)
    endif()
    if(SDL3_FOUND AND SDL3_image_FOUND AND SDL3_mixer_FOUND)
        message(STATUS "SDL3: using system libraries (pkg-config)")
        target_link_libraries(kfx_sdl3 INTERFACE
            PkgConfig::SDL3 PkgConfig::SDL3_image PkgConfig::SDL3_mixer)
    else()
        message(STATUS "SDL3: system libraries not found; building from source (FetchContent)")
        set(SDL3_VER      3.4.12)
        set(SDL3_MIX_VER  3.2.4)
        set(SDL3_IMG_VER  3.4.4)
        # Shared libs, no tests/examples. Use system decoder libraries rather than
        # vendored ones: the release source tarballs do not bundle the external/
        # decoder submodules, so VENDORED would fail. Distros that hit this path
        # need libpng + the ogg/vorbis/flac/mpg123 -dev packages (see CI).
        set(SDL_TEST_LIBRARY   OFF CACHE BOOL "" FORCE)
        set(SDL_EXAMPLES       OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_SAMPLES   OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_VENDORED  OFF CACHE BOOL "" FORCE)
        set(SDLMIXER_SAMPLES   OFF CACHE BOOL "" FORCE)
        set(SDLMIXER_VENDORED  OFF CACHE BOOL "" FORCE)
        # KeeperFX already bundles its own dr_mp3.h implementation (deps/dr_mp3.h,
        # used by bflib_sndlib.cpp) -- SDL_mixer's DRMP3 backend bundles a second
        # copy, and having both DR_MP3_IMPLEMENTATION TUs link into one executable
        # causes "multiple definition of `drmp3_*`" link errors. MP3 support stays
        # available via SDL_mixer's MPG123 backend (still default ON), which links
        # the system libmpg123 -- already an expected -dev dependency on this path.
        set(SDLMIXER_MP3_DRMP3 OFF CACHE BOOL "" FORCE)
        # Wayland-only: most distros have dropped X11 by default, and building
        # the X11 backend in pulls a chain of X11 dev headers (Xcursor, Xrandr,
        # Xfixes, ...) for a session type fewer and fewer users actually run.
        # SDL3 still auto-detects Wayland vs X11 at runtime; this only removes
        # the X11 backend from the build, so a build here can no longer fall
        # back to XWayland/X11 sessions.
        set(SDL_X11            OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(SDL3
            URL "https://github.com/libsdl-org/SDL/releases/download/release-${SDL3_VER}/SDL3-${SDL3_VER}.tar.gz")
        FetchContent_Declare(SDL3_image
            URL "https://github.com/libsdl-org/SDL_image/releases/download/release-${SDL3_IMG_VER}/SDL3_image-${SDL3_IMG_VER}.tar.gz")
        FetchContent_Declare(SDL3_mixer
            URL "https://github.com/libsdl-org/SDL_mixer/releases/download/release-${SDL3_MIX_VER}/SDL3_mixer-${SDL3_MIX_VER}.tar.gz")
        FetchContent_MakeAvailable(SDL3 SDL3_image SDL3_mixer)
        target_link_libraries(kfx_sdl3 INTERFACE
            SDL3::SDL3 SDL3_image::SDL3_image SDL3_mixer::SDL3_mixer)

        # Built from source, so not on the system's library path -- ship the
        # .so files next to the installed binary (mirrors the WIN32 branch's
        # SDL3*.dll install(FILES ...) above). Matches keeperfx's
        # INSTALL_RPATH "$ORIGIN" (CMakeLists.txt).
        #
        # install(CODE ...) + a glob at install time (not install(DIRECTORY
        # ... FILES_MATCHING)): the .so files don't exist yet at configure
        # time (built later, during `cmake --build`), and install(DIRECTORY
        # ... FILES_MATCHING) recreates every subdirectory it recurses
        # through even when nothing inside matches -- these binary dirs also
        # hold CMakeFiles/, generated headers, docs, wayland protocol XML,
        # etc., which then showed up as empty clutter alongside the libs.
        # Only the top-level *.so* files are the actual runtime libraries.
        install(CODE "
            file(GLOB _kfx_sdl3_runtime_libs
                \"${sdl3_BINARY_DIR}/*.so*\"
                \"${sdl3_image_BINARY_DIR}/*.so*\"
                \"${sdl3_mixer_BINARY_DIR}/*.so*\")
            file(INSTALL \${_kfx_sdl3_runtime_libs} DESTINATION \"\${CMAKE_INSTALL_PREFIX}\")
        " COMPONENT runtime)
    endif()

    # ffmpeg: always built from source here, unlike openal/spng/miniupnpc/
    # natpmp below -- a system ffmpeg is the problem, present or not. Distro
    # ffmpeg packages are built with every optional codec/protocol/font-
    # rendering feature enabled, dynamically linking 100+ transitive shared
    # libraries (X11, cairo, pango, fontconfig, Kerberos, video codecs
    # nothing here uses, ...), none of which build-package.sh's dist/
    # install bundles -- making a keeperfx linked against system ffmpeg
    # non-portable to any machine that doesn't happen to already have a
    # closely-compatible ffmpeg (and its own huge dependency chain)
    # installed. bflib_fmvids.cpp (the only ffmpeg caller, see its
    # #includes) only ever plays KeeperFX's own bundled .smk (Smacker)
    # cutscenes via avformat/avcodec/avutil/swresample -- no swscale, no
    # other container/codec -- so a minimal --disable-everything static
    # build covers the actual need with zero runtime library dependencies
    # of its own, fully solving the portability gap rather than just
    # matching what upstream's Makefile happened to do.
    #
    # ExternalProject_Add, not FetchContent: ffmpeg's build is its own
    # hand-written ./configure + make, not CMake, so there's no
    # add_subdirectory() to pull in the way SDL3/openal/spng do.
    set(FFMPEG_INSTALL_DIR "${KFX_DEP_CACHE_DIR}/ffmpeg-install")
    file(MAKE_DIRECTORY "${FFMPEG_INSTALL_DIR}/include")
    ExternalProject_Add(ffmpeg_build
        URL "https://ffmpeg.org/releases/ffmpeg-7.1.tar.xz"
        PREFIX "${KFX_DEP_CACHE_DIR}/ffmpeg"
        CONFIGURE_COMMAND <SOURCE_DIR>/configure
            --prefix=${FFMPEG_INSTALL_DIR}
            --disable-shared --enable-static --enable-pic
            --disable-programs --disable-doc --disable-avdevice --disable-postproc
            --disable-network --disable-x86asm
            --disable-avfilter --disable-swscale
            --disable-everything --disable-autodetect
            --enable-avformat --enable-avcodec --enable-avutil --enable-swresample
            --enable-demuxer=smacker
            --enable-decoder=smacker,smackaud
            --enable-protocol=file
        BUILD_COMMAND make -j${KFX_NPROC}
        INSTALL_COMMAND make install
        BUILD_BYPRODUCTS
            "${FFMPEG_INSTALL_DIR}/lib/libavformat.a"
            "${FFMPEG_INSTALL_DIR}/lib/libavcodec.a"
            "${FFMPEG_INSTALL_DIR}/lib/libavutil.a"
            "${FFMPEG_INSTALL_DIR}/lib/libswresample.a"
    )
    foreach(_lib avformat avcodec avutil swresample)
        add_library(kfx_ffmpeg_${_lib} STATIC IMPORTED GLOBAL)
        set_target_properties(kfx_ffmpeg_${_lib} PROPERTIES
            IMPORTED_LOCATION "${FFMPEG_INSTALL_DIR}/lib/lib${_lib}.a"
            INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INSTALL_DIR}/include")
        add_dependencies(kfx_ffmpeg_${_lib} ffmpeg_build)
    endforeach()
    # avformat/avcodec/avutil/swresample have circular internal refs (same
    # reasoning as the Windows static-archive RESCAN group above) -- link as
    # a group so ld.bfd's single-pass symbol resolution doesn't matter.
    add_library(kfx_ffmpeg INTERFACE)
    target_link_libraries(kfx_ffmpeg INTERFACE
        "$<LINK_GROUP:RESCAN,kfx_ffmpeg_avformat,kfx_ffmpeg_avcodec,kfx_ffmpeg_swresample,kfx_ffmpeg_avutil>"
        m pthread)

    pkg_check_modules(ZLIB       REQUIRED IMPORTED_TARGET zlib)

    # --- openal / luajit / spng / minizip / miniupnpc / natpmp / curl:
    # system pkg-config first, else build from source (FetchContent), same
    # two-tier shape as SDL3 above. Each gets its own kfx_<name> INTERFACE
    # target so kfx_link_dependencies() below doesn't care which path was
    # taken. These previously were plain `pkg_check_modules(... REQUIRED
    # ...)` with no fallback (miniupnpc/natpmp had no check at all -- bare
    # linker names, silently assuming the system already had them), despite
    # the stage-13-era claim that this file "already covers native Linux
    # builds itself" -- it only did for SDL3. Fixed here so `KFX_OS=linux
    # ./build-cmake.sh` doesn't require ~20 apt `-dev` packages that aren't
    # actually needed to produce a working binary.

    # openal: OpenAL-soft has a normal CMake build.
    pkg_check_modules(OPENAL IMPORTED_TARGET openal)
    add_library(kfx_openal INTERFACE)
    if(OPENAL_FOUND)
        message(STATUS "openal: using system library (pkg-config)")
        target_link_libraries(kfx_openal INTERFACE PkgConfig::OPENAL)
    else()
        message(STATUS "openal: system library not found; building from source (FetchContent)")
        set(LIBTYPE        STATIC CACHE STRING "" FORCE)
        set(ALSOFT_UTILS    OFF CACHE BOOL "" FORCE)
        set(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(ALSOFT_TESTS    OFF CACHE BOOL "" FORCE)
        set(ALSOFT_INSTALL  OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(openal_soft
            URL "https://github.com/kcat/openal-soft/releases/download/1.24.3/openal-soft-1.24.3.tar.bz2")
        FetchContent_MakeAvailable(openal_soft)
        target_link_libraries(kfx_openal INTERFACE OpenAL::OpenAL)
    endif()

    # spng: libspng has a normal CMake build.
    pkg_check_modules(SPNG IMPORTED_TARGET spng)
    add_library(kfx_spng INTERFACE)
    if(SPNG_FOUND)
        message(STATUS "spng: using system library (pkg-config)")
        target_link_libraries(kfx_spng INTERFACE PkgConfig::SPNG)
    else()
        message(STATUS "spng: system library not found; building from source (FetchContent)")
        set(SPNG_SHARED    OFF CACHE BOOL "" FORCE)
        set(SPNG_STATIC    ON  CACHE BOOL "" FORCE)
        set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(spng
            URL "https://github.com/randy408/libspng/archive/refs/tags/v0.7.4.tar.gz")
        FetchContent_MakeAvailable(spng)
        target_link_libraries(kfx_spng INTERFACE spng_static)
    endif()

    # minizip: the classic zlib contrib/minizip API (<minizip/unzip.h>,
    # unzOpen/unzGoToFilePos64/... -- matches what custom_zip.c/
    # custom_sprites.c actually call and how Debian's libminizip-dev lays
    # out headers), not minizip-ng's different API. No CMake build of its
    # own upstream (it's two files inside zlib's source tree) -- compile
    # them directly against the system zlib already found above. Nothing
    # in this codebase writes zips, so zip.c is intentionally omitted.
    pkg_check_modules(MINIZIP IMPORTED_TARGET minizip)
    add_library(kfx_minizip INTERFACE)
    if(MINIZIP_FOUND)
        message(STATUS "minizip: using system library (pkg-config)")
        target_link_libraries(kfx_minizip INTERFACE PkgConfig::MINIZIP)
    else()
        message(STATUS "minizip: system library not found; building contrib/minizip from zlib source")
        kfx_populate(zlib_minizip_src
            URL "https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz")
        add_library(minizip_from_source STATIC
            "${zlib_minizip_src_SOURCE_DIR}/contrib/minizip/ioapi.c"
            "${zlib_minizip_src_SOURCE_DIR}/contrib/minizip/unzip.c")
        target_link_libraries(minizip_from_source PUBLIC PkgConfig::ZLIB)
        # <minizip/unzip.h> needs a directory literally named "minizip" on
        # the include path -- contrib/minizip's own folder already has that
        # name, its parent just isn't laid out like an install prefix.
        # Symlink one instead of copying (same trick used for miniupnpc
        # below).
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/minizip_shim")
        file(CREATE_LINK "${zlib_minizip_src_SOURCE_DIR}/contrib/minizip" "${CMAKE_BINARY_DIR}/minizip_shim/minizip" SYMBOLIC)
        target_include_directories(kfx_minizip INTERFACE "${CMAKE_BINARY_DIR}/minizip_shim")
        target_link_libraries(kfx_minizip INTERFACE minizip_from_source)
    endif()

    # luajit: no CMake build upstream at all -- it's a hand-written
    # Makefile (bootstraps its own minilua/buildvm, generates lj_bcdef.h
    # etc.). Shell out to it via ExternalProject rather than reimplementing
    # any of that; import the resulting static lib afterward. `amalg`
    # (amalgamated build) keeps this to one translation unit.
    pkg_check_modules(LUAJIT IMPORTED_TARGET luajit)
    add_library(kfx_luajit INTERFACE)
    if(LUAJIT_FOUND)
        message(STATUS "luajit: using system library (pkg-config)")
        target_link_libraries(kfx_luajit INTERFACE PkgConfig::LUAJIT)
    else()
        message(STATUS "luajit: system library not found; building from source (its own Makefile, via ExternalProject)")
        kfx_populate(luajit_src
            URL "https://github.com/openresty/luajit2/archive/refs/tags/v2.1-20260724.tar.gz")
        ExternalProject_Add(luajit_build
            PREFIX            "${KFX_DEP_CACHE_DIR}/luajit-prefix"
            SOURCE_DIR        "${luajit_src_SOURCE_DIR}"
            CONFIGURE_COMMAND ""
            BUILD_IN_SOURCE   1
            BUILD_COMMAND     make -j${CMAKE_BUILD_PARALLEL_LEVEL} amalg CC=${CMAKE_C_COMPILER} BUILDMODE=static
            INSTALL_COMMAND   ""
            BUILD_BYPRODUCTS  "${luajit_src_SOURCE_DIR}/src/libluajit.a")
        add_library(luajit_from_source STATIC IMPORTED GLOBAL)
        set_target_properties(luajit_from_source PROPERTIES
            IMPORTED_LOCATION             "${luajit_src_SOURCE_DIR}/src/libluajit.a"
            INTERFACE_INCLUDE_DIRECTORIES "${luajit_src_SOURCE_DIR}/src"
            INTERFACE_LINK_LIBRARIES      "m;dl")
        add_dependencies(luajit_from_source luajit_build)
        target_link_libraries(kfx_luajit INTERFACE luajit_from_source)
    endif()

    # miniupnpc: part of the miniupnp monorepo; its miniupnpc/ subdir has
    # its own CMakeLists.txt (SOURCE_SUBDIR points straight at it).
    pkg_check_modules(MINIUPNPC IMPORTED_TARGET miniupnpc)
    add_library(kfx_miniupnpc INTERFACE)
    if(MINIUPNPC_FOUND)
        message(STATUS "miniupnpc: using system library (pkg-config)")
        target_link_libraries(kfx_miniupnpc INTERFACE PkgConfig::MINIUPNPC)
    else()
        message(STATUS "miniupnpc: system library not found; building from source (FetchContent)")
        set(UPNPC_BUILD_SHARED OFF CACHE BOOL "" FORCE)
        set(UPNPC_BUILD_STATIC ON  CACHE BOOL "" FORCE)
        set(UPNPC_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
        set(UPNPC_BUILD_SAMPLE OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(miniupnp_src
            URL "https://github.com/miniupnp/miniupnp/archive/refs/tags/miniupnpc_2_3_3.tar.gz"
            SOURCE_SUBDIR miniupnpc)
        FetchContent_MakeAvailable(miniupnp_src)
        # <miniupnpc/miniupnpc.h> needs a directory literally named
        # "miniupnpc" on the include path; upstream's own generated
        # include/ dir (a flat copy for its in-tree consumers) isn't named
        # that -- symlink one.
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/miniupnpc_shim")
        file(CREATE_LINK "${miniupnp_src_SOURCE_DIR}/miniupnpc/include" "${CMAKE_BINARY_DIR}/miniupnpc_shim/miniupnpc" SYMBOLIC)
        target_include_directories(kfx_miniupnpc INTERFACE "${CMAKE_BINARY_DIR}/miniupnpc_shim")
        target_link_libraries(kfx_miniupnpc INTERFACE libminiupnpc-static)
    endif()

    # libnatpmp: untagged upstream (no releases) -- pinned to a commit
    # instead of a tag for reproducibility. Has its own CMakeLists.txt
    # (target `natpmp`); EXCLUDE_FROM_ALL skips building its two sample
    # CLI tools since nothing here needs them.
    pkg_check_modules(NATPMP IMPORTED_TARGET libnatpmp)
    if(NOT NATPMP_FOUND)
        pkg_check_modules(NATPMP IMPORTED_TARGET natpmp)
    endif()
    add_library(kfx_natpmp INTERFACE)
    if(NATPMP_FOUND)
        message(STATUS "natpmp: using system library (pkg-config)")
        target_link_libraries(kfx_natpmp INTERFACE PkgConfig::NATPMP)
    else()
        message(STATUS "natpmp: system library not found; building from source (FetchContent)")
        kfx_populate(natpmp_src
            URL "https://github.com/miniupnp/libnatpmp/archive/134fc89e2781e154e40042641f4d8bcbe42579f1.tar.gz")
        # Its own CMakeLists.txt does `option(BUILD_SHARED_LIBS ... OFF)`,
        # which is a no-op once some earlier FetchContent'd project (SDL3)
        # has already cached BUILD_SHARED_LIBS ON -- force it back off for
        # this one subdirectory so natpmp links in statically like every
        # other kfx_natpmp/kfx_* fallback here, not as a co-shipped .so.
        set(BUILD_SHARED_LIBS OFF)
        add_subdirectory("${natpmp_src_SOURCE_DIR}" "${natpmp_src_BINARY_DIR}" EXCLUDE_FROM_ALL)
        target_link_libraries(kfx_natpmp INTERFACE natpmp)
    endif()

    # libcurl: CURL_USE_OPENSSL needs the system's OpenSSL -- already an
    # accepted assumption for this target (see build-cmake.sh's dist/linux
    # portability note: libssl/libcrypto are the one pair of non-bundled,
    # non-libc runtime deps the final binary has). curl's own CMake
    # auto-detects zlib (present via the system package pkg_check_modules
    # just found above) and libz.a/-ldz linkage falls out of that
    # automatically -- no need to seed ZLIB_INCLUDE_DIR/ZLIB_LIBRARY by hand
    # here the way the WIN32 branch above has to.
    pkg_check_modules(CURL IMPORTED_TARGET libcurl)
    add_library(kfx_curl INTERFACE)
    if(CURL_FOUND)
        message(STATUS "libcurl: using system library (pkg-config)")
        target_link_libraries(kfx_curl INTERFACE PkgConfig::CURL)
    else()
        message(STATUS "libcurl: system library not found; building from source (FetchContent)")
        set(BUILD_CURL_EXE    OFF CACHE BOOL "" FORCE)
        set(BUILD_TESTING     OFF CACHE BOOL "" FORCE)
        set(CURL_USE_OPENSSL  ON  CACHE BOOL "" FORCE)
        set(CURL_USE_LIBPSL   OFF CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(curl_src
            URL "https://github.com/curl/curl/archive/refs/tags/curl-8_22_0.tar.gz")
        FetchContent_MakeAvailable(curl_src)
        target_compile_definitions(libcurl_static PUBLIC CURL_STATICLIB)
        target_link_libraries(kfx_curl INTERFACE libcurl_static)
    endif()
endif()

# Link every dependency onto TARGET.
function(kfx_link_dependencies TARGET)
    if(WIN32)
        target_link_libraries(${TARGET} PRIVATE
            kfx_sdl3 kfx_win32_static_deps centitoml)
    else()
        target_link_libraries(${TARGET} PRIVATE
            kfx_sdl3
            kfx_ffmpeg kfx_openal kfx_luajit
            kfx_spng kfx_minizip PkgConfig::ZLIB
            astronomy_static centijson_static enet6_static kfx_curl
            centitoml
            kfx_miniupnpc kfx_natpmp dl)
    endif()
endfunction()

# --- Dear ImGui (docs/refactor/renderer/04-imgui-gui-foundation.md, Phase A)
# Vendored at deps/imgui/ (upstream ocornut/imgui v1.92.7, MIT) -- an in-tree
# source dep like deps/centitoml above, not a fetched binary; see that
# directory's git history for how it was pulled in. Only the two SDL3
# backends are compiled (imgui_impl_sdl3 + imgui_impl_sdlrenderer3) since
# kfx_platform's window/renderer are exactly those types (RendererSoftware.h
# / WindowSystemSDL.h) -- see §3.1/§3.2 of the plan doc. imgui_demo.cpp is
# included too (Phase A's imgui_demo proof, §7); it costs nothing in a
# release build if ImGui::ShowDemoWindow() is never called.
#
# One OBJECT library, linked through kfx_common_opts (see centitoml just
# above for the identical reasoning).
set(KFX_IMGUI_SRC "${CMAKE_SOURCE_DIR}/deps/imgui")
add_library(imgui OBJECT
    "${KFX_IMGUI_SRC}/imgui.cpp"
    "${KFX_IMGUI_SRC}/imgui_draw.cpp"
    "${KFX_IMGUI_SRC}/imgui_tables.cpp"
    "${KFX_IMGUI_SRC}/imgui_widgets.cpp"
    "${KFX_IMGUI_SRC}/imgui_demo.cpp"
    "${KFX_IMGUI_SRC}/backends/imgui_impl_sdl3.cpp"
    "${KFX_IMGUI_SRC}/backends/imgui_impl_sdlrenderer3.cpp")
target_include_directories(imgui PUBLIC "${KFX_IMGUI_SRC}" "${KFX_IMGUI_SRC}/backends")
target_link_libraries(imgui PUBLIC kfx_sdl3)

# --- ImGuiColorTextEdit (vendored source, same shape as tinyfiledialogs
# above): a syntax-highlighting text editor widget for Dear ImGui
# (docs/refactor/editor/05-script-and-level-settings.md §4.1 -- the
# in-game level editor's script text editor). MIT licensed
# (deps/ImGuiColorTextEdit/LICENSE), just two files, no runtime
# dependencies beyond ImGui + the C++ standard library. Scoped to
# kfx_editor alone (not kfx_common_opts, unlike imgui/tinyfiledialogs
# above) since nothing else in the codebase needs it.
set(KFX_IMGUICOLORTEXTEDIT_SRC "${CMAKE_SOURCE_DIR}/deps/ImGuiColorTextEdit")
add_library(imgui_color_text_edit OBJECT "${KFX_IMGUICOLORTEXTEDIT_SRC}/TextEditor.cpp")
target_include_directories(imgui_color_text_edit PUBLIC "${KFX_IMGUICOLORTEXTEDIT_SRC}")
target_link_libraries(imgui_color_text_edit PUBLIC imgui)
