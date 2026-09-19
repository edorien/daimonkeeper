# Stages the subset of a real KeeperFX game-data install that
# src/ftests/ftest_list.c's registered non-long-running tests actually
# need, from SOURCE_DIR (a real install, e.g. core_files/ -- see
# src/ftests/README.md) into DEST_DIR (a build tree the ftest-driven
# `coverage` target runs keeperfx from). Determined empirically with
# strace against a real install, not guessed: every registered short test
# (example_template_test, bug_imp_tp_attack_door__*, bug_imp_goldseam_dig,
# bug_pathing_stair_treasury) ran and passed against exactly this subset.
#
# Usage: cmake -DSOURCE_DIR=<install> -DDEST_DIR=<dest> -DKEEPERFX_CFG=<cfg>
#              -P StageFtestData.cmake
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

file(COPY "${KEEPERFX_CFG}" DESTINATION "${DEST_DIR}")

# config/keeperfx.cfg's own default is INGAME_RES=DESKTOP (fullscreen at the
# current desktop's real resolution) -- meaningless under -headless's dummy
# SDL video driver, which has no real desktop to query. That combination is
# a guaranteed SIGFPE, not a slow/degraded path: PlatformManager_
# ForcesAllModesAvailable() (bflib_video.c, set by -headless) short-circuits
# LbHwCheckIsModeAvailable() before the DESKTOP/ALL branches ever compute a
# real width/height, leaving the mode's registered 0x0 placeholder in place
# -- LbScreenSetup() then creates a 0x0 draw surface, and
# is_ar_wider_than_original() divides by that zero height. Confirmed via
# gdb/coredumpctl against a real install: identical crash with DESKTOP,
# clean run (loads levels, executes ftest actions) with a fixed resolution
# and nothing else changed. Force one here so every ftest-coverage run gets
# it, regardless of what the repo's own default config says.
get_filename_component(_cfg_name "${KEEPERFX_CFG}" NAME)
set(_staged_cfg "${DEST_DIR}/${_cfg_name}")
file(READ "${_staged_cfg}" _cfg_contents)
string(REGEX REPLACE "INGAME_RES=[A-Za-z0-9]+" "INGAME_RES=1920x1080x32" _cfg_contents "${_cfg_contents}")
file(WRITE "${_staged_cfg}" "${_cfg_contents}")

# Every game session scans all installed campaigns/mappacks/multiplayer
# maps at startup (to list them in menus / validate NAME_TEXT_ID etc.),
# regardless of which one actually gets loaded -- so their top-level
# config files are needed even though only keeporig/deepdngn's own data
# gets fully copied below. Missing/incomplete ones just log a startup
# warning (e.g. "Couldn't load Map Pack ..., no .LIF files could be
# found"), confirmed harmless by a real run.
file(GLOB _campgn_cfgs "${SOURCE_DIR}/campgns/*.cfg" "${SOURCE_DIR}/campgns/*.txt")
file(COPY ${_campgn_cfgs} DESTINATION "${DEST_DIR}/campgns")
file(GLOB _level_cfgs "${SOURCE_DIR}/levels/*.cfg" "${SOURCE_DIR}/levels/*.txt")
file(COPY ${_level_cfgs} DESTINATION "${DEST_DIR}/levels")
file(GLOB _mp_cfgs "${SOURCE_DIR}/multiplayer/*.cfg" "${SOURCE_DIR}/multiplayer/*.txt")
file(COPY ${_mp_cfgs} DESTINATION "${DEST_DIR}/multiplayer")

# Fully-populated campaign/level actually loaded by the registered short
# tests (level_file="keeporig" / "deepdngn" in ftest_list.c). Update this
# list if a newly-registered test uses a different one.
file(COPY "${SOURCE_DIR}/campgns/keeporig" DESTINATION "${DEST_DIR}/campgns")
file(COPY "${SOURCE_DIR}/campgns/keeporig_lnd" DESTINATION "${DEST_DIR}/campgns")
file(COPY "${SOURCE_DIR}/levels/deepdngn" DESTINATION "${DEST_DIR}/levels")

# Sprites/palettes/fonts, engine Lua scripts, and creature stat configs --
# needed unconditionally by any game session, small enough (~50MB
# combined) to copy in full rather than figure out a real subset.
file(COPY "${SOURCE_DIR}/data" DESTINATION "${DEST_DIR}")
file(COPY "${SOURCE_DIR}/fxdata" DESTINATION "${DEST_DIR}")
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
