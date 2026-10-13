/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file config_state_validate.c
 *     Whether a configuration state that came from outside can be used without leaving its tables. See
 *     config_state_validate.h.
 */
/******************************************************************************/
#include "pre_inc.h"
#include <stdint.h>
#include <string.h>
#include "config_state_validate.h"
#include "state_check.h"

#include "kfx_config_state.h"
#include "config_cubes.h"
#include "config_funcnames.h"
#include "config_slabsets.h"
#include "config_terrain.h"
#include "post_inc.h"

_Static_assert(COLUMN_FLOOR_TEXTURES_COUNT == TEXTURE_BLOCKS_COUNT, "a column's floor textures are the textures");
_Static_assert(COLUMN_CUBES_COUNT == CUBE_ITEMS_MAX, "a column's cubes are the cubes");

struct Ctx {
    struct KfxConfigState *s; /**< written only when `repair` is set */
    TbBool repair;
    struct StateCheck chk;
};

#define note(cc, what, i, value, ...) (state_check_note(&((struct Ctx *)(cc))->chk, (what), (i), (value), __VA_ARGS__) != 0)

/** Records the first thing found wrong and leaves the function that found it. */
#define BAD(what, i, value, ...) do { if (!note(c, (what), (i), (value), __VA_ARGS__)) return false; } while (0)

/** Writes `v` into the integer field at `p`, which is `size` bytes wide. */
static void store_field(void *p, size_t size, int64_t v)
{
    switch (size)
    {
    case 1: { int8_t x = (int8_t)v; memcpy(p, &x, sizeof(x)); break; }
    case 2: { int16_t x = (int16_t)v; memcpy(p, &x, sizeof(x)); break; }
    case 4: { int32_t x = (int32_t)v; memcpy(p, &x, sizeof(x)); break; }
    default: memcpy(p, &v, sizeof(v)); break;
    }
}

/** In repair mode, the value a field out of range gets: 0 (none) where that is allowed, else the nearest end. */
static void repair_field(const struct Ctx *c, void *p, size_t size, int64_t lo, int64_t hi)
{
    if (c->repair)
        store_field(p, size, ((lo <= 0) && (hi >= 0)) ? 0 : lo);
}

/** `value` (a field) is within lo..hi; when not, it is reported, and set to 0 if the check is a repair. */
#define RANGE(value, lo, hi, what, i) do { \
        const int64_t v_ = (int64_t)(value); \
        if ((v_ < (int64_t)(lo)) || (v_ > (int64_t)(hi))) \
        { \
            BAD((what), (long long)(i), v_, "%s[%lld] = %lld is outside %lld..%lld", (what), (long long)(i), (long long)v_, (long long)(lo), (long long)(hi)); \
            repair_field(c, &(value), sizeof(value), (int64_t)(lo), (int64_t)(hi)); \
        } \
    } while (0)

/** More samples than one creature sound can have: a sound bank is not that big. */
#define CREATURE_SOUND_COUNT_MAX 1024

#define LEN(array) ((int64_t)(sizeof(array) / sizeof((array)[0])))
/** A count of entries in a table. */
#define COUNT_OF(value, array, what) RANGE(value, 0, LEN(array), what, 0)
/** An index into a function list of `slots` entries. */
#define FUNC(value, slots, what, i) RANGE(value, 0, (slots) - 1, what, i)
/** A function that may also be one of the level script's Lua functions (a negative number, see lua_cfg_funcs.c). */
#define FUNC_OR_LUA(value, slots, what, i) RANGE(value, -(LUA_FUNCS_MAX - 1), (slots) - 1, what, i)
#define LUA_ONLY(value, what, i) RANGE(value, -(LUA_FUNCS_MAX - 1), 0, what, i)

