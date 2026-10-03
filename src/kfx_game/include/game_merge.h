/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_merge.h
 *     Header file for game_merge.c.
 * @par Purpose:
 *     Handles game state serialization, campaign progression, and random number generation systems.
 * @par Comment:
 *     Defines data structures for persistent campaign data, random number generation macros,
 *     and various game system flags used throughout the engine.
 * @author   Tomasz Lis
 * @date     21 Oct 2009 - 25 Nov 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/

#ifndef DK_GAMEMERGE_H
#define DK_GAMEMERGE_H

#include "bflib_basics.h"
#include "bflib_math.h"
#include "globals.h"

#include "actionpt.h"
#include "creature_control.h"
#include "dungeon_data.h"
#include "thing_creature.h"
#include "thing_objects.h"
#include "light_data.h"
#include "lvl_script.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define MESSAGE_TEXT_LEN           1024
#define QUICK_MESSAGES_COUNT        256
#define BONUS_LEVEL_STORAGE_COUNT     6
#define PLAYERS_FOR_CAMPAIGN_FLAGS    5
#define CAMPAIGN_FLAGS_PER_PLAYER     8
#define TRANSFER_CREATURE_STORAGE_COUNT     255
#define ENSIGN_OVERRIDES_COUNT       64

// THING_RANDOM/GAME_RANDOM/UNSYNC_RANDOM/SOUND_RANDOM/AI_RANDOM/
// PLAYER_RANDOM moved to kfx_sim_state.h (stage 13.3, docs/refactor/
// stage-13-enforce-and-document.md) -- all six read kfx_sim_state's
// random seeds directly, and kfx_sim is the lowest-ranked of their
// real consumers.

// enum GameSystemFlags moved to kfx_sim_state.h (stage 13.3,
// docs/refactor/stage-13-enforce-and-document.md) -- it's the flag type
// for kfx_sim_state.system_flags, and kfx_sim is the lowest-ranked of
// its real consumers (kfx_net/kfx_render/kfx_frontend/kfx_apploop also
// read it).

enum GameGUIFlags {
    GGUI_1Player         = 0x0001,
    GGUI_CountdownTimer  = 0x0002,
    GGUI_ScriptTimer     = 0x0004,
    GGUI_Variable        = 0x0008,
    GGUI_SoloChatEnabled = 0x0080
};

// enum ClassicBugFlags moved to kfx_config_state.h (stage 13.3,
// docs/refactor/stage-13-enforce-and-document.md) -- kfx_config's
// config_rules.c (the PRESERVECLASSICBUGS named-field table) is the
// lowest-ranked of its real consumers (kfx_sim/kfx_game also read it).

enum GameFlags2 {
    GF2_ClearPauseOnSync          = 0x0001,
    GF2_ClearPauseOnPacket        = 0x0002,
    GF2_Timer                     = 0x0004,
    GF2_Server                    = 0x0008,
    GF2_Connect                   = 0x0010,
    GF2_ShowEventLog              = 0x00010000,
    GF2_PERSISTENT_FLAGS          = 0xFFFF0000
};
/******************************************************************************/
#pragma pack(1)

// struct TextScrollWindow moved to kfx_frontend_state.h (stage 10,
// docs/refactor/stage-10-kfx-frontend.md) -- only ever embedded by
// value in evntbox_scroll_window, moved there too.

struct LevelEnsignOverride {
    LevelNumber lvnum;
    TbBool active;
    int64_t ensign_type;
};
/**
 * Structure which stores data copied between levels.
 * This data is not lost between levels of a campaign.
 */
struct IntralevelData {
    unsigned char bonuses_found[BONUS_LEVEL_STORAGE_COUNT];
    struct CreatureStorage transferred_creatures[PLAYERS_COUNT][TRANSFER_CREATURE_STORAGE_COUNT];
    int64_t campaign_flags[PLAYERS_FOR_CAMPAIGN_FLAGS][CAMPAIGN_FLAGS_PER_PLAYER];
    LevelNumber next_level;
    struct LevelEnsignOverride ensign_overrides[ENSIGN_OVERRIDES_COUNT];
};


extern uint64_t game_flags2; // Should be reset to zero on new level

#pragma pack()

/******************************************************************************/
extern struct IntralevelData intralvl;
/******************************************************************************/
// get_loaded_level_number/set_loaded_level_number/get_continue_level_number/
// set_continue_level_number/get_selected_level_number/
// set_selected_level_number/get_level_number moved to kfx_game_state.h
// as static inlines (stage 13.3, docs/refactor/
// stage-13-enforce-and-document.md) -- each is a trivial accessor for a
// kfx_game_state field, and kfx_sim is the lowest-ranked of their real
// consumers.
TbBool activate_bonus_level(struct PlayerInfo *player);
// GamePort's activate_bonus_level_for_singleplayer -- fixes visible=true, matching
// power_specials.c's own use of set_bonus_level_visibility_for_singleplayer_level.
TbBool activate_bonus_level_for_singleplayer(struct PlayerInfo *player, uint64_t sp_lvnum);
TbBool is_bonus_level_visible(struct PlayerInfo *player, LevelNumber bn_lvnum);
void hide_all_bonus_levels(struct PlayerInfo *player);
int64_t get_extra_level_kind_visibility(int64_t elv_kind);
void update_extra_levels_visibility(void);
TbBool set_bonus_level_visibility_for_singleplayer_level(struct PlayerInfo *player, uint64_t sp_lvnum, int64_t visible);
TbBool set_bonus_level_visibility(LevelNumber bn_lvnum, TbBool visible);
TbBool update_or_create_level_ensign_override(LevelNumber lvnum, int64_t ensign_type);
struct LevelEnsignOverride *get_level_ensign_override(LevelNumber lvnum);
TbBool set_level_ensign(LevelNumber lvnum, int64_t ensign_id);
/******************************************************************************/

#ifdef __cplusplus
}
#endif
#endif
