/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file config_players.h
 *     Header file for config_players.c.
 * @par Purpose:
 *     Players configuration loading functions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   KeeperFX Team
 * @date     17 Sep 2012 - 06 Mar 2015
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_CFGPLAYERS_H
#define DK_CFGPLAYERS_H

#include "globals.h"
#include "bflib_basics.h"

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

#define PLAYER_STATES_COUNT_MAX    255

struct PlayerStateConfigStats {
    char code_name[COMMAND_WORD_LEN];
    unsigned char pointer_group;
    TbBool stop_own_units;
};

struct PlayerStateConfig {
    struct PlayerStateConfigStats plrst_cfg_stats[PLAYER_STATES_COUNT_MAX];
};

enum PlayerStates {
    PSt_None = 0,
    PSt_CtrlDungeon,
    PSt_BuildRoom,
    PSt_MkDigger,
    PSt_MkGoodCreatr,
    PSt_HoldInHand, // 5
    PSt_CallToArms,
    PSt_CastPowerOnSubtile,
    PSt_SightOfEvil,
    PSt_Slap,
    PSt_CtrlPassngr, // 10
    PSt_CtrlDirect,
    PSt_CreatrQuery,
    PSt_OrderCreatr,
    PSt_MkBadCreatr,
    PSt_CreatrInfo, // 15
    PSt_PlaceTrap,
    PSt_PlaceDoor,
    PST_CastPowerOnTarget,
    PSt_Sell,
    PSt_MkGoldPot, // 20
    PSt_FreeDestroyWalls,
    PSt_FreeCastDisease,
    PSt_FreeTurnChicken,
    PSt_FreeCtrlPassngr,
    PSt_FreeCtrlDirect, // 25
    PSt_StealRoom,
    PSt_DestroyRoom,
    PSt_KillCreatr,
    PSt_ConvertCreatr,
    PSt_StealSlab, // 30
    PSt_LevelCreatureUp,
    PSt_LevelCreatureDown,
    PSt_KillPlayer,
    PSt_HeartHealth,
    PSt_QueryAll, // 35
    PSt_MkHappy,
    PSt_MkAngry,
    PSt_PlaceTerrain,
    PSt_DestroyThing,
    PSt_CreatrInfoAll, // 40
    PSt_CreateDigger,
    PST_CastGenericLevelPower,
    // docs/refactor/editor/02-editing-toolbox.md §2.3 -- appended at the
    // end, not inserted among the numbered original entries above: this
    // enum's values are the same ones saved in savegames
    // (PlayerInfo::work_state) and packet-replay (.pck) files, so a new
    // entry has to get a new number, never renumber an existing one.
    PSt_EditorFill,
    // §2.6 -- placement itself is triggered directly by kfx_editor, not
    // this per-frame work-state switch (F17: object model has no
    // CheatSelection field to read back from), so this case exists mainly
    // to keep a stray click from falling through to whatever PSt_* was
    // active before, and to give the same cursor-highlight feedback other
    // placement tools have.
    PSt_EditorPlaceObject,
    // §2.7 -- unlike Objects, Traps/Doors reuse the *existing* UserState
    // fields the classic workshop-based PSt_PlaceTrap/PSt_PlaceDoor already
    // has (chosen_trap_kind/chosen_door_kind) -- no F17 gap here, since
    // those fields exist independently of CheatSelection. What's genuinely
    // new is the editor's *free* placement (no workshop stock check, no
    // resource cost), so these get their own work states rather than
    // reusing PSt_PlaceTrap/PSt_PlaceDoor themselves, whose dispatch
    // (packets_input.c) is wired to the resource-checked placement path.
    PSt_EditorPlaceTrap,
    PSt_EditorPlaceDoor,
    // §2.4 -- the Terrain tool's "Rectangle" mode (toggle alongside "Brush",
    // the existing continuous drag-paint PSt_PlaceTerrain already does).
    // A separate work state rather than a flag on PSt_PlaceTerrain because
    // the two need genuinely different PCtr_LBtn* handling shapes: Brush
    // paints on every Held frame, Rectangle only tracks the drag and
    // commits the whole marked box once, on Release.
    PSt_EditorPlaceTerrainRect,
    // §2.10 -- Eyedropper. Same "kfx_editor watches for its own click, no
    // packets_cheats.c dispatch" shape as Objects (F17-adjacent: the picked
    // kind+owner aren't known until the click happens, so there's nothing
    // for a per-work-state case to read back) -- this case exists only for
    // cursor-highlight feedback and to keep a stray click from falling
    // through to whatever tool was active before.
    PSt_EditorEyedropper,
    // §2.2 -- "Clear to Earth" area op: same mark-a-box/commit-on-release
    // shape as PSt_EditorPlaceTerrainRect (Rectangle mode), just with a
    // fixed target kind/owner (SlbT_EARTH/neutral) instead of the picker's
    // current selection -- a separate work state rather than a parameter
    // on the Rectangle one so the toolbox's mode row can offer it as its
    // own button.
    PSt_EditorRectClearEarth,
    // §2.2 -- "Delete Things Inside" area op. Same mark-a-box/commit shape
    // as Clear Earth, but the release handler sweeps every subtile in the
    // box for things to delete rather than repainting slabs.
    PSt_EditorRectDeleteThings,
    // §2.2 -- "Set Owner" area op. Same mark-a-box/commit shape again; the
    // handler properly transfers room ownership (delete_room_slab() then
    // re-place with the new owner -- see editor_set_owner_rect()'s own
    // comment) and skips ownerless kinds.
    PSt_EditorRectSetOwner,
    // §2.4 -- "Brush" (grab region -> stamp). Unlike every other tool
    // above, capture and stamp happen entirely client-side in kfx_editor,
    // calling the same sim mutation primitives (place_slab_type_on_map()
    // etc.) directly rather than through a packet -- the doc's own
    // sanctioned "single-player-local exception" (D2) for when queuing a
    // large stamp as a burst of packets, one per turn, would be visibly
    // janky. This work state exists only so a stray click doesn't fall
    // through to whatever tool was active before (same reason
    // PSt_EditorPlaceObject/PSt_EditorEyedropper exist) -- no dispatch of
    // its own.
    PSt_EditorStamp,
    // §2.10 -- Query. Not `PSt_QueryAll`: that state's classic dispatch
    // (packets_cheats.c) calls query_thing()/query_room() for anything
    // that isn't a creature, both of which show a classic (unmigrated)
    // GMnu_MSG_BOX popup -- jarring inside an otherwise all-ImGui editor.
    // Creature queries are fine as-is (GMnu_CREATURE_QUERY1-4 *are*
    // ImGui-migrated, per frontgui_ingame_creature.cpp's own
    // creature_query_panel()) -- kfx_editor calls query_creature()
    // directly for those. For everything else, kfx_editor reads the
    // thing/room itself and renders its own small ImGui inspector instead
    // of going through query_thing()/query_room() at all. Like Object/
    // Eyedropper/Stamp, this state exists only so a stray click doesn't
    // fall through to whatever tool was active before -- no dispatch of
    // its own.
    PSt_EditorQuery,
    // docs/refactor/editor/05-script-and-level-settings.md's "per-slab
    // texture paint" item -- same "client-side direct mutation, work
    // state exists only for click routing, no dispatch of its own" shape
    // as PSt_EditorStamp right above (see that constant's own comment for
    // the full D2 single-player-local-exception rationale).
    PSt_EditorPaintTexture,
    // phase5/06-slices6-8-points-tool.md -- the Points tool (lights, action
    // points, effect generators). Same "client-side direct mutation, work
    // state exists only for click routing" shape as PSt_EditorPaintTexture.
    PSt_EditorPlacePoint,
    PSt_ListEnd
};

enum PlayerStatePointerGroup {
    PsPg_None,
    PsPg_CtrlDungeon,
    PsPg_BuildRoom,
    PsPg_Invisible,
    PsPg_Spell,
    PsPg_Query,
    PsPg_PlaceTrap,
    PsPg_PlaceDoor,
    PsPg_Sell,
    PsPg_PlaceTerrain,
    PsPg_MkDigger,
    PsPg_MkCreatr,
    PsPg_OrderCreatr
};

/******************************************************************************/
extern struct NamedCommand player_state_commands[];
// Moved from kfx_game's lvl_script_commands.c -- pure PLAYER*/
// ALL_PLAYERS name<->ID table with no functional coupling to script
// command parsing; kfx_config is its lowest-ranked real consumer
// (config_rules.c). See docs/refactor/stage-13-enforce-and-document.md.
extern const struct NamedCommand player_desc[];
extern const struct ConfigFileData keeper_playerstates_file_data;

/******************************************************************************/
const char *player_state_code_name(int64_t wrkstate);
struct PlayerStateConfigStats *get_player_state_stats(PlayerState plr_state);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
