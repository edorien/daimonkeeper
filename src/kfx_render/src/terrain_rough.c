/******************************************************************************/
// Free implementation of the Dungeon Keeper strategy game.
/******************************************************************************/
/** @file terrain_rough.c
 *     Rough-terrain displacement: the pure functions.
 * @par Purpose:
 *     See terrain_rough.h.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     04 Oct 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "terrain_rough.h"

#include "globals.h"
#include "bflib_basics.h"
#include "kfx_sim_state.h"
#include "map_data.h"
#include "map_columns.h"
#include "slab_data.h"
#include "player_data.h"
#include "config_settings.h"
#include "config_keeperfx.h"
#include "config_terrain.h"
#include "kfx_config_state.h"
#include "engine_lenses.h"
#include "creature_control.h"
#include "creature_states.h"
#include "list_walk.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "post_inc.h"

/******************************************************************************/
const struct TerrainRoughParams terrain_rough_default_params = {
    .amplitude = 96,
    .floor_amplitude = 24,
    .period = 6,
    .corner_radius = 320,
    .shapeable_percent = 30,
    .seed = 0x5eed1234u,
};

struct TerrainRoughSettings terrain_rough_settings = {
    .mode = TSM_Off,
    .params = {
        .amplitude = 96,
        .floor_amplitude = 24,
        .period = 6,
        .corner_radius = 320,
        .shapeable_percent = 30,
        .seed = 0x5eed1234u,
    },
    .walls = 1,
    .floors = 0,
    .ao_percent = 35,
    .normal_percent = 100,
    .from_config = 1,
};

/* Cells already classified this frame: a direct-mapped cache, keyed by position, valid while its
   generation is the frame's. */
#define CELL_CACHE_BITS 15
#define CELL_CACHE_SLOTS (1 << CELL_CACHE_BITS)
struct CellCacheSlot {
    uint64_t key;
    uint32_t gen;
    unsigned char cls;
};
static struct CellCacheSlot cell_cache[CELL_CACHE_SLOTS];
static uint32_t cell_cache_gen = 1;

/** Temporary switches until the options screen has them (terrain-v2 phase 6): KFX_ROUGH_TERRAIN
 *  (off, gated, all), KFX_ROUGH_AMPLITUDE, KFX_ROUGH_FLOORS (0/1), KFX_ROUGH_AO and KFX_ROUGH_NORMAL_SHADE
 *  (percent). */
static void apply_environment(void)
{
    const char *v = getenv("KFX_ROUGH_TERRAIN");
    if (v == NULL)
        return;
    terrain_rough_settings.from_config = 0;
    if (strcmp(v, "gated") == 0)
        terrain_rough_settings.mode = TSM_Gated;
    else if (strcmp(v, "all") == 0)
        terrain_rough_settings.mode = TSM_All;
    else
        terrain_rough_settings.mode = TSM_Off;
    if ((v = getenv("KFX_ROUGH_AMPLITUDE")) != NULL)
        terrain_rough_settings.params.amplitude = atoll(v);
    if ((v = getenv("KFX_ROUGH_FLOORS")) != NULL)
        terrain_rough_settings.floors = (atoi(v) != 0);
    if ((v = getenv("KFX_ROUGH_CORNERS")) != NULL)
        terrain_rough_settings.params.corner_radius = atoll(v);
    if ((v = getenv("KFX_ROUGH_FLOOR_AMPLITUDE")) != NULL)
        terrain_rough_settings.params.floor_amplitude = atoll(v);
    if ((v = getenv("KFX_ROUGH_AO")) != NULL)
        terrain_rough_settings.ao_percent = atoi(v);
    if ((v = getenv("KFX_ROUGH_NORMAL_SHADE")) != NULL)
        terrain_rough_settings.normal_percent = atoi(v);
    JUSTLOG("Rough terrain from the environment: mode %d, amplitude %" PRId64 ", floors %d, ao %d%%",
        (int)terrain_rough_settings.mode, terrain_rough_settings.params.amplitude,
        terrain_rough_settings.floors, terrain_rough_settings.ao_percent);
}

static void reinforcement_update(void);
static enum SlabRoughness slab_roughness(SlabKind kind);

void terrain_rough_frame_begin(void)
{
    static TbBool environment_read = false;
    if (!environment_read)
    {
        environment_read = true;
        apply_environment();
    }
    if (terrain_rough_settings.from_config)
    {
        // The options screen's ROUGH_TERRAIN (1 off, 2 walls, 3 walls and floors) and ROUGH_TERRAIN_DEPTH.
        const int64_t depth = keeperfx_ui_config.rough_terrain_depth;
        terrain_rough_settings.mode = (keeperfx_ui_config.rough_terrain > 1) ? TSM_Gated : TSM_Off;
        terrain_rough_settings.walls = 1;
        terrain_rough_settings.floors = (keeperfx_ui_config.rough_terrain == 3);
        terrain_rough_settings.params.amplitude = (depth < 0) ? 0 : depth;
        terrain_rough_settings.params.floor_amplitude = terrain_rough_settings.params.amplitude / 4;
        terrain_rough_settings.params.corner_radius = keeperfx_ui_config.rough_terrain_corners;
    }
    reinforcement_update();
    cell_cache_gen++;
    if (cell_cache_gen == 0)
    {
        memset(cell_cache, 0, sizeof(cell_cache));
        cell_cache_gen = 1;
    }
}

const struct TerrainCellSource terrain_rough_map_source = {
    .cls = terrain_rough_map_cell_class,
    .ctx = NULL,
    .weight = terrain_rough_map_cell_weight,
};

/******************************************************************************/
/* Reinforcement easing: the slabs imps are reinforcing, with how far along they look. */
#define REINFORCE_SLOTS 128
/** Hits an imp makes on a slab before it is fortified (instf_reinforce()). */
#define REINFORCE_HITS 26
/** Turns a wall left alone takes to relax from fully smooth back to fully rough. */
#define REINFORCE_RELAX_TURNS 40
/** Least climb per turn toward the imps' progress: one hit's worth over 8 turns, about as long as an
 *  imp takes between hits, so the wall smooths steadily rather than in steps. */
#define REINFORCE_CLIMB (TERRAIN_WEIGHT_ONE / (REINFORCE_HITS * 8))

struct ReinforceSlot {
    int64_t slb_x, slb_y;
    int64_t progress; ///< as drawn, 0..TERRAIN_WEIGHT_ONE
    int64_t target;   ///< the imps' progress this turn
    TbBool used;
    TbBool pinned;    ///< held by terrain_rough_reinforcement_pin()
};
static struct ReinforceSlot reinforce_slots[REINFORCE_SLOTS];
static int reinforce_used;
/** The slots' progress by slab, for the many lookups a frame makes. */
static uint16_t reinforce_map[MAX_TILES_Y][MAX_TILES_X];

static TbBool reinforce_on_map(int64_t slb_x, int64_t slb_y)
{
    return (slb_x >= 0) && (slb_y >= 0) && (slb_x < MAX_TILES_X) && (slb_y < MAX_TILES_Y);
}

static struct ReinforceSlot *reinforce_find(int64_t slb_x, int64_t slb_y, TbBool add)
{
    struct ReinforceSlot *free_slot = NULL;
    for (int i = 0; i < REINFORCE_SLOTS; i++)
    {
        struct ReinforceSlot *rs = &reinforce_slots[i];
        if (!rs->used)
        {
            if (free_slot == NULL)
                free_slot = rs;
            continue;
        }
        if ((rs->slb_x == slb_x) && (rs->slb_y == slb_y))
            return rs;
    }
    if (!add || (free_slot == NULL) || !reinforce_on_map(slb_x, slb_y))
        return NULL;
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->used = true;
    free_slot->slb_x = slb_x;
    free_slot->slb_y = slb_y;
    reinforce_used++;
    return free_slot;
}

static void reinforce_drop(struct ReinforceSlot *rs)
{
    rs->used = false;
    reinforce_used--;
    reinforce_map[rs->slb_y][rs->slb_x] = 0;
}

int64_t terrain_rough_reinforcement_progress(int64_t slb_x, int64_t slb_y)
{
    if ((reinforce_used == 0) || !reinforce_on_map(slb_x, slb_y))
        return 0;
    return reinforce_map[slb_y][slb_x];
}

void terrain_rough_reinforcement_pin(int64_t slb_x, int64_t slb_y, int64_t progress)
{
    struct ReinforceSlot *rs = reinforce_find(slb_x, slb_y, progress >= 0);
    if (rs == NULL)
        return;
    if (progress < 0)
    {
        rs->pinned = false;
        return;
    }
    rs->pinned = true;
    rs->progress = (progress > TERRAIN_WEIGHT_ONE) ? TERRAIN_WEIGHT_ONE : progress;
    rs->target = rs->progress;
    reinforce_map[slb_y][slb_x] = (uint16_t)rs->progress;
}

void terrain_rough_reinforcement_clear(void)
{
    memset(reinforce_slots, 0, sizeof(reinforce_slots));
    memset(reinforce_map, 0, sizeof(reinforce_map));
    reinforce_used = 0;
}

/** Once per game turn: each slab eases toward the furthest progress of the imps reinforcing it, or
 *  relaxes back when none is. */
