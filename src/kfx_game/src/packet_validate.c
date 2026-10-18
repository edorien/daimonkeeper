/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file packet_validate.c
 *     What a packet may ask for. See packet_validate.h.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "packet_validate.h"

#include "cheat_mode.h"
#include "config_compp.h"
#include "config_creature.h"
#include "config_magic.h"
#include "config_terrain.h"
#include "config_trapdoor.h"
#include "creature_battle.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "map_events.h"
#include "player_data.h"
#include "room_data.h"
#include "roomspace.h"
#include "thing_list.h"
#include "bflib_netsp.h"
#include "net_main.h"
#include "post_inc.h"

#define PAF_CHEAT     0x01 /**< A cheat, editor or assist action: not accepted where cheats are refused. */
#define PAF_HOST_ONLY 0x02 /**< Only the host's packets may carry it. */
#define PAF_KNOWN     0x80 /**< The action is in this table (anything else is not an action of this game). */

struct PacketActionSpec {
    uint8_t flags;
    uint8_t kind[4];
};

#define ACT(action, fl) [action] = {PAF_KNOWN | (fl), {PK_ANY, PK_ANY, PK_ANY, PK_ANY}}
#define ACT_P(action, fl, k1, k2, k3, k4) [action] = {PAF_KNOWN | (fl), {k1, k2, k3, k4}}

/** Indexed by action number. Absent: PckA_None (an empty action, always fine), the unused numbers and the network
 *  message types that share the numbering (Login, UserUpdate, Frame, Resync), which no gameplay packet carries. */
