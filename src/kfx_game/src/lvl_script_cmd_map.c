/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file lvl_script_cmd_map.c
 *     Level script commands: the map (slabs, doors, traps, objects, effects, revealing, action points).
 * @par Purpose:
 *     Their check and process functions; lvl_script_commands.c has the command tables that name them
 *     (refactor pass 5, S09: split out of it by command family).
 * @author   KeeperFX Team
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "lvl_script_conditions.h"
#include "lvl_script_lib.h"
#include "map_blocks.h"
#include "room_util.h"
#include "thing_effects.h"
#include "lvl_script_commands_private.h"
#include "lvl_script_commands_private.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

const struct NamedCommand is_free_desc[] = {
  {"PAID", 0},
  {"FREE", 1},
  {NULL, 0}
};

const struct NamedCommand orientation_desc[] = {
  {"North",     ANGLE_NORTH},
  {"NorthEast", ANGLE_NORTHEAST},
  {"East",      ANGLE_EAST},
  {"SouthEast", ANGLE_SOUTHEAST},
  {"South",     ANGLE_SOUTH},
  {"SouthWest", ANGLE_SOUTHWEST},
  {"West",      ANGLE_WEST},
  {"NorthWest", ANGLE_NORTHWEST},
  {NULL, 0}
};

void tag_map_rect_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);

    MapSlabCoord x = scline->np[1];
    MapSlabCoord y = scline->np[2];
    MapSlabDelta width;
    MapSlabDelta height;

    if (scline->np[3] != '\0')
        width = scline->np[3];
    else
        width = 1;
    if (scline->np[4] != '\0')
        height = scline->np[4];
    else
        height = 1;

    MapSlabCoord start_x = x - (width / 2);
    MapSlabCoord end_x = x + (width / 2) + (width & 1);
    MapSlabCoord start_y = y - (height / 2);
    MapSlabCoord end_y = y + (height / 2) + (height & 1);

    if (start_x < 0)
    {
        SCRPTWRNLOG("Starting X slab '%" PRId64 "' (from %" PRId64 "-%" PRId64 "/2) is out of range, fixing it to '0'.", (int64_t)(start_x), (int64_t)(x), (int64_t)(width));
        start_x = 0;
    }
    else if (start_x > kfx_sim_state.map_tiles_x)
    {
        SCRPTWRNLOG("Starting X slab '%" PRId64 "' (from %" PRId64 "-%" PRId64 "/2) is out of range, fixing it to '%" PRId64 "'.", (int64_t)(start_x), (int64_t)(x), (int64_t)(width), (int64_t)(kfx_sim_state.map_tiles_x));
        start_x = kfx_sim_state.map_tiles_x;
    }
    if (end_x < 0)
    {
        SCRPTWRNLOG("Ending X slab '%" PRId64 "' (from %" PRId64 "+%" PRId64 "/2) is out of range, fixing it to '0'.", (int64_t)(end_x), (int64_t)(x), (int64_t)(width));
        end_x = 0;
    }
    else if (end_x > kfx_sim_state.map_tiles_x)
    {
        SCRPTWRNLOG("Ending X slab '%" PRId64 "' (from %" PRId64 "+%" PRId64 "/2) is out of range, fixing it to '%" PRId64 "'.", (int64_t)(end_x), (int64_t)(x), (int64_t)(width), (int64_t)(kfx_sim_state.map_tiles_x));
        end_x = kfx_sim_state.map_tiles_x;
    }
    if (start_y < 0)
    {
        SCRPTWRNLOG("Starting Y slab '%" PRId64 "' (from %" PRId64 "-%" PRId64 "/2) is out of range, fixing it to '0'.", (int64_t)(start_y), (int64_t)(y), (int64_t)(height));
        start_y = 0;
    }
    else if (start_y > kfx_sim_state.map_tiles_y)
    {
        SCRPTWRNLOG("Starting Y slab '%" PRId64 "' (from %" PRId64 "-%" PRId64 "/2) is out of range, fixing it to '%" PRId64 "'.", (int64_t)(start_y), (int64_t)(y), (int64_t)(height), (int64_t)(kfx_sim_state.map_tiles_y));
        start_y = kfx_sim_state.map_tiles_y;
    }
    if (end_y < 0)
    {
        SCRPTWRNLOG("Ending Y slab '%" PRId64 "' (from %" PRId64 "+%" PRId64 "/2) is out of range, fixing it to '0'.", (int64_t)(end_y), (int64_t)(y), (int64_t)(height));
        end_y = 0;
    }
    else if (end_y > kfx_sim_state.map_tiles_y)
    {
        SCRPTWRNLOG("Ending Y slab '%" PRId64 "' (from %" PRId64 "+%" PRId64 "/2) is out of range, fixing it to '%" PRId64 "'.", (int64_t)(end_y), (int64_t)(y), (int64_t)(height), (int64_t)(kfx_sim_state.map_tiles_y));
        end_y = kfx_sim_state.map_tiles_y;
    }
    if ((x < 0) || (x > kfx_sim_state.map_tiles_x) || (y < 0) || (y > kfx_sim_state.map_tiles_y))
    {
        SCRPTERRLOG("Tag slabs out of range, trying to set tag center point to (%" PRId64 ",%" PRId64 ") on map that's %" PRId64 "x%" PRId64 " slabs", (int64_t)(x), (int64_t)(y), (int64_t)(kfx_sim_state.map_tiles_x), (int64_t)(kfx_sim_state.map_tiles_y));
        DEALLOCATE_SCRIPT_VALUE
            return;
    }
    value->shorts[1] = start_x;
    value->shorts[2] = end_x;
    value->shorts[3] = start_y;
    value->shorts[4] = end_y;

    PROCESS_SCRIPT_VALUE(scline->command);
}