static void reinforcement_update(void)
{
    static GameTurn last_turn = 0;
    const GameTurn turn = kfx_sim_state.play_gameturn;
    int64_t steps;
    if (turn == last_turn)
        return;
    if ((turn < last_turn) || (turn - last_turn > 2 * REINFORCE_RELAX_TURNS))
    {
        // A loaded game or a new level: start over (keeping a test's pinned slabs).
        for (int i = 0; i < REINFORCE_SLOTS; i++)
            if (reinforce_slots[i].used && !reinforce_slots[i].pinned)
                reinforce_drop(&reinforce_slots[i]);
        steps = 1;
    }
    else
    {
        steps = (int64_t)(turn - last_turn);
    }
    last_turn = turn;
    for (int i = 0; i < REINFORCE_SLOTS; i++)
        reinforce_slots[i].target = 0;
    FOR_EACH_THING(thing, thing_walk_list(kfx_sim_state.thing_lists[TngList_Creatures].index, THINGS_COUNT))
    {
        struct CreatureControl *cctrl;
        struct ReinforceSlot *rs;
        int64_t hits;
        if (get_creature_state_besides_interruptions(thing) != CrSt_ImpReinforces)
            continue;
        cctrl = creature_control_get_from_thing(thing);
        if (creature_control_invalid(cctrl) || (cctrl->digger.consecutive_reinforcements == 0))
            continue;
        hits = cctrl->digger.consecutive_reinforcements;
        rs = reinforce_find(subtile_slab(stl_num_decode_x(cctrl->digger.working_stl)),
            subtile_slab(stl_num_decode_y(cctrl->digger.working_stl)), true);
        if (rs == NULL)
            continue;
        hits = hits * TERRAIN_WEIGHT_ONE / REINFORCE_HITS;
        if (hits > rs->target)
            rs->target = (hits > TERRAIN_WEIGHT_ONE) ? TERRAIN_WEIGHT_ONE : hits;
    }
    for (int i = 0; i < REINFORCE_SLOTS; i++)
    {
        struct ReinforceSlot *rs = &reinforce_slots[i];
        struct SlabMap *slb;
        if (!rs->used || rs->pinned)
            continue;
        for (int64_t k = 0; k < steps; k++)
        {
            if (rs->progress < rs->target)
            {
                // Steadily, catching up faster when several imps hit it at once.
                const int64_t gap = rs->target - rs->progress;
                const int64_t step = (gap / 8 > REINFORCE_CLIMB) ? gap / 8 : REINFORCE_CLIMB;
                rs->progress += (step < gap) ? step : gap;
            }
            else if (rs->progress > rs->target)
            {
                rs->progress -= TERRAIN_WEIGHT_ONE / REINFORCE_RELAX_TURNS;
                if (rs->progress < rs->target)
                    rs->progress = rs->target;
            }
        }
        // Done with: relaxed, or fortified (no longer a rough wall, so nothing of it moves anyway).
        slb = get_slabmap_block(rs->slb_x, rs->slb_y);
        if (((rs->progress == 0) && (rs->target == 0)) || slabmap_block_invalid(slb) || (slab_roughness(slb->kind) != SlbRgh_Rough))
            reinforce_drop(rs);
        else
            reinforce_map[rs->slb_y][rs->slb_x] = (uint16_t)rs->progress;
    }
}

int64_t terrain_rough_map_cell_weight(void *ctx, int64_t stl_x, int64_t stl_y)
{
    struct Map *mapblk;
    (void)ctx;
    if (reinforce_used == 0)
        return TERRAIN_WEIGHT_ONE;
    // Ground the player can't see keeps its roughness: easing it would show an enemy's imps at work.
    mapblk = get_map_block_at(stl_x, stl_y);
    if (map_block_invalid(mapblk) || !map_block_revealed(mapblk, my_player_number))
        return TERRAIN_WEIGHT_ONE;
    return TERRAIN_WEIGHT_ONE - terrain_rough_reinforcement_progress(stl_x / STL_PER_SLB, stl_y / STL_PER_SLB);
}

/******************************************************************************/
static int64_t floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        q--;
    return q;
}

static int64_t isqrt64(int64_t v)
{
    int64_t r;
    if (v <= 0)
        return 0;
    r = (int64_t)sqrt((double)v);
    // Settle rounding of the double estimate so the result is exact on every compiler.
    while (r * r > v)
        r--;
    while ((r + 1) * (r + 1) <= v)
        r++;
    return r;
}

static int64_t cell_weight(const struct TerrainCellSource *src, int64_t stl_x, int64_t stl_y)
{
    int64_t w;
    if (src->weight == NULL)
        return TERRAIN_WEIGHT_ONE;
    w = src->weight(src->ctx, stl_x, stl_y);
    if (w < 0)
        return 0;
    return (w > TERRAIN_WEIGHT_ONE) ? TERRAIN_WEIGHT_ONE : w;
}

/******************************************************************************/
/** terrain.cfg's Roughness for a slab kind. */
static enum SlabRoughness slab_roughness(SlabKind kind)
{
    const struct SlabConfigStats *slabst;
    if (kind >= kfx_config_state.conf.slab_conf.slab_types_count)
        return SlbRgh_None;
    slabst = get_slab_kind_stats(kind);
    if (slabst->roughness > SlbRgh_Subtle)
        return SlbRgh_None;
    return (enum SlabRoughness)slabst->roughness;
}

static enum TerrainCellClass map_cell_class(int64_t stl_x, int64_t stl_y, int64_t level);

/** The map's border ring of slabs: it keeps its straight faces. */
static TbBool on_map_border(int64_t stl_x, int64_t stl_y)
{
    const int64_t slb_x = stl_x / STL_PER_SLB, slb_y = stl_y / STL_PER_SLB;
    return (slb_x <= 0) || (slb_y <= 0) || (slb_x >= kfx_sim_state.map_tiles_x - 1) || (slb_y >= kfx_sim_state.map_tiles_y - 1);
}

enum TerrainCellClass terrain_rough_map_cell_class(void *ctx, int64_t stl_x, int64_t stl_y, int64_t level)
{
    const uint64_t key = ((uint64_t)(uint16_t)stl_x << 32) | ((uint64_t)(uint16_t)stl_y << 16) | (uint64_t)(uint16_t)(int16_t)level;
    struct CellCacheSlot *slot = &cell_cache[(key * 0x9E3779B97F4A7C15ull) >> (64 - CELL_CACHE_BITS)];
    (void)ctx;
    if ((slot->gen != cell_cache_gen) || (slot->key != key))
    {
        slot->key = key;
        slot->gen = cell_cache_gen;
        slot->cls = (unsigned char)map_cell_class(stl_x, stl_y, level);
    }
    return (enum TerrainCellClass)slot->cls;
}

static enum TerrainCellClass map_cell_class(int64_t stl_x, int64_t stl_y, int64_t level)
{
    const TbBool cluedo = (lens_mode == 0) && settings.video_cluedo_mode;
    uint64_t solidmask;
    struct Map *mapblk;
    const struct Column *col;
    struct SlabMap *slb;
    if ((stl_x < 0) || (stl_y < 0) || (stl_x >= kfx_sim_state.map_subtiles_x) || (stl_y >= kfx_sim_state.map_subtiles_y))
        return TCC_HardPinned;
    mapblk = get_map_block_at(stl_x, stl_y);
    if (map_block_invalid(mapblk))
        return TCC_HardPinned;
    if (!map_block_revealed(mapblk, my_player_number))
    {
        // The mesher draws unrevealed ground as solid rock; so does this.
        if ((level < 0) || (level >= (cluedo ? 2 : COLUMN_STACK_HEIGHT)))
            return (level < 0) ? TCC_HardPinned : TCC_Open;
        if (on_map_border(stl_x, stl_y))
            return TCC_HardPinned;
        return terrain_rough_settings.walls ? TCC_Rough : TCC_HardPinned;
    }
    slb = get_slabmap_for_subtile(stl_x, stl_y);
    col = get_map_column(mapblk);
    solidmask = col->solidmask;
    // Cluedo mode draws ordinary walls two cubes high (fill_in_points_cluedo() and
    // do_a_plane_of_engine_columns_cluedo() cut the mask the same way).
    if (cluedo && (solidmask >= (1 << 3)) && ((mapblk->flags & (SlbAtFlg_IsDoor|SlbAtFlg_IsRoom)) == 0) && ((col->bitfields & 0xE) == 0))
        solidmask &= 3;
    if (level == -1)
    {
        // Just below the lowest cube: the surface under water or lava; under a solid column, the same
        // as its bottom cube (so a wall standing in water can recess down to the waterline).
        if (!slabmap_block_invalid(slb) && ((slb->kind == SlbT_WATER) || (slb->kind == SlbT_LAVA)))
            return TCC_PlanePinned;
        if ((solidmask & 1) == 0)
            return TCC_HardPinned;
        level = 0;
    }
    if (level < 0)
        return TCC_HardPinned;
    if (level >= COLUMN_STACK_HEIGHT)
        return TCC_Open;
    if ((solidmask & (1 << level)) == 0)
        return TCC_Open;
    if (on_map_border(stl_x, stl_y))
        return TCC_HardPinned;
    if (slabmap_block_invalid(slb))
        return TCC_HardPinned;
    switch (slab_roughness(slb->kind))
    {
    case SlbRgh_Rough:
        // A floor-only column (a path) dips; anything taller is a wall and recesses.
        if ((level == 0) && ((solidmask & 2) == 0))
            return terrain_rough_settings.floors ? TCC_RoughFloor : TCC_PlanePinned;
        return terrain_rough_settings.walls ? TCC_Rough : TCC_HardPinned;
    case SlbRgh_Subtle:
        return (terrain_rough_settings.walls && (terrain_rough_settings.params.corner_radius > 0)) ? TCC_Shapeable : TCC_HardPinned;
    default:
        return TCC_HardPinned;
    }
}

/******************************************************************************/
/* The cells touching a lattice vertex: along each axis, one cell if the vertex is inside it, two if
   it's on the boundary between them (side -1 and +1). */
struct TouchAxis {
    int count;
    int64_t cell[2];
    int side[2];
};

static void touch_axis(int64_t f, struct TouchAxis *ta)
{
    if (f % TERRAIN_SUBDIV == 0)
    {
        ta->count = 2;
        ta->cell[0] = f / TERRAIN_SUBDIV - 1;
        ta->side[0] = -1;
        ta->cell[1] = f / TERRAIN_SUBDIV;
        ta->side[1] = 1;
    }
    else
    {
        ta->count = 1;
        ta->cell[0] = floor_div(f, TERRAIN_SUBDIV);
        ta->side[0] = 0;
    }
}

/** True if no hard-pinned cell touches the vertex: the corner rounding may slide it sideways, even on
 *  open floor next to the rock. */