static const struct PacketActionSpec action_specs[256] = {
    ACT(PckA_QuitToMainMenu, 0),
    ACT(PckA_ForceApplicationClose, 0),
    ACT(PckA_NoOperation, 0),
    ACT(PckA_FinishGame, 0),
    ACT(PckA_PlyrMsgBegin, 0),
    ACT(PckA_PlyrMsgEnd, 0),
    ACT(PckA_ToggleLights, 0),
    ACT(PckA_TogglePause, 0),
    ACT(PckA_SetCluedo, 0),
    ACT(PckA_ChangeWindowSize, 0),
    ACT(PckA_BookmarkLoad, 0),
    ACT(PckA_SetGammaLevel, 0),
    ACT(PckA_SetMinimapConf, 0),
    ACT(PckA_SetMapRotation, 0),
    ACT_P(PckA_PasngrCtrlExit, 0, PK_THING, PK_ANY, PK_ANY, PK_ANY),
    ACT_P(PckA_DirectCtrlExit, 0, PK_THING, PK_ANY, PK_ANY, PK_ANY),
    ACT(PckA_SetPlyrState, 0),
    ACT(PckA_SwitchView, 0),
    ACT_P(PckA_CtrlCrtrSetInstnc, 0, PK_INSTANCE, PK_ANY, PK_ANY, PK_ANY),
    ACT_P(PckA_GenericLevelPower, 0, PK_ANY, PK_POWER, PK_ANY, PK_ANY),
    ACT(PckA_HoldAudience, 0),
    ACT(PckA_ToggleTendency, 0),
    ACT(PckA_CheatEnter, PAF_CHEAT),
    ACT(PckA_CheatAllFree, PAF_CHEAT),
    ACT(PckA_CheatCrtSpells, PAF_CHEAT),
    ACT(PckA_CheatRevealMap, PAF_CHEAT),
    ACT(PckA_CheatCrAllSpls, PAF_CHEAT),
    ACT(PckA_CheatUnusedPlaceholder065, PAF_CHEAT),
    ACT(PckA_CheatAllMagic, PAF_CHEAT),
    ACT(PckA_CheatAllRooms, PAF_CHEAT),
    ACT(PckA_CheatUnusedPlaceholder068, PAF_CHEAT),
    ACT(PckA_CheatUnusedPlaceholder069, PAF_CHEAT),
    ACT(PckA_CheatAllResrchbl, PAF_CHEAT),
    ACT(PckA_SetViewType, 0),
    ACT(PckA_ZoomFromMap, 0),
    ACT(PckA_UpdatePause, 0),
    ACT_P(PckA_ZoomToEvent, 0, PK_EVENT, PK_ANY, PK_ANY, PK_ANY),
    ACT_P(PckA_ZoomToRoom, 0, PK_ROOM, PK_ANY, PK_ANY, PK_ANY),
    ACT_P(PckA_ZoomToTrap, 0, PK_THING, PK_ANY, PK_ANY, PK_ANY),
    ACT_P(PckA_ZoomToDoor, 0, PK_THING, PK_ANY, PK_ANY, PK_ANY),
    ACT(PckA_ZoomToPosition, 0),
    ACT(PckA_ToggleComputerProcessing, 0),
    ACT(PckA_PwrCTADis, 0),
    ACT_P(PckA_UsePwrHandPick, 0, PK_THING, PK_ANY, PK_ANY, PK_ANY),
    ACT_P(PckA_UsePwrHandDrop, 0, PK_SUBTILE_X, PK_SUBTILE_Y, PK_ANY, PK_ANY),
    ACT_P(PckA_EventBoxTurnOff, 0, PK_EVENT, PK_ANY, PK_ANY, PK_ANY),
    ACT_P(PckA_UseSpecialBox, 0, PK_THING, PK_ANY, PK_ANY, PK_ANY),
    ACT_P(PckA_ResurrectCrtr, 0, PK_THING, PK_ANY, PK_ANY, PK_ANY),
    ACT_P(PckA_TransferCreatr, 0, PK_THING, PK_THING, PK_ANY, PK_ANY),
    ACT(PckA_UsePwrObey, 0),
    ACT(PckA_UsePwrArmageddon, 0),
    ACT(PckA_TurnOffQuery, 0),
    ACT_P(PckA_ZoomToBattle, 0, PK_BATTLE, PK_ANY, PK_ANY, PK_ANY),
    ACT_P(PckA_ZoomToSpell, 0, PK_POWER, PK_ANY, PK_ANY, PK_ANY),
    ACT(PckA_ToggleComputer, 0),
    ACT(PckA_PlyrFastMsg, 0),
    ACT_P(PckA_SetComputerKind, 0, PK_COMPUTER_ASSIST, PK_ANY, PK_ANY, PK_ANY),
    ACT(PckA_GoSpectator, 0),
    ACT(PckA_DumpHeldThingToOldPos, 0),
    ACT(PckA_ToggleSpectate, 0),
    ACT(PckA_PwrSOEDis, 0),
    ACT(PckA_EventBoxActivate, 0),
    ACT(PckA_EventBoxClose, 0),
    ACT_P(PckA_UsePwrOnThing, 0, PK_POWER, PK_THING, PK_ANY, PK_ANY),
    ACT_P(PckA_PlyrToggleAlly, 0, PK_PLAYER, PK_ANY, PK_ANY, PK_ANY),
    ACT(PckA_SaveViewType, 0),
    ACT(PckA_LoadViewType, 0),
    ACT(PckA_PlyrMsgClear, 0),
    ACT(PckA_PlyrMsgLast, 0),
    ACT(PckA_PlyrMsgCmdAutoCompletion, 0),
    ACT(PckA_DirectCtrlDragDrop, 0),
    ACT_P(PckA_CheatPlaceTerrain, PAF_CHEAT, PK_SLAB, PK_PLAYER, PK_ANY, PK_ANY),
    ACT_P(PckA_CheatMakeCreature, PAF_CHEAT, PK_CREATURE, PK_PLAYER_LOW8, PK_ANY, PK_ANY),
    ACT_P(PckA_CheatMakeDigger, PAF_CHEAT, PK_PLAYER, PK_LEVEL, PK_ANY, PK_ANY),
    ACT_P(PckA_CheatStealSlab, PAF_CHEAT, PK_SLAB, PK_PLAYER_LOW8, PK_ANY, PK_ANY),
    ACT_P(PckA_CheatStealRoom, PAF_CHEAT, PK_PLAYER, PK_ANY, PK_ANY, PK_ANY),
    ACT_P(PckA_CheatHeartHealth, PAF_CHEAT, PK_PLAYER, PK_ANY, PK_ANY, PK_ANY),
    ACT_P(PckA_CheatKillPlayer, PAF_CHEAT, PK_PLAYER, PK_ANY, PK_ANY, PK_ANY),
    ACT_P(PckA_CheatConvertCreature, PAF_CHEAT, PK_PLAYER, PK_ANY, PK_ANY, PK_ANY),
    ACT_P(PckA_CheatSwitchTerrain, PAF_CHEAT, PK_SLAB, PK_ANY, PK_ANY, PK_ANY),
    ACT_P(PckA_CheatSwitchPlayer, PAF_CHEAT, PK_PLAYER, PK_ANY, PK_ANY, PK_ANY),
    ACT_P(PckA_CheatSwitchCreature, PAF_CHEAT, PK_CREATURE, PK_ANY, PK_ANY, PK_ANY),
    ACT_P(PckA_CheatSwitchHero, PAF_CHEAT, PK_CREATURE, PK_ANY, PK_ANY, PK_ANY),
    ACT_P(PckA_CheatSwitchExperience, PAF_CHEAT, PK_LEVEL, PK_ANY, PK_ANY, PK_ANY),
    ACT_P(PckA_CheatCtrlCrtrSetInstnc, PAF_CHEAT, PK_INSTANCE, PK_ANY, PK_ANY, PK_ANY),
    ACT(PckA_SetFirstPersonDigMode, 0),
    ACT(PckA_SwitchTeleportDest, 0),
    ACT_P(PckA_SelectFPPickup, 0, PK_THING, PK_ANY, PK_ANY, PK_ANY),
    ACT(PckA_CheatAllDoors, PAF_CHEAT),
    ACT(PckA_CheatAllTraps, PAF_CHEAT),
    ACT(PckA_SetRoomspaceAuto, 0),
    ACT_P(PckA_SetRoomspaceMan, 0, PK_ROOMSPACE_SIZE, PK_ANY, PK_ANY, PK_ANY),
    ACT(PckA_SetRoomspaceDrag, 0),
    ACT_P(PckA_SetRoomspaceDefault, 0, PK_ROOMSPACE_SIZE, PK_ANY, PK_ANY, PK_ANY),
    ACT(PckA_SetRoomspaceWholeRoom, 0),
    ACT(PckA_SetRoomspaceSubtile, 0),
    ACT_P(PckA_SetRoomspaceHighlight, 0, PK_ANY, PK_ROOMSPACE_SIZE, PK_ANY, PK_ANY),
    ACT(PckA_SetNearestTeleport, 0),
    ACT(PckA_SetRoomspaceDragPaint, 0),
    ACT_P(PckA_PlyrQueryCreature, 0, PK_THING, PK_ANY, PK_ANY, PK_ANY),
    ACT(PckA_CheatGiveDoorTrap, PAF_CHEAT),
    ACT(PckA_RoomspaceHighlightToggle, 0),
    ACT_P(PckA_ApplyRoomspaceDigTag, 0, PK_ANY, PK_ROOMSPACE_SIZE, PK_ANY, PK_ANY),
    ACT(PckA_CheatWinLevel, PAF_CHEAT),
    ACT(PckA_CheatLoseLevel, PAF_CHEAT),
    ACT(PckA_CheatLevelUp, PAF_CHEAT),
    ACT(PckA_CheatLevelDown, PAF_CHEAT),
    ACT_P(PckA_CheatApplySpell, PAF_CHEAT, PK_SPELL, PK_ANY, PK_ANY, PK_ANY),
    ACT(PckA_CheatKillCreature, PAF_CHEAT),
    ACT_P(PckA_EditorFloodFill, PAF_CHEAT, PK_SLAB, PK_PLAYER, PK_ANY, PK_ANY),
    ACT_P(PckA_EditorPlaceObject, PAF_CHEAT, PK_MAPCOORD_X, PK_MAPCOORD_Y, PK_ANY, PK_PLAYER),
    ACT_P(PckA_CheatSwitchTrap, PAF_CHEAT, PK_TRAP, PK_ANY, PK_ANY, PK_ANY),
    ACT_P(PckA_CheatSwitchDoor, PAF_CHEAT, PK_DOOR, PK_ANY, PK_ANY, PK_ANY),
    ACT_P(PckA_EditorPlaceTrap, PAF_CHEAT, PK_TRAP, PK_PLAYER, PK_ANY, PK_ANY),
    ACT_P(PckA_EditorPlaceDoor, PAF_CHEAT, PK_DOOR, PK_PLAYER, PK_ANY, PK_ANY),
    ACT_P(PckA_EditorPlaceTerrainRect, PAF_CHEAT, PK_SUBTILE_X, PK_SUBTILE_Y, PK_SLAB, PK_PLAYER),
    ACT_P(PckA_EditorToggleDoorLock, PAF_CHEAT, PK_THING, PK_ANY, PK_ANY, PK_ANY),
    ACT_P(PckA_EditorUndo, PAF_CHEAT, PK_THING, PK_ANY, PK_ANY, PK_ANY),
    ACT_P(PckA_EditorRectClearEarth, PAF_CHEAT, PK_SUBTILE_X, PK_SUBTILE_Y, PK_ANY, PK_ANY),
    ACT_P(PckA_EditorRectDeleteThings, PAF_CHEAT, PK_SUBTILE_X, PK_SUBTILE_Y, PK_ANY, PK_ANY),
    ACT_P(PckA_EditorRectSetOwner, PAF_CHEAT, PK_SUBTILE_X, PK_SUBTILE_Y, PK_PLAYER, PK_ANY),
    ACT_P(PckA_EditorRedoCreature, PAF_CHEAT, PK_MAPCOORD_X, PK_MAPCOORD_Y, PK_CREATURE, PK_PLAYER_LOW8),
    ACT_P(PckA_EditorRedoDigger, PAF_CHEAT, PK_MAPCOORD_X, PK_MAPCOORD_Y, PK_PLAYER, PK_LEVEL),
    ACT_P(PckA_EditorRedoTrap, PAF_CHEAT, PK_MAPCOORD_X, PK_MAPCOORD_Y, PK_TRAP, PK_PLAYER),
    ACT_P(PckA_EditorRedoDoor, PAF_CHEAT, PK_MAPCOORD_X, PK_MAPCOORD_Y, PK_DOOR, PK_PLAYER),
    ACT_P(PckA_EditorSetGoldValue, PAF_CHEAT, PK_THING, PK_ANY, PK_ANY, PK_ANY),
    ACT_P(PckA_EditorSetThingPosition, PAF_CHEAT, PK_MAPCOORD_X, PK_MAPCOORD_Y, PK_THING, PK_ANY),
    ACT_P(PckA_EditorGoSpectator, PAF_CHEAT, PK_MAPCOORD_X, PK_MAPCOORD_Y, PK_ANY, PK_ANY),
    ACT(PckA_AssistStageRule, PAF_CHEAT),
    ACT(PckA_AssistStageRuleMoney, PAF_CHEAT),
    ACT(PckA_AssistStageCond, PAF_CHEAT),
    ACT(PckA_AssistStageGlobal, PAF_CHEAT),
    ACT(PckA_AssistCommit, PAF_CHEAT),
    ACT(PckA_AssistEnable, PAF_CHEAT),
    ACT(PckA_SetTimestamp, PAF_HOST_ONLY),
    ACT(PckA_AssistCommitBank, PAF_CHEAT),
    ACT(PckA_AssistUseBank, PAF_CHEAT),
    ACT(PckA_AssistStageRuleHand, PAF_CHEAT),
};

