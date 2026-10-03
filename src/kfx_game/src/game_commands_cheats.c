/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_commands_cheats.c
 *     Applying cheat and editor actions from a player's packet.
 * @par Purpose:
 *     The cheat cursor states and actions, and the in-game editor's packet
 *     actions. Was kfx_net's packets_cheats.c; moved in refactor pass 2
 *     (S12).
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     30 Jan 2009 - 10 Mar 2022
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "game_commands.h"
#include "packets.h"
#include "net_game.h"
#include "player_data.h"
#include "config_players.h"
#include "thing_creature.h"
#include "player_utils.h"
#include "thing_physics.h"
#include "thing_navigate.h"
#include "player_instances.h"
#include "creature_states.h"
#include "bflib_sound.h"
#include "config_sounds.h"
#include "thing_effects.h"
#include "config_effects.h"
#include "map_utils.h"
#include "map_blocks.h"
#include "map_data.h"
#include "magic_powers.h"
#include "room_util.h"
#include "room_workshop.h"
#include "cursor_tag.h"
#include "bflib_math.h"
#include "room_treasure.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "config_strings.h"
#include "config_crtrmodel.h"
#include "thing_objects.h"
#include "thing_doors.h"
#include "config_trapdoor.h"
#include "thing_list.h"
#include "engine_render.h"
#include <stdlib.h>
#include "player_availability.h"
#include "ports/ui_port.h"
#include "ports/audio_port.h"
#include "ports/editor_port.h"
#include "post_inc.h"

extern void clear_input(struct Packet* packet);

/******************************************************************************/
TbBool terrain_details = false;
// docs/refactor/editor/02-editing-toolbox.md §2.4 -- Terrain "Rectangle"
// mode's drag-start corner, recorded on PCtr_LBtnClick and consumed on
// PCtr_LBtnRelease. File-scope static, single-player-editor-only state
// (same precedent as terrain_details above), not a UserState field: it's
// transient per-drag bookkeeping, not a persisted selection.
static MapSubtlCoord s_rect_drag_stl_x = -1;
static MapSubtlCoord s_rect_drag_stl_y = -1;

// Shared by every "mark a box by dragging, commit once on release" tool
// (Terrain Rectangle mode, Clear-to-Earth -- §2.2/§2.4) so the drag-state
// machine (record the start on Click, draw a live preview box while
// dragging, recognise a valid Release) isn't duplicated per op. Returns
// true exactly on the frame a completed drag should be committed, with
// *out_drag_stl_x/y set to the recorded start -- the caller still packs
// that into its own verb's params (same reasoning as
// PckA_EditorPlaceTerrainRect's own comment: corner 1 can't just be
// pos_x/pos_y like corner 2 is, since a packet has only one position
// field). Box-bounds computation for the actual mutation stays with each
// verb's own handler, not here, since it needs the packet's own
// (already-int32) corners, not this function's subtile-precision ones.
static TbBool editor_rect_drag_update(PlayerNumber plyr_idx, struct Packet *pckt,
    MapSubtlCoord stl_x, MapSubtlCoord stl_y, MapSlabCoord slb_x, MapSlabCoord slb_y,
    MapSubtlCoord *out_drag_stl_x, MapSubtlCoord *out_drag_stl_y)
{
    if ((pckt->control_flags & PCtr_LBtnClick) != 0)
    {
        s_rect_drag_stl_x = stl_x;
        s_rect_drag_stl_y = stl_y;
    }
    if ((s_rect_drag_stl_x >= 0) && is_my_player_number(plyr_idx) && !ui_game_is_busy_doing_gui())
    {
        MapSlabCoord drag_slb_x = subtile_slab(s_rect_drag_stl_x);
        MapSlabCoord drag_slb_y = subtile_slab(s_rect_drag_stl_y);
        MapSlabCoord box_beg_x = min(drag_slb_x, slb_x);
        MapSlabCoord box_beg_y = min(drag_slb_y, slb_y);
        MapSlabCoord box_end_x = max(drag_slb_x, slb_x) + 1;
        MapSlabCoord box_end_y = max(drag_slb_y, slb_y) + 1;
        int64_t floor_height_z = floor_height_for_volume_box(plyr_idx, slb_x, slb_y);
        draw_map_volume_box(subtile_coord(slab_subtile(box_beg_x, 0), 0), subtile_coord(slab_subtile(box_beg_y, 0), 0),
            subtile_coord(slab_subtile(box_end_x, 0), 0), subtile_coord(slab_subtile(box_end_y, 0), 0), floor_height_z, SLC_YELLOW);
    }
    if (((pckt->control_flags & PCtr_LBtnRelease) != 0) && ((pckt->control_flags & PCtr_MapCoordsValid) != 0) && (s_rect_drag_stl_x >= 0))
    {
        *out_drag_stl_x = s_rect_drag_stl_x;
        *out_drag_stl_y = s_rect_drag_stl_y;
        s_rect_drag_stl_x = -1;
        s_rect_drag_stl_y = -1;
        return true;
    }
    return false;
}

// Shared by PckA_EditorPlaceTerrainRect and PckA_EditorRectClearEarth's
// handlers: applies one slab kind/owner to every slab in a box, deleting
// any room slab first exactly like PSt_PlaceTerrain's own single-tile path
// does (same per-tile mutation PckA_CheatPlaceTerrain uses, just looped
// over an area instead of one tile).
static void editor_apply_slab_rect(MapSlabCoord box_beg_x, MapSlabCoord box_beg_y,
    MapSlabCoord box_end_x, MapSlabCoord box_end_y, SlabKind kind, PlayerNumber owner)
{
    for (MapSlabCoord sy = box_beg_y; sy <= box_end_y; sy++)
    {
        for (MapSlabCoord sx = box_beg_x; sx <= box_end_x; sx++)
        {
            place_slab_type_replacing_room(kind, sx, sy, owner);
        }
    }
}

// docs/refactor/editor/09-toolbox-remainder.md §1 -- rect-terrain-op undo.
// Snapshots each slab's pre-mutation kind+owner into a heap buffer (box
// size is unbounded -- a drag can span the whole map, so this can't be a
// fixed-size stack array) and hands it to kfx_editor's journal via the
// callback, which copies it into its own std::vector before this function
// frees it. Called by PckA_EditorPlaceTerrainRect/_RectClearEarth/
// _RectSetOwner's own handlers *before* applying the mutation -- capturing
// the "before" state has to happen here, synchronously in this per-turn
// dispatch, since kfx_editor's journal has no other visibility into "a
// rect op is about to happen" (unlike thing placement, whose journal call
// happens *after* success, since there's nothing to snapshot beforehand).
static void editor_snapshot_slab_rect(unsigned char pcktype, MapSlabCoord box_beg_x, MapSlabCoord box_beg_y,
    MapSlabCoord box_end_x, MapSlabCoord box_end_y, SlabKind new_kind, PlayerNumber new_owner)
{
    int64_t width = box_end_x - box_beg_x + 1;
    int64_t height = box_end_y - box_beg_y + 1;
    int64_t count = width * height;
    struct EditorRectSlabSnapshot *before = malloc(sizeof(struct EditorRectSlabSnapshot) * (size_t)count);
    if (before == NULL)
        return;
    int64_t i = 0;
    for (MapSlabCoord sy = box_beg_y; sy <= box_end_y; sy++)
    {
        for (MapSlabCoord sx = box_beg_x; sx <= box_end_x; sx++)
        {
            struct SlabMap *slb = get_slabmap_block(sx, sy);
            before[i].kind = slb->kind;
            before[i].owner = (unsigned char)slabmap_owner(slb);
            i++;
        }
    }
    editorport_record_rect_terrain(pcktype, box_beg_x, box_beg_y, box_end_x, box_end_y, new_kind, new_owner, before, count);
    free(before);
}

// §2.2 -- "Delete Things Inside" area op's handler. Unlike
// editor_apply_slab_rect() above (a per-slab loop), this needs a *thing*
// enumeration -- walks every subtile in the box's own mapwho linked list
// (the same get_mapwho_thing_index()/next_on_mapblk traversal
// find_base_thing_on_mapwho() already does for one subtile, generalized to
// sweep an area) and deletes whatever it finds, via the eraser tool's own
// per-class dispatch (PSt_DestroyThing above: destroy_door() for doors,
// destroy_effect_thing() for effects, destroy_object() -- which falls
// through to delete_thing_structure() -- for everything else). Skips the
// Dungeon Heart specifically: losing it outright via a big box drag would
// be a much harder mistake to recover from than any other thing this op
// might catch.
static void editor_delete_things_in_rect(MapSlabCoord box_beg_x, MapSlabCoord box_beg_y,
    MapSlabCoord box_end_x, MapSlabCoord box_end_y)
{
    for (MapSlabCoord sy = box_beg_y; sy <= box_end_y; sy++)
    {
        for (MapSlabCoord sx = box_beg_x; sx <= box_end_x; sx++)
        {
            for (int64_t sub_y = 0; sub_y < STL_PER_SLB; sub_y++)
            {
                for (int64_t sub_x = 0; sub_x < STL_PER_SLB; sub_x++)
                {
                    struct Map *mapblk = get_map_block_at(slab_subtile(sx, sub_x), slab_subtile(sy, sub_y));
                    int64_t i = get_mapwho_thing_index(mapblk);
                    while (i != 0)
                    {
                        struct Thing *thing = thing_get(i);
                        if (thing_is_invalid(thing))
                            break;
                        // Capture next before this thing might be deleted --
                        // deletion invalidates next_on_mapblk on `thing` itself.
                        i = thing->next_on_mapblk;
                        if (thing_is_dungeon_heart(thing))
                            continue;
                        if (thing->class_id == TCls_Door)
                            destroy_door(thing);
                        else if (thing->class_id == TCls_Effect)
                            destroy_effect_thing(thing);
                        else
                            destroy_object(thing);
                    }
                }
            }
        }
    }
}

// §2.2 -- "Set Owner" area op's handler. Room slabs need real ownership
// transfer, not a raw owner-byte rewrite (rooms carry dungeon-tracking
// state -- area totals, slab lists, room index -- keyed by owner). User's
// own suggested approach: delete_room_slab() first (tears the room slab
// down to ground *and* properly updates the old owner's room-area
// accounting/slab list -- the exact same call Paint/Clear Earth already
// make for a *kind* change, just applied here for an owner-only change
// instead), then place_slab_type_on_map() with the *same* kind but the
// *new* owner rebuilds it as a room the new owner actually owns. Ownerless
// kinds (rock, gems, lava, ...) are skipped, same check
// PckA_CheatSwitchTerrain's own "no ownership" handling uses elsewhere in
// this file -- setting an owner on them wouldn't mean anything.
static void editor_set_owner_rect(MapSlabCoord box_beg_x, MapSlabCoord box_beg_y,
    MapSlabCoord box_end_x, MapSlabCoord box_end_y, PlayerNumber owner)
{
    for (MapSlabCoord sy = box_beg_y; sy <= box_end_y; sy++)
    {
        for (MapSlabCoord sx = box_beg_x; sx <= box_end_x; sx++)
        {
            struct SlabMap *slb = get_slabmap_block(sx, sy);
            if (slab_kind_has_no_ownership(slb->kind))
                continue;
            place_slab_type_replacing_room(slb->kind, sx, sy, owner);
        }
    }
}
/******************************************************************************/

