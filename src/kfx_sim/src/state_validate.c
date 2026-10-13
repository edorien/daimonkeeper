/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file state_validate.c
 *     Whether a simulation state that came from outside can be used without leaving its arrays. See state_validate.h.
 */
/******************************************************************************/
#include "pre_inc.h"
#include <string.h>
#include "state_validate.h"
#include "state_check.h"

#include "kfx_sim_state.h"
#include "config_cubes.h"
#include "config_effects.h"
#include "config_players.h"
#include "creature_control.h"
#include "dungeon_data.h"
#include "light_registry.h"
#include "map_events.h"
#include "player_data.h"
#include "room_data.h"
#include "thing_list.h"
#include "post_inc.h"

_Static_assert(COLUMN_FLOOR_TEXTURES_COUNT == sizeof(((struct KfxSimState *)0)->top_cube) / sizeof(int64_t), "top_cube[] has an entry per floor texture");

struct Ctx {
    const struct KfxSimState *s;
    struct StateCheck chk;
};

/** Notes a violation. @return true if checking should go on (survey mode), false to leave with the reason. */
#define note(cc, what, i, value, ...) (state_check_note(&((struct Ctx *)(cc))->chk, (what), (i), (value), __VA_ARGS__) != 0)

#define THINGS ((int64_t)THINGS_COUNT)

/** Records the first thing found wrong and leaves the function that found it. */
#define BAD(what, i, value, ...) do { if (!note(c, (what), (i), (value), __VA_ARGS__)) return false; } while (0)

/** `value` of the named field is within lo..hi (a count of entries minus one, or an enumeration's last value). */
#define RANGE(value, lo, hi, what, i) do { \
        const int64_t v_ = (int64_t)(value); \
        if ((v_ < (int64_t)(lo)) || (v_ > (int64_t)(hi))) \
            BAD((what), (long long)(i), v_, "%s[%lld] = %lld is outside %lld..%lld", (what), (long long)(i), (long long)v_, (long long)(lo), (long long)(hi)); \
    } while (0)

/** A thing index: 0 means none. */
#define THING_IDX(value, what, i) RANGE(value, 0, THINGS - 1, what, i)
#define ROOM_IDX(value, what, i) RANGE(value, 0, ROOMS_COUNT - 1, what, i)
#define PLAYER_NUM(value, what, i) RANGE(value, 0, PLAYERS_COUNT - 1, what, i)
/** A player number that may also be -1 ("none") -- the neutral player is a number of its own, within PLAYERS_COUNT. */
#define PLAYER_NUM_OR_NONE(value, what, i) RANGE(value, -1, PLAYERS_COUNT - 1, what, i)
#define CCTRL_IDX(value, what, i) RANGE(value, 0, CREATURES_COUNT - 1, what, i)
#define SUBTL_CODED(value, what, i) RANGE(value, 0, (int64_t)MAX_SUBTILES_X * MAX_SUBTILES_Y - 1, what, i)
#define SLAB_CODED(value, what, i) RANGE(value, 0, (int64_t)MAX_TILES_X * MAX_TILES_Y - 1, what, i)
#define COUNT_LE(value, max, what, i) RANGE(value, 0, max, what, i)

/** A position on the map, in map coordinates (a subtile times 256). */
static TbBool coord_ok(const struct Ctx *c, const struct Coord3d *p, const char *what, int64_t i)
{
    const int64_t max_x = (int64_t)MAX_SUBTILES_X * COORD_PER_STL - 1;
    const int64_t max_y = (int64_t)MAX_SUBTILES_Y * COORD_PER_STL - 1;
    if (((int64_t)p->x.val < 0) || ((int64_t)p->x.val > max_x) || ((int64_t)p->y.val < 0) || ((int64_t)p->y.val > max_y))
        BAD(what, i, (int64_t)p->x.val, "%s[%lld] position (%lld,%lld) is outside the largest map", what, (long long)i, (long long)p->x.val, (long long)p->y.val);
    return true;
}

