#include "ftest_terrain_rough_reinforce.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "player_data.h"
#include "thing_creature.h"
#include "thing_data.h"
#include "slab_data.h"
#include "kfx_sim_state.h"
#include "config_settings.h"
#include "config_keeperfx.h"
#include "engine_camera.h"
#include "terrain_audit.h"
#include "terrain_rough.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The pocket: earth TRR_SIZE slabs square with claimed floor in its middle TRR_ROOM square. */
#define TRR_SIZE 7
#define TRR_ROOM_LO 2
#define TRR_ROOM_HI 4
#define TRR_IMPS 3
/** Give up if no wall is fortified by then. */
#define TRR_TIMEOUT 6000
/** Turns a wall left alone may take to relax (the easing's own 40, and a margin). */
#define TRR_RELAX_TURNS 80

struct ftest_terrain_rough_reinforce__variables
{
    MapSlabCoord pk_x;           // the pocket's corner, in slabs
    MapSlabCoord pk_y;
    struct Thing *imps[TRR_IMPS];
    GameTurn start_turn;
    GameTurn relax_start;
    uint64_t last_frame;
    int64_t progress[TRR_SIZE][TRR_SIZE]; // last turn's
    int64_t frames_checked;
    int64_t frames_eased;        // audited frames drawn while some wall was partway
    int64_t climbs;              // turns on which a wall's progress rose, partway
    int64_t fortified;
    MapSlabCoord relax_x;        // the wall left alone mid-way
    MapSlabCoord relax_y;
    int64_t relax_progress;
};
struct ftest_terrain_rough_reinforce__variables ftest_terrain_rough_reinforce__vars = {
    .pk_x = 0,
    .pk_y = 0,
};

static TbBool trr_in_room(int x, int y)
{
    return (x >= TRR_ROOM_LO) && (x <= TRR_ROOM_HI) && (y >= TRR_ROOM_LO) && (y <= TRR_ROOM_HI);
}

/** Fails the test on anything wrong with the last audited frame, once per frame. Returns whether there was a new one. */
static TbBool trr_check_frame(struct ftest_terrain_rough_reinforce__variables *vars)
{
    const struct TerrainAuditStats *st = terrain_audit_last();
    if (st->frame == vars->last_frame)
        return false;
    vars->last_frame = st->frame;
    vars->frames_checked++;
    if ((st->mismatches != 0) || (st->tjunctions != 0) || (st->foldovers != 0) || (st->overflow != 0)
        || (st->unstamped != 0) || (st->stamp_disagree != 0) || (st->tris == 0))
    {
        FTEST_FAIL_TEST("Turn %llu: audit mismatch=%llu tjunction=%llu foldover=%llu overflow=%llu unstamped=%llu disagree=%llu tris=%llu",
            (unsigned long long)st->gameturn, (unsigned long long)st->mismatches, (unsigned long long)st->tjunctions,
            (unsigned long long)st->foldovers, (unsigned long long)st->overflow, (unsigned long long)st->unstamped,
            (unsigned long long)st->stamp_disagree, (unsigned long long)st->tris);
    }
    return true;
}

