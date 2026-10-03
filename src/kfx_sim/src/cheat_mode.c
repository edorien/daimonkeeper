/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cheat_mode.c
 *     Whether cheats are allowed.
 * @par Purpose:
 *     Cheats are off in a multiplayer game (refactor pass 5, P5-F17): every machine runs every packet and chat
 *     command, so a cheat one player sends changes every machine's game, and cheat mode itself comes from each
 *     machine's own command line, so machines started differently would run different games. A one-player game
 *     (campaign, skirmish) keeps them.
 * @par Comment:
 *     None.
 */
/******************************************************************************/
#include "pre_inc.h"

#include "cheat_mode.h"

#include "config_players.h"
#include "kfx_sim_state.h"

#include "post_inc.h"

/******************************************************************************/
TbBool game_refuses_cheats(void)
{
    return (kfx_sim_state.game_kind == GKind_MultiGame);
}

TbBool cheat_mode_enabled(void)
{
    return kfx_sim_state.easter_eggs_enabled && !game_refuses_cheats();
}

TbBool player_state_is_cheat(int64_t work_state)
{
    switch (work_state)
    {
    case PSt_MkDigger:
    case PSt_MkGoodCreatr:
    case PSt_OrderCreatr:
    case PSt_MkBadCreatr:
    case PSt_MkGoldPot:
    case PSt_FreeDestroyWalls:
    case PSt_FreeCastDisease:
    case PSt_FreeTurnChicken:
    case PSt_FreeCtrlPassngr:
    case PSt_FreeCtrlDirect:
    case PSt_StealRoom:
    case PSt_DestroyRoom:
    case PSt_KillCreatr:
    case PSt_ConvertCreatr:
    case PSt_StealSlab:
    case PSt_LevelCreatureUp:
    case PSt_LevelCreatureDown:
    case PSt_KillPlayer:
    case PSt_HeartHealth:
    case PSt_QueryAll:
    case PSt_MkHappy:
    case PSt_MkAngry:
    case PSt_PlaceTerrain:
    case PSt_DestroyThing:
    case PSt_CreatrInfoAll:
    case PSt_EditorFill:
    case PSt_EditorPlaceObject:
    case PSt_EditorPlaceTrap:
    case PSt_EditorPlaceDoor:
    case PSt_EditorPlaceTerrainRect:
    case PSt_EditorEyedropper:
    case PSt_EditorRectClearEarth:
    case PSt_EditorRectDeleteThings:
    case PSt_EditorRectSetOwner:
    case PSt_EditorStamp:
    case PSt_EditorQuery:
    case PSt_EditorPaintTexture:
    case PSt_EditorPlacePoint:
        return true;
    default:
        return false;
    }
}
/******************************************************************************/