static TbBool validate_map_geometry(const struct Ctx *c)
{
    const struct KfxSimState *s = c->s;
    RANGE(s->map_subtiles_x, 0, MAX_SUBTILES_X, "map_subtiles_x", 0);
    RANGE(s->map_subtiles_y, 0, MAX_SUBTILES_Y, "map_subtiles_y", 0);
    RANGE(s->map_tiles_x, 0, MAX_TILES_X, "map_tiles_x", 0);
    RANGE(s->map_tiles_y, 0, MAX_TILES_Y, "map_tiles_y", 0);
    RANGE(s->slabset_num, 0, SLABSET_COUNT, "slabset_num", 0);
    RANGE(s->slabobjs_num, 0, SLABOBJS_COUNT, "slabobjs_num", 0);
    RANGE(s->unrevealed_column_idx, 0, COLUMNS_COUNT - 1, "unrevealed_column_idx", 0);
    return true;
}

static TbBool validate_columns(const struct Ctx *c)
{
    const struct KfxSimState *s = c->s;
    for (int64_t i = 0; i < COLUMNS_COUNT; i++)
    {
        const struct Column *col = &s->columns_data[i];
        for (int k = 0; k < COLUMN_STACK_HEIGHT; k++)
            RANGE(col->cubes[k], 0, CUBE_ITEMS_MAX - 1, "columns_data.cubes", i);
        RANGE(col->floor_texture, 0, COLUMN_FLOOR_TEXTURES_COUNT - 1, "columns_data.floor_texture", i);
    }
    return true;
}

static TbBool validate_map_blocks(const struct Ctx *c)
{
    const struct KfxSimState *s = c->s;
    const int64_t blocks = (int64_t)MAX_SUBTILES_X * MAX_SUBTILES_Y;
    for (int64_t i = 0; i < blocks; i++)
    {
        const struct Map *m = &s->map[i];
        RANGE(m->col_idx, 0, COLUMNS_COUNT - 1, "map.col_idx", i);
        THING_IDX(m->mapwho, "map.mapwho", i);
    }
    const int64_t slabs = (int64_t)MAX_TILES_X * MAX_TILES_Y;
    for (int64_t i = 0; i < slabs; i++)
    {
        const struct SlabMap *sl = &s->slabmap[i];
        // 0 ends a room's list of slabs
        SLAB_CODED(sl->next_in_room, "slabmap.next_in_room", i);
        RANGE(sl->kind, 0, TERRAIN_ITEMS_MAX - 1, "slabmap.kind", i);
        ROOM_IDX(sl->room_index, "slabmap.room_index", i);
        RANGE(sl->owner, -1, PLAYERS_COUNT - 1, "slabmap.owner", i);
    }
    return true;
}