void packet_limits_current(struct PacketLimits *limits)
{
    limits->things = THINGS_COUNT;
    limits->events = EVENTS_COUNT;
    limits->rooms = ROOMS_COUNT;
    limits->battles = BATTLES_COUNT;
    limits->players = PLAYERS_COUNT;
    limits->subtiles_x = kfx_sim_state.map_subtiles_x;
    limits->subtiles_y = kfx_sim_state.map_subtiles_y;
    limits->slab_kinds = kfx_config_state.conf.slab_conf.slab_types_count;
    limits->creature_models = kfx_config_state.conf.crtr_conf.model_count;
    limits->trap_kinds = kfx_config_state.conf.trapdoor_conf.trap_types_count;
    limits->door_kinds = kfx_config_state.conf.trapdoor_conf.door_types_count;
    limits->power_kinds = kfx_config_state.conf.magic_conf.power_types_count;
    limits->spell_kinds = kfx_config_state.conf.magic_conf.spell_types_count;
    limits->instances = kfx_config_state.conf.crtr_conf.instances_count;
    limits->levels = CREATURE_MAX_LEVEL;
    limits->roomspace_size = MAX_ROOMSPACE_WIDTH;
    limits->computer_assist_types = COMPUTER_ASSIST_TYPES_COUNT;
}

