/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file map_events.c
 *     Map events support functions.
 * @par Purpose:
 *     Functions to create and maintain events placed on map.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     11 Mar 2010 - 12 May 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "map_events.h"
#include "kfx_sim_state.h"

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_planar.h"
#include "bflib_sound.h"
#include "bflib_sndlib.h"
#include "config_sounds.h"
#include "thing_doors.h"
#include "thing_traps.h"
#include "config_strings.h"
#include "config_campaigns.h"
#include "config_creature.h"
#include "config_trapdoor.h"
#include "room_workshop.h"
#include "power_hand.h"
#include "config_players.h"
#include "player_instances.h"
#include "kfx_config_state.h"
#include "thing_objects.h"
#include "thing_stats.h"
#include "ports/ui_port.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
TbBool event_is_invalid(const struct Event *event)
{
    return (event <= &kfx_sim_state.event[0]) || (event > &kfx_sim_state.event[EVENTS_COUNT-1]) || (event == NULL);
}

TbBool event_exists(const struct Event* event)
{
    if (event_is_invalid(event))
        return false;
    if ((event->flags & EvF_Exists) == 0)
        return false;
    return true;
}

struct Event *get_event_nearby_of_type_for_player(MapCoord map_x, MapCoord map_y, int64_t max_dist, EventKind evkind, PlayerNumber plyr_idx)
{
    for (int64_t i = 1; i < EVENTS_COUNT; i++)
    {
        struct Event* event = &kfx_sim_state.event[i];
        if (((event->flags & EvF_Exists) != 0) && (event->owner == plyr_idx) && (event->kind == evkind)
         && get_distance_xy(event->mappos_x, event->mappos_y, map_x, map_y) < max_dist) {
            return event;
        }
    }
    return INVALID_EVENT;
}

struct Event *get_event_of_target_and_type_for_player(int64_t target, EventKind evkind, PlayerNumber plyr_idx)
{
    for (int64_t i = 1; i < EVENTS_COUNT; i++)
    {
        struct Event* event = &kfx_sim_state.event[i];
        if (((event->flags & EvF_Exists) != 0) && (event->owner == plyr_idx) && (event->kind == evkind)
         && (event->target == target)) {
            return event;
        }
    }
    return INVALID_EVENT;
}

struct Event *get_event_of_type_for_player(EventKind evkind, PlayerNumber plyr_idx)
{
    for (int64_t i = 1; i < EVENTS_COUNT; i++)
    {
        struct Event* event = &kfx_sim_state.event[i];
        if (((event->flags & EvF_Exists) != 0) && (event->owner == plyr_idx) && (event->kind == evkind)) {
            return event;
        }
    }
    return INVALID_EVENT;
}

/** Creates a map event or updates existing map event of given kind which is within 5 subtiles of the new event location.
 *
 * @param map_x Event position on map, X coord.
 * @param map_y Event position on map, Y coord.
 * @param evkind Event kind to be searched or created.
 * @param dngn_id Owning dungeon index.
 * @param target Event target identification parameter, its meaning depends on event kind.
 * @return Index of the new event, or negative index of updated event. Zero if no action was taken.
 */
EventIndex event_create_event_or_update_nearby_existing_event(MapCoord map_x, MapCoord map_y, EventKind evkind, unsigned char dngn_id, int64_t target)
{
    int64_t range = (evkind == EvKind_HeartAttacked) ? 35 : 5;
    struct Event* event = get_event_nearby_of_type_for_player(map_x, map_y, subtile_coord(range, 0), evkind, dngn_id);
    if (!event_is_invalid(event))
    {
        SYNCDBG(3,"Updating event %" PRId64 " to be kind %" PRId64 " at (%" PRId64 ",%" PRId64 ")",(int64_t)event->index,(int64_t)evkind,(int64_t)coord_subtile(map_x),(int64_t)coord_subtile(map_y));
        event_initialise_event(event, map_x, map_y, evkind, dngn_id, target);
        return -(EventIndex)event->index;
    }
    SYNCDBG(3,"Creating event kind %" PRId64 " at (%" PRId64 ",%" PRId64 ")",(int64_t)evkind,(int64_t)coord_subtile(map_x),(int64_t)coord_subtile(map_y));
    event = event_create_event(map_x, map_y, evkind, dngn_id, target);
    if (event_is_invalid(event)) {
        return 0;
    }
    return (EventIndex)event->index;
}