static FTestActionResult trr_setup(struct FTestActionArgs* const args)
{
    struct ftest_terrain_rough_reinforce__variables* const vars = args->data;
    struct Thing *heart = get_player_soul_container(PLAYER0);
    struct TerrainAuditCamera pin;
    if (thing_is_invalid(heart))
    {
        FTEST_FAIL_TEST("Player 0 has no dungeon heart");
        return FTRs_Go_To_Next_Action;
    }
    vars->pk_x = (MapSlabCoord)(heart->mappos.x.val / (COORD_PER_STL * STL_PER_SLB)) + 5;
    vars->pk_y = (MapSlabCoord)(heart->mappos.y.val / (COORD_PER_STL * STL_PER_SLB)) - 6;
    if (!ftest_util_replace_slabs(vars->pk_x, vars->pk_y, vars->pk_x + TRR_SIZE - 1, vars->pk_y + TRR_SIZE - 1, SlbT_EARTH, PLAYER_NEUTRAL)
        || !ftest_util_replace_slabs(vars->pk_x + TRR_ROOM_LO, vars->pk_y + TRR_ROOM_LO, vars->pk_x + TRR_ROOM_HI, vars->pk_y + TRR_ROOM_HI, SlbT_CLAIMED, PLAYER0))
    {
        FTEST_FAIL_TEST("Couldn't stamp the pocket");
        return FTRs_Go_To_Next_Action;
    }
    ftest_util_reveal_map(PLAYER0);
    for (int i = 0; i < TRR_IMPS; i++)
    {
        const MapCoord x = (vars->pk_x + TRR_ROOM_LO + i) * STL_PER_SLB * COORD_PER_STL + COORD_PER_STL * 3 / 2;
        const MapCoord y = (vars->pk_y + 3) * STL_PER_SLB * COORD_PER_STL + COORD_PER_STL * 3 / 2;
        vars->imps[i] = create_owned_special_digger(x, y, PLAYER0);
        if (thing_is_invalid(vars->imps[i]))
        {
            FTEST_FAIL_TEST("Couldn't create imp %d", i);
            return FTRs_Go_To_Next_Action;
        }
    }
    terrain_rough_reinforcement_clear();
    terrain_rough_settings.from_config = 0;
    terrain_rough_settings.mode = TSM_Gated;
    terrain_rough_settings.params = terrain_rough_default_params;
    terrain_rough_settings.walls = 1;
    terrain_rough_settings.floors = 1;
    terrain_audit_set_enabled(1);
    pin.x = (vars->pk_x * STL_PER_SLB * 2 + STL_PER_SLB * TRR_SIZE) * COORD_PER_STL / 2;
    pin.y = (vars->pk_y * STL_PER_SLB * 2 + STL_PER_SLB * TRR_SIZE) * COORD_PER_STL / 2;
    pin.z = 0;
    pin.angle_x = 256;
    pin.angle_y = get_player(PLAYER0)->cameras[CamIV_Isometric].rotation_angle_y;
    pin.angle_z = 0;
    pin.zoom = CAMERA_ZOOM_MAX;
    pin.view_mode = PVM_IsoWibbleView;
    terrain_audit_pin_camera(&pin);
    if (!terrain_audit_is_enabled())
        FTEST_FAIL_TEST("Terrain audit could not be enabled");
    vars->start_turn = get_gameturn();
    vars->last_frame = terrain_audit_last()->frame;
    return FTRs_Go_To_Next_Action;
}