TbBool packets_process_cheats(
        NetUserId user,
        PlayerNumber plyr_idx,
        MapCoord x, MapCoord y,
        struct Packet* pckt,
        MapSubtlCoord stl_x, MapSubtlCoord stl_y,
        MapSlabCoord slb_x, MapSlabCoord slb_y)
{
    struct Thing *thing;
    struct Room* room = NULL;
    int64_t i;
    PowerKind pwkind;
    struct SlabMap *slb;
    struct PlayerInfo* player = get_player(plyr_idx);
    struct UserState* ustate = get_user_state(user);
    TbBool allowed;
    char str[255] = "";
    switch (player->work_state)
    {
        case PSt_MkDigger:
        player->render_roomspace = create_box_roomspace(player->render_roomspace, 1, 1, slb_x, slb_y);
        allowed = tag_cursor_blocks_place_thing(plyr_idx, stl_x, stl_y);
        ui_clear_messages_from_player(MsgType_Player, ustate->cheatselection.chosen_player);
        snprintf(str, sizeof(str), "%" PRId64, (int64_t)(ustate->cheatselection.chosen_experience_level + 1));
        ui_targeted_message_add(MsgType_Player, ustate->cheatselection.chosen_player, plyr_idx, 1, str);
        if (((pckt->control_flags & PCtr_LBtnRelease) != 0) && ((pckt->control_flags & PCtr_MapCoordsValid) != 0))
        {
            if (allowed)
            {
                set_packet_action(pckt, PckA_CheatMakeDigger, ustate->cheatselection.chosen_player, ustate->cheatselection.chosen_experience_level, 0, 0);
            }
            else
            {
                if (is_my_player(player))
                {
                    play_non_3d_sample(snd_refusal);
                }
            }
            unset_packet_control(pckt, PCtr_LBtnRelease);
        }
        break;
        case PSt_MkGoodCreatr:
        player->render_roomspace = create_box_roomspace(player->render_roomspace, 1, 1, slb_x, slb_y);
        allowed = tag_cursor_blocks_place_thing(plyr_idx, stl_x, stl_y);
        ui_clear_messages_from_player(MsgType_Player, ustate->cheatselection.chosen_player);
        if (ustate->cheatselection.chosen_hero_kind == 0)
        {
            snprintf(str, sizeof(str), "?");
        }
        else
        {
            struct CreatureModelConfig* crconf = creature_stats_get(ustate->cheatselection.chosen_hero_kind);
            snprintf(str, sizeof(str), "%s %" PRId64, get_string(crconf->namestr_idx), (int64_t)(ustate->cheatselection.chosen_experience_level + 1));
        }
        ui_targeted_message_add(MsgType_Player, ustate->cheatselection.chosen_player, plyr_idx, 1, str);
        if (((pckt->control_flags & PCtr_LBtnRelease) != 0) && ((pckt->control_flags & PCtr_MapCoordsValid) != 0))
        {
            if (allowed)
            {
                ThingModel crmodel;
                unsigned char exp;
                if (ustate->cheatselection.chosen_hero_kind == 0)
                {
                    while (1)
                    {
                        crmodel = GAME_RANDOM(kfx_config_state.conf.crtr_conf.model_count) + 1;
                        if (crmodel >= kfx_config_state.conf.crtr_conf.model_count)
                        {
                            continue;
                        }
                        struct CreatureModelConfig* crconf = creature_stats_get(crmodel);
                        if ((crconf->model_flags & CMF_IsSpectator) != 0)
                        {
                            continue;
                        }
                        if ((crconf->model_flags & CMF_IsEvil) == 0)
                        {
                            break;
                        }
                    }
                    exp = GAME_RANDOM(CREATURE_MAX_LEVEL);
                }
                else
                {
                    crmodel = ustate->cheatselection.chosen_hero_kind;
                    exp = ustate->cheatselection.chosen_experience_level;
                }
                int64_t param2 = ustate->cheatselection.chosen_player | (exp << 8);
                set_packet_action(pckt, PckA_CheatMakeCreature, crmodel, param2, 0, 0);
            }
            else
            {
                if (is_my_player(player))
                {
                    play_non_3d_sample(snd_refusal);
                }
            }
            unset_packet_control(pckt, PCtr_LBtnRelease);
        }
        break;
        case PSt_MkGoldPot:
        player->render_roomspace = create_box_roomspace(player->render_roomspace, 1, 1, slb_x, slb_y);
        allowed = tag_cursor_blocks_place_thing(plyr_idx, stl_x, stl_y);
        if (((pckt->control_flags & PCtr_LBtnRelease) != 0) && ((pckt->control_flags & PCtr_MapCoordsValid) != 0))
        {
            if (allowed)
            {
                thing = create_gold_pot_at(x, y, player->id_number);
                if (!thing_is_invalid(thing))
                {
                    if (thing_in_wall_at(thing, &thing->mappos))
                    {
                        move_creature_to_nearest_valid_position(thing);
                    }
                    room = subtile_room_get(stl_x, stl_y);
                    if (room_exists(room))
                    {
                        if (room_role_matches(room->kind,RoRoF_GoldStorage))
                        {
                            count_gold_hoardes_in_room(room);
                        }
                    }
                }
            }
            else
            {
                if (is_my_player(player))
                {
                    play_non_3d_sample(snd_refusal);
                }
            }
            unset_packet_control(pckt, PCtr_LBtnRelease);
        }
        break;
        case PSt_OrderCreatr:
        thing = get_creature_near(x, y);
        if (!thing_is_creature(thing))
            player->thing_under_hand = 0;
        else
            player->thing_under_hand = thing->index;
        thing = thing_get(player->controlled_thing_idx);
        if (thing_is_creature(thing))
        {
            player->render_roomspace = create_box_roomspace(player->render_roomspace, 1, 1, slb_x, slb_y);
            allowed = tag_cursor_blocks_order_creature(plyr_idx, stl_x, stl_y, thing);
        }
        else
        {
            allowed = false;
        }
        if ((pckt->control_flags & PCtr_LBtnRelease) != 0)
        {
          if (player->thing_under_hand > 0)
          {
            if (player->controlled_thing_idx != player->thing_under_hand)
            {
                player->influenced_thing_idx = player->thing_under_hand;
                player->influenced_thing_creation = thing->creation_turn;
            }
          }
          if ((player->controlled_thing_idx > 0) && (player->controlled_thing_idx < THINGS_COUNT))
          {
            if ( (stl_x == thing->mappos.x.stl.num) && (stl_y == thing->mappos.y.stl.num) )
            {
                set_start_state(thing);
                clear_selected_thing(player);
            }
            else if ((pckt->control_flags & PCtr_MapCoordsValid) != 0)
            {
              if (allowed)
              {
                if (!setup_person_move_to_position(thing, stl_x, stl_y, NavRtF_Default))
                    WARNLOG("Move %s order failed",thing_model_name(thing));
                thing->continue_state = CrSt_ManualControl;
              }
              else
              {
                if (is_my_player(player))
                {
                    play_non_3d_sample(snd_refusal);
                }
              }
            }
          } else
          {
            thing = get_creature_near(x, y);
            if (!thing_is_invalid(thing))
            {
                set_selected_creature(player, thing);
                initialise_thing_state(thing, CrSt_ManualControl);
                if (creature_is_group_member(thing)) {
                    make_group_member_leader(thing);
                }
            }
          }
          unset_packet_control(pckt, PCtr_LBtnRelease);
        }
        break;
        case PSt_MkBadCreatr:
        player->render_roomspace = create_box_roomspace(player->render_roomspace, 1, 1, slb_x, slb_y);
        allowed = tag_cursor_blocks_place_thing(plyr_idx, stl_x, stl_y);
        ui_clear_messages_from_player(MsgType_Player, ustate->cheatselection.chosen_player);
        if (ustate->cheatselection.chosen_creature_kind == 0)
        {
            snprintf(str, sizeof(str), "?");
        }
        else
        {
            struct CreatureModelConfig* crconf = creature_stats_get(ustate->cheatselection.chosen_creature_kind);
            snprintf(str, sizeof(str), "%s %" PRId64, get_string(crconf->namestr_idx), (int64_t)(ustate->cheatselection.chosen_experience_level + 1));
        }
        ui_targeted_message_add(MsgType_Player, ustate->cheatselection.chosen_player, plyr_idx, 1, str);
        if (((pckt->control_flags & PCtr_LBtnRelease) != 0) && ((pckt->control_flags & PCtr_MapCoordsValid) != 0))
        {
            if (allowed)
            {
                ThingModel crmodel;
                unsigned char exp;
                if (ustate->cheatselection.chosen_creature_kind == 0)
                {
                    while (1)
                    {
                        crmodel = GAME_RANDOM(kfx_config_state.conf.crtr_conf.model_count) + 1;
                        struct CreatureModelConfig* crconf = creature_stats_get(crmodel);
                        if ((crconf->model_flags & CMF_IsSpectator) != 0) {
                            continue;
                        }
                        if ((crconf->model_flags & CMF_IsEvil) != 0) {
                            break;
                        }
                    }
                    exp = GAME_RANDOM(CREATURE_MAX_LEVEL);
                }
                else
                {
                    crmodel = ustate->cheatselection.chosen_creature_kind;
                    exp = ustate->cheatselection.chosen_experience_level;
                }
                int64_t param2 = ustate->cheatselection.chosen_player | (exp << 8);
                set_packet_action(pckt, PckA_CheatMakeCreature, crmodel, param2, 0, 0);
            }
            else
            {
                if (is_my_player(player))
                {
                    play_non_3d_sample(snd_refusal);
                }
            }
            unset_packet_control(pckt, PCtr_LBtnRelease);
        }
        break;
        case PSt_FreeDestroyWalls:
            if (((pckt->control_flags & PCtr_LBtnRelease) != 0) && ((pckt->control_flags & PCtr_MapCoordsValid) != 0))
            {
                pwkind = ustate->chosen_power_kind;
                i = get_power_overcharge_level(player);
                magic_use_power_direct(plyr_idx,pwkind,i,stl_x, stl_y,INVALID_THING, PwMod_CastForFree);
                unset_packet_control(pckt, PCtr_LBtnRelease);
            }
            break;
        case PSt_FreeTurnChicken:
        case PSt_FreeCastDisease:
        {
            pwkind = ustate->chosen_power_kind;
            thing = get_creature_near_to_be_keeper_power_target(x, y, pwkind, plyr_idx);
            if (thing_is_invalid(thing))
            {
                player->thing_under_hand = 0;
                break;
            }
            player->thing_under_hand = thing->index;
            if ((pckt->control_flags & PCtr_LBtnRelease) != 0)
            {
                KeepPwrLevel power_level = get_power_overcharge_level(player);
                magic_use_power_direct(plyr_idx,pwkind,power_level,stl_x,stl_y,thing,PwMod_CastForFree);
                unset_packet_control(pckt, PCtr_LBtnRelease);
            }
            break;
        }
        case PSt_StealRoom:
        ui_clear_messages_from_player(MsgType_Player, ustate->cheatselection.chosen_player);
        slb = get_slabmap_block(slb_x, slb_y);
        room = room_get(slb->room_index);
        allowed = ( (room_exists(room)) && (room->owner != ustate->cheatselection.chosen_player) );
        if (allowed)
        {
            snprintf(str, sizeof(str), "%s", get_string(GUIStr_MnuOk));
        }
        ui_targeted_message_add(MsgType_Player, ustate->cheatselection.chosen_player, plyr_idx, 1, str);
        if (((pckt->control_flags & PCtr_LBtnRelease) != 0) && ((pckt->control_flags & PCtr_MapCoordsValid) != 0))
        {
            if (allowed)
            {
                TbBool effect = ((pckt->control_flags & PCtr_ModRAlt) != 0);
                set_packet_action(pckt, PckA_CheatStealRoom, ustate->cheatselection.chosen_player, effect, 0, 0);
            }
            unset_packet_control(pckt, PCtr_LBtnRelease);
        }
        break;
        case PSt_DestroyRoom:
        ui_clear_messages_from_player(MsgType_Blank, -1);
        slb = get_slabmap_block(slb_x, slb_y);
        room = room_get(slb->room_index);
        allowed = (room_exists(room));
        if (allowed)
        {
            ui_targeted_message_add(MsgType_Blank, 0, plyr_idx, 1, get_string(GUIStr_MnuOk));
        }
        if (((pckt->control_flags & PCtr_LBtnRelease) != 0) && ((pckt->control_flags & PCtr_MapCoordsValid) != 0))
        {
            if (allowed)
            {
                destroy_room_leaving_unclaimed_ground(room, false);
            }
            unset_packet_control(pckt, PCtr_LBtnRelease);
        }
        break;
        case PSt_KillCreatr:
            thing = get_creature_near(x, y);
            if (!thing_is_creature(thing))
            {
                player->thing_under_hand = 0;
            }
            else
            {
                player->thing_under_hand = thing->index;
            }
            if (((pckt->control_flags & PCtr_LBtnRelease) != 0) && ((pckt->control_flags & PCtr_MapCoordsValid) != 0))
            {
                if (player->thing_under_hand > 0)
                {
                    kill_creature(thing, INVALID_THING, -1, CrDed_NoUnconscious);
                }
                unset_packet_control(pckt, PCtr_LBtnRelease);
            }
            break;
        case PSt_ConvertCreatr:
        ui_clear_messages_from_player(MsgType_Player, ustate->cheatselection.chosen_player);
        ui_targeted_message_add(MsgType_Player, ustate->cheatselection.chosen_player, plyr_idx, 1, str);
        thing = get_creature_near(x, y);
        if ((!thing_is_creature(thing)) || (thing->owner == ustate->cheatselection.chosen_player))
        {
            player->thing_under_hand = 0;
        }
        else
        {
            player->thing_under_hand = thing->index;
        }
        if (((pckt->control_flags & PCtr_LBtnRelease) != 0) && ((pckt->control_flags & PCtr_MapCoordsValid) != 0))
        {
            set_packet_action(pckt, PckA_CheatConvertCreature, ustate->cheatselection.chosen_player, 0, 0, 0);
            unset_packet_control(pckt, PCtr_LBtnRelease);
        }
        break;
        case PSt_StealSlab:
        player->render_roomspace = create_box_roomspace(player->render_roomspace, 1, 1, slb_x, slb_y);
        allowed = tag_cursor_blocks_steal_slab(user, stl_x, stl_y);
        ui_clear_messages_from_player(MsgType_Player, ustate->cheatselection.chosen_player);
        ui_targeted_message_add(MsgType_Player, ustate->cheatselection.chosen_player, plyr_idx, 1, str);
        if (((pckt->control_flags & PCtr_LBtnRelease) != 0) && ((pckt->control_flags & PCtr_MapCoordsValid) != 0))
        {
            if (allowed)
            {
                slb = get_slabmap_block(slb_x, slb_y);
                if (slb->kind >= SlbT_EARTH && slb->kind <= SlbT_CLAIMED)
                {
                    SlabKind slbkind;
                    switch(slb->kind)
                    {
                        case SlbT_PATH:
                        {
                            slbkind = SlbT_CLAIMED;
                            break;
                        }
                        case SlbT_EARTH:
                        {
                            if ((pckt->control_flags & PCtr_ModRShift) != 0)
                            {
                                slbkind = choose_pretty_type(ustate->cheatselection.chosen_player, slb_x, slb_y);
                            }
                            else
                            {
                                slbkind = SlbT_WALLDRAPE + GAME_RANDOM(5);
                            }
                            break;
                        }
                        case SlbT_TORCHDIRT:
                        {
                            if ((pckt->control_flags & PCtr_ModRShift) != 0)
                            {
                                slbkind = choose_pretty_type(ustate->cheatselection.chosen_player, slb_x, slb_y);
                            }
                            else
                            {
                                slbkind = SlbT_WALLTORCH;
                            }
                            break;
                        }
                        default:
                        {
                            slbkind = slb->kind;
                            break;
                        }
                    }
                    TbBool effect;
                    if ((slbkind == SlbT_CLAIMED) || ((slbkind >= SlbT_WALLDRAPE) && (slbkind <= SlbT_WALLPAIRSHR)))
                    {
                        effect = ((pckt->control_flags & PCtr_ModRAlt) != 0);
                    }
                    else
                    {
                        effect = false;
                    }
                    int64_t param2 = ustate->cheatselection.chosen_player | (effect << 8);
                    set_packet_action(pckt, PckA_CheatStealSlab, slbkind, param2, 0, 0);
                }
            }
            else
            {
                if (is_my_player(player))
                {
                    play_non_3d_sample(snd_refusal);
                }
            }
            unset_packet_control(pckt, PCtr_LBtnRelease);
        }
        break;
        case PSt_LevelCreatureUp:
        case PSt_LevelCreatureDown:
            thing = get_creature_near(x, y);
            if (!thing_is_creature(thing))
            {
                player->thing_under_hand = 0;
            }
            else
            {
                player->thing_under_hand = thing->index;
            }
            if (((pckt->control_flags & PCtr_LBtnRelease) != 0) && ((pckt->control_flags & PCtr_MapCoordsValid) != 0))
            {
                if (player->thing_under_hand > 0)
                {
                    switch (player->work_state)
                    {
                        case PSt_LevelCreatureUp:
                        {
                            creature_increase_level(thing);
                            break;
                        }
                        case PSt_LevelCreatureDown:
                        {
                            struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
                            if (!creature_control_invalid(cctrl))
                            {
                                set_creature_level(thing, cctrl->exp_level-1);
                            }
                            break;
                        }
                    }
                }
                unset_packet_control(pckt, PCtr_LBtnRelease);
            }
            break;
        case PSt_KillPlayer:
          ui_clear_messages_from_player(MsgType_Player, ustate->cheatselection.chosen_player);
          struct PlayerInfo* PlayerToKill = get_player(ustate->cheatselection.chosen_player);
          if (player_exists(PlayerToKill))
          {
              ui_targeted_message_add(MsgType_Player, ustate->cheatselection.chosen_player, plyr_idx, 1, str);
              if ((pckt->control_flags & PCtr_LBtnRelease) != 0)
              {
                set_packet_action(pckt, PckA_CheatKillPlayer, PlayerToKill->id_number, 0, 0, 0);
                unset_packet_control(pckt, PCtr_LBtnRelease);
              }
          }
        break;
        case PSt_HeartHealth:
        ui_clear_messages_from_player(MsgType_Player, ustate->cheatselection.chosen_player);
        thing = get_player_soul_container(ustate->cheatselection.chosen_player);
        struct ObjectConfigStats* objst = get_object_model_stats(thing->model);
        if (thing_exists(thing))
        {
            snprintf(str, sizeof(str), "%" PRId64 "/%" PRId64, (int64_t)(thing->health), (int64_t)(objst->health));
            ui_targeted_message_add(MsgType_Player, thing->owner, plyr_idx, 1, str);
        }
        else
        {
            break;
        }
        HitPoints new_health = thing->health;
        TbBool changed = true;
        switch ((pckt->control_flags & PCtr_HeartHealthMask) >> PCtr_HeartHealthShift)
        {
        case PHHS_Up1:
            changed = (new_health < objst->health);
            if (changed)
                new_health++;
            break;
        case PHHS_Down1:
            new_health--;
            break;
        case PHHS_Up100:
            new_health += 100;
            break;
        case PHHS_Down100:
            new_health -= 100;
            break;
        default:
            changed = false;
            break;
        }
        if (changed)
        {
            set_packet_action(pckt, PckA_CheatHeartHealth, ustate->cheatselection.chosen_player, new_health, 0, 0);
        }
        break;
        case PSt_QueryAll:
        case PSt_CreatrInfoAll:
            thing = get_creature_near(x, y);
            TbBool CanQuery = false;
            if (thing_is_creature(thing))
            {
                CanQuery = true;
            }
            else
            {
                thing = get_nearest_thing_at_position(stl_x, stl_y);
                CanQuery = (!thing_is_invalid(thing));
                if (!CanQuery)
                {
                    room = subtile_room_get(stl_x, stl_y);
                }
            }
            if (!CanQuery)
            {
                player->thing_under_hand = 0;
            }
            else
            {
                player->thing_under_hand = thing->index;
            }
            if ((pckt->control_flags & PCtr_LBtnRelease) != 0)
            {
                if (player->thing_under_hand > 0)
                {
                    if (thing->class_id == TCls_Creature)
                    {
                        if (player->controlled_thing_idx != player->thing_under_hand)
                        {
                            query_creature(player, player->thing_under_hand, true, false);
                        }
                    }
                    query_thing(thing, (pckt->control_flags & PCtr_ModLAlt) != 0);
                    unset_packet_control(pckt, PCtr_LBtnRelease);
                }
                else if (room_exists(room) )
                {
                    query_room(room);
                }
            }
            if ( player->work_state == PSt_CreatrInfoAll )
            {
                thing = thing_get(player->controlled_thing_idx);
                if ((pckt->control_flags & PCtr_RBtnRelease) != 0)
                {
                    if (is_my_player(player))
                    {
                        ui_turn_off_query_menus();
                        ui_turn_on_main_panel_menu();
                    }
                    set_player_instance(player, PI_UnqueryCrtr, 0);
                    unset_packet_control(pckt, PCtr_RBtnRelease);
                } else
                if (creature_is_dying(thing))
                {
                    set_player_instance(player, PI_UnqueryCrtr, 0);
                    if (is_my_player(player))
                    {
                        ui_turn_off_query_menus();
                        ui_turn_on_main_panel_menu();
                    }
                }
            }
            break;
        case PSt_MkHappy:
        case PSt_MkAngry:
            thing = get_creature_near(x, y);
            if (!thing_is_creature(thing))
            {
                player->thing_under_hand = 0;
            }
            else
            {
                player->thing_under_hand = thing->index;
            }
            if ((pckt->control_flags & PCtr_LBtnRelease) != 0)
            {
                if (player->thing_under_hand > 0)
                {
                    if (player->work_state == PSt_MkHappy)
                    {
                        anger_set_creature_anger_all_types(thing, 0);
                    }
                    else if (player->work_state == PSt_MkAngry)
                    {
                        anger_set_creature_anger_all_types(thing, 10000);
                    }
                    unset_packet_control(pckt, PCtr_LBtnRelease);
                }
            }
            break;
        case PSt_PlaceTerrain:
        {
            player->render_roomspace = create_box_roomspace(player->render_roomspace, 1, 1, slb_x, slb_y);
            tag_cursor_blocks_place_terrain(plyr_idx, stl_x, stl_y);
            struct SlabConfigStats* slab_cfgstats;
            ui_clear_messages_from_player(MsgType_Player, ustate->cheatselection.chosen_player);
            struct SlabConfigStats *slabst = get_slab_kind_stats(ustate->cheatselection.chosen_terrain_kind);
            if (slab_kind_has_no_ownership(ustate->cheatselection.chosen_terrain_kind))
            {
                ustate->cheatselection.chosen_player = kfx_config_state.neutral_player_num;
            }
            if (slabst->tooltip_stridx <= GUI_STRINGS_COUNT)
            {
                const char* msg = get_string(slabst->tooltip_stridx);
                strcpy(str, msg);
                char* dis_msg = strtok(str, ":");
                if (dis_msg == NULL)
                {
                    dis_msg = str;
                }
                ui_targeted_message_add(MsgType_Player, ustate->cheatselection.chosen_player, plyr_idx, 1, dis_msg);
            }
            else
            {
                slab_cfgstats = get_slab_kind_stats(ustate->cheatselection.chosen_terrain_kind);
                ui_targeted_message_add(MsgType_Player, ustate->cheatselection.chosen_player, plyr_idx, 1, slab_cfgstats->code_name);
            }
            ui_clear_messages_from_player(MsgType_Blank, -1);
            if ((pckt->control_flags & PCtr_ToggleDetails) != 0)
            {
                terrain_details ^= 1;
            }
            if (terrain_details)
            {
                slb = get_slabmap_block(slb_x, slb_y);
                slab_cfgstats = get_slab_kind_stats(slb->kind);
                snprintf(str, sizeof(str), "%s (%" PRId64 ") %" PRId64 " %" PRId64 " (%" PRIu64 ") %" PRId64 " %" PRId64 " (%" PRId64 ")", slab_cfgstats->code_name, (int64_t)(slabmap_owner(slb)), (int64_t)(slb_x), (int64_t)(slb_y), (uint64_t)(get_slab_number(slb_x, slb_y)), (int64_t)(stl_x), (int64_t)(stl_y), (int64_t)(get_subtile_number(stl_x, stl_y)));
                ui_targeted_message_add(MsgType_Blank, 0, plyr_idx, 1, str);
            }
            // docs/refactor/editor/02-editing-toolbox.md §2.2 -- drag
            // painting. Originally PCtr_LBtnRelease-only (a single tile
            // per click, matching the classic cheat menu's own behaviour);
            // also firing on PCtr_LBtnHeld lets holding the button down
            // and dragging across the map paint every tile the cursor
            // passes over, like a normal paint-tool brush, without pulling
            // in room placement's separate roomspace-mark/cost/undo
            // machinery (§2.2's own fuller "mark rectangle, apply once"
            // design is still future work -- this is the simpler
            // continuous-paint half of it).
            if (((pckt->control_flags & (PCtr_LBtnRelease | PCtr_LBtnHeld)) != 0) && ((pckt->control_flags & PCtr_MapCoordsValid) != 0))
            {
                if (subtile_is_room(stl_x, stl_y))
                {
                    room = subtile_room_get(stl_x, stl_y);
                    delete_room_slab(slb_x, slb_y, true);
                }
                PlayerNumber id = (slab_kind_has_no_ownership(ustate->cheatselection.chosen_terrain_kind)) ? kfx_config_state.neutral_player_num : ustate->cheatselection.chosen_player;
                set_packet_action(pckt, PckA_CheatPlaceTerrain, ustate->cheatselection.chosen_terrain_kind, id, 0, 0);
                if ( (ustate->cheatselection.chosen_terrain_kind >= SlbT_WALLDRAPE) && (ustate->cheatselection.chosen_terrain_kind <= SlbT_WALLPAIRSHR) )
                {
                    ustate->cheatselection.chosen_terrain_kind = SlbT_WALLDRAPE + GAME_RANDOM(5);
                }
            }
            unset_packet_control(pckt, PCtr_LBtnRelease);
            break;
        }
        case PSt_EditorPlaceTerrainRect:
        {
            // §2.4 -- "Rectangle" mode: mark a box by dragging, commit the
            // whole box in one action on release, instead of Brush's
            // continuous per-tile paint above. Reuses the exact same
            // picker/selection state as PSt_PlaceTerrain
            // (chosen_terrain_kind/chosen_player) -- only the click
            // handling differs, so switching the toolbox's Brush/Rectangle
            // toggle just changes which of these two work states the
            // Terrain tool button sends. Drag-tracking factored into
            // editor_rect_drag_update(), shared with Clear-to-Earth below.
            player->render_roomspace = create_box_roomspace(player->render_roomspace, 1, 1, slb_x, slb_y);
            tag_cursor_blocks_place_terrain(plyr_idx, stl_x, stl_y);
            MapSubtlCoord drag_stl_x, drag_stl_y;
            if (editor_rect_drag_update(plyr_idx, pckt, stl_x, stl_y, slb_x, slb_y, &drag_stl_x, &drag_stl_y))
            {
                PlayerNumber id = (slab_kind_has_no_ownership(ustate->cheatselection.chosen_terrain_kind)) ? kfx_config_state.neutral_player_num : ustate->cheatselection.chosen_player;
                set_packet_action(pckt, PckA_EditorPlaceTerrainRect, drag_stl_x, drag_stl_y, ustate->cheatselection.chosen_terrain_kind, id);
            }
            unset_packet_control(pckt, PCtr_LBtnRelease);
            break;
        }
        case PSt_EditorRectClearEarth:
        {
            // §2.2 -- "Clear to Earth" area op: same drag/commit shape as
            // Rectangle mode above, fixed target kind/owner instead of the
            // picker's current selection.
            player->render_roomspace = create_box_roomspace(player->render_roomspace, 1, 1, slb_x, slb_y);
            tag_cursor_blocks_place_terrain(plyr_idx, stl_x, stl_y);
            MapSubtlCoord drag_stl_x, drag_stl_y;
            if (editor_rect_drag_update(plyr_idx, pckt, stl_x, stl_y, slb_x, slb_y, &drag_stl_x, &drag_stl_y))
            {
                set_packet_action(pckt, PckA_EditorRectClearEarth, drag_stl_x, drag_stl_y, 0, 0);
            }
            unset_packet_control(pckt, PCtr_LBtnRelease);
            break;
        }
        case PSt_EditorRectDeleteThings:
        {
            // §2.2 -- "Delete Things Inside" area op: same drag/commit
            // shape as Clear Earth, but the release handler sweeps for
            // things rather than repainting slabs.
            player->render_roomspace = create_box_roomspace(player->render_roomspace, 1, 1, slb_x, slb_y);
            tag_cursor_blocks_place_terrain(plyr_idx, stl_x, stl_y);
            MapSubtlCoord drag_stl_x, drag_stl_y;
            if (editor_rect_drag_update(plyr_idx, pckt, stl_x, stl_y, slb_x, slb_y, &drag_stl_x, &drag_stl_y))
            {
                set_packet_action(pckt, PckA_EditorRectDeleteThings, drag_stl_x, drag_stl_y, 0, 0);
            }
            unset_packet_control(pckt, PCtr_LBtnRelease);
            break;
        }
        case PSt_EditorRectSetOwner:
        {
            // §2.2 -- "Set Owner" area op: same drag/commit shape again,
            // target owner is the bottom bar's own chosen_player (no new
            // picker needed).
            player->render_roomspace = create_box_roomspace(player->render_roomspace, 1, 1, slb_x, slb_y);
            tag_cursor_blocks_place_terrain(plyr_idx, stl_x, stl_y);
            MapSubtlCoord drag_stl_x, drag_stl_y;
            if (editor_rect_drag_update(plyr_idx, pckt, stl_x, stl_y, slb_x, slb_y, &drag_stl_x, &drag_stl_y))
            {
                set_packet_action(pckt, PckA_EditorRectSetOwner, drag_stl_x, drag_stl_y, ustate->cheatselection.chosen_player, 0);
            }
            unset_packet_control(pckt, PCtr_LBtnRelease);
            break;
        }
        case PSt_EditorFill:
        {
            // docs/refactor/editor/02-editing-toolbox.md §2.3 -- reuses
            // the same chosen_terrain_kind/chosen_player selection the
            // Terrain tool's palette already sets (no new selection state
            // needed); release-only, not held -- one fill per click, not
            // one per subtile crossed while dragging.
            player->render_roomspace = create_box_roomspace(player->render_roomspace, 1, 1, slb_x, slb_y);
            tag_cursor_blocks_place_terrain(plyr_idx, stl_x, stl_y);
            if (((pckt->control_flags & PCtr_LBtnRelease) != 0) && ((pckt->control_flags & PCtr_MapCoordsValid) != 0))
            {
                PlayerNumber id = (slab_kind_has_no_ownership(ustate->cheatselection.chosen_terrain_kind)) ? kfx_config_state.neutral_player_num : ustate->cheatselection.chosen_player;
                set_packet_action(pckt, PckA_EditorFloodFill, ustate->cheatselection.chosen_terrain_kind, id, 0, 0);
            }
            unset_packet_control(pckt, PCtr_LBtnRelease);
            break;
        }
        case PSt_EditorPlaceObject:
            // docs/refactor/editor/02-editing-toolbox.md §2.6 -- no
            // placement dispatch here on purpose: kfx_editor sends
            // PckA_EditorPlaceObject directly once it sees a world click,
            // since the chosen object model has nowhere to live in
            // CheatSelection (F17) for this switch to read back. Just the
            // usual cursor-highlight, so hovering shows the same
            // solid/non-solid feedback other placement tools give.
            player->render_roomspace = create_box_roomspace(player->render_roomspace, 1, 1, slb_x, slb_y);
            tag_cursor_blocks_place_thing(plyr_idx, stl_x, stl_y);
            break;
        case PSt_EditorEyedropper:
            // §2.10 -- same "no dispatch here, kfx_editor watches its own
            // click" shape as Objects: the picked kind/owner aren't known
            // until the click happens. Reuse the Terrain highlight so
            // hovering shows the same cursor feedback the Terrain tool has.
            player->render_roomspace = create_box_roomspace(player->render_roomspace, 1, 1, slb_x, slb_y);
            tag_cursor_blocks_place_terrain(plyr_idx, stl_x, stl_y);
            break;
        case PSt_EditorStamp:
            // §2.4 -- capture (RMB-drag) and stamp (LMB-click) both happen
            // entirely in kfx_editor (handle_brush_capture_and_stamp(),
            // editor_toolbox.cpp), calling sim mutation primitives
            // directly rather than through a packet -- see this work
            // state's own comment (config_players.h) for why. Just the
            // usual cursor highlight here.
            player->render_roomspace = create_box_roomspace(player->render_roomspace, 1, 1, slb_x, slb_y);
            tag_cursor_blocks_place_terrain(plyr_idx, stl_x, stl_y);
            break;
        case PSt_EditorQuery:
            // §2.10 -- no dispatch here on purpose: kfx_editor handles the
            // whole query (creature or otherwise) itself, directly, to
            // avoid the classic GMnu_MSG_BOX popup query_thing()/
            // query_room() would otherwise show -- see this work state's
            // own comment (config_players.h). Just the usual cursor
            // highlight, so hovering something shows the same feedback
            // other tools give.
            player->render_roomspace = create_box_roomspace(player->render_roomspace, 1, 1, slb_x, slb_y);
            tag_cursor_blocks_place_thing(plyr_idx, stl_x, stl_y);
            break;
        case PSt_EditorPlaceTrap:
        {
            // docs/refactor/editor/02-editing-toolbox.md §2.7 -- free
            // placement (no workshop-stock check) for whichever owner the
            // bottom bar has selected -- ustate->chosen_trap_kind is set by
            // the picker via PckA_CheatSwitchTrap. No occupancy/validity
            // gate yet (matches Objects' own first-slice scope: bare
            // click-to-place, refinements deferred) -- traps don't have
            // Doors' create_door() out-of-bounds risk on a bad position, so
            // there's nothing here that *must* be checked before placing.
            player->render_roomspace = create_box_roomspace(player->render_roomspace, 1, 1, slb_x, slb_y);
            tag_cursor_blocks_place_trap(plyr_idx, stl_x, stl_y, ustate->chosen_trap_kind);
            if (((pckt->control_flags & PCtr_LBtnRelease) != 0) && ((pckt->control_flags & PCtr_MapCoordsValid) != 0)
                && thing_is_invalid(find_base_thing_on_mapwho(TCls_Trap, 0, stl_x, stl_y))) // one trap per subtile
            {
                set_packet_action(pckt, PckA_EditorPlaceTrap, ustate->chosen_trap_kind, ustate->cheatselection.chosen_player, 0, 0);
            }
            unset_packet_control(pckt, PCtr_LBtnRelease);
            break;
        }
        case PSt_EditorPlaceDoor:
        {
            // Doors need at least one real validity check before placing,
            // unlike Traps: create_door() indexes doorst->slbkind[orient]
            // with whatever find_door_angle() returns, and that's -1 (an
            // out-of-bounds read, not just a visual glitch) unless this
            // slab is SlbT_CLAIMED and owned by the chosen owner -- see
            // find_door_angle()'s own doc-quoted rule ("only on the
            // selected player's claimed floor, between two walls"). Check
            // it here against the *chosen* owner (thing_doors.h), not
            // tag_cursor_blocks_place_door()'s own plyr_idx-based check,
            // which also drags in fog-of-war/is_my_player_number gating
            // that doesn't make sense for an editor session placing on
            // behalf of an arbitrary owner.
            player->render_roomspace = create_box_roomspace(player->render_roomspace, 1, 1, slb_x, slb_y);
            allowed = (find_door_angle(stl_x, stl_y, ustate->cheatselection.chosen_player) != -1);
            tag_cursor_blocks_place_door(plyr_idx, stl_x, stl_y);
            if (((pckt->control_flags & PCtr_LBtnRelease) != 0) && ((pckt->control_flags & PCtr_MapCoordsValid) != 0))
            {
                // §2.7's own deferred "Ctrl+LMB toggles lock" item. Checked
                // first, before normal placement: a Ctrl-click on a square
                // that already has a door toggles its lock instead of
                // trying (and, per the `allowed` gate above, likely
                // failing anyway, since a slab already occupied by a door
                // can still pass find_door_angle()) to place a second one
                // on top of it.
                // A door sits on its slab's centre subtile, wherever the click landed.
                struct Thing *doortng = find_base_thing_on_mapwho(TCls_Door, 0, slab_subtile(subtile_slab(stl_x), 1), slab_subtile(subtile_slab(stl_y), 1));
                TbBool ctrl_held = ((pckt->control_flags & PCtr_ModCtrl) != 0);
                if (ctrl_held && !thing_is_invalid(doortng))
                {
                    set_packet_action(pckt, PckA_EditorToggleDoorLock, doortng->index, 0, 0, 0);
                }
                else if (allowed && thing_is_invalid(doortng))
                {
                    // Never a second door on a square that already has one.
                    set_packet_action(pckt, PckA_EditorPlaceDoor, ustate->chosen_door_kind, ustate->cheatselection.chosen_player, 0, 0);
                }
            }
            unset_packet_control(pckt, PCtr_LBtnRelease);
            break;
        }
        case PSt_DestroyThing:
            thing = get_nearest_thing_at_position(stl_x, stl_y);
            if (thing_is_invalid(thing))
            {
                player->thing_under_hand = 0;
            }
            else
            {
                player->thing_under_hand = thing->index;
            }
            if (((pckt->control_flags & PCtr_LBtnRelease) != 0) && ((pckt->control_flags & PCtr_MapCoordsValid) != 0))
            {
                if (player->thing_under_hand > 0)
                {
                    room = get_room_thing_is_on(thing);
                    TbBool IsRoom = (!room_is_invalid(room));
                    switch(thing->class_id)
                    {
                        case TCls_Door:
                        {
                            destroy_door(thing);
                            break;
                        }
                        case TCls_Effect:
                        {
                            destroy_effect_thing(thing);
                            break;
                        }
                        default:
                        {
                            if (thing_is_spellbook(thing))
                            {
                                if (!is_neutral_thing(thing))
                                {
                                    remove_power_from_player(book_thing_to_power_kind(thing), thing->owner);
                                }
                            }
                            else if (thing_is_workshop_crate(thing))
                            {
                                if (!is_neutral_thing(thing))
                                {
                                    ThingClass tngclass = crate_thing_to_workshop_item_class(thing);
                                    ThingModel tngmodel = crate_thing_to_workshop_item_model(thing);
                                    if (IsRoom)
                                    {
                                        remove_workshop_item_from_amount_stored(thing->owner, tngclass, tngmodel, WrkCrtF_NoOffmap);
                                    }
                                    remove_workshop_item_from_amount_placeable(thing->owner, tngclass, tngmodel);
                                }
                            }
                            destroy_object(thing);
                            break;
                        }
                    }
                    if (IsRoom)
                    {
                        update_room_contents(room);
                    }
                }
                unset_packet_control(pckt, PCtr_LBtnRelease);
            }
            break;
        default:
            return false;
    }
    return true;
}

