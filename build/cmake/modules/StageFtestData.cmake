# Stages the subset of a real KeeperFX game-data install that
# src/ftests/ftest_list.c's registered non-long-running tests actually
# need, from SOURCE_DIR (a real install, e.g. core_files/ -- see
# src/ftests/README.md) into DEST_DIR (a build tree the ftest-driven
# `coverage` target runs keeperfx from). The engine-wide part (data/,
# fxdata/, creatrs/, every pack's top-level cfg, active mods) was
# determined empirically with strace against a real install; the
# per-campaign part is derived from ftest_list.c's own .level_file entries
# on every run, so a newly-registered test that loads a different campaign
# or map pack gets its data staged without touching this file.
#
# Usage: cmake -DSOURCE_DIR=<install> -DDEST_DIR=<dest> -DKEEPERFX_CFG=<cfg>
#              -DREPO_DIR=<repo root>
#              [-DFTEST_LIST=<src/ftests/ftest_list.c>] -P StageFtestData.cmake
#
# SOURCE_DIR is usually a KeeperFX install, whose fxdata/ holds KeeperFX's
# configs, Lua and language files. The tests must run on this repo's, so the
# staged fxdata/ is SOURCE_DIR's (for the sprite packs and .fxfont fonts only
# an install has) with the repo's config/fxdata and compiled language files
# copied over it.
if(NOT DEFINED REPO_DIR)
    message(FATAL_ERROR "StageFtestData: REPO_DIR must be set (see Usage above)")
endif()
if(NOT EXISTS "${SOURCE_DIR}")
    message(FATAL_ERROR "StageFtestData: SOURCE_DIR '${SOURCE_DIR}' does not "
        "exist -- see src/ftests/README.md for how to obtain the real, "
        "proprietary Dungeon Keeper data files this needs.")
endif()

# gcov ties each .gcda to the .gcno/.o it was produced against by
# checksum; a .gcda left over from an earlier build of the same source
# tree (different compiler flags, or just a different set of tests
# actually executed) makes libgcov noisily warn "overwriting an existing
# profile data with a different checksum" for every mismatched file on
# the next run -- harmless (the new run's data does win), but confusing,
# and avoidable by starting the DEST_DIR tree's coverage counters clean
# before every run rather than accumulating across them.
file(GLOB_RECURSE _stale_gcda "${DEST_DIR}/*.gcda")
if(_stale_gcda)
    file(REMOVE ${_stale_gcda})
endif()

# Start every run from pristine data, not whatever the previous run left.
# Several tests write into the data tree they run from (the editor and
# config_content_* tests save maps, generate campaign/map-pack scratch
# folders, rewrite cfg files) and not all of them clean up after
# themselves -- e.g. config_content_campaign_editor leaves
# campgns/ft_camp{,.cfg} behind, which every later session then lists as
# an installed campaign. file(COPY) only ever adds/overwrites, so without
# this those leftovers would accumulate across `coverage` runs and make
# each run's starting data depend on what the previous one did. Only the data directories this script itself
# stages are removed (none of these names is a CMake build output), and
# re-staging is cheap (~1-2s for ~145MB).
foreach(_staged_dir campgns levels multiplayer data fxdata creatrs mods save replays)
    file(REMOVE_RECURSE "${DEST_DIR}/${_staged_dir}")
endforeach()

file(COPY "${KEEPERFX_CFG}" DESTINATION "${DEST_DIR}")

# Every game session scans all installed campaigns/mappacks/multiplayer
# maps at startup (to list them in menus / validate NAME_TEXT_ID etc.),
# regardless of which one actually gets loaded -- so their top-level
# config files are needed even though only the packs ftest_list.c uses
# get their data fully copied below. Missing/incomplete ones just log a startup
# warning (e.g. "Couldn't load Map Pack ..., no .LIF files could be
# found"), confirmed harmless by a real run.
file(GLOB _campgn_cfgs "${SOURCE_DIR}/campgns/*.cfg" "${SOURCE_DIR}/campgns/*.txt")
file(COPY ${_campgn_cfgs} DESTINATION "${DEST_DIR}/campgns")
file(GLOB _level_cfgs "${SOURCE_DIR}/levels/*.cfg" "${SOURCE_DIR}/levels/*.txt")
file(COPY ${_level_cfgs} DESTINATION "${DEST_DIR}/levels")
file(GLOB _mp_cfgs "${SOURCE_DIR}/multiplayer/*.cfg" "${SOURCE_DIR}/multiplayer/*.txt")
file(COPY ${_mp_cfgs} DESTINATION "${DEST_DIR}/multiplayer")