static TbBool validate_counts(const struct Ctx *c)
{
    struct Configs *cf = &c->s->conf;
    COUNT_OF(cf->slab_conf.slab_types_count, cf->slab_conf.slab_cfgstats, "slab_types_count");
    COUNT_OF(cf->slab_conf.room_types_count, cf->slab_conf.room_cfgstats, "room_types_count");
    COUNT_OF(cf->magic_conf.spell_types_count, cf->magic_conf.spell_config, "spell_types_count");
    COUNT_OF(cf->magic_conf.shot_types_count, cf->magic_conf.shot_cfgstats, "shot_types_count");
    COUNT_OF(cf->magic_conf.power_types_count, cf->magic_conf.power_cfgstats, "power_types_count");
    COUNT_OF(cf->magic_conf.special_types_count, cf->magic_conf.special_cfgstats, "special_types_count");
    COUNT_OF(cf->cube_conf.cube_types_count, cf->cube_conf.cube_cfgstats, "cube_types_count");
    COUNT_OF(cf->trapdoor_conf.trap_types_count, cf->trapdoor_conf.trap_cfgstats, "trap_types_count");
    COUNT_OF(cf->trapdoor_conf.door_types_count, cf->trapdoor_conf.door_cfgstats, "door_types_count");
    COUNT_OF(cf->trapdoor_conf.manufacture_types_count, cf->trapdoor_conf.manufacture_data, "manufacture_types_count");
    COUNT_OF(cf->effects_conf.effectgen_cfgstats_count, cf->effects_conf.effectgen_cfgstats, "effectgen_cfgstats_count");
    COUNT_OF(cf->crtr_conf.model_count, cf->crtr_conf.model, "creature model_count");
    COUNT_OF(cf->crtr_conf.states_count, cf->crtr_conf.states, "creature states_count");
    COUNT_OF(cf->crtr_conf.instances_count, cf->crtr_conf.instances, "creature instances_count");
    COUNT_OF(cf->crtr_conf.jobs_count, cf->crtr_conf.jobs, "creature jobs_count");
    COUNT_OF(cf->crtr_conf.angerjobs_count, cf->crtr_conf.angerjobs, "creature angerjobs_count");
    COUNT_OF(cf->crtr_conf.attacktypes_count, cf->crtr_conf.attacktypes, "creature attacktypes_count");
    COUNT_OF(cf->object_conf.object_types_count, cf->object_conf.object_cfgstats, "object_types_count");
    COUNT_OF(cf->column_conf.columns_count, cf->column_conf.cols, "columns_count");
    return true;
}

/** Every function a configuration can select by number, from the table it selects in: a number outside the
 *  table reads (and then calls) whatever lies beyond it. */