/** Creates a map event or updates existing map event of given kind which has the same target.
 *
 * @param map_x Event position on map, X coord.
 * @param map_y Event position on map, Y coord.
 * @param evkind Event kind to be searched or created.
 * @param dngn_id Owning dungeon index.
 * @param target Event target identification parameter, its meaning depends on event kind.
 * @return Index of the new event, or negative index of updated event. Zero if no action was taken.
 */
EventIndex event_create_event_or_update_same_target_existing_event(MapCoord map_x, MapCoord map_y, EventKind evkind, unsigned char dngn_id, int64_t target)
{
    struct Event* event = get_event_of_target_and_type_for_player(target, evkind, dngn_id);
    if (!event_is_invalid(event))
    {
        SYNCDBG(3,"Updating event %" PRId64 " to be kind %" PRId64 " at (%" PRId64 ",%" PRId64 ")",(int64_t)event->index,(int64_t)evkind,(int64_t)coord_subtile(map_x),(int64_t)coord_subtile(map_y));
        event_initialise_event(event, map_x, map_y, evkind, dngn_id, target);
        return -(EventIndex)event->index;
    }
    SYNCDBG(3,"Creating event kind %" PRId64 " at (%" PRId64 ",%" PRId64 ")",(int64_t)evkind,(int64_t)coord_subtile(map_x),(int64_t)coord_subtile(map_y));
    event = event_create_event(map_x, map_y, evkind, dngn_id, target);
    if (event_is_invalid(event)) {
        return 0;
    }
    return (EventIndex)event->index;
}

/** Creates a map event or updates existing map event of given kind if it exists anywhere on map.
 *
 * @param map_x Event position on map, X coord.
 * @param map_y Event position on map, Y coord.
 * @param evkind Event kind to be searched or created.
 * @param plyr_idx Owning player index.
 * @param target Event target identification parameter, its meaning depends on event kind.
 * @return Index of the new event, or negative index of updated event. Zero if no action was taken.
 */
EventIndex event_create_event_or_update_old_event(MapCoord map_x, MapCoord map_y, EventKind evkind, unsigned char plyr_idx, int64_t target)
{
    // Check if such event already exists
    struct Event* event = get_event_of_type_for_player(evkind, plyr_idx);
    // If we've found a matching event, replace it
    if (!event_is_invalid(event))
    {
        event_initialise_event(event, map_x, map_y, evkind, plyr_idx, target);
        event_add_to_event_buttons_list_or_replace_button(event, get_dungeon(plyr_idx));
        return -(EventIndex)event->index;
    }
    // If no matching event found, then create new one
    event = event_create_event(map_x, map_y, evkind, plyr_idx, target);
    if (event_is_invalid(event)) {
        return 0;
    }
    return (EventIndex)event->index;
}

void event_initialise_all(void)
{
    ui_set_visible_event_idx(0);
    ui_clear_all_event_button_states();
    for (int64_t i = 0; i < DUNGEONS_COUNT; i++)
    {
        struct Dungeon* dungeon = get_dungeon(i);
        for (int64_t k = 0; k <= EVENT_BUTTONS_COUNT; k++)
        {
            dungeon->event_button_index[k] = 0;
        }
    }
}

int64_t event_move_player_towards_event(struct PlayerInfo *player, int64_t event_idx)
{
    struct Event* event = &kfx_sim_state.event[event_idx];

    player->zoom_to_pos_x = event->mappos_x;
    player->zoom_to_pos_y = event->mappos_y;

    set_player_instance(player, PI_ZoomToPos, 0);
    return 1;
}