static TbBool in_range(int64_t value, int64_t count)
{
    return (value >= 0) && (value < count);
}

/** A map coordinate is a subtile times 256 (coord_subtile() is a shift). */
static TbBool coord_on_map(int64_t value, int64_t subtiles)
{
    return (value >= 0) && ((value >> 8) < subtiles);
}

static TbBool param_ok(const struct PacketLimits *limits, enum PacketParamKind kind, int64_t value)
{
    switch (kind)
    {
    case PK_ANY:             return true;
    case PK_PLAYER:          return in_range(value, limits->players);
    case PK_PLAYER_LOW8:     return in_range(value & 0xFF, limits->players);
    case PK_THING:           return in_range(value, limits->things);
    case PK_EVENT:           return in_range(value, limits->events);
    case PK_ROOM:            return in_range(value, limits->rooms);
    case PK_BATTLE:          return in_range(value, limits->battles);
    case PK_SUBTILE_X:       return in_range(value, limits->subtiles_x);
    case PK_SUBTILE_Y:       return in_range(value, limits->subtiles_y);
    case PK_MAPCOORD_X:      return coord_on_map(value, limits->subtiles_x);
    case PK_MAPCOORD_Y:      return coord_on_map(value, limits->subtiles_y);
    case PK_SLAB:            return in_range(value, limits->slab_kinds);
    case PK_CREATURE:        return in_range(value, limits->creature_models);
    case PK_TRAP:            return in_range(value, limits->trap_kinds);
    case PK_DOOR:            return in_range(value, limits->door_kinds);
    case PK_POWER:           return in_range(value, limits->power_kinds);
    case PK_SPELL:           return in_range(value, limits->spell_kinds);
    case PK_INSTANCE:        return in_range(value, limits->instances);
    case PK_LEVEL:           return in_range(value, limits->levels);
    case PK_ROOMSPACE_SIZE:  return in_range(value, limits->roomspace_size);
    case PK_COMPUTER_ASSIST: return (value >= 1) && (value <= limits->computer_assist_types);
    }
    return false;
}