static TbBool validate_functions(const struct Ctx *c)
{
    struct Configs *cf = &c->s->conf;
    for (int64_t i = 0; i < LEN(cf->slab_conf.room_cfgstats); i++)
    {
        struct RoomConfigStats *r = &cf->slab_conf.room_cfgstats[i];
        FUNC(r->update_total_capacity_idx, ROOM_TOTAL_CAPACITY_FUNC_SLOTS, "room_cfgstats.update_total_capacity_idx", i);
        FUNC(r->update_storage_in_room_idx, ROOM_USED_CAPACITY_FUNC_SLOTS, "room_cfgstats.update_storage_in_room_idx", i);
        FUNC(r->update_workers_in_room_idx, ROOM_USED_CAPACITY_FUNC_SLOTS, "room_cfgstats.update_workers_in_room_idx", i);
    }
    for (int64_t i = 0; i < LEN(cf->magic_conf.shot_cfgstats); i++)
        LUA_ONLY(cf->magic_conf.shot_cfgstats[i].hit_thing_lua_func_idx, "shot_cfgstats.hit_thing_lua_func_idx", i);
    for (int64_t i = 0; i < LEN(cf->magic_conf.power_cfgstats); i++)
    {
        struct PowerConfigStats *p = &cf->magic_conf.power_cfgstats[i];
        RANGE(p->overcharge_check_idx, 0, POWERMODEL_EXPAND_CHECK_FUNC_SLOTS - 1, "power_cfgstats.overcharge_check_idx", i);
        FUNC_OR_LUA(p->magic_use_func_idx, MAGIC_USE_FUNC_SLOTS, "power_cfgstats.magic_use_func_idx", i);
    }
    for (int64_t i = 0; i < LEN(cf->magic_conf.instance_info); i++)
    {
        struct InstanceInfo *inst = &cf->magic_conf.instance_info[i];
        FUNC(inst->func_idx, CREATURE_INSTANCES_FUNC_TYPE_SLOTS, "instance_info.func_idx", i);
        FUNC(inst->validate_source_func, CREATURE_INSTANCES_VALIDATE_FUNC_TYPE_SLOTS, "instance_info.validate_source_func", i);
        FUNC(inst->validate_target_func, CREATURE_INSTANCES_VALIDATE_FUNC_TYPE_SLOTS, "instance_info.validate_target_func", i);
        FUNC(inst->search_func, CREATURE_INSTANCES_SEARCH_TARGETS_FUNC_TYPE_SLOTS, "instance_info.search_func", i);
    }
    for (int64_t i = 0; i < LEN(cf->trapdoor_conf.trap_cfgstats); i++)
    {
        LUA_ONLY(cf->trapdoor_conf.trap_cfgstats[i].activation_lua_func_idx, "trap_cfgstats.activation_lua_func_idx", i);
        LUA_ONLY(cf->trapdoor_conf.trap_cfgstats[i].updatefn_idx, "trap_cfgstats.updatefn_idx", i);
    }
    for (int64_t i = 0; i < LEN(cf->trapdoor_conf.door_cfgstats); i++)
        LUA_ONLY(cf->trapdoor_conf.door_cfgstats[i].updatefn_idx, "door_cfgstats.updatefn_idx", i);
    for (int64_t i = 0; i < LEN(cf->crtr_conf.states); i++)
    {
        struct CreatureStateConfig *st = &cf->crtr_conf.states[i];
        FUNC(st->process_state, PROCESS_FUNC_COMMANDS_SLOTS, "creature states.process_state", i);
        FUNC(st->cleanup_state, CLEANUP_FUNC_COMMANDS_SLOTS, "creature states.cleanup_state", i);
        FUNC(st->move_from_slab, MOVE_FROM_SLAB_FUNC_COMMANDS_SLOTS, "creature states.move_from_slab", i);
        FUNC(st->move_check, MOVE_CHECK_FUNC_COMMANDS_SLOTS, "creature states.move_check", i);
    }
    for (int64_t i = 0; i < LEN(cf->crtr_conf.jobs); i++)
    {
        struct CreatureJobConfig *j = &cf->crtr_conf.jobs[i];
        FUNC(j->func_plyr_check_idx, CREATURE_JOB_PLAYER_CHECK_FUNC_TYPE_SLOTS, "creature jobs.func_plyr_check_idx", i);
        FUNC(j->func_plyr_assign_idx, CREATURE_JOB_PLAYER_ASSIGN_FUNC_TYPE_SLOTS, "creature jobs.func_plyr_assign_idx", i);
        FUNC(j->func_cord_check_idx, CREATURE_JOB_COORDS_CHECK_FUNC_TYPE_SLOTS, "creature jobs.func_cord_check_idx", i);
        FUNC(j->func_cord_assign_idx, CREATURE_JOB_COORDS_ASSIGN_FUNC_TYPE_SLOTS, "creature jobs.func_cord_assign_idx", i);
    }
    for (int64_t i = 0; i < LEN(cf->object_conf.object_cfgstats); i++)
        FUNC_OR_LUA(cf->object_conf.object_cfgstats[i].updatefn_idx, OBJECT_UPDATE_FUNC_SLOTS, "object_cfgstats.updatefn_idx", i);
    // The names of the script's functions are looked up as strings.
    for (int64_t i = 0; i < LEN(cf->lua.lua_funcs); i++)
    {
        if (memchr(cf->lua.lua_funcs[i], '\0', sizeof(cf->lua.lua_funcs[i])) == NULL)
        {
            BAD("lua_funcs name is not terminated", i, 0, "lua_funcs[%lld] has no end", (long long)i);
            if (c->repair)
                cf->lua.lua_funcs[i][sizeof(cf->lua.lua_funcs[i]) - 1] = '\0';
        }
    }
    return true;
}