/** The imps reinforce: walls climb, at least one is fortified; then they are removed mid-wall. */
static FTestActionResult trr_watch_reinforcing(struct FTestActionArgs* const args)
{
    struct ftest_terrain_rough_reinforce__variables* const vars = args->data;
    MapSlabCoord best_x = -1, best_y = -1;
    int64_t best = 0, partway = 0;
    vars->fortified = 0;
    for (int y = 0; y < TRR_SIZE; y++)
        for (int x = 0; x < TRR_SIZE; x++)
        {
            const struct SlabMap *slb = get_slabmap_block(vars->pk_x + x, vars->pk_y + y);
            const int64_t p = terrain_rough_reinforcement_progress(vars->pk_x + x, vars->pk_y + y);
            if (trr_in_room(x, y))
                continue;
            if (slb->kind != SlbT_EARTH)
                vars->fortified++;
            if ((p > 0) && (p < TERRAIN_WEIGHT_ONE))
            {
                partway++;
                if (p > vars->progress[y][x])
                    vars->climbs++;
            }
            if ((slb->kind == SlbT_EARTH) && (p > best))
            {
                best = p;
                best_x = vars->pk_x + x;
                best_y = vars->pk_y + y;
            }
            vars->progress[y][x] = p;
        }
    if (trr_check_frame(vars) && (partway > 0))
        vars->frames_eased++;
    if ((vars->fortified > 0) && (vars->frames_eased >= 3) && (vars->climbs >= 10) && (best >= TERRAIN_WEIGHT_ONE / 4))
    {
        JUSTLOG("TRNREINF: fortified=%lld climbs=%lld frames_checked=%lld frames_eased=%lld; leaving slab (%d,%d) at %lld",
            (long long)vars->fortified, (long long)vars->climbs, (long long)vars->frames_checked, (long long)vars->frames_eased,
            (int)best_x, (int)best_y, (long long)best);
        for (int i = 0; i < TRR_IMPS; i++)
            if (!thing_is_invalid(vars->imps[i]) && thing_exists(vars->imps[i]))
                kill_creature(vars->imps[i], INVALID_THING, -1, CrDed_NoEffects | CrDed_NoUnconscious | CrDed_NotReallyDying);
        vars->relax_x = best_x;
        vars->relax_y = best_y;
        vars->relax_progress = best;
        vars->relax_start = get_gameturn();
        return FTRs_Go_To_Next_Action;
    }
    if (get_gameturn() > vars->start_turn + TRR_TIMEOUT)
    {
        FTEST_FAIL_TEST("No wall reinforced with easing in %d turns: fortified=%lld climbs=%lld frames_eased=%lld best=%lld",
            TRR_TIMEOUT, (long long)vars->fortified, (long long)vars->climbs, (long long)vars->frames_eased, (long long)best);
        return FTRs_Go_To_Next_Action;
    }
    return FTRs_Repeat_Current_Action;
}

/** Left alone, the wall relaxes back to fully rough, never climbing on the way. */
static FTestActionResult trr_watch_relaxing(struct FTestActionArgs* const args)
{
    struct ftest_terrain_rough_reinforce__variables* const vars = args->data;
    const int64_t p = terrain_rough_reinforcement_progress(vars->relax_x, vars->relax_y);
    trr_check_frame(vars);
    if (p > vars->relax_progress)
    {
        FTEST_FAIL_TEST("Slab (%d,%d) climbed from %lld to %lld with no imp at it",
            (int)vars->relax_x, (int)vars->relax_y, (long long)vars->relax_progress, (long long)p);
        return FTRs_Go_To_Next_Action;
    }
    vars->relax_progress = p;
    if (p == 0)
    {
        JUSTLOG("TRNREINF: slab (%d,%d) relaxed in %d turns", (int)vars->relax_x, (int)vars->relax_y,
            (int)(get_gameturn() - vars->relax_start));
        return FTRs_Go_To_Next_Action;
    }
    if (get_gameturn() > vars->relax_start + TRR_RELAX_TURNS)
    {
        FTEST_FAIL_TEST("Slab (%d,%d) still at %lld after %d turns alone", (int)vars->relax_x, (int)vars->relax_y, (long long)p, TRR_RELAX_TURNS);
        return FTRs_Go_To_Next_Action;
    }
    return FTRs_Repeat_Current_Action;
}

static FTestActionResult trr_finish(struct FTestActionArgs* const args)
{
    (void)args;
    terrain_audit_pin_camera(NULL);
    terrain_audit_set_enabled(0);
    terrain_rough_reinforcement_clear();
    terrain_rough_settings.mode = TSM_Off;
    terrain_rough_settings.floors = 0;
    terrain_rough_settings.params = terrain_rough_default_params;
    terrain_rough_settings.from_config = 1;
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_terrain_rough_reinforce_init()
{
    ftest_append_action(trr_setup, 20, &ftest_terrain_rough_reinforce__vars);
    ftest_append_action(trr_watch_reinforcing, 0, &ftest_terrain_rough_reinforce__vars);
    ftest_append_action(trr_watch_relaxing, 0, &ftest_terrain_rough_reinforce__vars);
    ftest_append_action(trr_finish, 0, &ftest_terrain_rough_reinforce__vars);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