TbBool process_user_global_cheats_packet_action(NetUserId user, struct Packet* pckt)
{
  struct PlayerInfo* player = get_player(get_net_user_player_number(user));
  PlayerNumber plyr_idx = player->id_number;
  struct UserState* ustate = get_user_state(user);
  switch (pckt->action)
  {
      case PckA_CheatEnter:
    //      game.???[my_player_number].cheat_mode = 1;
          {
              char msg_buf[128];
              snprintf(msg_buf, sizeof(msg_buf), "Cheat mode activated by player %" PRId64, (int64_t)(plyr_idx));
              ui_show_onscreen_msg(2*kfx_sim_state.turns_per_second, msg_buf);
          }
          return true;
      case PckA_CheatAllFree:
          make_all_creatures_free();
          make_all_rooms_free();
          make_all_powers_cost_free();
          return true;
      case PckA_CheatCrtSpells:
          //TODO: remake from beta
          return false;
      case PckA_CheatRevealMap:
      {
          player = get_player(plyr_idx);
          reveal_whole_map(player);
          return false;
      }
      case PckA_CheatCrAllSpls:
          //TODO: remake from beta
          return false;
      case PckA_CheatAllMagic:
          make_available_all_researchable_powers(plyr_idx);
          return false;
      case PckA_CheatAllRooms:
          make_available_all_researchable_rooms(plyr_idx);
          return false;
      case PckA_CheatAllResrchbl:
          make_all_powers_researchable(plyr_idx);
          make_all_rooms_researchable(plyr_idx);
          return false;
      case PckA_CheatSwitchTerrain:
        {
            ustate->cheatselection.chosen_terrain_kind = pckt->actn_par1;
            if (slab_kind_has_no_ownership(ustate->cheatselection.chosen_terrain_kind))
            {
               ui_clear_messages_from_player(MsgType_Player, ustate->cheatselection.chosen_player);
               ustate->cheatselection.chosen_player = kfx_config_state.neutral_player_num;
            }
            return false;
        }
      case PckA_CheatSwitchPlayer:
        {
            player = get_player(plyr_idx);
            ui_clear_messages_from_player(MsgType_Player, ustate->cheatselection.chosen_player);
            ustate->cheatselection.chosen_player = pckt->actn_par1;
            return false;
        }
      case PckA_CheatSwitchCreature:
        {
            ustate->cheatselection.chosen_creature_kind = pckt->actn_par1;
            return false;
        }
      case PckA_CheatSwitchHero:
        {
            ustate->cheatselection.chosen_hero_kind = pckt->actn_par1;
            return false;
        }
      case PckA_CheatSwitchExperience:
        {
            ustate->cheatselection.chosen_experience_level = pckt->actn_par1;
            return false;
        }
      case PckA_CheatSwitchTrap:
        {
            // docs/refactor/editor/02-editing-toolbox.md §2.7 -- writes the
            // same chosen_trap_kind the classic workshop PSt_PlaceTrap
            // dispatch reads (UserState, not CheatSelection -- no F17 gap
            // here), unconditionally, same shape as the CheatSwitch* cases
            // above.
            ustate->chosen_trap_kind = pckt->actn_par1;
            return false;
        }
      case PckA_CheatSwitchDoor:
        {
            ustate->chosen_door_kind = pckt->actn_par1;
            return false;
        }
        case PckA_CheatAllDoors:
        {
            make_available_all_doors(plyr_idx);
            return false;
        }
        case PckA_CheatAllTraps:
        {
            make_available_all_traps(plyr_idx);
            return false;
        }
        case PckA_CheatGiveDoorTrap:
        {
            int64_t model;
            for (model = 1; model < kfx_config_state.conf.trapdoor_conf.door_types_count; model++)
            {
                if (is_door_buildable(plyr_idx, model))
                {
                    set_door_buildable_and_add_to_amount(plyr_idx, model, 1, 1);
                }
            }
            for (model = 1; model < kfx_config_state.conf.trapdoor_conf.trap_types_count; model++)
            {
                if (is_trap_buildable(plyr_idx, model))
                {
                    set_trap_buildable_and_add_to_amount(plyr_idx, model, 1, 1);
                }
            }
            ui_update_trap_tab_to_config();
            return false;
        }
		case PckA_CheatWinLevel:
		{
			player = get_player(plyr_idx);
			set_player_as_won_level(player);
			return false;
		}
		case PckA_CheatLoseLevel:
		{
			player = get_player(plyr_idx);
			set_player_as_lost_level(player);
			return false;
		}
		case PckA_CheatLevelUp:
		{
			player = get_player(plyr_idx);
			struct Thing* thing = thing_get(player->controlled_thing_idx);
			creature_increase_level(thing);
			return false;
		}
		case PckA_CheatLevelDown:
		{
			player = get_player(plyr_idx);
			struct Thing* thing = thing_get(player->controlled_thing_idx);
			struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
            if (!creature_control_invalid(cctrl))
            {
                set_creature_level(thing, cctrl->exp_level-1);
            }
			return false;
		}
		case PckA_CheatApplySpell:
		{
			player = get_player(plyr_idx);
			struct Thing* thing = thing_get(player->controlled_thing_idx);
			struct SpellConfig* spconf = get_spell_config(pckt->actn_par1);
			SoundSmplTblID smptbl_idx;
			if (spconf->caster_affected)
			{
				if (spconf->caster_affect_sound > 0)
				{
					smptbl_idx = spconf->caster_affect_sound + SOUND_RANDOM(spconf->caster_sounds_count);
				}
				else
				{
					smptbl_idx = 0;
				}
			}
			else
			{
				struct PowerConfigStats *powerst = get_power_model_stats(spconf->linked_power);
				smptbl_idx = powerst->select_sound_idx;
			}
			audio_thing_play_sample(thing, smptbl_idx, NORMAL_PITCH, 0, 3, 0, 4, FULL_LOUDNESS);
			apply_spell_effect_to_thing(thing, pckt->actn_par1, SPELL_MAX_LEVEL, plyr_idx);
			return false;
		}
		case PckA_CheatKillCreature:
		{
			player = get_player(plyr_idx);
			struct Thing* thing = thing_get(player->controlled_thing_idx);
			kill_creature(thing, INVALID_THING, -1, CrDed_NoUnconscious);
			return false;
		}
        default:
          return false;
  }
}