/** Slabs, cubes, columns and textures: what the map is drawn and walked from. */
static TbBool validate_map_tables(const struct Ctx *c)
{
    struct KfxConfigState *s = c->s;
    struct Configs *cf = &s->conf;
    for (int64_t i = 0; i < LEN(cf->slab_conf.slab_cfgstats); i++)
        // block_health[] of the simulation state has this many entries
        RANGE(cf->slab_conf.slab_cfgstats[i].block_health_index, 0, 9, "slab_cfgstats.block_health_index", i);
    for (int64_t i = 0; i < LEN(cf->cube_conf.cube_cfgstats); i++)
    {
        struct CubeConfigStats *cu = &cf->cube_conf.cube_cfgstats[i];
        for (int k = 0; k < 6; k++)
            RANGE(cu->texture_id[k], 0, TEXTURE_BLOCKS_COUNT - 1, "cube_cfgstats.texture_id", i);
        RANGE(cu->ownershipGroup, 0, LEN(cf->cube_conf.cube_bits) - 1, "cube_cfgstats.ownershipGroup", i);
    }
    for (int64_t g = 0; g < LEN(cf->cube_conf.cube_bits); g++)
        for (int64_t k = 0; k < LEN(cf->cube_conf.cube_bits[0]); k++)
            RANGE(cf->cube_conf.cube_bits[g][k], 0, CUBE_ITEMS_MAX - 1, "cube_bits", g * 16 + k);
    for (int64_t i = 0; i < LEN(cf->column_conf.cols); i++)
    {
        struct Column *col = &cf->column_conf.cols[i];
        for (int k = 0; k < COLUMN_STACK_HEIGHT; k++)
            RANGE(col->cubes[k], 0, CUBE_ITEMS_MAX - 1, "column_conf.cubes", i);
        RANGE(col->floor_texture, 0, COLUMN_FLOOR_TEXTURES_COUNT - 1, "column_conf.floor_texture", i);
    }
    for (int64_t i = 0; i < LEN(s->col_static_entries); i++)
        // (the column number, negated: columns_add_static_entries())
        RANGE(s->col_static_entries[i], -(COLUMNS_COUNT - 1), 0, "col_static_entries", i);
    for (int64_t i = 0; i < LEN(s->texture_animation); i++)
        RANGE(s->texture_animation[i], 0, TEXTURE_BLOCKS_COUNT - 1, "texture_animation", i);
    RANGE(s->neutral_player_num, -1, PLAYERS_COUNT - 1, "neutral_player_num", 0);
    return true;
}

/** The sound tables: how many samples a creature's sound has is a loop bound, and a random pick among them. */
static TbBool validate_sounds(const struct Ctx *c)
{
    struct Configs *cf = &c->s->conf;
    for (int64_t i = 0; i < LEN(cf->crtr_conf.creature_sounds); i++)
    {
        struct CreatureSounds *cs = &cf->crtr_conf.creature_sounds[i];
        struct CreatureSound *all[] = { &cs->hit, &cs->happy, &cs->sad, &cs->hang, &cs->drop, &cs->torture, &cs->slap,
            &cs->die, &cs->foot, &cs->fight, &cs->piss };
        for (size_t k = 0; k < sizeof(all) / sizeof(all[0]); k++)
            RANGE(all[k]->count, 0, CREATURE_SOUND_COUNT_MAX, "creature_sounds.count", i);
    }
    return true;
}

static TbBool run_checks(const struct Ctx *c)
{
    // the counts first: the loops after them are bounded by the arrays, not by these, but a count outside its array
    // says the state is not one this build wrote
    return validate_counts(c) && validate_functions(c) && validate_map_tables(c) && validate_sounds(c);
}

TbBool config_state_validate(const struct KfxConfigState *state, char *why, size_t why_size)
{
    // nothing is written unless `repair` is set, so the constness only has to be dropped for the one type
    struct Ctx ctx = { (struct KfxConfigState *)(uintptr_t)state, false, { why, why_size, NULL, 0 } };
    if ((why != NULL) && (why_size > 0))
        why[0] = '\0';
    return run_checks(&ctx);
}

int64_t config_state_survey(const struct KfxConfigState *state, char *report, size_t report_size)
{
    struct StateSurveyEntry entries[STATE_SURVEY_MAX];
    memset(entries, 0, sizeof(entries));
    struct Ctx ctx = { (struct KfxConfigState *)(uintptr_t)state, false, { NULL, 0, entries, 0 } };
    run_checks(&ctx);
    return state_check_report(&ctx.chk, report, report_size);
}

int64_t config_state_repair(struct KfxConfigState *state)
{
    struct StateSurveyEntry entries[STATE_SURVEY_MAX];
    memset(entries, 0, sizeof(entries));
    struct Ctx ctx = { state, true, { NULL, 0, entries, 0 } };
    run_checks(&ctx);
    if (ctx.chk.survey_used > 0)
    {
        char report[4096];
        state_check_report(&ctx.chk, report, sizeof(report));
        ERRORLOG("The configuration has values outside the tables they index; they were reset (to none, or the nearest valid value):\n%s", report);
    }
    return ctx.chk.survey_used;
}
