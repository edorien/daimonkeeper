/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_query.cpp
 *     See editor_query.h.
 */
#include "pre_inc.h"
#include "editor_query.h"
#include "thing_list.h"
#include "thing_creature.h"
#include "thing_stats.h"
#include "thing_objects.h"
#include "config_objects.h"
#include "config_trapdoor.h"
#include "room_data.h"
#include "map_data.h"
#include "slab_data.h"
#include <cstdio>
#include "post_inc.h"


void editor_query_from_thing(const struct Thing *thing, EditorQueryResult *out)
{
    out->kind = EditorQueryResult::QR_Thing;
    snprintf(out->title, sizeof(out->title), "Thing #%d", thing->index);
    snprintf(out->name, sizeof(out->name), "%s", thing_model_name(thing));
    snprintf(out->owner, sizeof(out->owner), "Owner: %d", (int)thing->owner);
    snprintf(out->extra1, sizeof(out->extra1), "Pos: %d, %d, %d",
        (int)thing->mappos.x.stl.num, (int)thing->mappos.y.stl.num, (int)thing->mappos.z.stl.num);
    out->extra2[0] = '\0';
    switch (thing->class_id)
    {
        case TCls_Trap:
        {
            struct TrapConfigStats *trapst = get_trap_model_stats(thing->model);
            snprintf(out->health, sizeof(out->health), "Health: %d", (int)thing->health);
            snprintf(out->extra2, sizeof(out->extra2), "Shots: %d/%d", (int)thing->trap.num_shots, (int)trapst->shots);
            break;
        }
        case TCls_Object:
        {
            struct ObjectConfigStats *objst = get_object_model_stats(thing->model);
            snprintf(out->health, sizeof(out->health), "Health: %d/%d", (int)thing->health, (int)objst->health);
            if (object_is_gold(thing))
                snprintf(out->extra2, sizeof(out->extra2), "Amount: %d", (int)thing->valuable.gold_stored);
            break;
        }
        case TCls_Door:
        {
            struct DoorConfigStats *doorst = get_door_model_stats(thing->model);
            snprintf(out->health, sizeof(out->health), "Health: %d/%d", (int)thing->health, (int)doorst->health);
            snprintf(out->extra2, sizeof(out->extra2), "%s", thing->door.is_locked ? "Locked" : "Unlocked");
            break;
        }
        default:
            snprintf(out->health, sizeof(out->health), "Health: %d", (int)thing->health);
            break;
    }
}

void editor_query_from_room(const struct Room *room, EditorQueryResult *out)
{
    out->kind = EditorQueryResult::QR_Room;
    snprintf(out->title, sizeof(out->title), "Room #%d", room->index);
    snprintf(out->name, sizeof(out->name), "%s", room_code_name(room->kind));
    snprintf(out->owner, sizeof(out->owner), "Owner: %d", (int)room->owner);
    snprintf(out->health, sizeof(out->health), "Health: %d", (int)room->health);
    snprintf(out->extra1, sizeof(out->extra1), "Capacity: %d/%d", (int)room->used_capacity, (int)room->total_capacity);
    float efficiency_pct = ((float)room->efficiency / (float)ROOM_EFFICIENCY_MAX) * 100.0f;
    snprintf(out->extra2, sizeof(out->extra2), "Efficiency: %d", (int)(efficiency_pct + 0.5f));
}

EditorQueryResult editor_query_at(const struct Coord3d *pos, ThingIndex *creature_idx)
{
    *creature_idx = 0;
    const MapSubtlCoord stl_x = coord_subtile(pos->x.val);
    const MapSubtlCoord stl_y = coord_subtile(pos->y.val);
    // Same "creature first, else nearest thing" precedence
    // packets_cheats.c's own PSt_QueryAll case uses.
    struct Thing *thing = get_creature_near(pos->x.val, pos->y.val);
    if (!thing_is_creature(thing))
        thing = get_nearest_thing_at_position(stl_x, stl_y);
    EditorQueryResult result;
    if (!thing_is_invalid(thing) && thing_is_creature(thing))
    {
        *creature_idx = thing->index;
        return result;
    }
    if (!thing_is_invalid(thing))
    {
        editor_query_from_thing(thing, &result);
        return result;
    }
    struct Room *room = subtile_room_get(stl_x, stl_y);
    if (room_exists(room))
        editor_query_from_room(room, &result);
    return result;
}