enum PacketVerdict packet_check(const struct PacketLimits *limits, const struct Packet *pckt, TbBool cheats_refused,
    TbBool from_host, int *bad_param)
{
    if (bad_param != NULL)
        *bad_param = 0;
    if (pckt->action == PckA_None)
        return PKT_OK;
    const struct PacketActionSpec *spec = &action_specs[pckt->action];
    if ((spec->flags & PAF_KNOWN) == 0)
        return PKT_UNKNOWN_ACTION;
    if (cheats_refused && ((spec->flags & PAF_CHEAT) != 0))
        return PKT_CHEAT_REFUSED;
    if (((spec->flags & PAF_HOST_ONLY) != 0) && !from_host)
        return PKT_NOT_ALLOWED_HERE;
    // The 3rd and 4th parameters share their bytes with the camera position in the actions that carry one.
    const TbBool camera = packet_action_has_camera_position((enum TbPacketAction)pckt->action);
    const int64_t values[4] = {pckt->actn_par1, pckt->actn_par2, camera ? 0 : pckt->actn_par3, camera ? 0 : pckt->actn_par4};
    for (int i = 0; i < 4; i++)
    {
        if (!param_ok(limits, (enum PacketParamKind)spec->kind[i], values[i]))
        {
            if (bad_param != NULL)
                *bad_param = i + 1;
            return PKT_BAD_PARAMETER;
        }
    }
    return PKT_OK;
}

enum PacketVerdict packet_validate_for_user(NetUserId user, struct Packet *pckt)
{
    struct PacketLimits limits;
    packet_limits_current(&limits);
    int bad_param = 0;
    const enum PacketVerdict verdict = packet_check(&limits, pckt, game_refuses_cheats(), (user == SERVER_ID), &bad_param);
    if (verdict == PKT_OK)
        return PKT_OK;
    switch (verdict)
    {
    case PKT_UNKNOWN_ACTION:
        SYNCLOG("Packet action %d from user %d dropped: not an action of this game", (int)pckt->action, (int)user);
        break;
    case PKT_CHEAT_REFUSED:
        SYNCLOG("Cheat action %d from user %d refused: no cheats in this game", (int)pckt->action, (int)user);
        break;
    case PKT_NOT_ALLOWED_HERE:
        SYNCLOG("Packet action %d from user %d dropped: not allowed from this user", (int)pckt->action, (int)user);
        break;
    default:
        SYNCLOG("Packet action %d from user %d dropped: parameter %d (%d) is out of range", (int)pckt->action, (int)user,
            bad_param, (int)((bad_param == 1) ? pckt->actn_par1 : (bad_param == 2) ? pckt->actn_par2 : (bad_param == 3) ? pckt->actn_par3 : pckt->actn_par4));
        break;
    }
    pckt->action = PckA_None;
    return verdict;
}