// docs/refactor/editor/02-editing-toolbox.md §2.3 -- 4-connected flood
// fill from a seed slab across contiguous slabs of the *seed's own*
// original kind, bounded by the map and refusing to cross into rooms
// (matches the original editor's own flood-fill rule). Iterative BFS with
// static (not stack-allocated, not recursive) queue/visited buffers sized
// to the largest possible map, so a large flood can't stack-overflow.
void editor_flood_fill_terrain(MapSlabCoord seed_x, MapSlabCoord seed_y, SlabKind target_kind, PlayerNumber owner)
{
    static MapSlabCoord queue_x[MAX_TILES_X * MAX_TILES_Y];
    static MapSlabCoord queue_y[MAX_TILES_X * MAX_TILES_Y];
    static TbBool visited[MAX_TILES_X * MAX_TILES_Y];
    memset(visited, 0, sizeof(visited));

    struct SlabMap *seed_slb = get_slabmap_block(seed_x, seed_y);
    if (slabmap_block_invalid(seed_slb) || (seed_slb->kind == target_kind))
        return; // nothing to flood, or already the target kind
    if (subtile_is_room(slab_subtile(seed_x, 1), slab_subtile(seed_y, 1)))
        return; // never flood starting from a room
    SlabKind source_kind = seed_slb->kind;

    int64_t head = 0, tail = 0;
    queue_x[tail] = seed_x;
    queue_y[tail] = seed_y;
    tail++;
    visited[seed_y * kfx_sim_state.map_tiles_x + seed_x] = true;

    static const int64_t dx[4] = {1, -1, 0, 0};
    static const int64_t dy[4] = {0, 0, 1, -1};
    while (head < tail)
    {
        MapSlabCoord x = queue_x[head];
        MapSlabCoord y = queue_y[head];
        head++;

        place_slab_type_on_map(target_kind, slab_subtile(x, 0), slab_subtile(y, 0), owner, 0);

        for (int64_t i = 0; i < 4; i++)
        {
            MapSlabCoord nx = x + dx[i];
            MapSlabCoord ny = y + dy[i];
            if ((nx < 0) || (nx >= kfx_sim_state.map_tiles_x) || (ny < 0) || (ny >= kfx_sim_state.map_tiles_y))
                continue;
            int64_t idx = (int64_t)ny * kfx_sim_state.map_tiles_x + nx;
            if (visited[idx])
                continue;
            visited[idx] = true;
            struct SlabMap *nslb = get_slabmap_block(nx, ny);
            if (slabmap_block_invalid(nslb) || (nslb->kind != source_kind))
                continue;
            if (subtile_is_room(slab_subtile(nx, 1), slab_subtile(ny, 1)))
                continue; // original refuses to flood rooms
            if (tail >= MAX_TILES_X * MAX_TILES_Y)
                continue; // defensive -- area-capped at the whole map anyway
            queue_x[tail] = nx;
            queue_y[tail] = ny;
            tail++;
        }
    }
}