static TbBool validate_things(const struct Ctx *c)
{
    const struct KfxSimState *s = c->s;
    RANGE(s->synced_free_things_count, 0, SYNCED_THINGS_COUNT, "synced_free_things_count", 0);
    RANGE(s->unsynced_free_things_count, 0, UNSYNCED_THINGS_COUNT, "unsynced_free_things_count", 0);
    for (int64_t i = 0; i < s->synced_free_things_count; i++)
        THING_IDX(s->synced_free_things[i], "synced_free_things", i);
    for (int64_t i = 0; i < s->unsynced_free_things_count; i++)
        THING_IDX(s->unsynced_free_things[i], "unsynced_free_things", i);

    for (int64_t i = 0; i < THINGS; i++)
    {
        const struct Thing *t = &s->things_data[i];
        if ((t->alloc_flags & TAlF_Exists) == 0)
            continue;
        if (t->index != i)
            BAD("things_data.index", i, t->index, "things_data[%lld].index = %lld is not its own position", (long long)i, (long long)t->index);
        RANGE(t->class_id, 1, TCls_CaveIn, "things_data.class_id", i);
        RANGE(t->owner, 0, PLAYERS_COUNT - 1, "things_data.owner", i);
        THING_IDX(t->next_on_mapblk, "things_data.next_on_mapblk", i);
        THING_IDX(t->prev_on_mapblk, "things_data.prev_on_mapblk", i);
        THING_IDX(t->next_of_class, "things_data.next_of_class", i);
        THING_IDX(t->prev_of_class, "things_data.prev_of_class", i);
        // a parent of -1 or 65535 means none (both occur in real levels)
        if ((t->parent_idx != -1) && (t->parent_idx != 65535))
            THING_IDX(t->parent_idx, "things_data.parent_idx", i);
        CCTRL_IDX(t->ccontrol_idx, "things_data.ccontrol_idx", i);
        RANGE(t->light_id, 0, LIGHTS_COUNT - 1, "things_data.light_id", i);
        RANGE(t->snd_emitter_id, 0, SOUND_EMITTERS_MAX, "things_data.snd_emitter_id", i);
        PLAYER_NUM_OR_NONE(t->pushed_by_player, "things_data.pushed_by_player", i);
        PLAYER_NUM_OR_NONE(t->holding_player, "things_data.holding_player", i);
        if (!coord_ok(c, &t->mappos, "things_data.mappos", i))
            return false;
        switch (t->class_id)
        {
        case TCls_Creature:
        case TCls_DeadCreature:
            RANGE(t->model, 0, CREATURE_TYPES_MAX - 1, "things_data.model(creature)", i);
            break;
        case TCls_Object:
            RANGE(t->model, 0, OBJECT_TYPES_MAX - 1, "things_data.model(object)", i);
            break;
        case TCls_Shot:
            RANGE(t->model, 0, MAGIC_ITEMS_MAX - 1, "things_data.model(shot)", i);
            THING_IDX(t->shot.target_idx, "things_data.shot.target_idx", i);
            COUNT_LE(t->shot.num_wind_affected, CREATURES_COUNT, "things_data.shot.num_wind_affected", i);
            for (int64_t k = 0; (k < t->shot.num_wind_affected) && (k < CREATURES_COUNT); k++)
                CCTRL_IDX(t->shot.wind_affected_creature[k], "things_data.shot.wind_affected_creature", i);
            break;
        case TCls_EffectElem:
            RANGE(t->model, 0, EFFECTSELLEMENTS_TYPES_MAX - 1, "things_data.model(effect element)", i);
            break;
        case TCls_Effect:
            RANGE(t->model, 0, EFFECTS_TYPES_MAX - 1, "things_data.model(effect)", i);
            break;
        case TCls_EffectGen:
            RANGE(t->model, 0, EFFECTSGEN_TYPES_MAX - 1, "things_data.model(effect generator)", i);
            break;
        case TCls_Trap:
            RANGE(t->model, 0, TRAPDOOR_TYPES_MAX - 1, "things_data.model(trap/door)", i);
            break;
        case TCls_Door:
            RANGE(t->model, 0, TRAPDOOR_TYPES_MAX - 1, "things_data.model(trap/door)", i);
            // indexes the door's two slab kinds
            RANGE(t->door.orientation, 0, 1, "things_data.door.orientation", i);
            break;
        default:
            break;
        }
    }
    return true;
}

/** The things on each map block form a list that has to end, and every member has to be a thing. */
static TbBool validate_mapwho_chains(const struct Ctx *c)
{
    const struct KfxSimState *s = c->s;
    const int64_t blocks = (int64_t)MAX_SUBTILES_X * MAX_SUBTILES_Y;
    for (int64_t b = 0; b < blocks; b++)
    {
        int64_t idx = s->map[b].mapwho;
        int64_t steps = 0;
        while (idx != 0)
        {
            if ((idx < 0) || (idx >= THINGS))
                break; // (already noted as an out-of-range link)
            if (++steps > THINGS)
            {
                BAD("map.mapwho chain never ends", b, idx, "map[%lld].mapwho: the list of things on the block never ends", (long long)b);
                break;
            }
            // A thing freed while still linked on a block does happen in real play and the engine's walkers
            // bounds-check what they follow; it is only required that its link stays inside the array.
            const struct Thing *t = &s->things_data[idx];
            if (((t->alloc_flags & TAlF_Exists) == 0) && ((t->next_on_mapblk < 0) || (t->next_on_mapblk >= THINGS)))
            {
                BAD("map.mapwho passes a freed thing whose link is out of range", b, t->next_on_mapblk, "map[%lld].mapwho: freed thing %lld links to %lld", (long long)b, (long long)idx, (long long)t->next_on_mapblk);
                break;
            }
            idx = t->next_on_mapblk;
        }
    }
    return true;
}

/** The lists of things by class (creatures, shots, ...) have to end within their stated length. The last two are
 *  lists of lights, through light_registry.lights[].next_in_list (see validate_lights()). */