static TbBool shape_may_move(const struct TerrainCellSource *src, int64_t fx, int64_t fy, int64_t fz)
{
    struct TouchAxis ax, ay, az;
    touch_axis(fx, &ax);
    touch_axis(fy, &ay);
    touch_axis(fz, &az);
    for (int i = 0; i < ax.count; i++)
        for (int j = 0; j < ay.count; j++)
            for (int k = 0; k < az.count; k++)
                if (src->cls(src->ctx, ax.cell[i], ay.cell[j], az.cell[k]) == TCC_HardPinned)
                    return false;
    return true;
}

enum TerrainMoveMask terrain_rough_move_mask(const struct TerrainCellSource *src, int64_t fx, int64_t fy, int64_t fz)
{
    struct TouchAxis ax, ay, az;
    TbBool any_rough = false;
    TbBool any_plane = false;
    touch_axis(fx, &ax);
    touch_axis(fy, &ay);
    touch_axis(fz, &az);
    for (int i = 0; i < ax.count; i++)
        for (int j = 0; j < ay.count; j++)
            for (int k = 0; k < az.count; k++)
            {
                switch (src->cls(src->ctx, ax.cell[i], ay.cell[j], az.cell[k]))
                {
                case TCC_HardPinned:
                    return TMM_None;
                case TCC_Rough:
                case TCC_RoughFloor:
                case TCC_Shapeable:
                    any_rough = true;
                    break;
                case TCC_PlanePinned:
                    any_plane = true;
                    break;
                default:
                    break;
                }
            }
    if (!any_rough)
        return TMM_None;
    return any_plane ? TMM_Horizontal : TMM_All;
}

/** The pull before the rules about who may pull which way (raw: every solid cell, every axis), and
 *  after them (eff: only floor cubes pull down, then restricted to mask). weight (if not NULL): the
 *  least weight of the rough and shapeable cells touching the vertex. */
static void pull_sums(const struct TerrainCellSource *src, int64_t fx, int64_t fy, int64_t fz,
    enum TerrainMoveMask mask, int64_t raw[3], int64_t eff[3], int *noisy, int64_t *weight)
{
    struct TouchAxis ax, ay, az;
    *noisy = 0;
    if (weight != NULL)
        *weight = TERRAIN_WEIGHT_ONE;
    raw[0] = raw[1] = raw[2] = 0;
    eff[0] = eff[1] = eff[2] = 0;
    touch_axis(fx, &ax);
    touch_axis(fy, &ay);
    touch_axis(fz, &az);
    for (int i = 0; i < ax.count; i++)
        for (int j = 0; j < ay.count; j++)
            for (int k = 0; k < az.count; k++)
            {
                const enum TerrainCellClass cls = src->cls(src->ctx, ax.cell[i], ay.cell[j], az.cell[k]);
                if (cls == TCC_Open)
                    continue;
                if ((cls == TCC_Rough) || (cls == TCC_RoughFloor))
                    *noisy = 2;
                else if ((cls == TCC_Shapeable) && (*noisy == 0))
                    *noisy = 1;
                if ((weight != NULL) && (src->weight != NULL) && ((cls == TCC_Rough) || (cls == TCC_RoughFloor) || (cls == TCC_Shapeable)))
                {
                    const int64_t w = cell_weight(src, ax.cell[i], ay.cell[j]);
                    if (w < *weight)
                        *weight = w;
                }
                raw[0] += ax.side[i];
                raw[1] += ay.side[j];
                raw[2] += az.side[k];
                eff[0] += ax.side[i];
                eff[1] += ay.side[j];
                if (cls != TCC_Rough)
                    eff[2] += az.side[k];
            }
    if ((mask & TMM_Horizontal) == 0)
        eff[0] = eff[1] = 0;
    if ((mask & TMM_Vertical) == 0)
        eff[2] = 0;
}

static void normalise_dir(const int64_t sum[3], int64_t dir[3])
{
    const int64_t len = isqrt64((sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]) * TERRAIN_DIR_ONE * TERRAIN_DIR_ONE);
    dir[0] = dir[1] = dir[2] = 0;
    if (len == 0)
        return;
    for (int i = 0; i < 3; i++)
        dir[i] = sum[i] * TERRAIN_DIR_ONE * TERRAIN_DIR_ONE / len;
}

void terrain_rough_pull_dir(const struct TerrainCellSource *src, int64_t fx, int64_t fy, int64_t fz,
    enum TerrainMoveMask mask, int64_t dir[3])
{
    int64_t raw[3], eff[3];
    int noisy;
    dir[0] = dir[1] = dir[2] = 0;
    if (mask == TMM_None)
        return;
    pull_sums(src, fx, fy, fz, mask, raw, eff, &noisy, NULL);
    if (noisy)
        normalise_dir(eff, dir);
}

/******************************************************************************/
static int64_t noise_hash(const struct TerrainRoughParams *prm, int64_t x, int64_t y, int64_t z)
{
    uint64_t h = (uint64_t)x * 374761393ull + (uint64_t)y * 668265263ull + (uint64_t)z * 2246822519ull + prm->seed;
    h = (h ^ (h >> 13)) * 1274126177ull;
    h ^= h >> 16;
    return (int64_t)(h & 0xFFFFu);
}

/** 3t^2 - 2t^3 for t = f / period, in 1/256ths. */
static int64_t smooth_frac(int64_t f, int64_t period)
{
    int64_t t = f * 256 / period;
    return t * t * (3 * 256 - 2 * t) / (256 * 256);
}

static int64_t lerp256(int64_t a, int64_t b, int64_t t)
{
    return a + (b - a) * t / 256;
}

int64_t terrain_rough_noise(const struct TerrainRoughParams *prm, int64_t fx, int64_t fy, int64_t fz)
{
    const int64_t p = (prm->period > 0) ? prm->period : 1;
    const int64_t ix = floor_div(fx, p), iy = floor_div(fy, p), iz = floor_div(fz, p);
    const int64_t tx = smooth_frac(fx - ix * p, p), ty = smooth_frac(fy - iy * p, p), tz = smooth_frac(fz - iz * p, p);
    int64_t c[2][2][2];
    for (int dx = 0; dx < 2; dx++)
        for (int dy = 0; dy < 2; dy++)
            for (int dz = 0; dz < 2; dz++)
                c[dx][dy][dz] = noise_hash(prm, ix + dx, iy + dy, iz + dz);
    {
        const int64_t x00 = lerp256(c[0][0][0], c[1][0][0], tx);
        const int64_t x10 = lerp256(c[0][1][0], c[1][1][0], tx);
        const int64_t x01 = lerp256(c[0][0][1], c[1][0][1], tx);
        const int64_t x11 = lerp256(c[0][1][1], c[1][1][1], tx);
        const int64_t y0 = lerp256(x00, x10, ty);
        const int64_t y1 = lerp256(x01, x11, ty);
        return lerp256(y0, y1, tz);
    }
}

int64_t terrain_rough_depth(const struct TerrainRoughParams *prm, int64_t fx, int64_t fy, int64_t fz)
{
    if (prm->amplitude <= 0)
        return 0;
    return prm->amplitude * terrain_rough_noise(prm, fx, fy, fz) / 65535;
}

static TbBool cell_is_shapeable(enum TerrainCellClass cls)
{
    return (cls == TCC_Rough) || (cls == TCC_Shapeable);
}

/** How many subtiles a convex corner's rock face runs straight from the corner, up to limit: rock
 *  (sx + k*dx, sy + k*dy) with open ground beside it at (+ox, +oy), whose floor isn't claimed. */
static int64_t straight_run(const struct TerrainCellSource *src, int64_t sx, int64_t sy, int64_t dx, int64_t dy,
    int64_t ox, int64_t oy, int64_t level, int64_t limit)
{
    int64_t k;
    for (k = 0; k < limit; k++)
    {
        if (!cell_is_shapeable(src->cls(src->ctx, sx + k * dx, sy + k * dy, level)))
            break;
        if (src->cls(src->ctx, sx + k * dx + ox, sy + k * dy + oy, level) != TCC_Open)
            break;
        if (src->cls(src->ctx, sx + k * dx + ox, sy + k * dy + oy, 0) == TCC_HardPinned)
            break;
    }
    return k;
}

/** World cell of local cell (i, j) of a corner (cx, cy) whose local axes run along ex, ey. */
static void bay_cell(int64_t cx, int64_t cy, int64_t ex, int64_t ey, int64_t i, int64_t j, int64_t *wx, int64_t *wy)
{
    *wx = (ex > 0) ? cx + i : cx - 1 - i;
    *wy = (ey > 0) ? cy + j : cy - 1 - j;
}

/** What a corner (cx, cy) does at a level: nothing, rounds (Case A: one column of rock, three open),
 *  is carved into a bay (Case B: three of rock round one open floor, with clean surroundings), or
 *  carries the arch over a dead end (Case C: two bays a TERRAIN_BAY_BLOCK apart along their shared
 *  face, and no other bay along either one's side wall; one of the two carries it, the other nothing). */
enum CornerKind {
    CK_None = 0,
    CK_Convex,
    CK_Bay,
    CK_Cap,
};

struct CornerInfo {
    unsigned char kind;
    unsigned char axis;   ///< Cap: 0 if its face runs along local x (the rock in +y), 1 along local y
    signed char qx, qy;   ///< Convex: which way the rock lies. Bay, Cap: local axes (the floor in -x -y).
    int64_t r;            ///< the radius it is rounded or carved to (Cap: how deep the arch is), map units
    int64_t w;            ///< how much of the shape is applied: the least weight of its rock columns
};

/** Case B's conditions (02-design.md section 4.5): the floor in the TERRAIN_BAY_BLOCK square off the
 *  corner open and unclaimed, the rock in the other three solid (so the faces run straight and the
 *  squeezed rock fits). */
static TbBool bay_is_clean(const struct TerrainCellSource *src, int64_t cx, int64_t cy, int64_t ex, int64_t ey, int64_t level)
{
    const int64_t kb = TERRAIN_BAY_BLOCK;
    int64_t wx, wy;
    for (int64_t j = -kb; j < kb; j++)
        for (int64_t i = -kb; i < kb; i++)
        {
            enum TerrainCellClass c;
            bay_cell(cx, cy, ex, ey, i, j, &wx, &wy);
            c = src->cls(src->ctx, wx, wy, level);
            if ((i < 0) && (j < 0))
            {
                if ((c != TCC_Open) || (src->cls(src->ctx, wx, wy, 0) == TCC_HardPinned))
                    return false;
            }
            else if (!cell_is_shapeable(c))
            {
                return false;
            }
        }
    return true;
}