void tag_map_rect_process(struct ScriptContext* context)
{
    MapSlabCoord start_x = context->value->shorts[1];
    MapSlabCoord end_x = context->value->shorts[2];
    MapSlabCoord start_y = context->value->shorts[3];
    MapSlabCoord end_y = context->value->shorts[4];

    for (int64_t x = start_x; x < end_x; x++)
    {
        for (int64_t y = start_y; y < end_y; y++)
        {
            MapSubtlCoord stl_x = slab_subtile_center(x);
            MapSubtlCoord stl_y = slab_subtile_center(y);

            if (subtile_is_diggable_for_player(context->player_idx, stl_x, stl_y, false))
            {
                tag_blocks_for_digging_in_area(stl_x, stl_y, context->player_idx);
            }
        }
    }
}

void untag_map_rect_process(struct ScriptContext* context)
{
    MapSlabCoord start_x = context->value->shorts[1];
    MapSlabCoord end_x = context->value->shorts[2];
    MapSlabCoord start_y = context->value->shorts[3];
    MapSlabCoord end_y = context->value->shorts[4];

    for (int64_t x = start_x; x < end_x; x++)
    {
        for (int64_t y = start_y; y < end_y; y++)
        {
            MapSubtlCoord stl_x = slab_subtile_center(x);
            MapSubtlCoord stl_y = slab_subtile_center(y);

            if (subtile_is_diggable_for_player(context->player_idx, stl_x, stl_y, false))
            {
                untag_blocks_for_digging_in_area(stl_x, stl_y, context->player_idx);
            }
        }
    }
}