static TbBool validate_thing_lists(const struct Ctx *c)
{
    const struct KfxSimState *s = c->s;
    for (int64_t l = 0; l < 13; l++)
    {
        const struct StructureList *list = &s->thing_lists[l];
        if ((l == TngList_StaticLights) || (l == TngList_DynamLights))
        {
            COUNT_LE(list->count, LIGHTS_COUNT, "thing_lists.count (lights)", l);
            RANGE(list->index, 0, LIGHTS_COUNT - 1, "thing_lists.index (lights)", l);
            int64_t idx = (int64_t)list->index;
            int64_t steps = 0;
            while (idx != 0)
            {
                if ((idx < 0) || (idx >= LIGHTS_COUNT))
                    break;
                if (++steps > LIGHTS_COUNT)
                {
                    BAD("thing_lists light chain never ends", l, idx, "thing_lists[%lld]: the list of lights never ends", (long long)l);
                    break;
                }
                // A light on a list says so: one that does not is added again by the next call that puts it on a
                // list, which links it to itself (the corruption fuzzer found the render loop spinning on it).
                if ((s->light_registry.lights[idx].flags2 & LgtF2_InList) == 0)
                {
                    BAD("thing_lists has a light that is not marked as listed", l, idx, "thing_lists[%lld]: light %lld is on the list but not marked as listed", (long long)l, (long long)idx);
                    break;
                }
                // ...and sits on the list its Dynamic flag names: removing a light looks in that list, so one that is
                // on the other is never unlinked, and is then linked again while still on it (found by the fuzzer)
                const TbBool dynamic = (s->light_registry.lights[idx].flags & LgtF_Dynamic) != 0;
                if ((s->light_registry.lights[idx].flags & LgtF_Allocated) == 0)
                {
                    BAD("thing_lists has a light that is not allocated", l, idx, "thing_lists[%lld]: light %lld is on the list but not allocated", (long long)l, (long long)idx);
                    break;
                }
                if (dynamic != (l == TngList_DynamLights))
                {
                    BAD("thing_lists has a light on the wrong list", l, idx, "thing_lists[%lld]: light %lld is on the wrong list for its Dynamic flag", (long long)l, (long long)idx);
                    break;
                }
                idx = s->light_registry.lights[idx].next_in_list;
            }
            continue;
        }
        COUNT_LE(list->count, THINGS, "thing_lists.count", l);
        THING_IDX(list->index, "thing_lists.index", l);
        int64_t idx = (int64_t)list->index;
        int64_t steps = 0;
        while (idx != 0)
        {
            if ((idx < 0) || (idx >= THINGS))
                break;
            if (++steps > THINGS)
            {
                BAD("thing_lists chain never ends", l, idx, "thing_lists[%lld]: the list never ends", (long long)l);
                break;
            }
            const struct Thing *t = &s->things_data[idx];
            if (((t->alloc_flags & TAlF_Exists) == 0) && ((t->next_of_class < 0) || (t->next_of_class >= THINGS)))
            {
                BAD("thing_lists passes a freed thing whose link is out of range", l, t->next_of_class, "thing_lists[%lld]: freed thing %lld links to %lld", (long long)l, (long long)idx, (long long)t->next_of_class);
                break;
            }
            idx = t->next_of_class;
        }
    }
    return true;
}

static TbBool validate_lights(const struct Ctx *c)
{
    const struct LightRegistry *lr = &c->s->light_registry;
    for (int64_t i = 0; i < LIGHTS_COUNT; i++)
    {
        const struct Light *l = &lr->lights[i];
        RANGE(l->index, 0, LIGHTS_COUNT - 1, "lights.index", i);
        // a light that is in use is its own index: the lists of lights link by it, and adding or removing a light writes
        // it into them (a light whose index was something else makes a list that loops)
        if ((l->index != 0) && (l->index != i))
            BAD("lights.index is not the light's position", i, l->index, "lights[%lld].index = %lld is not its own position", (long long)i, (long long)l->index);
        RANGE(l->next_in_list, 0, LIGHTS_COUNT - 1, "lights.next_in_list", i);
        RANGE(l->shadow_index, 0, SHADOW_CACHE_COUNT - 1, "lights.shadow_index", i);
        // (-1 and 0xFFFFFFFF both mean no slab)
        if (((int64_t)l->attached_slb != -1) && ((int64_t)l->attached_slb != 0xFFFFFFFFLL))
            SLAB_CODED(l->attached_slb, "lights.attached_slb", i);
    }
    COUNT_LE(lr->total_dynamic_lights, LIGHTS_COUNT, "light_registry.total_dynamic_lights", 0);
    COUNT_LE(lr->total_stat_lights, LIGHTS_COUNT, "light_registry.total_stat_lights", 0);
    return true;
}