/** Cases A and B at one corner, on its own. */
static void analyse_corner_alone(const struct TerrainCellSource *src, int64_t r_max, int64_t cx, int64_t cy, int64_t level, struct CornerInfo *ci)
{
    int64_t sx = 0, sy = 0, ox = 0, oy = 0;
    int solid = 0, open = 0, pinned_floor = 0;
    int64_t w = TERRAIN_WEIGHT_ONE;
    ci->kind = CK_None;
    ci->axis = 0;
    ci->qx = ci->qy = 1;
    ci->r = 0;
    ci->w = TERRAIN_WEIGHT_ONE;
    for (int64_t j = 0; j < 2; j++)
    {
        for (int64_t i = 0; i < 2; i++)
        {
            const enum TerrainCellClass c = src->cls(src->ctx, cx - 1 + i, cy - 1 + j, level);
            if (cell_is_shapeable(c))
            {
                const int64_t cw = cell_weight(src, cx - 1 + i, cy - 1 + j);
                solid++;
                sx = cx - 1 + i;
                sy = cy - 1 + j;
                if (cw < w)
                    w = cw;
            }
            else if (c == TCC_Open)
            {
                open++;
                ox = cx - 1 + i;
                oy = cy - 1 + j;
            }
            if (src->cls(src->ctx, cx - 1 + i, cy - 1 + j, 0) == TCC_HardPinned)
                pinned_floor++;
        }
    }
    if ((pinned_floor != 0) || (w <= 0))
        return;
    ci->w = w;
    if ((solid == 1) && (open == 3))
    {
        // qx, qy: which way the rock lies from the corner; R at most half of each straight face.
        const int64_t qx = (sx == cx) ? 1 : -1;
        const int64_t qy = (sy == cy) ? 1 : -1;
        const int64_t limit = r_max / 128 + 2;
        const int64_t run_x = straight_run(src, sx, sy, qx, 0, 0, -qy, level, limit);
        const int64_t run_y = straight_run(src, sx, sy, 0, qy, -qx, 0, level, limit);
        int64_t r = r_max;
        if (r > run_x * 128)
            r = run_x * 128;
        if (r > run_y * 128)
            r = run_y * 128;
        if (r <= 0)
            return;
        ci->kind = CK_Convex;
        ci->qx = (signed char)qx;
        ci->qy = (signed char)qy;
        ci->r = r;
    }
    else if ((solid == 3) && (open == 1))
    {
        // Local axes with the open floor in -x -y.
        const int64_t ex = (ox == cx - 1) ? 1 : -1;
        const int64_t ey = (oy == cy - 1) ? 1 : -1;
        // The bay and the rock squeezed behind it have to fit in the clean block: deeper than R = 5/12 of
        // it (320 for 3 subtiles), the squeeze turns lattice triangles over (the unit tests check this).
        int64_t r = r_max;
        if (r > TERRAIN_BAY_BLOCK * 256 * 5 / 12)
            r = TERRAIN_BAY_BLOCK * 256 * 5 / 12;
        if (!bay_is_clean(src, cx, cy, ex, ey, level))
            return;
        ci->kind = CK_Bay;
        ci->qx = (signed char)ex;
        ci->qy = (signed char)ey;
        ci->r = r;
    }
}

/** Whether the bay at (cx, cy), local axes (ex, ey), has a partner bay along its face on `axis` (0: the
 *  face along local x, 1: along local y): a TERRAIN_BAY_BLOCK along it, mirrored, the same floor
 *  between them. Gives the partner's weight. */
static TbBool bay_partner(const struct TerrainCellSource *src, int64_t r_max, int64_t cx, int64_t cy, int64_t level,
    int64_t ex, int64_t ey, int axis, int64_t *w)
{
    struct CornerInfo pi;
    const int64_t kb = TERRAIN_BAY_BLOCK;
    if (axis == 0)
        analyse_corner_alone(src, r_max, cx - ex * kb, cy, level, &pi);
    else
        analyse_corner_alone(src, r_max, cx, cy - ey * kb, level, &pi);
    if (pi.kind != CK_Bay)
        return false;
    if ((axis == 0) ? ((pi.qx != -ex) || (pi.qy != ey)) : ((pi.qx != ex) || (pi.qy != -ey)))
        return false;
    *w = pi.w;
    return true;
}

static void analyse_corner(const struct TerrainCellSource *src, int64_t r_max, int64_t cx, int64_t cy, int64_t level, struct CornerInfo *ci)
{
    int64_t w0 = 0, w1 = 0, wo;
    TbBool on0, on1;
    analyse_corner_alone(src, r_max, cx, cy, level, ci);
    if (ci->kind != CK_Bay)
        return;
    // Case C: a dead end. The two bays of its end face would leave the rock between them standing
    // as a bump; one arch over the whole face instead. Not when the floor is closed on both axes (a
    // pocket dug on its own keeps its four bays), nor when the partner's side wall ends in a bay too.
    on0 = bay_partner(src, r_max, cx, cy, level, ci->qx, ci->qy, 0, &w0);
    on1 = bay_partner(src, r_max, cx, cy, level, ci->qx, ci->qy, 1, &w1);
    if (on0 == on1)
        return;
    {
        const int axis = on0 ? 0 : 1;
        const int64_t kb = TERRAIN_BAY_BLOCK;
        const int64_t pcx = (axis == 0) ? cx - ci->qx * kb : cx;
        const int64_t pcy = (axis == 0) ? cy : cy - ci->qy * kb;
        const int64_t pex = (axis == 0) ? -ci->qx : ci->qx;
        const int64_t pey = (axis == 0) ? ci->qy : -ci->qy;
        if (bay_partner(src, r_max, pcx, pcy, level, pex, pey, 1 - axis, &wo))
            return;
        // The corner whose face runs toward -x (or -y) in the world carries the arch, the other nothing.
        if (((axis == 0) ? ci->qx : ci->qy) < 0)
        {
            ci->kind = CK_None;
            return;
        }
        ci->kind = CK_Cap;
        ci->axis = (unsigned char)axis;
        ci->r = ci->r * TERRAIN_CAP_DEPTH_PERCENT / 100;
        if (ci->r <= 0)
            ci->kind = CK_None;
        if ((on0 ? w0 : w1) < ci->w)
            ci->w = on0 ? w0 : w1;
    }
}

/* Corners already analysed this frame (the map source only): many vertices near a corner ask about it. */
#define CORNER_CACHE_BITS 14
#define CORNER_CACHE_SLOTS (1 << CORNER_CACHE_BITS)
struct CornerCacheSlot {
    uint64_t key;
    uint32_t gen;
    struct CornerInfo ci;
};
static struct CornerCacheSlot corner_cache[CORNER_CACHE_SLOTS];

static void corner_info(const struct TerrainCellSource *src, int64_t r_max, int64_t cx, int64_t cy, int64_t level, struct CornerInfo *ci)
{
    uint64_t key;
    struct CornerCacheSlot *slot;
    if (src->cls != terrain_rough_map_cell_class)
    {
        analyse_corner(src, r_max, cx, cy, level, ci);
        return;
    }
    key = ((uint64_t)(uint16_t)cx << 48) | ((uint64_t)(uint16_t)cy << 32) | ((uint64_t)(uint16_t)(int16_t)level << 16)
        | (uint64_t)(uint16_t)r_max;
    slot = &corner_cache[(key * 0x9E3779B97F4A7C15ull) >> (64 - CORNER_CACHE_BITS)];
    if ((slot->gen != cell_cache_gen) || (slot->key != key))
    {
        slot->key = key;
        slot->gen = cell_cache_gen;
        analyse_corner(src, r_max, cx, cy, level, &slot->ci);
    }
    *ci = slot->ci;
}

/** Keeps no more noise than 1 - moved / full, full = full_num / full_den (in 1/1024ths). */
static void keep_less_noise(int64_t moved, int64_t full_num, int64_t full_den, int64_t *noise_weight)
{
    const int64_t w = (full_num > 0) ? 1024 - moved * 1024 * full_den / full_num : 1024;
    const int64_t wc = (w < 0) ? 0 : w;
    if (wc < *noise_weight)
        *noise_weight = wc;
}

