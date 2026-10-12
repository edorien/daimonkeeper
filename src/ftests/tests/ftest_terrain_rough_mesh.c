#include "ftest_terrain_rough_mesh.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "player_data.h"
#include "player_instances.h"
#include "thing_creature.h"
#include "thing_data.h"
#include "dungeon_data.h"
#include "kfx_sim_state.h"
#include "config_settings.h"
#include "config_keeperfx.h"
#include "engine_camera.h"
#include "local_camera.h"
#include "terrain_audit.h"
#include "terrain_rough.h"
#include "kfx_config_state.h"
#include "scrcapt.h"

#include <stdlib.h>

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Camera stops, in slabs from the player's dungeon heart. */
static const int trm_stop_offsets[][2] = {
    { 0, 0 }, { -6, -4 }, { 6, -4 }, { -6, 5 }, { 6, 5 }, { 0, -9 }, { 0, 9 },
};
#define TRM_STOPS (sizeof(trm_stop_offsets) / sizeof(trm_stop_offsets[0]))

/** Overhead view settings each stop is drawn in. */
struct TrmViewConfig {
    const char *name;
    unsigned char view_mode;
    int64_t rotation;
    unsigned char cluedo;
    int focus_x2, focus_y2; ///< catalogue views: where the camera looks, half slabs from the catalogue's corner
};
static const struct TrmViewConfig trm_configs[] = {
    { "wibble",     PVM_IsoWibbleView,   256, 0, 0, 0 },
    { "straight",   PVM_IsoStraightView, 256, 0, 0, 0 },
    { "wibble-rot", PVM_IsoWibbleView,   768, 0, 0, 0 },
    { "cluedo",     PVM_IsoWibbleView,   256, 1, 0, 0 },
};
#define TRM_CONFIGS (sizeof(trm_configs) / sizeof(trm_configs[0]))

/** Rough-terrain subdivision modes the overhead stops are drawn in (displacement off, so only the
 *  topology changes: 03-verification-and-phases.md, phase 2). */
static const enum TerrainSubdivMode trm_modes[] = { TSM_Off, TSM_Gated, TSM_All, TSM_Gated, TSM_All };
static const char *const trm_mode_names[] = { "off", "gated", "all", "gated+recess", "all+recess" };
/** Recess depth per mode: 0 for the cut alone (phase 2), the default for walls recessed (phase 3). */
static const int64_t trm_mode_amplitude[] = { 0, 0, 0, -1, -1 };
#define TRM_MODES (sizeof(trm_modes) / sizeof(trm_modes[0]))

/** First-person stops: subtiles from the heart, eye height, yaw and pitch. Drawn while possessing
 *  an imp, so the perspective mesher (lens_mode != 0) is the one that runs. */
struct TrmFirstPersonStop {
    TbBool in_catalogue;  ///< offsets from the catalogue's corner, else from the heart
    int dx2, dy2;         ///< half subtiles
    int64_t z;
    int64_t yaw, pitch;
    const char *name;
};
static const struct TrmFirstPersonStop trm_fp_stops[] = {
    { false, -10,   0, 300,    0,    0, "heart-w" },
    { false,  10,   0, 300, 1024,    0, "heart-e" },
    { false,   0, -10, 300,  512,    0, "heart-n" },
    { false,   0,  10, 300, 1536,    0, "heart-s" },
    { false, -10, -10, 300,  256,    0, "heart-nw" },
    { false,  10,  10, 300, 1280,   96, "heart-se-up" },
    { false,   8,  -8, 600,  768, 1952, "heart-ne-down" },
    // Inside the catalogue's corridor (02-design.md section 7: the ceiling against recessed rims).
    { true,   12,   9, 400,  512,    0, "corridor-a" },
    { true,   12,   9, 400, 1536,    0, "corridor-b" },
    { true,   12,   9, 400,  512,  160, "corridor-up" },
    { true,   12,   9, 400,  512, 1888, "corridor-down" },
    { true,   51,   9, 400, 1024,    0, "l-corner" },
    { true,   51,  28, 400, 1536,    0, "water" },
    { true,   15,  45, 400,  256,    0, "lone-tile" },
    { true,   51,  40, 400, 1024,    0, "lava" },
    { true,   57,  57, 400,  256,    0, "rock-tile" },
};
#define TRM_FP_STOPS (sizeof(trm_fp_stops) / sizeof(trm_fp_stops[0]))