static TbBool validate_creature_controls(const struct Ctx *c)
{
    const struct KfxSimState *s = c->s;
    for (int64_t i = 0; i < CREATURES_COUNT; i++)
    {
        const struct CreatureControl *cc = &s->cctrl_data[i];
        CCTRL_IDX(cc->index, "cctrl_data.index", i);
        for (int k = 0; k < COMBAT_MELEE_OPPONENTS_LIMIT; k++)
            THING_IDX(cc->opponents_melee[k], "cctrl_data.opponents_melee", i);
        for (int k = 0; k < COMBAT_RANGED_OPPONENTS_LIMIT; k++)
            THING_IDX(cc->opponents_ranged[k], "cctrl_data.opponents_ranged", i);
        COUNT_LE(cc->opponents_melee_count, COMBAT_MELEE_OPPONENTS_LIMIT, "cctrl_data.opponents_melee_count", i);
        COUNT_LE(cc->opponents_ranged_count, COMBAT_RANGED_OPPONENTS_LIMIT, "cctrl_data.opponents_ranged_count", i);
        THING_IDX(cc->players_prev_creature_idx, "cctrl_data.players_prev_creature_idx", i);
        THING_IDX(cc->players_next_creature_idx, "cctrl_data.players_next_creature_idx", i);
        THING_IDX(cc->arming_thing_id, "cctrl_data.arming_thing_id", i);
        THING_IDX(cc->pickup_object_id, "cctrl_data.pickup_object_id", i);
        THING_IDX(cc->pickup_creature_id, "cctrl_data.pickup_creature_id", i);
        THING_IDX(cc->group_leader_idx, "cctrl_data.group_leader_idx", i);
        THING_IDX(cc->next_in_group, "cctrl_data.next_in_group", i);
        THING_IDX(cc->prev_in_group, "cctrl_data.prev_in_group", i);
        THING_IDX(cc->lairtng_idx, "cctrl_data.lairtng_idx", i);
        THING_IDX(cc->dragtng_idx, "cctrl_data.dragtng_idx", i);
        THING_IDX(cc->targtng_idx, "cctrl_data.targtng_idx", i);
        THING_IDX(cc->battle_prev_creatr, "cctrl_data.battle_prev_creatr", i);
        THING_IDX(cc->battle_next_creatr, "cctrl_data.battle_next_creatr", i);
        THING_IDX(cc->timebomb_countdown_id, "cctrl_data.timebomb_countdown_id", i);
        THING_IDX(cc->timebomb_target_id, "cctrl_data.timebomb_target_id", i);
        THING_IDX(cc->summoner_idx, "cctrl_data.summoner_idx", i);
        for (int k = 0; k < FAMILIAR_MAX; k++)
            THING_IDX(cc->familiar_idx[k], "cctrl_data.familiar_idx", i);
        for (int k = 0; k < 3; k++)
        {
            THING_IDX(cc->spell_thing_index_armour[k], "cctrl_data.spell_thing_index_armour", i);
            THING_IDX(cc->spell_thing_index_disease[k], "cctrl_data.spell_thing_index_disease", i);
        }
        THING_IDX(cc->next_in_room, "cctrl_data.next_in_room", i);
        THING_IDX(cc->prev_in_room, "cctrl_data.prev_in_room", i);
        ROOM_IDX(cc->lair_room_id, "cctrl_data.lair_room_id", i);
        ROOM_IDX(cc->last_work_room_id, "cctrl_data.last_work_room_id", i);
        ROOM_IDX(cc->work_room_id, "cctrl_data.work_room_id", i);
        ROOM_IDX(cc->target_room_id, "cctrl_data.target_room_id", i);
        RANGE(cc->battle_id, 0, BATTLES_COUNT - 1, "cctrl_data.battle_id", i);
        RANGE(cc->instance_id, 0, INSTANCE_TYPES_MAX - 1, "cctrl_data.instance_id", i);
        RANGE(cc->active_instance_id, 0, INSTANCE_TYPES_MAX - 1, "cctrl_data.active_instance_id", i);
        SUBTL_CODED(cc->collided_door_subtile, "cctrl_data.collided_door_subtile", i);
        SUBTL_CODED(cc->damage_wall_coords, "cctrl_data.damage_wall_coords", i);
        RANGE(cc->arid.current_waypoint, 0, ARID_WAYPOINTS_COUNT, "cctrl_data.arid.current_waypoint", i);
        RANGE(cc->arid.stored_waypoints, 0, ARID_WAYPOINTS_COUNT, "cctrl_data.arid.stored_waypoints", i);
        SUBTL_CODED(cc->navi.first_colliding_block, "cctrl_data.navi.first_colliding_block", i);
        SUBTL_CODED(cc->navi.second_colliding_block, "cctrl_data.navi.second_colliding_block", i);
    }
    return true;
}