struct Event *event_create_event(MapCoord map_x, MapCoord map_y, EventKind evkind, unsigned char dngn_id, int64_t target)
{
    int64_t i;
    if (dngn_id == kfx_config_state.neutral_player_num) {
        return INVALID_EVENT;
    }
    if (evkind >= EVENT_KIND_COUNT) {
        ERRORLOG("Illegal Event kind %" PRId64 " to be created",(int64_t)evkind);
        return INVALID_EVENT;
    }
    struct Dungeon* dungeon = get_dungeon(dngn_id);
    i = dungeon->event_last_run_turn[evkind];
    if (i != 0)
    {
        int64_t k = ui_get_event_button_info(evkind)->turns_between_events;
        if ((k != 0) && (i+k >= get_gameturn()))
        {
          return INVALID_EVENT;
        }
    }
    struct Event* event = event_allocate_free_event_structure();
    if (event_is_invalid(event)) {
        return INVALID_EVENT;
    }
    event_initialise_event(event, map_x, map_y, evkind, dngn_id, target);
    event_add_to_event_buttons_list_or_replace_button(event, dungeon);
    return event;
}

struct Event *event_allocate_free_event_structure(void)
{
    for (int64_t i = 1; i < EVENTS_COUNT; i++)
    {
        struct Event* event = &kfx_sim_state.event[i];
        if ((event->flags & EvF_Exists) == 0)
        {
            event->flags |= EvF_Exists;
            event->index = i;
            return event;
        }
    }
    return INVALID_EVENT;
}

void event_initialise_event(struct Event *event, MapCoord map_x, MapCoord map_y, EventKind evkind, unsigned char dngn_id, int64_t target)
{
    ui_clear_event_button_state(event->index);
    event->mappos_x = map_x;
    event->mappos_y = map_y;
    event->kind = evkind;
    event->owner = dngn_id;
    event->lifespan_turns = ui_get_event_button_info(evkind)->lifespan_turns;
    event->target = target;
    event->icon_idx = -1;
    event->flags |= EvF_BtnFirstFall;
}

void event_delete_event_structure(int64_t ev_idx)
{
    memset(&kfx_sim_state.event[ev_idx], 0, sizeof(struct Event));
}

void event_update_last_use(struct Event *event)
{
    struct Dungeon* dungeon = get_dungeon(event->owner);
    if (dungeon_invalid(dungeon)) {
        ERRORLOG("Player %" PRId64 " dungeon doesn't exist",(int64_t)event->owner);
        return;
    }
    if ((event->kind < 1) || (event->kind >= EVENT_KIND_COUNT)) {
        ERRORLOG("Illegal Event kind %" PRId64 " to be updated",(int64_t)event->kind);
        return;
    }
    dungeon->event_last_run_turn[event->kind] = get_gameturn();
}

void event_delete_event(int64_t plyr_idx, EventIndex evidx)
{
    struct Event* event = &kfx_sim_state.event[evidx];
    event_update_last_use(event);
    struct Dungeon* dungeon = get_dungeon(plyr_idx);
    for (int64_t i = 0; i <= EVENT_BUTTONS_COUNT; i++)
    {
        int64_t k = dungeon->event_button_index[i];
        if (k == evidx)
        {
            ui_turn_off_event_box_if_necessary(plyr_idx, evidx);
            dungeon->event_button_index[i] = 0;
            break;
        }
    }
    event_delete_event_structure(evidx);
}

void event_update_on_battle_removal(BattleIndex battle_idx)
{
    for (EventIndex i = 0; i < EVENTS_COUNT; i++)
    {
        struct Event* event = &kfx_sim_state.event[i];
        if ((event->kind == EvKind_FriendlyFight) || (event->kind == EvKind_EnemyFight))
        {
            if (event->target == battle_idx)
            {
                // Clear coords - new ones will be set during update_battle_events() call
                event->mappos_y = 0;
                event->mappos_x = 0;
            }
        }
    }
}