/** The catalogue (03-verification-and-phases.md section B): an area beside the heart stamped with
 *  the cases rough terrain has to get right, in slabs from its corner. Earth first, then: */
struct TrmStamp {
    int x0, y0, x1, y1;
    SlabKind kind;
    PlayerNumber owner;
};
#define TRM_CAT_W 12
#define TRM_CAT_H 12
static const struct TrmStamp trm_catalogue[] = {
    { 0, 0, TRM_CAT_W - 1, TRM_CAT_H - 1, SlbT_EARTH,     PLAYER_NEUTRAL }, // a clean block of earth
    { 1, 1,  8,  1, SlbT_PATH,      PLAYER_NEUTRAL }, // an L corridor: a dead end, an outer and an inner corner
    { 8, 1,  8,  7, SlbT_PATH,      PLAYER_NEUTRAL },
    { 5, 0,  5,  0, SlbT_WALLDRAPE, PLAYER0 },        // a reinforced wall in the corridor's side
    { 0, 1,  0,  1, SlbT_GOLD,      PLAYER_NEUTRAL }, // gold at its dead end
    { 2, 2,  4,  2, SlbT_PATH,      PLAYER_NEUTRAL }, // a wider stretch...
    { 2, 3,  4,  3, SlbT_CLAIMED,   PLAYER0 },        // ...with claimed floor beside rough walls
    { 6, 4,  7,  5, SlbT_WATER,     PLAYER_NEUTRAL }, // water beside the corridor
    { 9, 6, 10,  7, SlbT_LAVA,      PLAYER_NEUTRAL }, // lava beside it
    { 2, 7,  4,  9, SlbT_PATH,      PLAYER_NEUTRAL }, // a small dug room...
    { 3, 8,  3,  8, SlbT_EARTH,     PLAYER_NEUTRAL }, // ...around a lone earth tile
    { 5, 8,  7,  8, SlbT_PATH,      PLAYER_NEUTRAL }, // joined to the corridor
    { 9, 9, 11, 11, SlbT_PATH,      PLAYER_NEUTRAL }, // a lone rock tile on path (phase 8: a circle)
    {10,10, 10, 10, SlbT_ROCK,      PLAYER_NEUTRAL },
    { 8, 9,  8,  9, SlbT_PATH,      PLAYER_NEUTRAL },
    { 0, 9,  2, 11, SlbT_CLAIMED,   PLAYER0 },        // a lone gem tile on claimed floor (stays square)
    { 1,10,  1, 10, SlbT_GEMS,      PLAYER_NEUTRAL },
};
#define TRM_CAT_STAMPS (sizeof(trm_catalogue) / sizeof(trm_catalogue[0]))

/** Views of the catalogue: modes, then cameras (rotation, cluedo). */
static const enum TerrainSubdivMode trm_cat_modes[] = { TSM_Off, TSM_Gated, TSM_All, TSM_Gated, TSM_Gated, TSM_Gated, TSM_Gated };
static const char *const trm_cat_mode_names[] = { "off", "gated+recess", "all+recess", "gated+floors", "gated+circle",
    "gated+reinforcing", "gated+circle+reinforcing" };
/** Whether path floors dip too (phase 5). */
static const int trm_cat_floors[] = { 0, 0, 0, 1, 0, 1, 0 };
/** Corner radius per mode (phase 8); -1: the default. */
static const int64_t trm_cat_radius[] = { -1, -1, -1, -1, 384, -1, 384 };
/** Whether the rock is caught partway through being reinforced (phase 7: neighbouring slabs at
 *  different progress, so eased and rough faces meet). */