/** Case A at one corner: see terrain_rough_shape(). */
static void apply_convex(const struct CornerInfo *ci, int64_t cx, int64_t cy, int64_t fx, int64_t fy, int64_t out[2], int64_t *noise_weight)
{
    // Local coordinates, the rock in +x +y, times TERRAIN_SUBDIV to stay whole.
    // - Inside the rock square: each point moves along its ray from C, the arc's centre at (r3, r3), to
    //   distance m = max(|ux|, |uy|) from it (u = p - C). That puts the rock's two faces on the arc,
    //   keeps the square's inner edges where they are, and can't fold.
    // - On the floor beyond a face, a band3 wide strip moves as the face point beside it does, fading
    //   out across the strip; the floor diagonal from the corner moves as the corner does, fading out
    //   both ways. So the floor stretches to meet the arc instead of letting the face slide past its
    //   nearest floor vertices.
    const int64_t r3 = ci->r * TERRAIN_SUBDIV;
    const int64_t band3 = TERRAIN_CORNER_BAND * TERRAIN_SUBDIV;
    const int64_t lx = (fx - cx * TERRAIN_SUBDIV) * ci->qx * 256;
    const int64_t ly = (fy - cy * TERRAIN_SUBDIV) * ci->qy * 256;
    int64_t px, py, fade_num = 1, fade_den = 1, ux, uy, d, m, mv[2];
    if ((lx > r3) || (ly > r3) || (lx < -band3) || (ly < -band3))
        return;
    // The point of the rock square whose move this one takes, and how much of it.
    px = (lx < 0) ? 0 : lx;
    py = (ly < 0) ? 0 : ly;
    if (lx < 0)
    {
        fade_num *= band3 + lx;
        fade_den *= band3;
    }
    if (ly < 0)
    {
        fade_num *= band3 + ly;
        fade_den *= band3;
    }
    ux = px - r3;
    uy = py - r3;
    d = isqrt64(ux * ux + uy * uy);
    m = (llabs(ux) > llabs(uy)) ? llabs(ux) : llabs(uy);
    if (d == 0)
        return;
    {
        // p' = C + m (cos, sin) of the angle from the nearer axis, psi (tan psi = minor / major), spread
        // to psi' = (pi / 4) tan psi: a face point s from where the arc leaves the face then lands s / R
        // of the way round the arc, so the face squeezes evenly by pi / 4 instead of from 1 at the
        // arc's end to 0.5 at the corner. psi' grows with psi, so the map still can't fold.
        const TbBool x_major = (llabs(ux) >= llabs(uy));
        const double major = (double)(x_major ? llabs(ux) : llabs(uy));
        const double minor = (double)(x_major ? llabs(uy) : llabs(ux));
        const double psi2 = 0.78539816339744831 * (minor / major);
        const int64_t new_major = (int64_t)llround((double)m * cos(psi2));
        const int64_t new_minor = (int64_t)llround((double)m * sin(psi2));
        const int64_t nx = (ux < 0) ? -(x_major ? new_major : new_minor) : (x_major ? new_major : new_minor);
        const int64_t ny = (uy < 0) ? -(x_major ? new_minor : new_major) : (x_major ? new_minor : new_major);
        mv[0] = (nx - ux) * fade_num / fade_den;
        mv[1] = (ny - uy) * fade_num / fade_den;
    }
    // A corner being reinforced: the same map, part of the way (still fold-free).
    out[0] += mv[0] * ci->w / TERRAIN_WEIGHT_ONE * ci->qx / TERRAIN_SUBDIV;
    out[1] += mv[1] * ci->w / TERRAIN_WEIGHT_ONE * ci->qy / TERRAIN_SUBDIV;
    // Noise kept: none at the corner itself (moved r3 * (sqrt(2) - 1) = r3 * 424 / 1024), all where the
    // rounding has faded out.
    keep_less_noise(isqrt64(mv[0] * mv[0] + mv[1] * mv[1]), r3 * 424, 1024, noise_weight);
}

/** Case B at one corner. In local coordinates the floor lies in -x -y and the rock elsewhere. The bay
 *  is the circle round Cb = (-a, -a), a = R/4, through the two lips R along the faces from the corner;
 *  it digs about 0.44 R past the corner. Points move only along their rays from C2 = (-b, -b), b = R
 *  (at most TERRAIN_BAY_RAY_MAX, so the four bays of one dug slab keep apart): on each ray that leaves
 *  the floor inside that circle, the floor up to the face stretches out to the circle, and a band of
 *  rock behind the face (twice the bay's depth) is squeezed to make room. Nothing turns, distance
 *  along a ray only grows, so nothing folds. Rays from that far back spread the stretch over the
 *  floor's whole run from C2 (with C2 at Cb, the floor right off the corner stretched over 3 times). */
static void apply_bay(const struct CornerInfo *ci, int64_t cx, int64_t cy, int64_t fx, int64_t fy, int64_t out[2], int64_t *noise_weight)
{
    const int64_t r3 = ci->r * TERRAIN_SUBDIV;
    const int64_t a3 = r3 / 4;
    const int64_t b3 = (r3 < TERRAIN_BAY_RAY_MAX * TERRAIN_SUBDIV) ? r3 : TERRAIN_BAY_RAY_MAX * TERRAIN_SUBDIV;
    const int64_t rho3 = isqrt64((r3 - a3) * (r3 - a3) + a3 * a3);
    const int64_t depth3 = rho3 - a3 * 1414 / 1000;
    const int64_t room3 = TERRAIN_BAY_BLOCK * 256 * TERRAIN_SUBDIV - rho3 - a3 * 1414 / 1000 - 96 * TERRAIN_SUBDIV;
    const int64_t band3 = (2 * depth3 < room3) ? 2 * depth3 : room3;
    const int64_t lx = (fx - cx * TERRAIN_SUBDIV) * ci->qx * 256;
    const int64_t ly = (fy - cy * TERRAIN_SUBDIV) * ci->qy * 256;
    const int64_t ux = lx + b3;
    const int64_t uy = ly + b3;
    const int64_t w = a3 - b3; // C2 - Cb, along both axes
    int64_t d, t_face, t_far, wu, disc, dnew, mv[2];
    if ((depth3 <= 0) || (band3 <= depth3))
        return;
    d = isqrt64(ux * ux + uy * uy);
    if (d == 0)
        return;
    // Where the ray from C2 leaves the floor: across x = 0 or y = 0, whichever first.
    t_face = INT64_MAX;
    if (ux > 0)
        t_face = b3 * d / ux;
    if ((uy > 0) && (b3 * d / uy < t_face))
        t_face = b3 * d / uy;
    if (t_face == INT64_MAX)
        return;
    // Where it leaves the circle: |C2 - Cb + t u/d| = rho.
    wu = w * (ux + uy) / d;
    disc = wu * wu - 2 * w * w + rho3 * rho3;
    if (disc <= 0)
        return;
    t_far = -wu + isqrt64(disc);
    if (t_far <= t_face)
        return;
    if (d <= t_face)
        dnew = d * t_far / t_face;
    else if (d <= t_face + band3)
        dnew = t_far + (d - t_face) * (t_face + band3 - t_far) / band3;
    else
        return;
    mv[0] = ux * (dnew - d) / d;
    mv[1] = uy * (dnew - d) / d;
    out[0] += mv[0] * ci->w / TERRAIN_WEIGHT_ONE * ci->qx / TERRAIN_SUBDIV;
    out[1] += mv[1] * ci->w / TERRAIN_WEIGHT_ONE * ci->qy / TERRAIN_SUBDIV;
    keep_less_noise(isqrt64(mv[0] * mv[0] + mv[1] * mv[1]), depth3, 1, noise_weight);
}

/** Case C at the corner carrying it. In local coordinates (u along the end face from this corner
 *  to its partner, v into the rock) the face from u = 0 to W (a TERRAIN_BAY_BLOCK) becomes the half
 *  ellipse v = D sqrt(1 - (2u / W - 1)^2), D the arch's depth: it leaves the side walls at the old
 *  corners, square to the face, so the walls run straight into it. Points move only along v: the
 *  floor up to TERRAIN_BAY_RAY_MAX back from the face stretches out to the arch, and a band of rock
 *  behind it (twice the depth) is squeezed. Each line of constant u only stretches, so nothing folds. */
static void apply_cap(const struct CornerInfo *ci, int64_t cx, int64_t cy, int64_t fx, int64_t fy, int64_t out[2], int64_t *noise_weight)
{
    const int64_t w3 = TERRAIN_BAY_BLOCK * 256 * TERRAIN_SUBDIV;
    const int64_t half3 = w3 / 2;
    const int64_t d3 = ci->r * TERRAIN_SUBDIV;
    const int64_t floor3 = TERRAIN_BAY_RAY_MAX * TERRAIN_SUBDIV;
    const int64_t room3 = w3 - d3 - 96 * TERRAIN_SUBDIV;
    const int64_t band3 = (2 * d3 < room3) ? 2 * d3 : room3;
    const int64_t lx = (fx - cx * TERRAIN_SUBDIV) * ci->qx * 256;
    const int64_t ly = (fy - cy * TERRAIN_SUBDIV) * ci->qy * 256;
    const int64_t u = (ci->axis == 0) ? -lx : -ly;
    const int64_t v = (ci->axis == 0) ? ly : lx;
    int64_t e, mv;
    if ((d3 <= 0) || (band3 <= d3))
        return;
    if ((u <= 0) || (u >= w3) || (v < -floor3) || (v >= band3))
        return;
    e = d3 * isqrt64(half3 * half3 - (u - half3) * (u - half3)) / half3;
    if (v <= 0)
        mv = e * (floor3 + v) / floor3;
    else
        mv = e * (band3 - v) / band3;
    if (ci->axis == 0)
        out[1] += mv * ci->w / TERRAIN_WEIGHT_ONE * ci->qy / TERRAIN_SUBDIV;
    else
        out[0] += mv * ci->w / TERRAIN_WEIGHT_ONE * ci->qx / TERRAIN_SUBDIV;
    keep_less_noise(mv, d3, 1, noise_weight);
}

/** terrain_rough_shape(), also giving how much of the noise to keep there, in 1/1024ths: none at a
 *  rounded corner itself, all of it where the rounding has faded out. (The two together could
 *  otherwise push a corner past its neighbour along a face, though each alone can't.) */
/* The shaped corners within reach of a subtile's points (the map source), found once a frame: most
   corners round a point are plain, and the points of a subtile share their neighbourhood. */
#define CORNER_LIST_MAX 12
#define CORNER_LIST_BITS 13
#define CORNER_LIST_SLOTS (1 << CORNER_LIST_BITS)
struct CornerListEntry {
    int32_t cx, cy;
    struct CornerInfo ci;
};
struct CornerList {
    uint64_t key;
    uint32_t gen;
    int count; ///< -1: more than CORNER_LIST_MAX, scan instead
    struct CornerListEntry e[CORNER_LIST_MAX];
};
static struct CornerList corner_lists[CORNER_LIST_SLOTS];