static TbBool validate_rooms(const struct Ctx *c)
{
    const struct KfxSimState *s = c->s;
    for (int64_t i = 0; i < ROOMS_COUNT; i++)
    {
        const struct Room *r = &s->rooms[i];
        if ((r->alloc_flags & 0x01) == 0 && r->index == 0)
            continue; // never used
        ROOM_IDX(r->index, "rooms.index", i);
        RANGE(r->owner, -1, PLAYERS_COUNT - 1, "rooms.owner", i);
        ROOM_IDX(r->prev_of_owner, "rooms.prev_of_owner", i);
        ROOM_IDX(r->next_of_owner, "rooms.next_of_owner", i);
        ROOM_IDX(r->prev_of_kind, "rooms.prev_of_kind", i);
        ROOM_IDX(r->next_of_kind, "rooms.next_of_kind", i);
        RANGE(r->kind, 0, TERRAIN_ITEMS_MAX - 1, "rooms.kind", i);
        THING_IDX(r->cached_nearby_creature_index, "rooms.cached_nearby_creature_index", i);
        THING_IDX(r->creatures_list, "rooms.creatures_list", i);
        SLAB_CODED(r->slabs_list, "rooms.slabs_list", i);
        SLAB_CODED(r->slabs_list_tail, "rooms.slabs_list_tail", i);
        SLAB_CODED(r->flame_slb, "rooms.flame_slb", i);
        COUNT_LE(r->slabs_count, (int64_t)MAX_TILES_X * MAX_TILES_Y, "rooms.slabs_count", i);
        RANGE(r->central_stl_x, 0, MAX_SUBTILES_X - 1, "rooms.central_stl_x", i);
        RANGE(r->central_stl_y, 0, MAX_SUBTILES_Y - 1, "rooms.central_stl_y", i);
        // the room's slabs are a list through slabmap.next_in_room that has to end
        int64_t slb = (int64_t)r->slabs_list;
        int64_t steps = 0;
        while (slb != 0)
        {
            if ((slb < 0) || (slb >= (int64_t)MAX_TILES_X * MAX_TILES_Y))
                break;
            if (++steps > (int64_t)MAX_TILES_X * MAX_TILES_Y)
            {
                BAD("rooms slab chain never ends", i, slb, "rooms[%lld]: the list of its slabs never ends", (long long)i);
                break;
            }
            slb = (int64_t)s->slabmap[slb].next_in_room;
        }
    }
    return true;
}