static const int trm_cat_reinforcing[] = { 0, 0, 0, 0, 0, 1, 1 };
#define TRM_CAT_MODES (sizeof(trm_cat_modes) / sizeof(trm_cat_modes[0]))
static const struct TrmViewConfig trm_cat_views[] = {
    { "near",        PVM_IsoWibbleView, 256, 0, TRM_CAT_W, TRM_CAT_H },
    { "near-rot",    PVM_IsoWibbleView, 768, 0, TRM_CAT_W, TRM_CAT_H },
    { "near-cluedo", PVM_IsoWibbleView, 256, 1, TRM_CAT_W, TRM_CAT_H },
    { "lone-tiles",  PVM_IsoWibbleView, 256, 0,  5, 19 },
    { "rock-tile",   PVM_IsoWibbleView, 768, 0, 21, 21 },
};
#define TRM_CAT_VIEWS (sizeof(trm_cat_views) / sizeof(trm_cat_views[0]))

/** Turns to let the camera settle before a frame counts. */
#define TRM_SETTLE 2
/** Give up on a stop whose frame never comes. */
#define TRM_TIMEOUT 200

struct ftest_terrain_rough_mesh__variables
{
    struct Thing *creature; // possessed for the first-person stops
    GameTurn phase_start;
    size_t step;            // (mode * TRM_STOPS + stop) * TRM_CONFIGS + config, then the first-person stop
    TbBool placed;
    TbBool shot_pending;    // a screenshot was asked for; wait for one more frame before moving on
    int floor_dips_seen;    // first-person stops where the floor under the eye dips
    MapSlabCoord cat_x;     // the catalogue's corner, in slabs
    MapSlabCoord cat_y;
    GameTurn placed_turn;
    uint64_t placed_frame;
    unsigned char cluedo_before;
    unsigned char view_mode_before;
    int64_t rotation_before;
};
struct ftest_terrain_rough_mesh__variables ftest_terrain_rough_mesh__vars = {
    .creature = NULL,
    .phase_start = 0,
    .step = 0,
    .placed = false,
    .placed_turn = 0,
    .placed_frame = 0,
};

static void trm_place_camera(size_t stop, const struct TrmViewConfig *cfg)
{
    struct PlayerInfo *player = get_player(PLAYER0);
    struct Thing *heart = get_player_soul_container(PLAYER0);
    struct Camera *cam = &player->cameras[CamIV_Isometric];
    const MapCoord max_x = (kfx_sim_state.map_subtiles_x + 1) * COORD_PER_STL - 1;
    const MapCoord max_y = (kfx_sim_state.map_subtiles_y + 1) * COORD_PER_STL - 1;
    MapCoord x = heart->mappos.x.val + trm_stop_offsets[stop][0] * STL_PER_SLB * COORD_PER_STL;
    MapCoord y = heart->mappos.y.val + trm_stop_offsets[stop][1] * STL_PER_SLB * COORD_PER_STL;
    struct TerrainAuditCamera pin;
    cam->mappos.x.val = clamp(x, 0, max_x);
    cam->mappos.y.val = clamp(y, 0, max_y);
    cam->rotation_angle_x = cfg->rotation;
    cam->view_mode = cfg->view_mode;
    settings.video_cluedo_mode = cfg->cluedo;
    sync_local_camera(player);
    // Camera input and real-time interpolation would move the drawn camera a little from run to run.
    pin.x = cam->mappos.x.val;
    pin.y = cam->mappos.y.val;
    pin.z = cam->mappos.z.val;
    pin.angle_x = cam->rotation_angle_x;
    pin.angle_y = cam->rotation_angle_y;
    pin.angle_z = cam->rotation_angle_z;
    pin.zoom = cam->zoom;
    pin.view_mode = cfg->view_mode;
    terrain_audit_pin_camera(&pin);
}