/** The shaped corners a point of subtile (sx, sy) can be moved by, or NULL to scan for them. */
static const struct CornerList *corner_list(const struct TerrainCellSource *src, int64_t r_max, int64_t reach,
    int64_t sx, int64_t sy, int64_t level)
{
    uint64_t key;
    struct CornerList *list;
    if (src->cls != terrain_rough_map_cell_class)
        return NULL;
    key = ((uint64_t)(uint16_t)sx << 48) | ((uint64_t)(uint16_t)sy << 32) | ((uint64_t)(uint16_t)(int16_t)level << 16)
        | (uint64_t)(uint16_t)r_max;
    list = &corner_lists[(key * 0x9E3779B97F4A7C15ull) >> (64 - CORNER_LIST_BITS)];
    if ((list->gen != cell_cache_gen) || (list->key != key))
    {
        // Every corner any point of the subtile reaches, in the scan's order.
        const int64_t lo_x = sx * TERRAIN_SUBDIV, hi_x = sx * TERRAIN_SUBDIV + TERRAIN_SUBDIV - 1;
        const int64_t lo_y = sy * TERRAIN_SUBDIV, hi_y = sy * TERRAIN_SUBDIV + TERRAIN_SUBDIV - 1;
        list->key = key;
        list->gen = cell_cache_gen;
        list->count = 0;
        for (int64_t cy = floor_div(lo_y - reach, TERRAIN_SUBDIV); (cy * TERRAIN_SUBDIV <= hi_y + reach) && (list->count >= 0); cy++)
        {
            for (int64_t cx = floor_div(lo_x - reach, TERRAIN_SUBDIV); cx * TERRAIN_SUBDIV <= hi_x + reach; cx++)
            {
                struct CornerInfo ci;
                corner_info(src, r_max, cx, cy, level, &ci);
                if (ci.kind == CK_None)
                    continue;
                if (list->count >= CORNER_LIST_MAX)
                {
                    list->count = -1;
                    break;
                }
                list->e[list->count].cx = (int32_t)cx;
                list->e[list->count].cy = (int32_t)cy;
                list->e[list->count].ci = ci;
                list->count++;
            }
        }
    }
    return (list->count < 0) ? NULL : list;
}

static void shape_and_noise_weight(const struct TerrainCellSource *src, const struct TerrainRoughParams *prm,
    int64_t fx, int64_t fy, int64_t fz, int64_t out[2], int64_t *noise_weight)
{
    const int64_t r_max = prm->corner_radius;
    // The corner pattern is read at the first wall level above the floor (cluedo walls have only it).
    const int64_t level = (fz <= TERRAIN_SUBDIV) ? 1 : floor_div(fz - 1, TERRAIN_SUBDIV);
    // Corners near enough to move this point: a rounded corner reaches R, a bay or an arch a few subtiles.
    const int64_t reach = (((r_max > TERRAIN_BAY_BLOCK * 256) ? r_max : TERRAIN_BAY_BLOCK * 256) * TERRAIN_SUBDIV + 255) / 256; // in fine cells
    const struct CornerList *list;
    out[0] = out[1] = 0;
    *noise_weight = 1024;
    if ((r_max <= 0) || (fz < TERRAIN_SUBDIV))
        return;
    list = corner_list(src, r_max, reach, floor_div(fx, TERRAIN_SUBDIV), floor_div(fy, TERRAIN_SUBDIV), level);
    if (list != NULL)
    {
        // The same corners, in the same order, as the scan below finds (those out of reach move nothing).
        for (int i = 0; i < list->count; i++)
        {
            const struct CornerListEntry *e = &list->e[i];
            if (e->ci.kind == CK_Convex)
                apply_convex(&e->ci, e->cx, e->cy, fx, fy, out, noise_weight);
            else if (e->ci.kind == CK_Bay)
                apply_bay(&e->ci, e->cx, e->cy, fx, fy, out, noise_weight);
            else
                apply_cap(&e->ci, e->cx, e->cy, fx, fy, out, noise_weight);
        }
        return;
    }
    for (int64_t cy = floor_div(fy - reach, TERRAIN_SUBDIV); cy * TERRAIN_SUBDIV <= fy + reach; cy++)
    {
        for (int64_t cx = floor_div(fx - reach, TERRAIN_SUBDIV); cx * TERRAIN_SUBDIV <= fx + reach; cx++)
        {
            struct CornerInfo ci;
            corner_info(src, r_max, cx, cy, level, &ci);
            if (ci.kind == CK_Convex)
                apply_convex(&ci, cx, cy, fx, fy, out, noise_weight);
            else if (ci.kind == CK_Bay)
                apply_bay(&ci, cx, cy, fx, fy, out, noise_weight);
            else if (ci.kind == CK_Cap)
                apply_cap(&ci, cx, cy, fx, fy, out, noise_weight);
        }
    }
}

/** Whether any corner shape moves anything within a subtile of lattice point (fx, fy): the boxes
 *  round the parts of the plan each corner's map moves (Case A: the rock square and the floor band
 *  beyond its faces; Case B: from its rays' start in the floor to well past the lips in the rock;
 *  Case C: the end face's width, from the floor it stretches to the rock it squeezes),
 *  against the four subtiles round the point. A subtile corner that doesn't move itself but has
 *  moving points that close must still count as moving: its edges are cut, so no long uncut edge
 *  runs past points the shape moves across it. */
static TbBool shape_near(const struct TerrainCellSource *src, const struct TerrainRoughParams *prm, int64_t fx, int64_t fy, int64_t fz)
{
    const int64_t r_max = prm->corner_radius;
    const int64_t level = (fz <= TERRAIN_SUBDIV) ? 1 : floor_div(fz - 1, TERRAIN_SUBDIV);
    // The furthest a box reaches from its corner (2 R into a bay's rock, TERRAIN_BAY_RAY_MAX back into its
    // floor, a band beyond a face, an arch's face), and a subtile more for the point's own subtiles.
    const int64_t far = max(max(max(2 * r_max, (int64_t)TERRAIN_BAY_RAY_MAX), (int64_t)TERRAIN_CORNER_BAND),
        (int64_t)(TERRAIN_BAY_BLOCK * 256)) + 256;
    const int64_t reach = (far * TERRAIN_SUBDIV + 255) / 256;
    if ((r_max <= 0) || (fz < TERRAIN_SUBDIV))
        return false;
    for (int64_t cy = floor_div(fy - reach, TERRAIN_SUBDIV); cy * TERRAIN_SUBDIV <= fy + reach; cy++)
    {
        for (int64_t cx = floor_div(fx - reach, TERRAIN_SUBDIV); cx * TERRAIN_SUBDIV <= fx + reach; cx++)
        {
            struct CornerInfo ci;
            int64_t lo_x, hi_x, lo_y, hi_y, ax0, ax1, ay0, ay1;
            corner_info(src, r_max, cx, cy, level, &ci);
            if (ci.kind == CK_None)
                continue;
            if (ci.kind == CK_Convex)
            {
                lo_x = lo_y = -TERRAIN_CORNER_BAND;
                hi_x = hi_y = ci.r;
            }
            else if (ci.kind == CK_Bay)
            {
                lo_x = lo_y = -((ci.r < TERRAIN_BAY_RAY_MAX) ? ci.r : TERRAIN_BAY_RAY_MAX);
                hi_x = hi_y = 2 * ci.r;
            }
            else
            {
                // Along the face: its width back from the corner; across it: the floor to the rock band.
                lo_x = -TERRAIN_BAY_BLOCK * 256;
                hi_x = 0;
                lo_y = -TERRAIN_BAY_RAY_MAX;
                hi_y = 2 * ci.r;
                if (ci.axis == 1)
                {
                    int64_t t;
                    t = lo_x; lo_x = lo_y; lo_y = t;
                    t = hi_x; hi_x = hi_y; hi_y = t;
                }
            }
            // The subtiles round the point, in the corner's local axes, map units.
            ax0 = (fx - TERRAIN_SUBDIV - cx * TERRAIN_SUBDIV) * ci.qx * 256 / TERRAIN_SUBDIV;
            ax1 = (fx + TERRAIN_SUBDIV - cx * TERRAIN_SUBDIV) * ci.qx * 256 / TERRAIN_SUBDIV;
            ay0 = (fy - TERRAIN_SUBDIV - cy * TERRAIN_SUBDIV) * ci.qy * 256 / TERRAIN_SUBDIV;
            ay1 = (fy + TERRAIN_SUBDIV - cy * TERRAIN_SUBDIV) * ci.qy * 256 / TERRAIN_SUBDIV;
            if ((max(ax0, ax1) > lo_x) && (min(ax0, ax1) < hi_x) && (max(ay0, ay1) > lo_y) && (min(ay0, ay1) < hi_y))
                return true;
        }
    }
    return false;
}

/* Shapes already worked out this frame (the map source, the current settings): a vertex's own
   displacement and its neighbours' edge bands ask for the same points. */
#define SHAPE_CACHE_BITS 15
#define SHAPE_CACHE_SLOTS (1 << SHAPE_CACHE_BITS)
struct ShapeCacheSlot {
    uint64_t key;
    uint32_t gen;
    int32_t shape[2];
    int32_t noise_weight;
};
static struct ShapeCacheSlot shape_cache[SHAPE_CACHE_SLOTS];

static void shape_and_noise_weight_cached(const struct TerrainCellSource *src, const struct TerrainRoughParams *prm,
    int64_t fx, int64_t fy, int64_t fz, int64_t out[2], int64_t *noise_weight)
{
    uint64_t key;
    struct ShapeCacheSlot *slot;
    if ((src->cls != terrain_rough_map_cell_class) || (prm != &terrain_rough_settings.params))
    {
        shape_and_noise_weight(src, prm, fx, fy, fz, out, noise_weight);
        return;
    }
    key = ((uint64_t)(uint16_t)fx << 32) | ((uint64_t)(uint16_t)fy << 16) | (uint64_t)(uint16_t)(int16_t)fz;
    slot = &shape_cache[(key * 0x9E3779B97F4A7C15ull) >> (64 - SHAPE_CACHE_BITS)];
    if ((slot->gen != cell_cache_gen) || (slot->key != key))
    {
        int64_t sh[2], nw;
        shape_and_noise_weight(src, prm, fx, fy, fz, sh, &nw);
        slot->shape[0] = (int32_t)sh[0];
        slot->shape[1] = (int32_t)sh[1];
        slot->noise_weight = (int32_t)nw;
        slot->key = key;
        slot->gen = cell_cache_gen;
    }
    out[0] = slot->shape[0];
    out[1] = slot->shape[1];
    *noise_weight = slot->noise_weight;
}

void terrain_rough_shape(const struct TerrainCellSource *src, const struct TerrainRoughParams *prm,
    int64_t fx, int64_t fy, int64_t fz, int64_t out[2])
{
    int64_t noise_weight;
    shape_and_noise_weight_cached(src, prm, fx, fy, fz, out, &noise_weight);
}