TbBool process_players_dungeon_control_cheats_packet_action(PlayerNumber plyr_idx, struct Packet* pckt)
{
    struct PlayerInfo* player = get_player(plyr_idx);
    MapCoord x, y;
    struct Thing* thing;
    MapSubtlCoord stl_x, stl_y;
    MapSlabCoord slb_x, slb_y;
    struct Coord3d pos;
    switch (pckt->action)
    {
        case PckA_CheatPlaceTerrain:
        {
            x = (pckt->pos_x);
            y = (pckt->pos_y);
            stl_x = coord_subtile(x);
            stl_y = coord_subtile(y);
            slb_x = subtile_slab(stl_x);
            slb_y = subtile_slab(stl_y);
            place_slab_type_replacing_room(pckt->actn_par1, slb_x, slb_y, pckt->actn_par2);
            break;
        }
        case PckA_EditorFloodFill:
        {
            x = (pckt->pos_x);
            y = (pckt->pos_y);
            stl_x = coord_subtile(x);
            stl_y = coord_subtile(y);
            slb_x = subtile_slab(stl_x);
            slb_y = subtile_slab(stl_y);
            editor_flood_fill_terrain(slb_x, slb_y, pckt->actn_par1, pckt->actn_par2);
            break;
        }
        case PckA_EditorPlaceObject:
        {
            // docs/refactor/editor/02-editing-toolbox.md §2.6. Sent by
            // kfx_editor directly (see PSt_EditorPlaceObject's own
            // comment) with the target position in actn_par1/actn_par2
            // (full int32 range -- NOT the packet's own pos_x/pos_y, which
            // are per-turn scratch that's already been reset by the time
            // this render-phase-originated action is processed; found live
            // via JUSTMSG diagnostics -- "no object appears", packet always
            // showed MapCoordsValid=0 pos=(0,0)) and model/owner in
            // actn_par3/actn_par4 (int16_t is plenty for those, unlike a
            // max-size map's subtile position).
            pos.x.val = pckt->actn_par1;
            pos.y.val = pckt->actn_par2;
            pos.z.val = 0;
            ThingModel model = pckt->actn_par3;
            PlayerNumber owner = pckt->actn_par4;
            thing = create_object(&pos, model, owner, -1);
            if (!thing_is_invalid(thing))
            {
                // Found live: nothing appeared. pos.z.val was left at the
                // placeholder 0 above (world floor, not *this* position's
                // actual floor height) and never corrected afterward --
                // the exact same gap PckA_CheatMakeCreature had before its
                // own fix, just missing here on the first pass. Same fix:
                // correct z from the real thing's own clipbox once it
                // exists, same as create_owned_special_digger() and the
                // now-fixed PckA_CheatMakeCreature both do.
                thing->mappos.z.val = get_thing_height_at(thing, &thing->mappos);
                if (thing_in_wall_at(thing, &thing->mappos))
                {
                    move_creature_to_nearest_valid_position(thing);
                }
                // create_object() already primes previous_mappos to its
                // *input* pos (see that function's own comment), but both
                // the z-correction and move_creature_to_nearest_valid_position()
                // above can move mappos again without re-syncing it -- same
                // two-step pattern as create_owned_special_digger().
                thing->previous_mappos = thing->mappos;
                // §4 -- journal the placement so Ctrl+Z can undo/redo it.
                // Position already lives in actn_par1/actn_par2 (this verb
                // never reads pos_x/pos_y), so pos_x/pos_y here are unused
                // by Redo for this verb -- passed as 0 for consistency.
                editorport_record_placement(thing->index, PckA_EditorPlaceObject,
                    pckt->actn_par1, pckt->actn_par2, pckt->actn_par3, pckt->actn_par4, 0, 0);
            }
            break;
        }
        case PckA_EditorPlaceTrap:
        {
            // §2.7. Unlike PckA_EditorPlaceObject, safe to read the
            // packet's own pos_x/pos_y here -- this is sent by
            // PSt_EditorPlaceTrap's dispatch (packets_cheats.c's
            // per-work-state switch above), which runs from within
            // input() itself, before exchange_packets() resets the
            // packet for the next turn.
            x = (pckt->pos_x);
            y = (pckt->pos_y);
            stl_x = coord_subtile(x);
            stl_y = coord_subtile(y);
            if (player_place_trap_at_subtile_without_check(stl_x, stl_y, pckt->actn_par2, pckt->actn_par1, true))
            {
                // §4 -- player_place_trap_without_check_at() returns only a
                // TbBool, not the created thing, so re-find it by the
                // position+model we just placed (the same
                // find_base_thing_on_mapwho() lookup thing_doors.c's own
                // key-management code already uses for doors below).
                thing = find_base_thing_on_mapwho(TCls_Trap, pckt->actn_par1, stl_x, stl_y);
                if (!thing_is_invalid(thing))
                    // §4. This verb reads position from the packet's own
                    // ambient pos_x/pos_y (x/y, captured above) rather than
                    // a param -- recorded explicitly so Redo can restore it
                    // (see editor_journal.cpp's own comment).
                    editorport_record_placement(thing->index, PckA_EditorPlaceTrap,
                        pckt->actn_par1, pckt->actn_par2, 0, 0, x, y);
            }
            break;
        }
        case PckA_EditorPlaceDoor:
        {
            x = (pckt->pos_x);
            y = (pckt->pos_y);
            stl_x = coord_subtile(x);
            stl_y = coord_subtile(y);
            if (player_place_door_without_check_at(stl_x, stl_y, pckt->actn_par2, pckt->actn_par1, true))
            {
                // §4 -- same "returns only a TbBool" gap as Trap above. A door
                // sits on its slab's centre subtile, whichever subtile the
                // click landed on -- looking at the clicked one missed it
                // (so the placement was never journaled and Undo undid the
                // previous edit instead).
                thing = find_base_thing_on_mapwho(TCls_Door, 0, slab_subtile(subtile_slab(stl_x), 1), slab_subtile(subtile_slab(stl_y), 1));
                if (!thing_is_invalid(thing))
                    // §4. Same ambient-position reasoning as Trap above.
                    editorport_record_placement(thing->index, PckA_EditorPlaceDoor,
                        pckt->actn_par1, pckt->actn_par2, 0, 0, x, y);
            }
            break;
        }
        case PckA_EditorToggleDoorLock:
        {
            // §2.7's deferred lock-toggle item. actn_par1 is the door
            // thing's own index -- PSt_EditorPlaceDoor's dispatch already
            // resolved which door via find_base_thing_on_mapwho(), so no
            // position/re-lookup needed here.
            thing = thing_get(pckt->actn_par1);
            if (!thing_is_invalid(thing) && (thing->class_id == TCls_Door))
            {
                editorport_record_door_lock(thing->index, thing->door.is_locked != 0);
                if (thing->door.is_locked)
                    unlock_door(thing);
                else
                    lock_door(thing);
            }
            break;
        }
        case PckA_EditorUndo:
        {
            // §4. Same per-class dispatch PSt_DestroyThing's own eraser
            // uses below: doors need destroy_door() (removes the animating
            // slab overlay + re-triangulates navigation, which a raw
            // delete_thing_structure() wouldn't); everything else this
            // journal ever records (creature/hero/digger/object/trap) is
            // safe via the generic destroy_object() -> delete_thing_structure()
            // path, same as the eraser's own "default:" branch for those
            // classes (minus the spellbook/workshop-crate refund handling,
            // which never applies to a thing this journal placed).
            thing = thing_get(pckt->actn_par1);
            if (!thing_is_invalid(thing))
            {
                if (thing->class_id == TCls_Door)
                    destroy_door(thing);
                else
                    destroy_object(thing);
            }
            break;
        }
        case PckA_EditorSetGoldValue:
        {
            // docs/refactor/editor/09-toolbox-remainder.md §1. kfx_editor
            // already resolved that the click landed on an existing
            // gold-family object (get_nearest_thing_at_position(), same
            // detection precedent as Query/Eyedropper's own thing lookups)
            // rather than empty ground -- just apply the new value here,
            // re-validating class/genre server-side rather than trusting
            // the client's own resolution blindly.
            thing = thing_get(pckt->actn_par1);
            if (!thing_is_invalid(thing) && (thing->class_id == TCls_Object) && object_is_gold(thing))
            {
                thing->valuable.gold_stored = (pckt->actn_par2 < 0) ? 0 : pckt->actn_par2;
            }
            break;
        }
        case PckA_EditorSetThingPosition:
        {
            // docs/refactor/editor/09-toolbox-remainder.md §1. kfx_editor
            // already resolved which thing to move client-side -- just
            // reposition it via move_thing_in_map() (thing_navigate.h),
            // which handles the mapwho re-link when the move crosses a
            // subtile boundary, same as any other in-world thing move
            // rather than a raw thing->mappos write.
            thing = thing_get(pckt->actn_par3);
            if (!thing_is_invalid(thing))
            {
                struct Coord3d newpos;
                newpos.x.val = pckt->actn_par1;
                newpos.y.val = pckt->actn_par2;
                newpos.z.val = pckt->actn_par4;
                move_thing_in_map(thing, &newpos);
            }
            break;
        }
        case PckA_EditorPlaceTerrainRect:
        {
            // §2.4. Corner 2 is the packet's own pos_x/pos_y (the release
            // point, untouched by set_packet_action() -- see
            // PSt_EditorPlaceTerrainRect's own comment); corner 1 is the
            // recorded drag-start, carried explicitly in actn_par1/actn_par2
            // since pos_x/pos_y can't hold two points at once. Applies the
            // chosen slab kind (actn_par3) + owner (actn_par4) to every
            // slab in the box via editor_apply_slab_rect() (shared with
            // PckA_EditorRectClearEarth below).
            MapSubtlCoord drag_stl_x = pckt->actn_par1;
            MapSubtlCoord drag_stl_y = pckt->actn_par2;
            MapSubtlCoord end_stl_x = coord_subtile(pckt->pos_x);
            MapSubtlCoord end_stl_y = coord_subtile(pckt->pos_y);
            MapSlabCoord box_beg_x = min(subtile_slab(drag_stl_x), subtile_slab(end_stl_x));
            MapSlabCoord box_beg_y = min(subtile_slab(drag_stl_y), subtile_slab(end_stl_y));
            MapSlabCoord box_end_x = max(subtile_slab(drag_stl_x), subtile_slab(end_stl_x));
            MapSlabCoord box_end_y = max(subtile_slab(drag_stl_y), subtile_slab(end_stl_y));
            editor_snapshot_slab_rect(PckA_EditorPlaceTerrainRect, box_beg_x, box_beg_y, box_end_x, box_end_y, pckt->actn_par3, pckt->actn_par4);
            editor_apply_slab_rect(box_beg_x, box_beg_y, box_end_x, box_end_y, pckt->actn_par3, pckt->actn_par4);
            break;
        }
        case PckA_EditorRectClearEarth:
        {
            // §2.2. Same corner layout as PckA_EditorPlaceTerrainRect
            // above, fixed to SlbT_EARTH/neutral instead of packet-carried
            // kind/owner.
            MapSubtlCoord drag_stl_x = pckt->actn_par1;
            MapSubtlCoord drag_stl_y = pckt->actn_par2;
            MapSubtlCoord end_stl_x = coord_subtile(pckt->pos_x);
            MapSubtlCoord end_stl_y = coord_subtile(pckt->pos_y);
            MapSlabCoord box_beg_x = min(subtile_slab(drag_stl_x), subtile_slab(end_stl_x));
            MapSlabCoord box_beg_y = min(subtile_slab(drag_stl_y), subtile_slab(end_stl_y));
            MapSlabCoord box_end_x = max(subtile_slab(drag_stl_x), subtile_slab(end_stl_x));
            MapSlabCoord box_end_y = max(subtile_slab(drag_stl_y), subtile_slab(end_stl_y));
            editor_snapshot_slab_rect(PckA_EditorRectClearEarth, box_beg_x, box_beg_y, box_end_x, box_end_y, SlbT_EARTH, kfx_config_state.neutral_player_num);
            editor_apply_slab_rect(box_beg_x, box_beg_y, box_end_x, box_end_y, SlbT_EARTH, kfx_config_state.neutral_player_num);
            break;
        }
        case PckA_EditorRectDeleteThings:
        {
            // §2.2. Same corner layout as PckA_EditorRectClearEarth above.
            MapSubtlCoord drag_stl_x = pckt->actn_par1;
            MapSubtlCoord drag_stl_y = pckt->actn_par2;
            MapSubtlCoord end_stl_x = coord_subtile(pckt->pos_x);
            MapSubtlCoord end_stl_y = coord_subtile(pckt->pos_y);
            MapSlabCoord box_beg_x = min(subtile_slab(drag_stl_x), subtile_slab(end_stl_x));
            MapSlabCoord box_beg_y = min(subtile_slab(drag_stl_y), subtile_slab(end_stl_y));
            MapSlabCoord box_end_x = max(subtile_slab(drag_stl_x), subtile_slab(end_stl_x));
            MapSlabCoord box_end_y = max(subtile_slab(drag_stl_y), subtile_slab(end_stl_y));
            editor_delete_things_in_rect(box_beg_x, box_beg_y, box_end_x, box_end_y);
            break;
        }
        case PckA_EditorRectSetOwner:
        {
            // §2.2. Same corner layout as PckA_EditorRectClearEarth above.
            MapSubtlCoord drag_stl_x = pckt->actn_par1;
            MapSubtlCoord drag_stl_y = pckt->actn_par2;
            MapSubtlCoord end_stl_x = coord_subtile(pckt->pos_x);
            MapSubtlCoord end_stl_y = coord_subtile(pckt->pos_y);
            MapSlabCoord box_beg_x = min(subtile_slab(drag_stl_x), subtile_slab(end_stl_x));
            MapSlabCoord box_beg_y = min(subtile_slab(drag_stl_y), subtile_slab(end_stl_y));
            MapSlabCoord box_end_x = max(subtile_slab(drag_stl_x), subtile_slab(end_stl_x));
            MapSlabCoord box_end_y = max(subtile_slab(drag_stl_y), subtile_slab(end_stl_y));
            // new_kind is unused for this pcktype (Set Owner never changes
            // a slab's kind, only its owner) -- 0 is a harmless sentinel,
            // ignored by the journal's own Redo dispatch (editor_journal.cpp
            // reads slab kind fresh per-slab for PckA_EditorRectSetOwner).
            editor_snapshot_slab_rect(PckA_EditorRectSetOwner, box_beg_x, box_beg_y, box_end_x, box_end_y, 0, pckt->actn_par3);
            editor_set_owner_rect(box_beg_x, box_beg_y, box_end_x, box_end_y, pckt->actn_par3);
            break;
        }
        case PckA_CheatMakeCreature:
        {
            x = (pckt->pos_x);
            y = (pckt->pos_y);
            pos.x.val = x;
            pos.y.val = y;
            // Found live via the in-game editor: pos.z.val was never set
            // here (unlike create_owned_special_digger()'s own z.val = 0
            // before its first create_creature() call), so the creature
            // landed at whatever this stack slot's leftover value from an
            // earlier case in this same switch happened to be -- usually
            // "close enough" to be unnoticed in normal cheat-menu use, but
            // undefined, and capable of placing the creature well outside
            // the visible floor height entirely. Same two-step fix
            // create_owned_special_digger() already uses: create at a
            // known z, then correct it from the real thing's own clipbox.
            pos.z.val = 0;
            PlayerNumber id = pckt->actn_par2;
            unsigned char exp = pckt->actn_par2 >> 8;
            thing = create_creature(&pos, pckt->actn_par1, id);
            if (!thing_is_invalid(thing))
            {
                thing->mappos.z.val = get_thing_height_at(thing, &thing->mappos);
                thing->previous_mappos = thing->mappos;
                set_creature_level(thing, exp);
                // §4. Unconditional -- this verb is also the classic cheat
                // menu's own "Make Creature", not editor-exclusive;
                // editorport_record_placement() no-ops itself outside
                // an active editor session (same convention as every other
                // EditorPort implementation).
                // Position is ambient (x/y, captured above) -- recorded
                // explicitly so Redo can restore it.
                editorport_record_placement(thing->index, PckA_CheatMakeCreature,
                    pckt->actn_par1, pckt->actn_par2, 0, 0, x, y);
            }
            break;
        }
        case PckA_CheatMakeDigger:
        {
            x = (pckt->pos_x);
            y = (pckt->pos_y);
            thing = create_owned_special_digger(x, y, pckt->actn_par1);
            if (!thing_is_invalid(thing))
            {
                set_creature_level(thing, pckt->actn_par2);
                editorport_record_placement(thing->index, PckA_CheatMakeDigger,
                    pckt->actn_par1, pckt->actn_par2, 0, 0, x, y);
            }
            break;
        }
        case PckA_EditorRedoCreature:
        {
            // §4 -- Redo counterpart for PckA_CheatMakeCreature: identical
            // logic, but position comes from actn_par1/actn_par2 (full
            // int32 range, set explicitly by editor_journal.cpp's Redo)
            // instead of the packet's own ambient pos_x/pos_y, which
            // can't reliably carry a value from a previous turn through to
            // this one (see this verb's own enum comment).
            pos.x.val = pckt->actn_par1;
            pos.y.val = pckt->actn_par2;
            pos.z.val = 0;
            PlayerNumber id = pckt->actn_par4;
            unsigned char exp = pckt->actn_par4 >> 8;
            thing = create_creature(&pos, pckt->actn_par3, id);
            if (!thing_is_invalid(thing))
            {
                thing->mappos.z.val = get_thing_height_at(thing, &thing->mappos);
                thing->previous_mappos = thing->mappos;
                set_creature_level(thing, exp);
                editorport_record_placement(thing->index, PckA_CheatMakeCreature,
                    pckt->actn_par3, pckt->actn_par4, 0, 0, pckt->actn_par1, pckt->actn_par2);
            }
            break;
        }
        case PckA_EditorRedoDigger:
        {
            // §4 -- Redo counterpart for PckA_CheatMakeDigger.
            thing = create_owned_special_digger(pckt->actn_par1, pckt->actn_par2, pckt->actn_par3);
            if (!thing_is_invalid(thing))
            {
                set_creature_level(thing, pckt->actn_par4);
                editorport_record_placement(thing->index, PckA_CheatMakeDigger,
                    pckt->actn_par3, pckt->actn_par4, 0, 0, pckt->actn_par1, pckt->actn_par2);
            }
            break;
        }
        case PckA_EditorRedoTrap:
        {
            // §4 -- Redo counterpart for PckA_EditorPlaceTrap.
            MapSubtlCoord redo_stl_x = coord_subtile(pckt->actn_par1);
            MapSubtlCoord redo_stl_y = coord_subtile(pckt->actn_par2);
            if (player_place_trap_at_subtile_without_check(redo_stl_x, redo_stl_y, pckt->actn_par4, pckt->actn_par3, true))
            {
                thing = find_base_thing_on_mapwho(TCls_Trap, pckt->actn_par3, redo_stl_x, redo_stl_y);
                if (!thing_is_invalid(thing))
                    editorport_record_placement(thing->index, PckA_EditorPlaceTrap,
                        pckt->actn_par3, pckt->actn_par4, 0, 0, pckt->actn_par1, pckt->actn_par2);
            }
            break;
        }
        case PckA_EditorRedoDoor:
        {
            // §4 -- Redo counterpart for PckA_EditorPlaceDoor.
            MapSubtlCoord redo_stl_x = coord_subtile(pckt->actn_par1);
            MapSubtlCoord redo_stl_y = coord_subtile(pckt->actn_par2);
            if (player_place_door_without_check_at(redo_stl_x, redo_stl_y, pckt->actn_par4, pckt->actn_par3, true))
            {
                thing = find_base_thing_on_mapwho(TCls_Door, 0, slab_subtile(subtile_slab(redo_stl_x), 1), slab_subtile(subtile_slab(redo_stl_y), 1));
                if (!thing_is_invalid(thing))
                    editorport_record_placement(thing->index, PckA_EditorPlaceDoor,
                        pckt->actn_par3, pckt->actn_par4, 0, 0, pckt->actn_par1, pckt->actn_par2);
            }
            break;
        }
        case PckA_CheatStealSlab:
        {
            x = (pckt->pos_x);
            y = (pckt->pos_y);
            stl_x = coord_subtile(x);
            stl_y = coord_subtile(y);
            slb_x = subtile_slab(stl_x);
            slb_y = subtile_slab(stl_y);
            PlayerNumber id = pckt->actn_par2;
            TbBool effect = pckt->actn_par2 >> 8;
            if (effect)
            {
                if (pckt->actn_par1 == SlbT_CLAIMED)
                {
                    pos.x.val = subtile_coord_center(slab_subtile_center(subtile_slab(stl_x)));
                    pos.y.val = subtile_coord_center(slab_subtile_center(subtile_slab(stl_y)));
                    pos.z.val = subtile_coord_center(1);
                    if (is_my_player(player))
                    {
                        play_non_3d_sample(snd_spell_stars);
                    }
                    create_effect(&pos, imp_spangle_effects[get_player_color_idx(id)], id);
                }
                else
                {
                    if (is_my_player(player))
                    {
                        play_non_3d_sample(snd_spell_wall);
                    }
                    for (int64_t n = 0; n < SMALL_AROUND_LENGTH; n++)
                    {
                        pos.x.stl.pos = 128;
                        pos.y.stl.pos = 128;
                        pos.z.stl.pos = 128;
                        pos.x.stl.num = stl_x + 2 * small_around[n].delta_x;
                        pos.y.stl.num = stl_y + 2 * small_around[n].delta_y;
                        struct Map* mapblk = get_map_block_at(pos.x.stl.num, pos.y.stl.num);
                        if (map_block_revealed(mapblk, id) && ((mapblk->flags & SlbAtFlg_Blocking) == 0))
                        {
                            pos.z.val = get_floor_height_at(&pos);
                            create_effect(&pos, imp_spangle_effects[get_player_color_idx(id)], id);
                        }
                    }
                }
            }
            place_slab_type_on_map(pckt->actn_par1, stl_x, stl_y, id, 0);
            do_slab_efficiency_alteration(slb_x, slb_y);
            struct SlabMap *slb = get_slabmap_block(slb_x, slb_y);
            for (int64_t i = 0; i < PLAYERS_COUNT; i++)
            {
                if (i != slabmap_owner(slb))
                {
                    untag_blocks_for_digging_in_area(stl_x, stl_y, i);
                }
            }
            break;
        }
        case PckA_CheatStealRoom:
        {
            x = (pckt->pos_x);
            y = (pckt->pos_y);
            stl_x = coord_subtile(x);
            stl_y = coord_subtile(y);
            struct Room* room = subtile_room_get(stl_x, stl_y);
            if (room_exists(room))
            {
                if (pckt->actn_par2)
                {
                    if (is_my_player(player))
                    {
                        play_non_3d_sample(snd_room_claim);
                    }
                    create_effects_on_room_slabs(room, imp_spangle_effects[get_player_color_idx(pckt->actn_par1)], 0, pckt->actn_par1);
                }
                take_over_room(room, pckt->actn_par1);
            }
            break;
        }
        case PckA_CheatHeartHealth:
        {
            thing = get_player_soul_container(pckt->actn_par1);
            if (!thing_is_invalid(thing))
            {
                thing->health = (int64_t)pckt->actn_par2;
            }
            if (thing->health <= 0)
            {
                    struct Dungeon* dungeon = get_dungeon(plyr_idx);
                    dungeon->lvstats.keeper_destroyed[pckt->actn_par1]++;
                    dungeon->lvstats.keepers_destroyed++;
            }
            break;
        }
        case PckA_CheatKillPlayer:
        {
            thing = get_player_soul_container(pckt->actn_par1);
            struct Dungeon* dungeon = get_dungeon(plyr_idx);
            if (!thing_is_invalid(thing))
            {
                thing->health = 0;
                dungeon->lvstats.keeper_destroyed[pckt->actn_par1]++;
                dungeon->lvstats.keepers_destroyed++;
            }
            struct Thing* heartng = find_players_backup_dungeon_heart(pckt->actn_par1);
            if (!thing_is_invalid(heartng))
            {
                heartng->health = 0;
                dungeon->lvstats.keeper_destroyed[pckt->actn_par1]++;
                dungeon->lvstats.keepers_destroyed++;
            }
            break;
        }
        case PckA_CheatConvertCreature:
        {
            thing = thing_get(player->thing_under_hand);
            if (thing_is_creature(thing))
            {
                change_creature_owner(thing, pckt->actn_par1);
            }
            break;
        }
        case PckA_EditorGoSpectator:
        {
            level_editor_go_spectator_at(plyr_idx, pckt->actn_par1, pckt->actn_par2);
            break;
        }
        default:
        {
           return false;
        }
    }
    return true;
}