static TbBool validate_dungeons(const struct Ctx *c)
{
    const struct KfxSimState *s = c->s;
    for (int64_t i = 0; i < DUNGEONS_COUNT; i++)
    {
        const struct Dungeon *d = &s->dungeon[i];
        // indexes the per-colour tables (rooms, paths, call-to-arms flags...): the 9 players, the neutral one and one more
        RANGE(d->color_idx, 0, 10, "dungeon.color_idx", i);
        THING_IDX(d->dnheart_idx, "dungeon.dnheart_idx", i);
        THING_IDX(d->creatr_list_start, "dungeon.creatr_list_start", i);
        THING_IDX(d->digger_list_start, "dungeon.digger_list_start", i);
        COUNT_LE(d->num_summon, MAX_SUMMONS, "dungeon.num_summon", i);
        for (int k = 0; k < MAX_SUMMONS; k++)
            THING_IDX(d->summon_list[k], "dungeon.summon_list", i);
        COUNT_LE(d->num_things_in_hand, MAX_THINGS_IN_HAND, "dungeon.num_things_in_hand", i);
        for (int k = 0; k < MAX_THINGS_IN_HAND; k++)
            THING_IDX(d->things_in_hand[k], "dungeon.things_in_hand", i);
        COUNT_LE(d->task_count, MAPTASKS_COUNT, "dungeon.task_count", i);
        for (int k = 0; k < MAPTASKS_COUNT; k++)
            SUBTL_CODED(d->task_list[k].coords, "dungeon.task_list.coords", i);
        COUNT_LE(d->research_num, DUNGEON_RESEARCH_COUNT, "dungeon.research_num", i);
        RANGE(d->current_research_idx, -1, DUNGEON_RESEARCH_COUNT, "dungeon.current_research_idx", i);
        COUNT_LE(d->digger_stack_length, DIGGER_TASK_MAX_COUNT, "dungeon.digger_stack_length", i);
        for (int k = 0; k < DIGGER_TASK_MAX_COUNT; k++)
            SUBTL_CODED(d->digger_stack[k].stl_num, "dungeon.digger_stack.stl_num", i);
        COUNT_LE(d->dead_creatures_count, DEAD_CREATURES_MAX_COUNT, "dungeon.dead_creatures_count", i);
        RANGE(d->dead_creature_idx, 0, DEAD_CREATURES_MAX_COUNT, "dungeon.dead_creature_idx", i);
        for (int k = 0; k < EVENT_BUTTONS_COUNT + 1; k++)
            RANGE(d->event_button_index[k], 0, EVENTS_COUNT - 1, "dungeon.event_button_index", i);
        for (int k = 0; k < TERRAIN_ITEMS_MAX; k++)
            ROOM_IDX(d->room_list_start[k], "dungeon.room_list_start", i);
        THING_IDX(d->backup_heart_idx, "dungeon.backup_heart_idx", i);
        THING_IDX(d->free_soul_idx, "dungeon.free_soul_idx", i);
        THING_IDX(d->gold_hoard_for_pickup, "dungeon.gold_hoard_for_pickup", i);
        THING_IDX(d->zoom_annoyed_creature_idx, "dungeon.zoom_annoyed_creature_idx", i);
        THING_IDX(d->sight_casted_thing_idx, "dungeon.sight_casted_thing_idx", i);
    }
    return true;
}