void event_add_to_event_buttons_list_or_replace_button(struct Event *event, struct Dungeon *dungeon)
{
    if (dungeon->owner != event->owner) {
      ERRORLOG("Illegal my_event player allocation");
    }
    if (ui_get_event_button_info(event->kind)->bttn_sprite == 0)
    {
        //Event without a button
        return;
    }
    EventKind replace_evkind = ui_get_event_button_info(event->kind)->replace_event_kind_button;
    int64_t i;
    EventIndex evidx;
    if (replace_evkind != EvKind_Nothing)
    {
        for (i=EVENT_BUTTONS_COUNT; i >= 0; i--)
        {
            evidx = dungeon->event_button_index[i];
            struct Event* event_prev = &kfx_sim_state.event[evidx];
            if ((event_prev->kind == event->kind) || (event_prev->kind == replace_evkind)) {
                SYNCDBG(1,"Replacing button at position %" PRId64,(int64_t)i);
                dungeon->event_button_index[i] = event->index;
                break;
            }
        }
    } else {
        i = -1;
    }
    if (i < 0)
    {
        for (i=EVENT_BUTTONS_COUNT; i >= 0; i--)
        {
            evidx = dungeon->event_button_index[i];
            if (evidx == 0) {
                if (is_my_player_number(dungeon->owner))
                {
                    struct PlayerInfo* player = get_player(dungeon->owner);
                    if ( (get_gameturn() > 10) && (player->view_type != PVT_DungeonTop || (kfx_sim_state.operation_flags & GOF_ShowGui)) )
                    {
                        play_non_3d_sample(snd_tab_fall);
                    }
                }
                SYNCDBG(1,"New button at position %" PRId64,(int64_t)i);
                dungeon->event_button_index[i] = event->index;
                break;
            }
        }
    }
    if (i < 0)
    {
        kill_oldest_my_event(dungeon);
        dungeon->event_button_index[EVENT_BUTTONS_COUNT] = event->index;
    }
}

void event_reset_scroll_window(void)
{
    kfx_sim_state.evntbox_scroll_window.start_y = 0;
    kfx_sim_state.evntbox_scroll_window.action = 0;
    kfx_sim_state.evntbox_scroll_window.text_height = 0;
    kfx_sim_state.evntbox_scroll_window.window_height = 0;
}

void event_text_for(const struct Event *event, PlayerNumber plyr_idx, char *buf, size_t len)
{
    if ((buf == NULL) || (len == 0)) {
        return;
    }
    buf[0] = 0;
    const struct Thing *thing;
    int64_t i;
    snprintf(buf, len, "%s", get_string(ui_get_event_button_info(event->kind)->msg_stridx));
    switch (event->kind)
    {
        case EvKind_Objective:
            snprintf(buf, len, "%s", kfx_sim_state.evntbox_text_objective[plyr_idx]);
            break;
        case EvKind_NewRoomResrch:
        case EvKind_RoomTakenOver:
        case EvKind_WorkRoomUnreachable:
        case EvKind_StorageRoomUnreachable:
            str_appendf(buf, len, ":\n%s", get_string(get_room_kind_stats(event->target)->name_stridx));
            break;
        case EvKind_NewCreature:
        case EvKind_CreatrScavenged:
        case EvKind_CreatrIsAnnoyed:
            // If the thing is gone the message is left without the creature's name.
            thing = thing_get(event->target);
            if (thing_exists(thing)) {
                str_appendf(buf, len, ":\n%s", get_string(creature_stats_get_from_thing(thing)->namestr_idx));
            }
            break;
        case EvKind_NewSpellResrch:
            str_appendf(buf, len, ":\n%s", get_string(get_power_name_strindex(event->target)));
            break;
        case EvKind_NewTrap:
            str_appendf(buf, len, ":\n%s", get_string(get_trap_model_stats(event->target)->name_stridx));
            break;
        case EvKind_NewDoor:
            str_appendf(buf, len, ":\n%s", get_string(get_door_model_stats(event->target)->name_stridx));
            break;
        case EvKind_CreaturePayday:
            str_appendf(buf, len, ":\n%" PRId64, (int64_t)(event->target));
            break;
        case EvKind_SpellPickedUp:
            thing = thing_get(event->target);
            if (thing_exists(thing)) {
                str_appendf(buf, len, ":\n%s", get_string(get_power_name_strindex(book_thing_to_power_kind(thing))));
            }
            break;
        case EvKind_Information:
            i = (int64_t)event->target;
            snprintf(buf, len, "%s", get_string((i < 0) ? -i : i));
            break;
        case EvKind_TrapCrateFound:
            thing = thing_get(event->target);
            if (thing_exists(thing)) {
                str_appendf(buf, len, ":\n%s", get_string(get_trap_model_stats(crate_thing_to_workshop_item_model(thing))->name_stridx));
            }
            break;
        case EvKind_DoorCrateFound:
            thing = thing_get(event->target);
            if (thing_exists(thing)) {
                str_appendf(buf, len, ":\n%s", get_string(get_door_model_stats(crate_thing_to_workshop_item_model(thing))->name_stridx));
            }
            break;
        case EvKind_DnSpecialFound:
            thing = thing_get(event->target);
            if (thing_exists(thing)) {
                str_appendf(buf, len, ":\n%s", get_string(get_special_description_strindex(box_thing_to_special(thing))));
            }
            break;
        case EvKind_QuickInformation:
            i = (int64_t)event->target;
            snprintf(buf, len, "%s", kfx_sim_state.quick_messages[((i < 0) ? -i : i) % QUICK_MESSAGES_COUNT]);
            break;
        default:
            break;
    }
}

