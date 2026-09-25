/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file mod_config_types.h
 *     Per-mod existence/name config-data types, shared by kfx_config's
 *     config_mods.h (which authors and owns struct ModsConfig/mods_conf)
 *     and kfx_platform's bflib_sndlib.cpp/sound_manager.cpp, which walk
 *     a mod list by value to resolve music/sound file paths.
 * @par Comment:
 *     Physically split out of kfx_config's config_mods.h into this new
 *     low-rank header, same pattern as creature_sounds.h/instance_info.h/
 *     camera_data.h: kfx_platform needs the complete type, not just a
 *     pointer, and it's a plain POD struct with no other kfx_config-
 *     specific type dependencies. kfx_platform is the true floor here.
 *     See docs/refactor/stage-13-enforce-and-document.md.
 */
/******************************************************************************/

#ifndef DK_MOD_CONFIG_TYPES_H
#define DK_MOD_CONFIG_TYPES_H

#include "globals.h"
#include "bflib_basics.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

#define MODS_DIR_NAME "mods"

// base, campaign, map
#define MOD_ITEM_TYPE_CNT  3
#define MOD_ITEM_MAX  50

struct ModExistState{
    int64_t mod_dir;

    int64_t fx_data;	// FGrp_FxData: string, config, sprite
    int64_t std_data;	// FGrp_StdData: texture
    int64_t cmpg_config;	// FGrp_CmpgConfig: config, sprite, texture
    int64_t cmpg_lvls;	// FGrp_CmpgLvls: creaturemodel, config, sprite, texture

    int64_t crtr_data;	// FGrp_CrtrData: creaturemodel
    int64_t cmpg_crtrs;	// FGrp_CmpgCrtrs: creaturemodel

    int64_t lrg_sound;		// FGrp_LrgSound: custom sound files (mods/<name>/sound/)

    int64_t music;	// FGrp_Music: play_music
};

struct ModConfigItem {
    char name[COMMAND_WORD_LEN];

    struct ModExistState state;
};

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif // DK_MOD_CONFIG_TYPES_H