static TbBool validate_players(const struct Ctx *c)
{
    const struct KfxSimState *s = c->s;
    for (int64_t i = 0; i < PLAYERS_COUNT; i++)
    {
        const struct PlayerInfo *p = &s->players[i];
        THING_IDX(p->controlled_thing_idx, "players.controlled_thing_idx", i);
        THING_IDX(p->thing_under_hand, "players.thing_under_hand", i);
        THING_IDX(p->hand_thing_idx, "players.hand_thing_idx", i);
        THING_IDX(p->cta_flag_idx, "players.cta_flag_idx", i);
        THING_IDX(p->influenced_thing_idx, "players.influenced_thing_idx", i);
        RANGE(p->active_camera_idx, 0, 3, "players.active_camera_idx", i);
        RANGE(p->view_type, 0, PVT_MapFadeOut + 8, "players.view_type", i);
        RANGE(p->work_state, 0, PSt_ListEnd, "players.work_state", i);
        RANGE(p->continue_work_state, 0, PSt_ListEnd, "players.continue_work_state", i);
        RANGE(p->user_id, -1, MAX_NET_USERS, "players.user_id", i);
        // The computer player's wander points: wander_point_add() writes points[point_insert_idx], the search loops
        // to num_check_per_run slabs from last_checked_slb_num (found by the corruption fuzzer: a huge count spins).
        const struct Wander *wanders[2] = { &p->wandr_within, &p->wandr_outside };
        for (int w = 0; w < 2; w++)
        {
            const struct Wander *wd = wanders[w];
            COUNT_LE(wd->points_count, WANDER_POINTS_COUNT, "players.wandr.points_count", i);
            COUNT_LE(wd->point_insert_idx, WANDER_POINTS_COUNT - 1, "players.wandr.point_insert_idx", i);
            COUNT_LE(wd->last_checked_slb_num, (int64_t)MAX_TILES_X * MAX_TILES_Y, "players.wandr.last_checked_slb_num", i);
            COUNT_LE(wd->num_check_per_run, (int64_t)MAX_TILES_X * MAX_TILES_Y, "players.wandr.num_check_per_run", i);
            COUNT_LE(wd->max_found_per_check, WANDER_POINTS_COUNT, "players.wandr.max_found_per_check", i);
            for (int k = 0; k < WANDER_POINTS_COUNT; k++)
            {
                RANGE(wd->points[k].stl_x, 0, MAX_SUBTILES_X, "players.wandr.points.stl_x", i);
                RANGE(wd->points[k].stl_y, 0, MAX_SUBTILES_Y, "players.wandr.points.stl_y", i);
            }
        }
    }
    for (int64_t i = 0; i < MAX_NET_USERS; i++)
    {
        const struct UserState *u = &s->user_states[i];
        RANGE(u->cursor_light_idx, 0, LIGHTS_COUNT - 1, "user_states.cursor_light_idx", i);
        THING_IDX(u->selected_fp_thing_pickup, "user_states.selected_fp_thing_pickup", i);
        RANGE(u->battleid, 0, BATTLES_COUNT - 1, "user_states.battleid", i);
        RANGE(u->chosen_trap_kind, 0, TRAPDOOR_TYPES_MAX - 1, "user_states.chosen_trap_kind", i);
        RANGE(u->chosen_door_kind, 0, TRAPDOOR_TYPES_MAX - 1, "user_states.chosen_door_kind", i);
    }
    return true;
}

static TbBool validate_misc(const struct Ctx *c)
{
    const struct KfxSimState *s = c->s;
    for (int64_t i = 0; i < BATTLES_COUNT; i++)
    {
        THING_IDX(s->battles[i].first_creatr, "battles.first_creatr", i);
        THING_IDX(s->battles[i].last_creatr, "battles.last_creatr", i);
    }
    for (int64_t i = 0; i < EVENTS_COUNT; i++)
        RANGE(s->event[i].owner, 0, PLAYERS_COUNT, "event.owner", i);
    RANGE(s->level_human_player, -1, PLAYERS_COUNT - 1, "level_human_player", 0);
    PLAYER_NUM_OR_NONE(s->armageddon_caster_idx, "armageddon_caster_idx", 0);
    RANGE(s->entrance_room_id, 0, ROOMS_COUNT - 1, "entrance_room_id", 0);
    return true;
}

static TbBool run_checks(struct Ctx *ctx)
{
    const struct Ctx *c = ctx;
    // The dimensions first: everything after relies on the map being no bigger than its arrays.
    if (!validate_map_geometry(c) || !validate_columns(c) || !validate_map_blocks(c))
        return false;
    if (!validate_things(c) || !validate_mapwho_chains(c) || !validate_thing_lists(c) || !validate_lights(c))
        return false;
    if (!validate_creature_controls(c) || !validate_rooms(c) || !validate_dungeons(c))
        return false;
    if (!validate_players(c) || !validate_misc(c))
        return false;
    return true;
}

TbBool sim_state_validate(const struct KfxSimState *state, char *why, size_t why_size)
{
    struct Ctx ctx = { state, { why, why_size, NULL, 0 } };
    if ((why != NULL) && (why_size > 0))
        why[0] = '\0';
    return run_checks(&ctx);
}

int64_t sim_state_survey(const struct KfxSimState *state, char *report, size_t report_size)
{
    struct StateSurveyEntry entries[STATE_SURVEY_MAX];
    memset(entries, 0, sizeof(entries));
    struct Ctx ctx = { state, { NULL, 0, entries, 0 } };
    run_checks(&ctx);
    return state_check_report(&ctx.chk, report, report_size);
}
