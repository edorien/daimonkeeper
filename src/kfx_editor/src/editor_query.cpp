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


static void editor_query_from_thing(const struct Thing *thing, EditorQueryResult *out)
{
    out->kind = EditorQueryResult::QR_Thing;
    snprintf(out->title, sizeof(out->title), "Thing #%" PRId64, (int64_t)(thing->index));
    snprintf(out->name, sizeof(out->name), "%s", thing_model_name(thing));
    snprintf(out->owner, sizeof(out->owner), "Owner: %" PRId64, (int64_t)thing->owner);
    snprintf(out->extra1, sizeof(out->extra1), "Pos: %" PRId64 ", %" PRId64 ", %" PRId64,
        (int64_t)thing->mappos.x.stl.num, (int64_t)thing->mappos.y.stl.num, (int64_t)thing->mappos.z.stl.num);
    out->extra2[0] = '\0';
    switch (thing->class_id)
    {
        case TCls_Trap:
        {
            struct TrapConfigStats *trapst = get_trap_model_stats(thing->model);
            snprintf(out->health, sizeof(out->health), "Health: %" PRId64, (int64_t)thing->health);
            snprintf(out->extra2, sizeof(out->extra2), "Shots: %" PRId64 "/%" PRId64, (int64_t)thing->trap.num_shots, (int64_t)trapst->shots);
            break;
        }
        case TCls_Object:
        {
            struct ObjectConfigStats *objst = get_object_model_stats(thing->model);
            snprintf(out->health, sizeof(out->health), "Health: %" PRId64 "/%" PRId64, (int64_t)thing->health, (int64_t)objst->health);
            if (object_is_gold(thing))
                snprintf(out->extra2, sizeof(out->extra2), "Amount: %" PRId64, (int64_t)thing->valuable.gold_stored);
            break;
        }
        case TCls_Door:
        {
            struct DoorConfigStats *doorst = get_door_model_stats(thing->model);
            snprintf(out->health, sizeof(out->health), "Health: %" PRId64 "/%" PRId64, (int64_t)thing->health, (int64_t)doorst->health);
            snprintf(out->extra2, sizeof(out->extra2), "%s", thing->door.is_locked ? "Locked" : "Unlocked");
            break;
        }
        default:
            snprintf(out->health, sizeof(out->health), "Health: %" PRId64, (int64_t)thing->health);
            break;
    }
}

static void editor_query_from_room(const struct Room *room, EditorQueryResult *out)
{
    out->kind = EditorQueryResult::QR_Room;
    snprintf(out->title, sizeof(out->title), "Room #%" PRId64, (int64_t)(room->index));
    snprintf(out->name, sizeof(out->name), "%s", room_code_name(room->kind));
    snprintf(out->owner, sizeof(out->owner), "Owner: %" PRId64, (int64_t)room->owner);
    snprintf(out->health, sizeof(out->health), "Health: %" PRId64, (int64_t)room->health);
    snprintf(out->extra1, sizeof(out->extra1), "Capacity: %" PRId64 "/%" PRId64, (int64_t)room->used_capacity, (int64_t)room->total_capacity);
    double efficiency_pct = ((double)room->efficiency / (double)ROOM_EFFICIENCY_MAX) * 100.0;
    snprintf(out->extra2, sizeof(out->extra2), "Efficiency: %" PRId64, (int64_t)(efficiency_pct + 0.5));
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
