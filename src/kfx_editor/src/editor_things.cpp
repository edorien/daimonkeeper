/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_things.cpp
 *     See editor_things.h.
 */
#include "pre_inc.h"
#include "editor_things.h"
#include "editor_journal.h"
#include "kfx_editor.h"
#include "thing_list.h"
#include "thing_creature.h"
#include "thing_objects.h"
#include "thing_doors.h"
#include "thing_effects.h"
#include "thing_stats.h"
#include "magic_powers.h"
#include "room_data.h"
#include "room_util.h"
#include "room_workshop.h"
#include "map_utils.h"
#include "post_inc.h"

bool editor_delete_thing_at(const struct Coord3d *pos_in)
{
    const struct Coord3d *pos = pos_in;
        MapSubtlCoord stl_x = coord_subtile(pos->x.val);
        MapSubtlCoord stl_y = coord_subtile(pos->y.val);
        struct Thing *thing = get_creature_near(pos->x.val, pos->y.val);
        if (!thing_is_creature(thing))
            thing = get_nearest_thing_at_position(stl_x, stl_y);
        if (thing_is_invalid(thing))
            return false;
        struct Room *room = get_room_thing_is_on(thing);
        const bool in_room = !room_is_invalid(room);
        editor_journal_stroke_begin();
        if (thing->class_id == TCls_Door)
            destroy_door(thing);
        else if (thing->class_id == TCls_Effect)
            destroy_effect_thing(thing);
        else
        {
            if (thing_is_spellbook(thing))
            {
                if (!is_neutral_thing(thing))
                    remove_power_from_player(book_thing_to_power_kind(thing), thing->owner);
            }
            else if (thing_is_workshop_crate(thing))
            {
                if (!is_neutral_thing(thing))
                {
                    const ThingClass tngclass = crate_thing_to_workshop_item_class(thing);
                    const ThingModel tngmodel = crate_thing_to_workshop_item_model(thing);
                    if (in_room)
                        remove_workshop_item_from_amount_stored(thing->owner, tngclass, tngmodel, WrkCrtF_NoOffmap);
                    remove_workshop_item_from_amount_placeable(thing->owner, tngclass, tngmodel);
                }
            }
            destroy_object(thing);
        }
        if (in_room)
            update_room_contents(room);
        editor_journal_stroke_end("Delete");
        editor_mark_dirty();
        return true;
}