void activate_event_box(EventIndex evidx)
{
    const struct Thing *thing;
    int64_t i;
    PlayerNumber plyr_idx = my_player_number;
    struct Dungeon* dungeon = get_my_dungeon();
    struct Event* event = &kfx_sim_state.event[evidx];
    SYNCDBG(6,"Starting for event kind %" PRId64,(int64_t)(event->kind));
    ui_set_visible_event_idx(evidx);
    ui_mark_event_button_read(evidx);
    // The text is event_text_for's (the same text the External-seat view reports); what follows is only which
    // panels open around it.
    event_text_for(event, plyr_idx, kfx_sim_state.evntbox_scroll_window.text, sizeof(kfx_sim_state.evntbox_scroll_window.text));
    if ((event->kind == EvKind_FriendlyFight) || (event->kind == EvKind_EnemyFight)) {
        // Restart the list of visible battles from the first one; the other slots are
        // refilled by maintain_my_battle_list(), which skips battles already on the list.
        // Overwriting just the first slot could put the same battle on the list twice.
        dungeon->visible_battles[0] = find_first_battle_of_mine(plyr_idx);
        dungeon->visible_battles[1] = 0;
        dungeon->visible_battles[2] = 0;
    }
    int64_t other_off = 0;
    switch (event->kind)
    {
        case EvKind_HeartAttacked:
        case EvKind_Breach:
            other_off = 1;
            ui_turn_on_menu(GMnu_TEXT_INFO);
            break;
        case EvKind_EnemyFight:
        case EvKind_FriendlyFight:
            ui_turn_off_menu(GMnu_TEXT_INFO);
            ui_turn_on_menu(GMnu_BATTLE);
            break;
        case EvKind_Objective:
        {
            int64_t k;
            for (i = EVENT_BUTTONS_COUNT; i >= 0; i--)
            {
              k = dungeon->event_button_index[i];
              if (kfx_sim_state.event[k%EVENTS_COUNT].kind == EvKind_Objective)
              {
                  other_off = 1;
                  ui_turn_on_menu(GMnu_TEXT_INFO);
                  kfx_sim_state.new_objective = 0;
                  break;
              }
            }
            break;
        }
        case EvKind_NewRoomResrch:
        case EvKind_NewCreature:
        case EvKind_NewSpellResrch:
        case EvKind_NewTrap:
        case EvKind_NewDoor:
        case EvKind_CreatrScavenged:
        case EvKind_TreasureRoomFull:
        case EvKind_AreaDiscovered:
        case EvKind_CreaturePayday:
        case EvKind_RoomTakenOver:
        case EvKind_WorkRoomUnreachable:
        case EvKind_StorageRoomUnreachable:
        case EvKind_CreatrIsAnnoyed:
        case EvKind_NoMoreLivingSet:
        case EvKind_AlarmTriggered:
        case EvKind_RoomUnderAttack:
        case EvKind_NeedTreasureRoom:
        case EvKind_RoomLost:
        case EvKind_CreatrHungry:
        case EvKind_SecretDoorDiscovered:
        case EvKind_SecretDoorSpotted:
            other_off = 1;
            ui_turn_on_menu(GMnu_TEXT_INFO);
            break;
        case EvKind_SpellPickedUp:
        case EvKind_TrapCrateFound:
        case EvKind_DoorCrateFound:
        case EvKind_DnSpecialFound:
            // These open the text panel only while the item still exists.
            other_off = 1;
            thing = thing_get(event->target);
            if (!thing_exists(thing))
                break;
            ui_turn_on_menu(GMnu_TEXT_INFO);
            break;
        case EvKind_Information:
        case EvKind_QuickInformation:
            snprintf(kfx_sim_state.evntbox_text_buffer, sizeof(kfx_sim_state.evntbox_text_buffer), "%s", kfx_sim_state.evntbox_scroll_window.text);
            other_off = 1;
            ui_turn_on_menu(GMnu_TEXT_INFO);
            break;
        default:
            ERRORLOG("Undefined event kind: %" PRId64, (int64_t)event->kind);
            break;
    }
    event_reset_scroll_window();
    if (other_off)
    {
        ui_turn_off_menu(GMnu_BATTLE);
        ui_turn_off_menu(GMnu_DUNGEON_SPECIAL);
        ui_turn_off_menu(GMnu_RESURRECT_CREATURE);
        ui_turn_off_menu(GMnu_TRANSFER_CREATURE);
    }
    SYNCDBG(8,"Finished");
}