# Fully-populated campaigns/map packs the registered tests actually load.
# Derived from ftest_list.c itself rather than a hand-kept list: every
# uncommented `.level_file="<name>"` entry (tests_list and
# long_running_tests_list alike -- they share the same few packs, so
# including the long-running ones costs nothing) names a campaign cfg that
# ftest_setup_test() hands to change_campaign(), which looks it up in
# campgns/, then levels/ (map packs), then multiplayer/ (multiplayer map
# packs) -- the same search order is mirrored here. That cfg's [common]
# section then says where the pack's data really lives, often outside its
# own directory (keeporig and multiplayer/original both take their rules
# from levels/classic_cfgs + levels/classic_crtr, and original borrows
# keeporig's land view), so each LEVELS/LAND/CREATURES/CONFIGS_LOCATION it
# names is copied whole.
#
# Deliberately not followed: MEDIA_LOCATION and the [speech] section --
# speech/music/soundtrack/movie files only, and -headless sets
# SoundDisabled and never plays movies (same reason sound/ and music/
# aren't staged below); keeporig's MEDIA_LOCATION alone is the 38MB ldata/.
#
# A name that resolves to no cfg in SOURCE_DIR is a hard error here: at
# runtime change_campaign() silently falls back to keeporig for an unknown
# or unloadable campaign, so a test asking for e.g. multiplayer "original"
# level 50 gets keeporig with no level 50 and hangs instead of failing
# (that is exactly how the old hand-kept keeporig/deepdngn list rotted).
if(NOT FTEST_LIST)
    get_filename_component(FTEST_LIST
        "${CMAKE_CURRENT_LIST_DIR}/../../../src/ftests/ftest_list.c" ABSOLUTE)
endif()
if(NOT EXISTS "${FTEST_LIST}")
    message(FATAL_ERROR "StageFtestData: can't find ftest_list.c at '${FTEST_LIST}' "
        "(pass -DFTEST_LIST=<path>) -- needed to know which campaigns to stage.")
endif()
file(STRINGS "${FTEST_LIST}" _ftest_lines REGEX "\\.level_file[ \t]*=")
set(_ftest_packs "")
foreach(_line IN LISTS _ftest_lines)
    # Commented-out/WIP entries (`// { .test_name=...`) aren't run.
    if(_line MATCHES "^[ \t]*//")
        continue()
    endif()
    string(REGEX MATCHALL "\\.level_file[ \t]*=[ \t]*\"[^\"]*\"" _hits "${_line}")
    foreach(_hit IN LISTS _hits)
        string(REGEX REPLACE ".*\"([^\"]*)\"" "\\1" _name "${_hit}")
        list(APPEND _ftest_packs "${_name}")
    endforeach()
endforeach()
list(REMOVE_DUPLICATES _ftest_packs)
if(NOT _ftest_packs)
    message(FATAL_ERROR "StageFtestData: found no .level_file entries in '${FTEST_LIST}'.")
endif()

set(_staged_locations "")
foreach(_pack IN LISTS _ftest_packs)
    # Same normalization as prepare_campaign_file_name(): optional
    # campgns/|levels/|multiplayer/ prefix pins the search to that folder,
    # and a missing .cfg extension is added.
    set(_search_dirs campgns levels multiplayer)
    if(_pack MATCHES "^(campgns|levels|multiplayer)/(.*)$")
        set(_search_dirs "${CMAKE_MATCH_1}")
        set(_pack "${CMAKE_MATCH_2}")
    endif()
    if(NOT _pack MATCHES "\\.[cC][fF][gG]$")
        string(APPEND _pack ".cfg")
    endif()
    set(_pack_cfg "")
    foreach(_dir IN LISTS _search_dirs)
        if(EXISTS "${SOURCE_DIR}/${_dir}/${_pack}")
            set(_pack_cfg "${SOURCE_DIR}/${_dir}/${_pack}")
            break()
        endif()
    endforeach()
    if(NOT _pack_cfg)
        message(FATAL_ERROR "StageFtestData: ftest_list.c references campaign/map pack "
            "'${_pack}', but no ${_search_dirs}/${_pack} exists under '${SOURCE_DIR}'. "
            "The game would silently fall back to keeporig and the test would "
            "hang or misbehave; add that pack to the data install.")
    endif()

    file(STRINGS "${_pack_cfg}" _cfg_lines)
    set(_in_common FALSE)
    foreach(_line IN LISTS _cfg_lines)
        if(_line MATCHES "^[ \t]*\\[([^]]*)\\]")
            string(TOLOWER "${CMAKE_MATCH_1}" _section)
            if(_section STREQUAL "common")
                set(_in_common TRUE)
            else()
                set(_in_common FALSE)
            endif()
            continue()
        endif()
        if(NOT _in_common)
            continue()
        endif()
        if(_line MATCHES "^[ \t]*(LEVELS|LAND|CREATURES|CONFIGS)_LOCATION[ \t]*=[ \t]*([^; \t]+)")
            set(_loc "${CMAKE_MATCH_2}")
            string(REGEX REPLACE "[/\\\\]+$" "" _loc "${_loc}")
            if(_loc IN_LIST _staged_locations)
                continue()
            endif()
            list(APPEND _staged_locations "${_loc}")
            if(NOT IS_DIRECTORY "${SOURCE_DIR}/${_loc}")
                message(WARNING "StageFtestData: ${_pack_cfg} names ${CMAKE_MATCH_1}_LOCATION "
                    "'${_loc}', which doesn't exist under '${SOURCE_DIR}' -- skipped.")
                continue()
            endif()
            get_filename_component(_loc_parent "${DEST_DIR}/${_loc}" DIRECTORY)
            file(COPY "${SOURCE_DIR}/${_loc}" DESTINATION "${_loc_parent}")
        endif()
    endforeach()