/** Logs an audited frame's numbers and fails the test on a mismatch, an empty frame or a full table. */
static void trm_check_frame(int stop, const char *view, const struct TerrainAuditStats *st)
{
    JUSTLOG("TRNMESH: stop=%d view=%s turn=%llu polys=%llu hash=%016llx geom=%016llx tex=%016llx shade=%016llx mesh=%016llx tris=%llu verts=%llu mismatch=%llu edges1=%llu edges2=%llu edges3+=%llu tjunction=%llu foldover=%llu overflow=%llu unstamped=%llu disagree=%llu cam=(%lld,%lld,%lld) angle=%lld zoom=%lld build_us=%lld draw_us=%lld",
        stop, view, (unsigned long long)st->gameturn, (unsigned long long)st->polys, (unsigned long long)st->poly_hash,
        (unsigned long long)st->geom_hash, (unsigned long long)st->tex_hash, (unsigned long long)st->shade_hash, (unsigned long long)st->mesh_hash,
        (unsigned long long)st->tris, (unsigned long long)st->vertices, (unsigned long long)st->mismatches,
        (unsigned long long)st->edges_once, (unsigned long long)st->edges_twice, (unsigned long long)st->edges_more,
        (unsigned long long)st->tjunctions, (unsigned long long)st->foldovers, (unsigned long long)st->overflow, (unsigned long long)st->unstamped, (unsigned long long)st->stamp_disagree, (long long)st->cam_x, (long long)st->cam_y, (long long)st->cam_z,
        (long long)st->cam_angle, (long long)st->cam_zoom, (long long)st->build_us, (long long)st->draw_us);
    if (st->polys == 0)
        FTEST_FAIL_TEST("Stop %d (%s): the frame drew no polygons", stop, view);
    if (st->mismatches != 0)
        FTEST_FAIL_TEST("Stop %d (%s): %llu lattice vertices drawn at two positions", stop, view, (unsigned long long)st->mismatches);
    if (st->overflow != 0)
        FTEST_FAIL_TEST("Stop %d (%s): audit tables overflowed", stop, view);
    if (st->tjunctions != 0)
        FTEST_FAIL_TEST("Stop %d (%s): %llu T-junctions", stop, view, (unsigned long long)st->tjunctions);
    if (st->foldovers != 0)
        FTEST_FAIL_TEST("Stop %d (%s): %llu sub-triangles turned over", stop, view, (unsigned long long)st->foldovers);
    if (st->tris == 0)
        FTEST_FAIL_TEST("Stop %d (%s): no terrain triangle was registered", stop, view);
    if (st->unstamped != 0)
        FTEST_FAIL_TEST("Stop %d (%s): %llu terrain triangles had a corner without a lattice id", stop, view, (unsigned long long)st->unstamped);
    if (st->stamp_disagree != 0)
        FTEST_FAIL_TEST("Stop %d (%s): %llu corners named another grid vertex than their slot", stop, view, (unsigned long long)st->stamp_disagree);
}