void conceal_map_rect_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    TbBool conceal_all = false;

    if (scline->np[5] == -1)
    {
        if ((strcmp(scline->tp[5], "") == 0))
        {
            conceal_all = false;
        }
        else if ((strcmp(scline->tp[5], "ALL") == 0))
        {
            conceal_all = true;
        }
        else
        {
            SCRPTWRNLOG("Hide value \"%s\" not recognized", scline->tp[5]);
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
    }
    else
    {
        conceal_all = scline->np[5];
    }

    MapSubtlCoord x = scline->np[1];
    MapSubtlCoord y = scline->np[2];
    MapSubtlDelta width = scline->np[3];
    MapSubtlDelta height = scline->np[4];

    MapSubtlCoord start_x = x - (width / 2);
    MapSubtlCoord end_x = x + (width / 2) + (width & 1);
    MapSubtlCoord start_y = y - (height / 2);
    MapSubtlCoord end_y = y + (height / 2) + (height & 1);

    if (start_x < 0)
    {
        SCRPTWRNLOG("Starting X coordinate '%" PRId64 "' (from %" PRId64 "-%" PRId64 "/2) is out of range, fixing it to '0'.", (int64_t)(start_x),(int64_t)(x),(int64_t)(width));
        start_x = 0;
    }
    else if (start_x > kfx_sim_state.map_subtiles_x)
    {
        SCRPTWRNLOG("Starting X coordinate '%" PRId64 "' (from %" PRId64 "-%" PRId64 "/2) is out of range, fixing it to '%" PRId64 "'.", (int64_t)(start_x), (int64_t)(x), (int64_t)(width), (int64_t)(kfx_sim_state.map_subtiles_x));
        start_x = kfx_sim_state.map_subtiles_x;
    }
    if (end_x < 0)
    {
        SCRPTWRNLOG("Ending X coordinate '%" PRId64 "' (from %" PRId64 "+%" PRId64 "/2) is out of range, fixing it to '0'.", (int64_t)(end_x), (int64_t)(x), (int64_t)(width));
        end_x = 0;
    }
    else if (end_x > kfx_sim_state.map_subtiles_x)
    {
        SCRPTWRNLOG("Ending X coordinate '%" PRId64 "' (from %" PRId64 "+%" PRId64 "/2) is out of range, fixing it to '%" PRId64 "'.", (int64_t)(end_x), (int64_t)(x), (int64_t)(width), (int64_t)(kfx_sim_state.map_subtiles_x));
        end_x = kfx_sim_state.map_subtiles_x;
    }
    if (start_y < 0)
    {
        SCRPTWRNLOG("Starting Y coordinate '%" PRId64 "' (from %" PRId64 "-%" PRId64 "/2) is out of range, fixing it to '0'.", (int64_t)(start_y), (int64_t)(y), (int64_t)(height));
        start_y = 0;
    }
    else if (start_y > kfx_sim_state.map_subtiles_y)
    {
        SCRPTWRNLOG("Starting Y coordinate '%" PRId64 "' (from %" PRId64 "-%" PRId64 "/2) is out of range, fixing it to '%" PRId64 "'.", (int64_t)(start_y), (int64_t)(y), (int64_t)(height), (int64_t)(kfx_sim_state.map_subtiles_y));
        start_y = kfx_sim_state.map_subtiles_y;
    }
    if (end_y < 0)
    {
        SCRPTWRNLOG("Ending Y coordinate '%" PRId64 "' (from %" PRId64 "+%" PRId64 "/2) is out of range, fixing it to '0'.", (int64_t)(end_y), (int64_t)(y), (int64_t)(height));
        end_y = 0;
    }
    else if (end_y > kfx_sim_state.map_subtiles_y)
    {
        SCRPTWRNLOG("Ending Y coordinate '%" PRId64 "' (from %" PRId64 "+%" PRId64 "/2) is out of range, fixing it to '%" PRId64 "'.", (int64_t)(end_y), (int64_t)(y), (int64_t)(height), (int64_t)(kfx_sim_state.map_subtiles_y));
        end_y = kfx_sim_state.map_subtiles_y;
    }
    if ((x < 0) || (x > kfx_sim_state.map_subtiles_x) || (y < 0) || (y > kfx_sim_state.map_subtiles_y))
    {
        SCRPTERRLOG("Conceal coordinates out of range, trying to set conceal center point to (%" PRId64 ",%" PRId64 ") on map that's %" PRId64 "x%" PRId64 " subtiles", (int64_t)(x), (int64_t)(y), (int64_t)(kfx_sim_state.map_subtiles_x), (int64_t)(kfx_sim_state.map_subtiles_y));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->shorts[1] = start_x;
    value->shorts[2] = end_x;
    value->shorts[3] = start_y;
    value->shorts[4] = end_y;
    value->shorts[5] = conceal_all;

    PROCESS_SCRIPT_VALUE(scline->command);
}

void conceal_map_rect_process(struct ScriptContext *context)
{
    MapSubtlCoord start_x = context->value->shorts[1];
    MapSubtlCoord end_x = context->value->shorts[2];
    MapSubtlCoord start_y = context->value->shorts[3];
    MapSubtlCoord end_y = context->value->shorts[4];
    TbBool conceal_all = context->value->shorts[5];
    conceal_map_area(context->player_idx, start_x, end_x, start_y, end_y, conceal_all);
}

void create_effect_at_pos_process(struct ScriptContext* context)
{
    struct Coord3d pos;
    set_coords_to_subtile_center(&pos, context->value->shorts[1], context->value->shorts[2], 0);
    pos.z.val += get_floor_height(pos.x.stl.num, pos.y.stl.num);
    script_create_effect(&pos,context->value->shorts[0],context->value->longs[2]);

}

void create_effect_process(struct ScriptContext *context)
{
    struct Coord3d pos;
    if (!get_coords_at_location(&pos, context->value->ulongs[1],true))
    {
        SCRPTWRNLOG("Could not find location %" PRIu64 " to create effect", (uint64_t)(context->value->ulongs[1]));
        return;
    }
    script_create_effect(&pos,context->value->shorts[0],context->value->longs[2]);

}

void set_door_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    int64_t doorAction = get_id(locked_desc, scline->tp[0]);
    if (doorAction == -1)
    {
        SCRPTERRLOG("Set Door state %s not recognized", scline->tp[0]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    if (slab_coords_invalid(scline->np[1], scline->np[2]))
    {
        SCRPTERRLOG("Invalid slab coordinates: %" PRId64 ", %" PRId64, (int64_t)(scline->np[1]), (int64_t)(scline->np[2]));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    value->shorts[0] = doorAction;
    value->shorts[1] = scline->np[1];
    value->shorts[2] = scline->np[2];
    PROCESS_SCRIPT_VALUE(scline->command);
}

void set_door_process(struct ScriptContext* context)
{
    struct Thing* doortng = get_door_for_position(slab_subtile_center(context->value->shorts[1]), slab_subtile_center(context->value->shorts[2]));
    if (!thing_is_invalid(doortng))
    {
        switch (context->value->shorts[0])
        {
        case 0:
            unlock_door(doortng);
            break;
        case 1:
            lock_door(doortng);
            break;
        }
    }
}

void place_door_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    const char* doorname = scline->tp[1];
    int64_t door_id = get_id(door_desc, doorname);

    if (door_id == -1)
    {
        SCRPTERRLOG("Unknown door, '%s'", doorname);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    if (slab_coords_invalid(scline->np[2], scline->np[3]))
    {
        SCRPTERRLOG("Invalid slab coordinates: %" PRId64 ", %" PRId64, (int64_t)(scline->np[2]), (int64_t)(scline->np[3]));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    int64_t locked = scline->np[4];
    if (locked == -1)
    {
        locked = get_id(locked_desc, scline->tp[4]);
        if (locked == -1)
        {
            SCRPTERRLOG("Door locked state %s not recognized", scline->tp[4]);
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
    }

    int64_t free = scline->np[5];
    if (free == -1)
    {
        free = get_id(is_free_desc, scline->tp[5]);
        if (free == -1)
        {
            SCRPTERRLOG("Place Door free state '%s' not recognized", scline->tp[5]);
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
    }

    value->shorts[1] = door_id;
    value->shorts[2] = scline->np[2];
    value->shorts[3] = scline->np[3];
    value->shorts[4] = locked;
    value->shorts[5] = free;
    PROCESS_SCRIPT_VALUE(scline->command);
}

void place_door_process(struct ScriptContext* context)
{
    ThingModel doorkind = context->value->shorts[1];
    MapSlabCoord slb_x = context->value->shorts[2];
    MapSlabCoord slb_y = context->value->shorts[3];
    TbBool locked = context->value->shorts[4];
    TbBool free = context->value->shorts[5];
    PlayerNumber plyridx = context->player_idx;

    script_place_door(plyridx, doorkind, slb_x, slb_y, locked, free);
}

void place_trap_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    const char* trapname = scline->tp[1];
    int64_t trap_id = get_id(trap_desc, trapname);

    if (trap_id == -1)
    {
        SCRPTERRLOG("Unknown trap, '%s'", trapname);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    if (subtile_coords_invalid(scline->np[2], scline->np[3]))
    {
        SCRPTERRLOG("Invalid subtile coordinates: %" PRId64 ", %" PRId64, (int64_t)(scline->np[2]), (int64_t)(scline->np[3]));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    int64_t free = scline->np[4];
    if (free == -1)
    {
        free = get_id(is_free_desc, scline->tp[4]);
        if (free == -1)
        {
            SCRPTERRLOG("Place Trap free state '%s' not recognized", scline->tp[4]);
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
    }

    value->shorts[1] = trap_id;
    value->shorts[2] = scline->np[2];
    value->shorts[3] = scline->np[3];
    value->shorts[4] = free;
    PROCESS_SCRIPT_VALUE(scline->command);
}

void place_trap_process(struct ScriptContext* context)
{
    ThingModel trapkind = context->value->shorts[1];
    MapSubtlCoord stl_x = context->value->shorts[2];
    MapSubtlCoord stl_y = context->value->shorts[3];
    TbBool free = context->value->shorts[4];
    PlayerNumber plyridx = context->player_idx;
    script_place_trap(plyridx, trapkind, stl_x, stl_y, free);

}

void create_effects_line_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);

    value->longs[0] = scline->np[0]; // AP `from`
    value->longs[1] = scline->np[1]; // AP `to`
    value->chars[8] = scline->np[2]; // curvature
    value->bytes[9] = scline->np[3]; // spatial stepping
    value->bytes[10] = scline->np[4]; // temporal stepping
    const char* effect_name = scline->tp[5];

    EffectOrEffElModel effct_id = effect_or_effect_element_id(effect_name);
    if (effct_id == 0)
    {
        SCRPTERRLOG("Unrecognised effect: %s", effect_name);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }

    value->shorts[6] = effct_id; // effect

    PROCESS_SCRIPT_VALUE(scline->command);
}

void create_effects_line_process(struct ScriptContext *context)
{
    TbMapLocation from = context->value->longs[0];
    TbMapLocation to   = context->value->longs[1];
    char curvature = context->value->chars[8];
    unsigned char spatial_stepping = context->value->bytes[9];
    unsigned char temporal_stepping = context->value->bytes[10];
    EffectOrEffElModel effct_id = context->value->shorts[6];

    create_effects_line(from, to, curvature, spatial_stepping, temporal_stepping, effct_id);
}

void create_effect_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    TbMapLocation location;
    const char *effect_name = scline->tp[0];
    int64_t effct_id = effect_or_effect_element_id(effect_name);
    if (effct_id == 0)
    {
        SCRPTERRLOG("Unrecognised effect: %s", effect_name);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->shorts[0] = effct_id;
    const char *locname = scline->tp[1];
    if (!get_map_location_id(locname, &location))
    {
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->ulongs[1] = location;
    value->longs[2] = scline->np[2];
    PROCESS_SCRIPT_VALUE(scline->command);
}

void create_effect_at_pos_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    const char *effect_name = scline->tp[0];
    int64_t effct_id = effect_or_effect_element_id(effect_name);
    if (effct_id == 0)
    {
        SCRPTERRLOG("Unrecognised effect: %s", effect_name);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->shorts[0] = effct_id;
    if (subtile_coords_invalid(scline->np[1], scline->np[2]))
    {
        SCRPTERRLOG("Invalid coordinates: %" PRId64 ", %" PRId64, (int64_t)(scline->np[1]), (int64_t)(scline->np[2]));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->shorts[1] = scline->np[1];
    value->shorts[2] = scline->np[2];
    value->longs[2] = scline->np[3];
    PROCESS_SCRIPT_VALUE(scline->command);
}

void change_slab_owner_check(const struct ScriptLine *scline)
{

    if (scline->np[0] < 0 || scline->np[0] > kfx_sim_state.map_tiles_x) //x coord
    {
        SCRPTERRLOG("Value '%" PRId64 "' out of range. Range 0-%" PRId64 " allowed.", (int64_t)(scline->np[0]),(int64_t)(kfx_sim_state.map_tiles_x));
        return;
    }
    if (scline->np[1] < 0 || scline->np[1] > kfx_sim_state.map_tiles_y) //y coord
    {
        SCRPTERRLOG("Value '%" PRId64 "' out of range. Range 0-%" PRId64 " allowed.", (int64_t)(scline->np[1]),(int64_t)(kfx_sim_state.map_tiles_y));
        return;
    }
    int64_t filltype = get_id(fill_desc, scline->tp[3]);
    if ((scline->tp[3][0] != '\0') && (filltype == -1))
    {
        SCRPTWRNLOG("Fill type %s not recognized", scline->tp[3]);
    }

    command_add_value(Cmd_CHANGE_SLAB_OWNER, scline->np[2], scline->np[0], scline->np[1], filltype);
}

void change_slab_owner_process(struct ScriptContext *context)
{
    MapSlabCoord x = context->value->longs[0];
    MapSlabCoord y = context->value->longs[1];
    int64_t fill_type = context->value->longs[2];
    if (fill_type > 0)
    {
        struct CompoundCoordFilterParam iter_param;
        iter_param.plyr_idx = context->player_idx;
        iter_param.primary_number = fill_type;
        iter_param.secondary_number = get_slabmap_block(x, y)->kind;
        slabs_fill_iterate_from_slab(x, y, slabs_change_owner, &iter_param);
    } else {
        change_slab_owner_from_script(x, y, context->player_idx);
    }
}

void change_slab_type_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);

    if (scline->np[0] < 0 || scline->np[0] > kfx_sim_state.map_tiles_x) //x coord
    {
        SCRPTERRLOG("Value '%" PRId64 "' out of range. Range 0-%" PRId64 " allowed.", (int64_t)(scline->np[0]),(int64_t)(kfx_sim_state.map_tiles_x));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    else
    {
        value->shorts[0] = scline->np[0];
    }

    if (scline->np[1] < 0 || scline->np[1] > kfx_sim_state.map_tiles_y) //y coord
    {
        SCRPTERRLOG("Value '%" PRId64 "' out of range. Range 0-%" PRId64 " allowed.", (int64_t)(scline->np[0]),(int64_t)(kfx_sim_state.map_tiles_y));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    else
    {
        value->shorts[1] = scline->np[1];
    }

    if (scline->np[2] < 0 || scline->np[2] >= kfx_config_state.conf.slab_conf.slab_types_count) //slab kind
    {
        SCRPTERRLOG("Unsupported slab '%" PRId64 "'. Slabs range 0-%" PRId64 " allowed.", (int64_t)(scline->np[2]),(int64_t)(kfx_config_state.conf.slab_conf.slab_types_count-1));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    else
    {
        value->shorts[2] = scline->np[2];
    }

    value->shorts[3] = get_id(fill_desc, scline->tp[3]);
    if ((scline->tp[3][0] != '\0') && (value->shorts[3] == -1))
    {
        SCRPTWRNLOG("Fill type %s not recognized", scline->tp[3]);
    }
    PROCESS_SCRIPT_VALUE(scline->command);
}

void change_slab_type_process(struct ScriptContext *context)
{
    int64_t x = context->value->shorts[0];
    int64_t y = context->value->shorts[1];
    int64_t slab_kind = context->value->shorts[2];
    int64_t fill_type = context->value->shorts[3];

    if (fill_type > 0)
    {
        struct CompoundCoordFilterParam iter_param;
        iter_param.primary_number = slab_kind;
        iter_param.secondary_number = fill_type;
        iter_param.tertiary_number = get_slabmap_block(x, y)->kind;
        slabs_fill_iterate_from_slab(x, y, slabs_change_type, &iter_param);
    }
    else
    {
        replace_slab_from_script(x, y, slab_kind);
    }
}

void reveal_map_location_check(const struct ScriptLine *scline)
{
    TbMapLocation location;
    if (!get_map_location_id(scline->tp[1], &location)) {
        return;
    }
    command_add_value(Cmd_REVEAL_MAP_LOCATION, scline->np[0], location, scline->np[2], 0);
}

void reveal_map_location_process(struct ScriptContext *context)
{
    TbMapLocation target = context->value->longs[0];
    SYNCDBG(0, "Revealing location type %" PRIu64, (uint64_t)(target));
    MapSubtlCoord x = 0;
    MapSubtlCoord y = 0;
    int64_t r = context->value->longs[1];
    find_map_location_coords(target, &x, &y, context->player_idx, __func__);
    if ((x == 0) && (y == 0))
    {
        WARNLOG("Can't decode location %" PRIu64, (uint64_t)(target));
        return;
    }
    if (r == -1)
    {
        struct CompoundCoordFilterParam iter_param;
        iter_param.plyr_idx = context->player_idx;
        slabs_fill_iterate_from_slab(subtile_slab(x), subtile_slab(y), slabs_reveal_slab_and_corners, &iter_param);
    } else
        reveal_map_area(context->player_idx, x-(r>>1), x+(r>>1)+(r&1), y-(r>>1), y+(r>>1)+(r&1));
}

void player_zoom_to_check(const struct ScriptLine *scline)
{
    TbMapLocation location;
    const char *where = scline->tp[1];
    if (!get_map_location_id(where, &location) || location == MLoc_NONE) {
        SCRPTERRLOG("invalid zoom location \"%s\"",where);
        return;
    }

    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    value->longs[0] = location;
    PROCESS_SCRIPT_VALUE(scline->command);
}

void player_zoom_to_process(struct ScriptContext *context)
{
    TbMapLocation target = context->value->longs[0];
    struct Coord3d pos;

    find_location_pos(target, context->player_idx, &pos, __func__);
    set_player_zoom_to_position(get_player(context->player_idx),&pos);
}


void hide_hero_gate_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    int64_t n = scline->np[0];
    if (scline->np[0] < 0)
    {
        n = -scline->np[0];
    }
    struct Thing* thing = find_hero_gate_of_number(n);
    if (thing_is_invalid(thing))
    {
        SCRPTERRLOG("Invalid hero gate: %" PRId64, (int64_t)(scline->np[0]));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->bytes[0] = n;
    value->bytes[1] = scline->np[1];

    PROCESS_SCRIPT_VALUE(scline->command);
}

void hide_hero_gate_process(struct ScriptContext* context)
{
    hero_gate_set_hidden(find_hero_gate_of_number(context->value->bytes[0]), context->value->bytes[1]);
}

void add_effectgen_to_level_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);

    const char* generator_name = scline->tp[0];
    const char* locname = scline->tp[1];
    int64_t range = scline->np[2];

    TbMapLocation location;
    ThingModel gen_id;
    if (parameter_is_number(generator_name))
    {
        gen_id = atoi(generator_name);
    }
    else
    {
        gen_id = get_id(effectgen_desc, generator_name);
    }
    if (gen_id <= 0)
    {
        SCRPTERRLOG("Unknown effect generator, '%s'", generator_name);
        DEALLOCATE_SCRIPT_VALUE;
        return;
    }
    if (kfx_game_state.script.party_triggers_num >= PARTY_TRIGGERS_COUNT)
    {
        SCRPTERRLOG("Too many ADD_CREATURE commands to spawn effect generator in script");
        DEALLOCATE_SCRIPT_VALUE;
        return;
    }

    // Recognize place where party is created
    if (!get_map_location_id(locname, &location))
    {
        DEALLOCATE_SCRIPT_VALUE;
        return;
    }
    value->shorts[0] = (int64_t)gen_id;
    value->ulongs[1] = location;
    value->shorts[4] = range * COORD_PER_STL;
    PROCESS_SCRIPT_VALUE(scline->command);
}

void add_effectgen_to_level_process(struct ScriptContext* context)
{
    ThingModel gen_id = context->value->shorts[0];
    TbMapLocation location = context->value->ulongs[1];
    int64_t range = context->value->shorts[4];
    if (get_script_current_condition() == CONDITION_ALWAYS)
    {
        script_process_new_effectgen(gen_id, location, range);
    }
    else
    {
        if (kfx_game_state.script.party_triggers_num < PARTY_TRIGGERS_COUNT)
        {
            struct PartyTrigger* pr_trig = &kfx_game_state.script.party_triggers[kfx_game_state.script.party_triggers_num];
            pr_trig->flags = TrgF_CREATE_EFFECT_GENERATOR;
            pr_trig->flags |= next_command_reusable ? TrgF_REUSABLE : 0;
            pr_trig->plyr_idx = 0; //not needed
            pr_trig->creatr_id = 0; //not needed
            pr_trig->exp_level = gen_id;
            pr_trig->carried_gold = range;
            pr_trig->location = location;
            pr_trig->ncopies = 1;
            pr_trig->condit_idx = get_script_current_condition();
        }
        else
        {
            SCRPTERRLOG("Max party triggers reached, failed to spawn effect generator");
        }
        kfx_game_state.script.party_triggers_num++;
    }
}

void reset_or_trigger_action_point_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    int64_t apt_idx = action_point_number_to_index(scline->np[0]);
    if (!action_point_exists_idx(apt_idx))
    {
        SCRPTERRLOG("Non-existing Action Point, no %" PRId64, (int64_t)(scline->np[0]));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->longs[0] = apt_idx;
    PlayerNumber plyr_idx = (scline->tp[1][0] == '\0') ? ALL_PLAYERS : get_id(player_desc, scline->tp[1]);
    if (plyr_idx == -1)
    {
        SCRPTERRLOG("Invalid player: %s", scline->tp[1]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->chars[4] = plyr_idx;
    PROCESS_SCRIPT_VALUE(scline->command);
}

void reset_action_point_process(struct ScriptContext* context)
{
    action_point_reset_idx(context->value->longs[0], context->value->chars[4]);
}

void add_object_to_level_at_pos_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    int64_t tngmodel = get_rid(object_desc, scline->tp[0]);
    if (tngmodel == -1)
    {
        SCRPTERRLOG("Unknown object: %s", scline->tp[0]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->shorts[0] = tngmodel;
    if (!subtile_coords_invalid(scline->np[1], scline->np[2]))
    {
        value->shorts[2] = scline->np[1];
        value->shorts[3] = scline->np[2];
    }
    else
    {
        SCRPTERRLOG("Invalid subtile co-ordinates: %" PRId64 ", %" PRId64, (int64_t)(scline->np[1]), (int64_t)(scline->np[2]));
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->longs[2] = scline->np[3];
    PlayerNumber plyr_idx = get_rid(player_desc, scline->tp[4]); // Optional variable
    if ((plyr_idx == -1) || (plyr_idx == ALL_PLAYERS))
    {
        plyr_idx = PLAYER_NEUTRAL;
    }
    int64_t angle = 0;
    if (strcmp(scline->tp[5], "") != 0) // Optional variable
    {
        if (parameter_is_number(scline->tp[5]))
        {
            angle = atoi(scline->tp[5]) % DEGREES_360;
        }
        else
        {
            angle = get_rid(orientation_desc, scline->tp[5]);
            if (angle < 0)
            {
                SCRPTERRLOG("Unknown orientation: %s", scline->tp[5]);
                DEALLOCATE_SCRIPT_VALUE
                return;
            }
        }
    }

    value->chars[2] = plyr_idx;
    value->shorts[6] = angle;
    PROCESS_SCRIPT_VALUE(scline->command);
}

void add_object_to_level_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, 0);
    int64_t obj_id = get_rid(object_desc, scline->tp[0]);
    if (obj_id == -1)
    {
        SCRPTERRLOG("Unknown object, '%s'", scline->tp[0]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->shorts[0] = obj_id;
    TbMapLocation location;
    if (!get_map_location_id(scline->tp[1], &location))
    {
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->ulongs[1] = location;
    value->longs[2] = scline->np[2];
    PlayerNumber plyr_idx = get_rid(player_desc, scline->tp[3]);
    if ((plyr_idx == -1) || (plyr_idx == ALL_PLAYERS)) //Optional variable
    {
        plyr_idx = PLAYER_NEUTRAL;
    }

    int64_t angle = 0;
    if (strcmp(scline->tp[4], "") != 0) //Optional variable
    {
        if (parameter_is_number(scline->tp[4]))
        {
            angle = atoi(scline->tp[4]) % DEGREES_360;
        }
        else
        {
            angle = get_rid(orientation_desc, scline->tp[4]);
            if (angle < 0)
            {
                SCRPTERRLOG("Unknown orientation: %s", scline->tp[4]);
                DEALLOCATE_SCRIPT_VALUE
                return;
            }
        }
    }

    value->chars[2] = plyr_idx;
    value->shorts[8] = angle;
    PROCESS_SCRIPT_VALUE(scline->command);
}

void add_object_to_level_process(struct ScriptContext* context)
{
    struct Coord3d pos;
    if (get_coords_at_location(&pos,context->value->ulongs[1],true))
    {
        script_process_new_object(context->value->shorts[0], pos.x.stl.num, pos.y.stl.num, context->value->longs[2], context->value->chars[2], context->value->shorts[8]);
    }
}

void add_object_to_level_at_pos_process(struct ScriptContext* context)
{
    script_process_new_object(context->value->shorts[0], context->value->shorts[2], context->value->shorts[3], context->value->longs[2], context->value->chars[2],context->value->shorts[6]);
}

void trigger_action_point_process(struct ScriptContext* context)
{
    action_point_trigger_idx(context->value->longs[0], context->value->chars[4]);
}

void reveal_map_rect_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    int64_t x = scline->np[1];
    int64_t y = scline->np[2];
    int64_t w = scline->np[3];
    int64_t h = scline->np[4];
    command_add_value(scline->command, plr_range_id, x, y, (h<<16)+w);
}

void computer_dig_to_location_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    const char* origin = scline->tp[1];
    const char* destination = scline->tp[2];
    TbMapLocation orig_loc;
    if (!get_map_location_id(origin, &orig_loc))
    {
        SCRPTWRNLOG("Dig to location script command has invalid source location: %s", origin);
        return;
    }
    TbMapLocation dest_loc;
    if (!get_map_location_id(destination, &dest_loc))
    {
        SCRPTWRNLOG("Dig to location script command has invalid destination location: %s", destination);
        return;
    }

    command_add_value(scline->command, plr_range_id, orig_loc, dest_loc, 0);
}

/** REVEAL_MAP_RECT: the centre, and the width and height packed in the third value. */
void reveal_map_rect_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    player_reveal_map_area(context->player_idx, value->longs[0], value->longs[1], (value->longs[2])&0xffff, (value->longs[2]>>16)&0xffff);
}

void computer_dig_to_location_process(struct ScriptContext *context)
{
    script_computer_dig_to_location(context->player_idx, context->value->longs[0], context->value->longs[1]);
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
