/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_palette.cpp
 *     Palette grouping for the toolbox.
 * @par Purpose:
 *     See editor_palette.h.
 * @par Comment:
 *     Terrain: a slab is a Room slab when some room's assigned_slab points at
 *     it (portal, heart pedestal, guard post and bridge frame included, as
 *     each is a room in the config). Doors, walls that belong to a room
 *     (slb_id != 0 under the fortified-wall category) and other obstacle
 *     slabs are Other; a wall that belongs to a room (its SlbID matches the
 *     room floor's) is Walls. Everything left -- rock, earth, gold, gems, paths,
 *     water, lava, and claimed walls such as DRAPE_WALL -- is Terrain.
 *     Objects: by the config's own genre.
 */
#include "pre_inc.h"
#include "editor_palette.h"
#include "kfx_config_state.h"
#include "config_terrain.h"
#include "config_objects.h"
#include "config_trapdoor.h"
#include "post_inc.h"

RoomKind editor_room_of_slab(SlabKind kind)
{
    const struct SlabsConfig &slabc = kfx_config_state.conf.slab_conf;
    for (int32_t r = 1; r < slabc.room_types_count; r++)
    {
        if (slabc.room_cfgstats[r].assigned_slab == kind)
            return (RoomKind)r;
    }
    return 0;
}

RoomKind editor_room_of_wall(SlabKind kind)
{
    if (!slab_kind_is_room_wall(kind))
        return 0;
    const struct SlabConfigStats *wall = get_slab_kind_stats(kind);
    const struct SlabsConfig &slabc = kfx_config_state.conf.slab_conf;
    for (int32_t r = 1; r < slabc.room_types_count; r++)
    {
        const struct SlabConfigStats *floor_stats = get_slab_kind_stats(slabc.room_cfgstats[r].assigned_slab);
        if ((floor_stats != NULL) && (floor_stats->slb_id == wall->slb_id))
            return (RoomKind)r;
    }
    return 0;
}

int editor_door_of_slab(SlabKind kind)
{
    const long door_count = kfx_config_state.conf.trapdoor_conf.door_types_count;
    for (long m = 1; m < door_count; m++)
    {
        const struct DoorConfigStats *ds = get_door_model_stats((int)m);
        if ((ds != NULL) && ((ds->slbkind[0] == kind) || (ds->slbkind[1] == kind)))
            return (int)m;
    }
    return 0;
}

int editor_terrain_group_of(SlabKind kind)
{
    if (editor_room_of_slab(kind) != 0)
        return ETG_Rooms;
    if (editor_room_of_wall(kind) != 0)
        return ETG_Walls;
    if (slab_kind_is_door(kind) || slab_kind_is_room_wall(kind))
        return ETG_Other;
    const struct SlabConfigStats *stats = get_slab_kind_stats(kind);
    if ((stats != NULL) && (stats->category == SlbAtCtg_Obstacle))
        return ETG_Other;
    return ETG_Terrain;
}

int editor_object_group_of(ThingModel model)
{
    const struct ObjectConfigStats *ostat = get_object_model_stats(model);
    if (ostat == NULL)
        return EOG_Decor;
    switch (ostat->genre)
    {
        case OCtg_Spellbook:   return EOG_Spells;
        case OCtg_SpecialBox:  return EOG_Specials;
        case OCtg_WrkshpBox:   return EOG_Crates;
        default:               return EOG_Decor;
    }
}

int editor_spellbook_power(ThingModel model)
{
    if ((model <= 0) || (model >= OBJECT_TYPES_MAX))
        return -1;
    int power = kfx_config_state.conf.object_conf.object_to_power_artifact[model];
    return (power > 0) ? power : -1;
}