static FTestActionResult trm_setup(struct FTestActionArgs* const args)
{
    struct ftest_terrain_rough_mesh__variables* const vars = args->data;
    struct PlayerInfo *player = get_player(PLAYER0);
    if (thing_is_invalid(get_player_soul_container(PLAYER0)))
    {
        FTEST_FAIL_TEST("Player 0 has no dungeon heart");
        return FTRs_Go_To_Next_Action;
    }
    ftest_util_reveal_map(PLAYER0);
    vars->cluedo_before = settings.video_cluedo_mode;
    vars->view_mode_before = player->cameras[CamIV_Isometric].view_mode;
    vars->rotation_before = player->cameras[CamIV_Isometric].rotation_angle_x;
    terrain_audit_set_enabled(1);
    terrain_rough_settings.from_config = 0; // the test sets the switches itself
    // KFX_FTEST_TRM_FLAT=1: screenshots with terrain at one shade on black, to find holes by eye.
    terrain_audit_set_flat(getenv("KFX_FTEST_TRM_FLAT") != NULL);
    if (!terrain_audit_is_enabled())
        FTEST_FAIL_TEST("Terrain audit could not be enabled");
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult trm_walk_stops(struct FTestActionArgs* const args)
{
    struct ftest_terrain_rough_mesh__variables* const vars = args->data;
    const size_t stop = (vars->step / TRM_CONFIGS) % TRM_STOPS;
    const size_t mode = vars->step / (TRM_CONFIGS * TRM_STOPS);
    const struct TrmViewConfig *cfg = &trm_configs[vars->step % TRM_CONFIGS];
    const struct TerrainAuditStats *st = terrain_audit_last();
    char view[64];
    if (mode >= TRM_MODES)
    {
        terrain_rough_settings.mode = TSM_Off;
        return FTRs_Go_To_Next_Action;
    }
    if (!vars->placed)
    {
        terrain_rough_settings.mode = trm_modes[mode];
        terrain_rough_settings.params = terrain_rough_default_params;
        if (trm_mode_amplitude[mode] >= 0)
        {
            // The cut alone: nothing displaced, corners not rounded either.
            terrain_rough_settings.params.amplitude = trm_mode_amplitude[mode];
            terrain_rough_settings.params.corner_radius = 0;
        }
        terrain_rough_settings.walls = 1;
        terrain_rough_settings.floors = 0;
        trm_place_camera(stop, cfg);
        vars->placed = true;
        vars->placed_turn = get_gameturn();
        vars->placed_frame = st->frame;
        return FTRs_Repeat_Current_Action;
    }
    if ((st->frame <= vars->placed_frame) || (st->gameturn < vars->placed_turn + TRM_SETTLE))
    {
        if (get_gameturn() > vars->placed_turn + TRM_TIMEOUT)
        {
            FTEST_FAIL_TEST("Stop %d (%s): no audited frame drawn", (int)stop, cfg->name);
            vars->placed = false;
            vars->step++;
        }
        return FTRs_Repeat_Current_Action;
    }
    snprintf(view, sizeof(view), "%s/%s", trm_mode_names[mode], cfg->name);
    if (vars->shot_pending)
    {
        // The frame after the one checked has been drawn (and captured) from the same camera.
        vars->shot_pending = false;
        vars->placed = false;
        vars->step++;
        return FTRs_Repeat_Current_Action;
    }
    trm_check_frame((int)stop, view, st);
    // KFX_FTEST_TRM_SHOTS=1: save the wibble view of each stop with rough terrain off and recessed,
    // to look at (scrshots/trm_<mode>_<stop>.png).
    if ((getenv("KFX_FTEST_TRM_SHOTS_ALL") != NULL) && (cfg == &trm_configs[0]) && ((trm_modes[mode] == TSM_Off) || (trm_mode_amplitude[mode] < 0)))
    {
        char fname[128];
        snprintf(fname, sizeof(fname), "scrshots/trm_%s_%d.png", trm_mode_names[mode], (int)stop);
        take_screenshot(fname);
        vars->shot_pending = true;
        vars->placed_frame = st->frame;
        return FTRs_Repeat_Current_Action;
    }
    vars->placed = false;
    vars->step++;
    return FTRs_Repeat_Current_Action;
}

static FTestActionResult trm_stamp_catalogue(struct FTestActionArgs* const args)
{
    struct ftest_terrain_rough_mesh__variables* const vars = args->data;
    struct Thing *heart = get_player_soul_container(PLAYER0);
    terrain_audit_pin_camera(NULL);
    vars->cat_x = (MapSlabCoord)(heart->mappos.x.val / (COORD_PER_STL * STL_PER_SLB)) + 5;
    vars->cat_y = (MapSlabCoord)(heart->mappos.y.val / (COORD_PER_STL * STL_PER_SLB)) - 6;
    for (size_t i = 0; i < TRM_CAT_STAMPS; i++)
    {
        const struct TrmStamp *st = &trm_catalogue[i];
        if (!ftest_util_replace_slabs(vars->cat_x + st->x0, vars->cat_y + st->y0, vars->cat_x + st->x1, vars->cat_y + st->y1, st->kind, st->owner))
            FTEST_FAIL_TEST("Couldn't stamp catalogue entry %d", (int)i);
    }
    ftest_util_reveal_map(PLAYER0);
    vars->step = 0;
    vars->placed = false;
    vars->shot_pending = false;
    return FTRs_Go_To_Next_Action;
}

/** Holds the catalogue's rock at a pattern of reinforcement progress (0, 1/4 .. all of the way), or
 *  lets it all go. */
static void trm_pin_reinforcement(const struct ftest_terrain_rough_mesh__variables *vars, int reinforcing)
{
    terrain_rough_reinforcement_clear();
    if (!reinforcing)
        return;
    for (int y = 0; y < TRM_CAT_H; y++)
        for (int x = 0; x < TRM_CAT_W; x++)
        {
            const struct SlabMap *slb = get_slabmap_block(vars->cat_x + x, vars->cat_y + y);
            const int64_t progress = ((x * 2 + y * 3) % 5) * TERRAIN_WEIGHT_ONE / 4;
            if (slabmap_block_invalid(slb) || (progress == 0))
                continue;
            if ((slb->kind == SlbT_EARTH) || (slb->kind == SlbT_GOLD) || (slb->kind == SlbT_ROCK) || (slb->kind == SlbT_GEMS))
                terrain_rough_reinforcement_pin(vars->cat_x + x, vars->cat_y + y, progress);
        }
}

static FTestActionResult trm_walk_catalogue(struct FTestActionArgs* const args)
{
    struct ftest_terrain_rough_mesh__variables* const vars = args->data;
    const size_t mode = vars->step / TRM_CAT_VIEWS;
    const struct TrmViewConfig *cfg = &trm_cat_views[vars->step % TRM_CAT_VIEWS];
    const struct TerrainAuditStats *st = terrain_audit_last();
    char view[64];
    if (mode >= TRM_CAT_MODES)
    {
        terrain_rough_settings.mode = TSM_Off;
        terrain_rough_reinforcement_clear();
        settings.video_cluedo_mode = vars->cluedo_before;
        return FTRs_Go_To_Next_Action;
    }
    if (!vars->placed)
    {
        struct TerrainAuditCamera pin;
        trm_pin_reinforcement(vars, trm_cat_reinforcing[mode]);
        terrain_rough_settings.mode = trm_cat_modes[mode];
        terrain_rough_settings.params = terrain_rough_default_params;
        terrain_rough_settings.walls = 1;
        terrain_rough_settings.floors = trm_cat_floors[mode];
        if (trm_cat_radius[mode] >= 0)
            terrain_rough_settings.params.corner_radius = trm_cat_radius[mode];
        settings.video_cluedo_mode = cfg->cluedo;
        pin.x = (vars->cat_x * STL_PER_SLB * 2 + STL_PER_SLB * cfg->focus_x2) * COORD_PER_STL / 2;
        pin.y = (vars->cat_y * STL_PER_SLB * 2 + STL_PER_SLB * cfg->focus_y2) * COORD_PER_STL / 2;
        pin.z = 0;
        pin.angle_x = cfg->rotation;
        pin.angle_y = get_player(PLAYER0)->cameras[CamIV_Isometric].rotation_angle_y;
        pin.angle_z = 0;
        pin.zoom = CAMERA_ZOOM_MAX;
        pin.view_mode = cfg->view_mode;
        terrain_audit_pin_camera(&pin);
        vars->placed = true;
        vars->placed_turn = get_gameturn();
        vars->placed_frame = st->frame;
        return FTRs_Repeat_Current_Action;
    }
    if ((st->frame <= vars->placed_frame) || (st->gameturn < vars->placed_turn + TRM_SETTLE))
    {
        if (get_gameturn() > vars->placed_turn + TRM_TIMEOUT)
        {
            FTEST_FAIL_TEST("Catalogue view %d: no audited frame drawn", (int)vars->step);
            vars->placed = false;
            vars->step++;
        }
        return FTRs_Repeat_Current_Action;
    }
    if (vars->shot_pending)
    {
        vars->shot_pending = false;
        vars->placed = false;
        vars->step++;
        return FTRs_Repeat_Current_Action;
    }
    snprintf(view, sizeof(view), "catalogue:%s/%s", trm_cat_mode_names[mode], cfg->name);
    trm_check_frame(0, view, st);
    if (getenv("KFX_FTEST_TRM_SHOTS") != NULL)
    {
        char fname[128];
        snprintf(fname, sizeof(fname), "scrshots/trm_cat_%s_%s.png", trm_cat_mode_names[mode], cfg->name);
        take_screenshot(fname);
        vars->shot_pending = true;
        vars->placed_frame = st->frame;
        return FTRs_Repeat_Current_Action;
    }
    vars->placed = false;
    vars->step++;
    return FTRs_Repeat_Current_Action;
}

static FTestActionResult trm_possess(struct FTestActionArgs* const args)
{
    struct ftest_terrain_rough_mesh__variables* const vars = args->data;
    struct PlayerInfo *player = get_player(PLAYER0);
    struct Thing *heart = get_player_soul_container(PLAYER0);
    terrain_audit_pin_camera(NULL);
    settings.video_cluedo_mode = vars->cluedo_before;
    vars->creature = create_owned_special_digger(heart->mappos.x.val + 3 * COORD_PER_STL, heart->mappos.y.val, PLAYER0);
    if (thing_is_invalid(vars->creature))
    {
        FTEST_FAIL_TEST("Failed to create a creature to possess");
        return FTRs_Go_To_Next_Action;
    }
    player->influenced_thing_idx = vars->creature->index;
    player->influenced_thing_creation = vars->creature->creation_turn;
    set_player_instance(player, PI_DirctCtrl, false);
    vars->phase_start = get_gameturn();
    vars->step = 0;
    vars->placed = false;
    vars->shot_pending = false;
    return FTRs_Go_To_Next_Action;
}

/** True once the player shows `view` with no instance running; fails the test on a timeout. */
static TbBool trm_reached_view(struct ftest_terrain_rough_mesh__variables *vars, int64_t view, const char *what)
{
    struct PlayerInfo *player = get_player(PLAYER0);
    if ((player->view_type == view) && (player->instance_num == PI_Unset))
        return true;
    if (get_gameturn() > vars->phase_start + TRM_TIMEOUT)
    {
        FTEST_FAIL_TEST("%s: view %d not reached (view %d, instance %d)", what, (int)view, (int)player->view_type, (int)player->instance_num);
        return true;
    }
    return false;
}

static FTestActionResult trm_walk_first_person(struct FTestActionArgs* const args)
{
    struct ftest_terrain_rough_mesh__variables* const vars = args->data;
    const struct TerrainAuditStats *st = terrain_audit_last();
    const size_t mode = vars->step / TRM_FP_STOPS;
    const struct TrmFirstPersonStop *fp = &trm_fp_stops[vars->step % TRM_FP_STOPS];
    char view[96];
    if (thing_is_invalid(vars->creature))
        return FTRs_Go_To_Next_Action;
    if (!trm_reached_view(vars, PVT_CreatureContrl, "possession"))
        return FTRs_Repeat_Current_Action;
    if (mode >= TRM_CAT_MODES)
    {
        terrain_rough_settings.mode = TSM_Off;
        terrain_rough_reinforcement_clear();
        return FTRs_Go_To_Next_Action;
    }
    if (!vars->placed)
    {
        struct TerrainAuditCamera pin;
        trm_pin_reinforcement(vars, trm_cat_reinforcing[mode]);
        if (fp->in_catalogue)
        {
            pin.x = vars->cat_x * STL_PER_SLB * COORD_PER_STL + fp->dx2 * COORD_PER_STL / 2;
            pin.y = vars->cat_y * STL_PER_SLB * COORD_PER_STL + fp->dy2 * COORD_PER_STL / 2;
        }
        else
        {
            struct Thing *heart = get_player_soul_container(PLAYER0);
            pin.x = heart->mappos.x.val + fp->dx2 * COORD_PER_STL / 2;
            pin.y = heart->mappos.y.val + fp->dy2 * COORD_PER_STL / 2;
        }
        terrain_rough_settings.mode = trm_cat_modes[mode];
        terrain_rough_settings.params = terrain_rough_default_params;
        terrain_rough_settings.walls = 1;
        terrain_rough_settings.floors = trm_cat_floors[mode];
        if (trm_cat_radius[mode] >= 0)
            terrain_rough_settings.params.corner_radius = trm_cat_radius[mode];
        pin.z = fp->z;
        pin.angle_x = fp->yaw;
        pin.angle_y = fp->pitch;
        pin.angle_z = 0;
        pin.zoom = get_player(PLAYER0)->cameras[CamIV_FirstPerson].zoom;
        pin.view_mode = PVM_CreatureView;
        terrain_audit_pin_camera(&pin);
        vars->placed = true;
        vars->placed_turn = get_gameturn();
        vars->placed_frame = st->frame;
        return FTRs_Repeat_Current_Action;
    }
    if ((st->frame <= vars->placed_frame) || (st->gameturn < vars->placed_turn + TRM_SETTLE))
    {
        if (get_gameturn() > vars->placed_turn + TRM_TIMEOUT)
        {
            FTEST_FAIL_TEST("First-person stop %s: no audited frame drawn", fp->name);
            vars->placed = false;
            vars->step++;
        }
        return FTRs_Repeat_Current_Action;
    }
    if (vars->shot_pending)
    {
        vars->shot_pending = false;
        vars->placed = false;
        vars->step++;
        return FTRs_Repeat_Current_Action;
    }
    snprintf(view, sizeof(view), "first-person:%s/%s", trm_cat_mode_names[mode], fp->name);
    trm_check_frame((int)(vars->step % TRM_FP_STOPS), view, st);
    if (trm_cat_floors[mode] && fp->in_catalogue)
    {
        // The eye follows the drawn floor (no gameplay effect): its offset is a dip no deeper than the floor amplitude.
        const struct TerrainAuditCamera *pin = terrain_audit_pinned_camera();
        const int64_t off = (pin != NULL) ? terrain_rough_floor_offset(pin->x, pin->y) : 0;
        JUSTLOG("TRNFLOOR: %s offset=%lld", fp->name, (long long)off);
        if ((off > 0) || (off < -terrain_rough_settings.params.floor_amplitude))
            FTEST_FAIL_TEST("First-person stop %s: floor offset %lld out of range", fp->name, (long long)off);
        if (off != 0)
            vars->floor_dips_seen++;
    }
    if ((getenv("KFX_FTEST_TRM_SHOTS") != NULL) && fp->in_catalogue)
    {
        char fname[128];
        snprintf(fname, sizeof(fname), "scrshots/trm_fp_%s_%s.png", trm_cat_mode_names[mode], fp->name);
        take_screenshot(fname);
        vars->shot_pending = true;
        vars->placed_frame = st->frame;
        return FTRs_Repeat_Current_Action;
    }
    vars->placed = false;
    vars->step++;
    return FTRs_Repeat_Current_Action;
}

static FTestActionResult trm_leave_possession(struct FTestActionArgs* const args)
{
    struct ftest_terrain_rough_mesh__variables* const vars = args->data;
    if (vars->floor_dips_seen == 0)
        FTEST_FAIL_TEST("The floor never dipped under a first-person eye with rough floors on");
    terrain_audit_pin_camera(NULL);
    if (!thing_is_invalid(vars->creature))
        set_player_instance(get_player(PLAYER0), PI_DirctCtLeave, false);
    vars->phase_start = get_gameturn();
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult trm_finish(struct FTestActionArgs* const args)
{
    terrain_rough_settings.mode = TSM_Off;
    terrain_rough_settings.floors = 0;
    terrain_rough_settings.from_config = 1;
    terrain_rough_settings.params = terrain_rough_default_params;
    terrain_rough_reinforcement_clear();
    struct ftest_terrain_rough_mesh__variables* const vars = args->data;
    struct PlayerInfo *player = get_player(PLAYER0);
    if (!trm_reached_view(vars, PVT_DungeonTop, "leaving possession"))
        return FTRs_Repeat_Current_Action;
    terrain_audit_pin_camera(NULL);
    terrain_audit_set_enabled(0);
    settings.video_cluedo_mode = vars->cluedo_before;
    player->cameras[CamIV_Isometric].view_mode = vars->view_mode_before;
    player->cameras[CamIV_Isometric].rotation_angle_x = vars->rotation_before;
    sync_local_camera(player);
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_terrain_rough_mesh_init()
{
    ftest_append_action(trm_setup, 20, &ftest_terrain_rough_mesh__vars);
    ftest_append_action(trm_walk_stops, 0, &ftest_terrain_rough_mesh__vars);
    ftest_append_action(trm_stamp_catalogue, 0, &ftest_terrain_rough_mesh__vars);
    ftest_append_action(trm_walk_catalogue, 0, &ftest_terrain_rough_mesh__vars);
    ftest_append_action(trm_possess, 0, &ftest_terrain_rough_mesh__vars);
    ftest_append_action(trm_walk_first_person, 0, &ftest_terrain_rough_mesh__vars);
    ftest_append_action(trm_leave_possession, 0, &ftest_terrain_rough_mesh__vars);
    ftest_append_action(trm_finish, 0, &ftest_terrain_rough_mesh__vars);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