TbBool terrain_rough_vertex_moves(const struct TerrainCellSource *src, const struct TerrainRoughParams *prm,
    int64_t fx, int64_t fy, int64_t fz)
{
    int64_t dir[3];
    const enum TerrainMoveMask mask = terrain_rough_move_mask(src, fx, fy, fz);
    terrain_rough_pull_dir(src, fx, fy, fz, mask, dir);
    if ((dir[0] != 0) || (dir[1] != 0) || (dir[2] != 0))
        return true;
    if ((prm->corner_radius > 0) && shape_may_move(src, fx, fy, fz))
    {
        int64_t shape[2];
        terrain_rough_shape(src, prm, fx, fy, fz, shape);
        if ((shape[0] != 0) || (shape[1] != 0))
            return true;
        // A subtile corner the shapes leave alone, with points they move close by.
        if ((fx % TERRAIN_SUBDIV == 0) && (fy % TERRAIN_SUBDIV == 0))
            return shape_near(src, prm, fx, fy, fz);
    }
    return false;
}

/** The noise part of a vertex's move (out), given its move mask and how much noise the corner shapes
 *  near it leave (noise_weight, in 1/1024ths). */
static void noise_part(const struct TerrainCellSource *src, const struct TerrainRoughParams *prm,
    int64_t fx, int64_t fy, int64_t fz, int64_t taper_num, int64_t taper_den, enum TerrainMoveMask mask,
    int64_t noise_weight, int64_t out[3])
{
    int64_t dir[3] = {0, 0, 0};
    int64_t raw[3], eff[3];
    int64_t cell_w;
    int noisy;
    out[0] = out[1] = out[2] = 0;
    pull_sums(src, fx, fy, fz, mask, raw, eff, &noisy, &cell_w);
    if (noisy)
        normalise_dir(eff, dir);
    if ((taper_num > 0) && (taper_den > 0) && (prm->amplitude > 0) && ((dir[0] != 0) || (dir[1] != 0) || (dir[2] != 0)))
    {
        // Rock and gems alone (noisy == 1): a subtler roughness.
        const int64_t scale = (noisy == 1) ? prm->shapeable_percent : 100;
        const int64_t depth = terrain_rough_depth(prm, fx, fy, fz) * taper_num / taper_den * noise_weight / 1024 * scale / 100
            * cell_w / TERRAIN_WEIGHT_ONE;
        out[0] = depth * dir[0] / TERRAIN_DIR_ONE;
        out[1] = depth * dir[1] / TERRAIN_DIR_ONE;
        // Floors dip less than walls recess: creatures still stand at a floor's own height.
        out[2] = depth * prm->floor_amplitude / prm->amplitude * dir[2] / TERRAIN_DIR_ONE;
        // A vertex on an edge or corner (where the solid around it turns: two or more axes in the raw
        // pull, whichever of them it may move along) moves partly along the faces beside it: by a fine
        // cell or more, it would pass the next vertex on one of them and turn that face's triangles over.
        // Its moves stay under three quarters of a fine cell. (A vertex inside a face moves only along
        // its normal and can't pass a neighbour, so it keeps the full depth.) Sideways, the next vertices
        // along a face can be diagonal from it (a lattice triangle's hypotenuse), so the two horizontal
        // moves together, not each, stay under the cap.
        if (((raw[0] != 0) + (raw[1] != 0) + (raw[2] != 0)) >= 2)
        {
            const int64_t cap = TERRAIN_EDGE_MOVE_CAP;
            const int64_t h = llabs(out[0]) + llabs(out[1]);
            if (h > cap)
            {
                out[0] = out[0] * cap / h;
                out[1] = out[1] * cap / h;
            }
            if (out[2] > cap)
                out[2] = cap;
            else if (out[2] < -cap)
                out[2] = -cap;
        }
    }
}

/** The noise part of another vertex's move, worked out from scratch. */
static void noise_move_uncached(const struct TerrainCellSource *src, const struct TerrainRoughParams *prm,
    int64_t fx, int64_t fy, int64_t fz, int64_t taper_num, int64_t taper_den, int64_t out[3])
{
    const enum TerrainMoveMask mask = terrain_rough_move_mask(src, fx, fy, fz);
    int64_t noise_weight = 1024;
    out[0] = out[1] = out[2] = 0;
    if (mask == TMM_None)
        return;
    if ((prm->corner_radius > 0) && shape_may_move(src, fx, fy, fz))
    {
        int64_t shape[2];
        shape_and_noise_weight_cached(src, prm, fx, fy, fz, shape, &noise_weight);
    }
    noise_part(src, prm, fx, fy, fz, taper_num, taper_den, mask, noise_weight, out);
}

/* Noise moves already worked out this frame (the map source): each floor and top vertex near an edge
   asks its neighbours for theirs. */
#define NOISE_CACHE_BITS 15
#define NOISE_CACHE_SLOTS (1 << NOISE_CACHE_BITS)
struct NoiseCacheSlot {
    uint64_t key;
    uint32_t gen;
    int32_t m[3];
    TbBool pinned; ///< a hard-pinned cell touches it
};
static struct NoiseCacheSlot noise_cache[NOISE_CACHE_SLOTS];

/** The noise part of another vertex's move. Returns false if a hard-pinned cell touches it (out is
 *  then zero). */
static TbBool noise_move_at(const struct TerrainCellSource *src, const struct TerrainRoughParams *prm,
    int64_t fx, int64_t fy, int64_t fz, int64_t taper_num, int64_t taper_den, int64_t out[3])
{
    uint64_t key;
    struct NoiseCacheSlot *slot;
    if ((src->cls != terrain_rough_map_cell_class) || (prm != &terrain_rough_settings.params)
        || (taper_num < 0) || (taper_num > 255) || (taper_den < 0) || (taper_den > 255))
    {
        out[0] = out[1] = out[2] = 0;
        if (!shape_may_move(src, fx, fy, fz))
            return false;
        noise_move_uncached(src, prm, fx, fy, fz, taper_num, taper_den, out);
        return true;
    }
    key = ((uint64_t)(uint16_t)fx << 48) | ((uint64_t)(uint16_t)fy << 32) | ((uint64_t)(uint16_t)(int16_t)fz << 16)
        | ((uint64_t)(uint8_t)taper_num << 8) | (uint64_t)(uint8_t)taper_den;
    slot = &noise_cache[(key * 0x9E3779B97F4A7C15ull) >> (64 - NOISE_CACHE_BITS)];
    if ((slot->gen != cell_cache_gen) || (slot->key != key))
    {
        int64_t m[3] = { 0, 0, 0 };
        slot->pinned = !shape_may_move(src, fx, fy, fz);
        if (!slot->pinned)
            noise_move_uncached(src, prm, fx, fy, fz, taper_num, taper_den, m);
        for (int i = 0; i < 3; i++)
            slot->m[i] = (int32_t)m[i];
        slot->key = key;
        slot->gen = cell_cache_gen;
    }
    for (int i = 0; i < 3; i++)
        out[i] = slot->m[i];
    return !slot->pinned;
}

/** A floor or the top of the rock moves with the edge beside it, fading out over a subtile: only the
 *  edge row of vertices is pulled into the rock, and on its own it would squeeze (a top) or stretch
 *  (a floor) the texture of the first fine cell by up to TERRAIN_EDGE_MOVE_CAP of its 85 units. Each of
 *  the four directions adds the sideways move of the first vertex along it that has one, times
 *  (TERRAIN_SUBDIV - k) / TERRAIN_SUBDIV at k fine cells away: zero at the next subtile corner, so
 *  coarse corners never move. Nothing is taken across a pinned vertex. Bands from two sides together
 *  move it no further than the larger alone. */