endforeach()
message(STATUS "StageFtestData: campaigns/map packs used by ftest_list.c: ${_ftest_packs}")
message(STATUS "StageFtestData: staged locations: ${_staged_locations}")

# Sprites/palettes/fonts, engine Lua scripts, and creature stat configs --
# needed unconditionally by any game session, small enough (~50MB
# combined) to copy in full rather than figure out a real subset.
file(COPY "${SOURCE_DIR}/data" DESTINATION "${DEST_DIR}")
if(EXISTS "${SOURCE_DIR}/fxdata")
    file(COPY "${SOURCE_DIR}/fxdata" DESTINATION "${DEST_DIR}")
else()
    message(STATUS "StageFtestData: ${SOURCE_DIR} has no fxdata/ -- staging the repo's "
        "config/fxdata only (no sprite packs or .fxfont fonts)")
endif()
file(COPY "${REPO_DIR}/config/fxdata/" DESTINATION "${DEST_DIR}/fxdata")
# Language files from `make pkg-languages` when it has been run; otherwise
# SOURCE_DIR's stay, and strings this repo added show untranslated -- which the
# functional tests don't mind.
file(GLOB _lang_dats "${REPO_DIR}/pkg/fxdata/gtext_*.dat")
if(_lang_dats)
    file(COPY ${_lang_dats} DESTINATION "${DEST_DIR}/fxdata")
else()
    message(STATUS "StageFtestData: no pkg/fxdata/gtext_*.dat (make pkg-languages) -- "
        "language files only as ${SOURCE_DIR}/fxdata has them")
endif()
file(COPY "${SOURCE_DIR}/creatrs" DESTINATION "${DEST_DIR}")

# mods/load_order.cfg (SOURCE_DIR's own, not keeperfx.cfg) is what
# actually decides which mods load -- only stage load_order.cfg plus the
# mod directories it references in [after_base], not every mod under
# SOURCE_DIR/mods (which included a 55MB mod unrelated to any of this).
set(_mods_load_order "${SOURCE_DIR}/mods/load_order.cfg")
if(EXISTS "${_mods_load_order}")
    file(COPY "${_mods_load_order}" DESTINATION "${DEST_DIR}/mods")
    file(STRINGS "${_mods_load_order}" _mods_lines)
    foreach(_line ${_mods_lines})
        string(STRIP "${_line}" _line)
        # Skip blanks, comments (`;...`), and section headers (`[...]`).
        if(_line MATCHES "^;" OR _line MATCHES "^\\[" OR _line STREQUAL "")
            continue()
        endif()
        if(EXISTS "${SOURCE_DIR}/mods/${_line}")
            file(COPY "${SOURCE_DIR}/mods/${_line}" DESTINATION "${DEST_DIR}/mods")
        endif()
    endforeach()
endif()

# Not staged at all -- confirmed unused for these tests via strace against
# a real run: sound/, music/ (SoundDisabled, set by -headless, skips
# audio device init and file access entirely), unearth/, scrshots/,
# ldata/, and every mods/ subdirectory load_order.cfg doesn't reference.
# save/ is write-only game output, not input data -- just needs to exist.
file(MAKE_DIRECTORY "${DEST_DIR}/save")
