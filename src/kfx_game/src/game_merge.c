/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_merge.c
 *     Module which merges all elements of the game into single Game structure.
 * @par Purpose:
 *     Allows easy saving and loading of game data.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     21 Oct 2009 - 25 Nov 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "game_merge.h"

#include "globals.h"
#include "bflib_basics.h"
#include "game_legacy.h"
#include "moonphase.h"
#include "config.h"
#include "state_versions.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct IntralevelData intralvl;
// Saved/resynced as raw bytes: see state_versions.h before changing the layout.
_Static_assert(sizeof(struct IntralevelData) == KFX_INTRALEVEL_SIZE, "struct IntralevelData changed size: bump KFX_INTRALEVEL_VER and update KFX_INTRALEVEL_SIZE in state_versions.h");
uint64_t game_flags2 = 0;
/******************************************************************************/
/******************************************************************************/
/**
 * Informs if we're going to emulate overflow for integer values with given amount of bits.
 * @param nbits Amount of bits for which we want to know the overflow emulation state.
 * @return Overflow emulation state.
 */
// get_loaded_level_number/set_loaded_level_number/get_continue_level_number/
// set_continue_level_number/get_selected_level_number/get_level_number/
// set_selected_level_number moved to kfx_game_state.h as static inlines
// (stage 13.3, docs/refactor/stage-13-enforce-and-document.md).

/**
 * Returns if the given bonus level is visible in land view screen.
 */
TbBool is_bonus_level_visible(struct PlayerInfo *player, LevelNumber bn_lvnum)
{
    int64_t i = storage_index_for_bonus_level(bn_lvnum);
    if (i < 0)
    {
        // This hapens quite often - status of bonus level is checked even
        // if there's no such bonus level. So no log message here.
        return false;
  }
  int64_t n = i / 8;
  int64_t k = (1 << (i % 8));
  if ((n < 0) || (n >= BONUS_LEVEL_STORAGE_COUNT))
  {
    WARNLOG("Bonus level %" PRId64 " has invalid store position.",(int64_t)bn_lvnum);
    return false;
  }
  return ((intralvl.bonuses_found[n] & k) != 0);
}

/**
 * Makes the bonus level visible on the land map screen.
 */
TbBool set_bonus_level_visibility(LevelNumber bn_lvnum, TbBool visible)
{
    int64_t i = storage_index_for_bonus_level(bn_lvnum);
    if (i < 0)
    {
        WARNLOG("Can't set state of non-existing bonus level %" PRId64 ".", (int64_t)bn_lvnum);
        return false;
    }
    int64_t n = i / 8;
    int64_t k = (1 << (i % 8));
    if ((n < 0) || (n >= BONUS_LEVEL_STORAGE_COUNT))
    {
        WARNLOG("Bonus level %" PRId64 " has invalid store position.",(int64_t)bn_lvnum);
        return false;
    }
    set_flag_value(intralvl.bonuses_found[n], k, visible);
    return true;
}

/**
 * Makes a bonus level for specified SP level visible on the land map screen.
 */
TbBool set_bonus_level_visibility_for_singleplayer_level(struct PlayerInfo *player, uint64_t sp_lvnum, int64_t visible)
{
    int64_t bn_lvnum = bonus_level_for_singleplayer_level(sp_lvnum);
    if (!set_bonus_level_visibility(bn_lvnum, visible))
    {
        if (visible)
            WARNMSG("Couldn't store bonus award for level %" PRIu64, (uint64_t)(sp_lvnum));
        return false;
    }
    if (visible)
        SYNCMSG("Bonus award for level %" PRIu64 " enabled",(uint64_t)(sp_lvnum));
    return true;
}

TbBool activate_bonus_level_for_singleplayer(struct PlayerInfo *player, uint64_t sp_lvnum)
{
    return set_bonus_level_visibility_for_singleplayer_level(player, sp_lvnum, true);
}

void hide_all_bonus_levels(struct PlayerInfo *player)
{
    for (int64_t i = 0; i < BONUS_LEVEL_STORAGE_COUNT; i++)
        intralvl.bonuses_found[i] = 0;
}

/**
 * Returns if the given extra level is visible in land view screen.
 */
int64_t get_extra_level_kind_visibility(int64_t elv_kind)
{
    LevelNumber ex_lvnum = get_extra_level(elv_kind);
    if (ex_lvnum <= 0)
        return LvSt_Hidden;
    switch (elv_kind)
    {
    case ExLv_FullMoon:
        if (is_full_moon)
            return LvSt_Visible;
        if (is_near_full_moon)
            return LvSt_HalfShow;
        break;
    case ExLv_NewMoon:
        if (is_new_moon)
            return LvSt_Visible;
        if (is_near_new_moon)
            return LvSt_HalfShow;
        break;
    }
    return LvSt_Hidden;
}

void update_extra_levels_visibility(void)
{
}

struct LevelEnsignOverride *get_level_ensign_override(LevelNumber lvnum)
{
    for (int64_t i = 0; i < ENSIGN_OVERRIDES_COUNT; i++)
    {
        struct LevelEnsignOverride *override = &intralvl.ensign_overrides[i];
        if (override->lvnum == lvnum)
        {
            return override;
        }
    }

    return NULL;
}

TbBool update_or_create_level_ensign_override(LevelNumber lvnum, int64_t ensign_type)
{
    struct LevelEnsignOverride *override = get_level_ensign_override(lvnum);
    if (override != NULL)
    {        
        if(ensign_type == -1){
            memset(override, 0, sizeof(*override));
        } else {
            override->ensign_type = ensign_type;
        }
        return true;
    }

    for (int64_t i = 0; i < CAMPAIGN_LEVELS_COUNT; i++)
    {
        override = &intralvl.ensign_overrides[i];

        if (!override->active)
        {
            struct LevelInformation *lvinfo = get_level_info(lvnum);

            if (lvinfo == NULL)
                return false;

            override->lvnum = lvnum;
            override->ensign_type = ensign_type;
            override->active = true;
            
            return true;
        }
    }
    return true;
}

/**
  * sets a custom ensign sprite sheet index for the level
 */
TbBool set_level_ensign(LevelNumber lvnum, int64_t ensign_id)
{
    if(!is_campaign_level(lvnum))
        return false;
    return update_or_create_level_ensign_override(lvnum, ensign_id);
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