void maintain_my_event_list(struct Dungeon *dungeon)
{
    for (int64_t i = 1; i <= EVENT_BUTTONS_COUNT; i++)
    {
        unsigned char curr_ev_idx = dungeon->event_button_index[i];
        if (curr_ev_idx != 0)
        {
            if (dungeon->event_button_index[i-1] == 0)
            {
                dungeon->event_button_index[i-1] = curr_ev_idx;
                dungeon->event_button_index[i] = 0;
                struct Event* event = &kfx_sim_state.event[curr_ev_idx];
                event->flags |= EvF_BtnFalling;
                if (flag_is_set(event->flags,EvF_BtnFirstFall))
                {
                    if ((i == 1) || ((i >= 2) && dungeon->event_button_index[i-2] != 0))
                    {
                        if (is_my_player_number(dungeon->owner)) {
                            struct SoundEmitter* emit = S3DGetSoundEmitter(Non3DEmitter);
                            stop_sample(get_emitter_id(emit), snd_tab_fall);
                            play_non_3d_sample(175);
                        }
                        unsigned char prev_ev_idx = dungeon->event_button_index[i - 1];
                        event = &kfx_sim_state.event[prev_ev_idx];
                        event->flags &= ~EvF_BtnFirstFall;
                    }
                }
            }
        }
    }
}

void kill_oldest_my_event(struct Dungeon *dungeon)
{
    int64_t old_idx = -1;
    int64_t old_birth = INT_MAX;
    for (int64_t i = EVENT_BUTTONS_COUNT; i > 0; i--)
    {
        int64_t k = dungeon->event_button_index[i];
        struct Event* event = &kfx_sim_state.event[k];
        if (event->lifespan_turns < old_birth)
        {
          old_idx = k;
          old_birth = event->lifespan_turns;
        }
    }
    if (old_idx >= 0)
      event_delete_event(dungeon->owner, old_idx);
    maintain_my_event_list(dungeon);
}

void maintain_all_players_event_lists(void)
{
    for (int64_t i = 0; i < PLAYERS_COUNT; i++)
    {
        struct PlayerInfo* player = get_player(i);
        if (player_exists(player))
        {
            struct Dungeon* dungeon = get_players_dungeon(player);
            maintain_my_event_list(dungeon);
        }
    }
}

ThingIndex get_thing_index_event_is_attached_to(const struct Event *event)
{
    int64_t i;
    switch (event->kind)
    {
    case EvKind_Objective:
    case EvKind_NewCreature:
    case EvKind_CreatrScavenged:
    case EvKind_SpellPickedUp:
    case EvKind_CreatrIsAnnoyed:
    case EvKind_NoMoreLivingSet:
    case EvKind_TrapCrateFound:
    case EvKind_DoorCrateFound:
    case EvKind_DnSpecialFound:
    case EvKind_HeartAttacked:
        i = event->target;
        break;
    default:
        i = 0;
        break;
    }
    return i;
}