static void edge_band(const struct TerrainCellSource *src, const struct TerrainRoughParams *prm,
    int64_t fx, int64_t fy, int64_t fz, int64_t taper_num, int64_t taper_den, int64_t out[3])
{
    static const int64_t dirs[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
    int64_t band[2] = { 0, 0 };
    int64_t largest = 0;
    for (int d = 0; d < 4; d++)
    {
        for (int64_t k = 1; k < TERRAIN_SUBDIV; k++)
        {
            int64_t m[3];
            // The edge's untapered move, tapered as this vertex is (so each edge vertex is worked out once).
            if (!noise_move_at(src, prm, fx + dirs[d][0] * k, fy + dirs[d][1] * k, fz, 1, 1, m))
                break;
            if ((m[0] != 0) || (m[1] != 0))
            {
                const int64_t bx = m[0] * (TERRAIN_SUBDIV - k) / TERRAIN_SUBDIV * taper_num / taper_den;
                const int64_t by = m[1] * (TERRAIN_SUBDIV - k) / TERRAIN_SUBDIV * taper_num / taper_den;
                band[0] += bx;
                band[1] += by;
                if (llabs(bx) + llabs(by) > largest)
                    largest = llabs(bx) + llabs(by);
                break;
            }
        }
    }
    {
        // Near a corner of a top, two edges both pull inward: together they would push the vertex across
        // the subtile's diagonal. Together they move it no further than the larger alone.
        const int64_t h = llabs(band[0]) + llabs(band[1]);
        if (h > largest)
        {
            band[0] = band[0] * largest / h;
            band[1] = band[1] * largest / h;
        }
    }
    out[0] += band[0];
    out[1] += band[1];
}

int64_t terrain_rough_displacement(const struct TerrainCellSource *src, const struct TerrainRoughParams *prm,
    int64_t fx, int64_t fy, int64_t fz, int64_t taper_num, int64_t taper_den, int64_t out[3])
{
    int64_t shape[2] = {0, 0};
    int64_t noise_weight = 1024;
    int64_t recess;
    enum TerrainMoveMask mask;
    out[0] = out[1] = out[2] = 0;
    // Rounded corners: a fold-free map of the plan, so it isn't capped, and isn't tapered either -- it
    // fades to nothing inside the rock and over the floor beside it, away from anything pinned.
    if ((prm->corner_radius > 0) && shape_may_move(src, fx, fy, fz))
        shape_and_noise_weight_cached(src, prm, fx, fy, fz, shape, &noise_weight);
    mask = terrain_rough_move_mask(src, fx, fy, fz);
    if (mask != TMM_None)
        noise_part(src, prm, fx, fy, fz, taper_num, taper_den, mask, noise_weight, out);
    // How deep the noise recessed it (what the shading darkens by); the corner shapes aren't recesses.
    recess = isqrt64(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    // A vertex of a floor or a top that doesn't move sideways itself, with nothing pinned touching it (a
    // plane-pinned floor's vertices have no move of their own, but may slide): the edge band.
    if ((out[0] == 0) && (out[1] == 0) && (fz % TERRAIN_SUBDIV == 0) && (prm->amplitude > 0) && (taper_num > 0) && (taper_den > 0)
        && (((mask & TMM_Horizontal) != 0) || ((mask == TMM_None) && shape_may_move(src, fx, fy, fz))))
        edge_band(src, prm, fx, fy, fz, taper_num, taper_den, out);
    out[0] += shape[0];
    out[1] += shape[1];
    return recess;
}

/******************************************************************************/
static void set_w(unsigned char w[3], int a, int b, int c)
{
    w[0] = (unsigned char)a;
    w[1] = (unsigned char)b;
    w[2] = (unsigned char)c;
}

static void put_tri(struct TerrainSubTri *t, const unsigned char p[3], const unsigned char q[3], const unsigned char r[3])
{
    for (int i = 0; i < 3; i++)
    {
        t->w[0][i] = p[i];
        t->w[1][i] = q[i];
        t->w[2][i] = r[i];
    }
}

/** Four times the squared distance between two lattice points, in the plane the triangle is drawn in
 *  as an equilateral one (only used to pick the shorter of two diagonals). */
static int64_t lattice_dist4(const unsigned char p[3], const unsigned char q[3])
{
    const int64_t db = (int64_t)q[1] - p[1];
    const int64_t dc = (int64_t)q[2] - p[2];
    return (2 * db + dc) * (2 * db + dc) + 3 * dc * dc;
}

int terrain_subdiv_triangulate(int n, const int fine[3], struct TerrainSubTri *out)
{
    int count = 0;
    if ((n < 3) || (n > TERRAIN_SUBDIV_MAX_N))
        return 0;
    if (!fine[0] && !fine[1] && !fine[2])
    {
        unsigned char a[3], b[3], c[3];
        set_w(a, n, 0, 0);
        set_w(b, 0, n, 0);
        set_w(c, 0, 0, n);
        put_tri(&out[0], a, b, c);
        return 1;
    }
    if (fine[0] && fine[1] && fine[2])
    {
        // The full lattice: point (i, j) weighs n-i-j for A, i for B, j for C.
        for (int j = 0; j < n; j++)
            for (int i = 0; i + j < n; i++)
            {
                unsigned char p[3], q[3], r[3];
                set_w(p, n - i - j, i, j);
                set_w(q, n - i - 1 - j, i + 1, j);
                set_w(r, n - i - j - 1, i, j + 1);
                put_tri(&out[count++], p, q, r);
                if (i + j < n - 1)
                {
                    unsigned char s[3];
                    set_w(s, n - i - j - 2, i + 1, j + 1);
                    put_tri(&out[count++], q, s, r);
                }
            }
        return count;
    }
    {
        // Mixed: the inner lattice (every weight >= 1) as usual, then the ring between it and the
        // boundary, whose coarse edges keep only their ends.
        unsigned char outer[3 * TERRAIN_SUBDIV_MAX_N][3];
        unsigned char inner[3 * TERRAIN_SUBDIV_MAX_N][3];
        int no = 0, ni = 0;
        const int m = n - 3; // inner lattice cuts
        // Outer loop A -> B -> C.
        set_w(outer[no++], n, 0, 0);
        if (fine[0])
            for (int t = 1; t < n; t++)
                set_w(outer[no++], n - t, t, 0);
        set_w(outer[no++], 0, n, 0);
        if (fine[1])
            for (int t = 1; t < n; t++)
                set_w(outer[no++], 0, n - t, t);
        set_w(outer[no++], 0, 0, n);
        if (fine[2])
            for (int t = 1; t < n; t++)
                set_w(outer[no++], t, 0, n - t);
        // Inner loop, same direction, starting near A.
        if (m == 0)
        {
            set_w(inner[ni++], 1, 1, 1);
        }
        else
        {
            for (int t = 0; t < m; t++)
                set_w(inner[ni++], n - 2 - t, 1 + t, 1);
            for (int t = 0; t < m; t++)
                set_w(inner[ni++], 1, n - 2 - t, 1 + t);
            for (int t = 0; t < m; t++)
                set_w(inner[ni++], 1 + t, 1, n - 2 - t);
            // The inner lattice's own triangles.
            for (int j = 0; j < m; j++)
                for (int i = 0; i + j < m; i++)
                {
                    unsigned char p[3], q[3], r[3];
                    set_w(p, m - i - j + 1, i + 1, j + 1);
                    set_w(q, m - i - j, i + 2, j + 1);
                    set_w(r, m - i - j, i + 1, j + 2);
                    put_tri(&out[count++], p, q, r);
                    if (i + j < m - 1)
                    {
                        unsigned char s[3];
                        set_w(s, m - i - j - 1, i + 2, j + 2);
                        put_tri(&out[count++], q, s, r);
                    }
                }
        }
        {
            int a = 0, b = 0;
            while ((a < no) || ((ni > 1) && (b < ni)))
            {
                TbBool advance_outer;
                if (a >= no)
                    advance_outer = false;
                else if ((ni <= 1) || (b >= ni))
                    advance_outer = true;
                else
                    advance_outer = lattice_dist4(outer[(a + 1) % no], inner[b % ni]) <= lattice_dist4(outer[a % no], inner[(b + 1) % ni]);
                if (advance_outer)
                {
                    put_tri(&out[count++], outer[a % no], outer[(a + 1) % no], inner[b % ni]);
                    a++;
                }
                else
                {
                    put_tri(&out[count++], outer[a % no], inner[(b + 1) % ni], inner[b % ni]);
                    b++;
                }
            }
        }
    }
    return count;
}
/******************************************************************************/

/* Displacements already worked out this frame (the map source, the current settings): a vertex's
   shading looks at its neighbours, so most lattice points are asked for several times. */
#define DISP_CACHE_BITS 15
#define DISP_CACHE_SLOTS (1 << DISP_CACHE_BITS)
struct DispCacheSlot {
    uint64_t key;
    uint32_t gen;
    int32_t d[3];
    int32_t depth;
};
static struct DispCacheSlot disp_cache[DISP_CACHE_SLOTS];

int64_t terrain_rough_map_displacement(int64_t fx, int64_t fy, int64_t fz, int64_t taper_num, int64_t taper_den, int64_t out[3])
{
    const uint64_t key = ((uint64_t)(uint16_t)fx << 48) | ((uint64_t)(uint16_t)fy << 32) | ((uint64_t)(uint16_t)(int16_t)fz << 16)
        | ((uint64_t)(uint8_t)taper_num << 8) | (uint64_t)(uint8_t)taper_den;
    struct DispCacheSlot *slot = &disp_cache[(key * 0x9E3779B97F4A7C15ull) >> (64 - DISP_CACHE_BITS)];
    if ((taper_num < 0) || (taper_num > 255) || (taper_den < 0) || (taper_den > 255))
        return terrain_rough_displacement(&terrain_rough_map_source, &terrain_rough_settings.params, fx, fy, fz, taper_num, taper_den, out);
    if ((slot->gen != cell_cache_gen) || (slot->key != key))
    {
        int64_t d[3];
        slot->depth = (int32_t)terrain_rough_displacement(&terrain_rough_map_source, &terrain_rough_settings.params, fx, fy, fz, taper_num, taper_den, d);
        for (int i = 0; i < 3; i++)
            slot->d[i] = (int32_t)d[i];
        slot->key = key;
        slot->gen = cell_cache_gen;
    }
    for (int i = 0; i < 3; i++)
        out[i] = slot->d[i];
    return slot->depth;
}

int64_t terrain_rough_floor_offset_src(const struct TerrainCellSource *src, const struct TerrainRoughParams *prm,
    int64_t map_x, int64_t map_y, int64_t floor_level)
{
    const int64_t fz = floor_level * TERRAIN_SUBDIV;
    const int64_t gx = map_x * TERRAIN_SUBDIV, gy = map_y * TERRAIN_SUBDIV; // in 1/256ths of a fine cell
    const int64_t fx0 = floor_div(gx, 256), fy0 = floor_div(gy, 256);
    const int64_t tx = gx - fx0 * 256, ty = gy - fy0 * 256;
    int64_t dz[2][2];
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 2; i++)
        {
            int64_t d[3];
            terrain_rough_displacement(src, prm, fx0 + i, fy0 + j, fz, TERRAIN_SUBDIV, TERRAIN_SUBDIV, d);
            dz[j][i] = d[2];
        }
    {
        const int64_t top = dz[0][0] * (256 - tx) + dz[0][1] * tx;
        const int64_t bottom = dz[1][0] * (256 - tx) + dz[1][1] * tx;
        return (top * (256 - ty) + bottom * ty) / (256 * 256);
    }
}

int64_t terrain_rough_floor_offset(int64_t map_x, int64_t map_y)
{
    const struct TerrainRoughSettings *trs = &terrain_rough_settings;
    struct Map *mapblk;
    const struct Column *col;
    int64_t level = 0;
    if ((trs->mode == TSM_Off) || !trs->floors || (trs->params.amplitude <= 0) || (trs->params.floor_amplitude <= 0))
        return 0;
    mapblk = get_map_block_at(map_x / COORD_PER_STL, map_y / COORD_PER_STL);
    if (map_block_invalid(mapblk) || !map_block_revealed(mapblk, my_player_number))
        return 0;
    col = get_map_column(mapblk);
    while ((level < COLUMN_STACK_HEIGHT) && ((col->solidmask & (1 << level)) != 0))
        level++;
    return terrain_rough_floor_offset_src(&terrain_rough_map_source, &trs->params, map_x, map_y, level);
}
/******************************************************************************/