struct Thing *event_is_attached_to_thing(EventIndex evidx)
{
    struct Event* event = &kfx_sim_state.event[evidx];
    if ((event->flags & EvF_Exists) == 0)
    {
        return INVALID_THING;
    }
    ThingIndex i = get_thing_index_event_is_attached_to(event);
    return thing_get(i);
}

void event_process_events(void)
{
    for (int64_t i = 0; i < EVENTS_COUNT; i++)
    {
        struct Event* event = &kfx_sim_state.event[i];
        if (!event_exists(event)) {
            continue;
        }
        struct PlayerInfo*player = get_player(event->owner);
        if (player->view_type <= PVT_DungeonTop) //Freeze lifespan of events of human player on map or possession
        {
            if (event->lifespan_turns > 0) {
                event->lifespan_turns--;
            }
        }
        if (event->lifespan_turns <= 0)
        {
            int64_t ev_owner = event->owner;
            EventIndex subev_idx = event->index;
            struct Dungeon* dungeon = get_dungeon(ev_owner);
            struct Event* subevent = &kfx_sim_state.event[subev_idx];
            event_update_last_use(subevent);
            for (int64_t j = 0; j <= EVENT_BUTTONS_COUNT; j++)
            {
                if (dungeon->event_button_index[j] == subev_idx) {
                    ui_turn_off_event_box_if_necessary(ev_owner, dungeon->event_button_index[j]);
                    dungeon->event_button_index[j] = 0;
                    break;
                }
            }
            event_delete_event_structure(subev_idx);
        }
    }
}

void update_all_events(void)
{
    for (int64_t i = EVENTS_COUNT - 1; i > 0; i--)
    {
        struct Thing* thing = event_is_attached_to_thing(i);
        if (thing_exists(thing))
        {
            struct Event* event = &kfx_sim_state.event[i];
            if ((thing->class_id == TCls_Creature) && thing_is_picked_up(thing))
            {
                event->mappos_x = 0;
                event->mappos_y = 0;
            } else
            {
                event->mappos_x = thing->mappos.x.val;
                event->mappos_y = thing->mappos.y.val;
            }
        }
    }
    maintain_all_players_event_lists();
}

void event_kill_all_players_events(int64_t plyr_idx)
{
    SYNCDBG(8,"Starting");
    TbBool keep_objective = kfx_sim_state.heart_lost_display_message;
    for (int64_t i = 1; i < EVENTS_COUNT; i++)
    {
        struct Event* event = &kfx_sim_state.event[i];
        if (((event->flags & EvF_Exists) != 0) && (event->owner == plyr_idx)) {
            if (keep_objective)
            {
                if (event->kind != EvKind_Objective)
                {
                    event_delete_event(plyr_idx, event->index);
                }
            }
            else
            {
                event_delete_event(plyr_idx, event->index);
            }
        }
    }
}

void remove_events_thing_is_attached_to(struct Thing *thing)
{
    SYNCDBG(8,"Starting");
    for (int64_t i = 1; i < EVENTS_COUNT; i++)
    {
        struct Event* event = &kfx_sim_state.event[i];
        if (((event->flags & EvF_Exists) != 0) && (event->kind != EvKind_Objective))
        {
            struct Thing* atchtng = event_is_attached_to_thing(i);
            if (thing_exists(atchtng))
            {
                if (atchtng->index == thing->index) {
                    event_delete_event(event->owner, event->index);
                }
            }
        }
    }
}

void clear_events(void)
{
    int64_t i;
    ui_set_visible_event_idx(0);
    ui_clear_all_event_button_states();
    for (i=0; i < EVENTS_COUNT; i++)
    {
      memset(&kfx_sim_state.event[i], 0, sizeof(struct Event));
    }
    memset(&kfx_sim_state.evntbox_scroll_window, 0, sizeof(struct TextScrollWindow));
    memset(&kfx_sim_state.evntbox_text_buffer, 0, MESSAGE_TEXT_LEN);
    memset(&kfx_sim_state.evntbox_text_objective, 0, sizeof(kfx_sim_state.evntbox_text_objective));
    for (i=0; i < 5; i++)
    {
      memset(&kfx_sim_state.bookmark[i], 0, sizeof(struct Bookmark));
    }
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
