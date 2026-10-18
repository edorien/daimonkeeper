#include "ftest_render_bench.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "player_data.h"
#include "thing_creature.h"
#include "thing_data.h"
#include "kfx_sim_state.h"
#include "kfx_config_state.h"
#include "config_creature.h"
#include "config_keeperfx.h"
#include "engine_camera.h"
#include "terrain_audit.h"
#include "bflib_basics.h"
#include "bflib_math.h"
#include "renderer/RendererManager.h"

#include <SDL3/SDL_timer.h>
#include <stdio.h>
#include <stdlib.h>

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Turns between placing the creatures and starting the clock: they spread out, the atlases fill. */
#define RB_WARMUP_TURNS 100
#define RB_SPOTS 24

struct ftest_render_bench__variables
{
    int64_t creatures;
    int64_t turns;
    GameTurn start_turn;
    uint64_t start_ticks;
    double max_turn_ms;
    uint64_t last_ticks;
};
struct ftest_render_bench__variables ftest_render_bench__vars;

static int64_t rb_env(const char *name, int64_t fallback)
{
    const char *v = getenv(name);
    return ((v != NULL) && (atoll(v) > 0)) ? atoll(v) : fallback;
}

static void rb_pin_camera(void)
{
    struct PlayerInfo *player = get_player(PLAYER0);
    struct Camera *cam = &player->cameras[CamIV_Isometric];
    struct Thing *heart = get_player_soul_container(PLAYER0);
    struct TerrainAuditCamera pin;
    pin.x = heart->mappos.x.val;
    pin.y = heart->mappos.y.val;
    pin.z = cam->mappos.z.val;
    pin.angle_x = 256;
    pin.angle_y = cam->rotation_angle_y;
    pin.angle_z = cam->rotation_angle_z;
    pin.zoom = cam->zoom;
    pin.view_mode = PVM_IsoWibbleView;
    terrain_audit_pin_camera(&pin);
}

static FTestActionResult rb_setup(struct FTestActionArgs* const args)
{
    struct ftest_render_bench__variables* const vars = args->data;
    struct Thing *heart = get_player_soul_container(PLAYER0);
    if (thing_is_invalid(heart))
    {
        FTEST_FAIL_TEST("Player 0 has no dungeon heart");
        return FTRs_Go_To_Next_Action;
    }
    vars->creatures = rb_env("KFX_FTEST_RB_CREATURES", 60);
    vars->turns = rb_env("KFX_FTEST_RB_TURNS", 600);
    ftest_util_reveal_map(PLAYER0);
    terrain_audit_set_enabled(1); // the camera pin needs it
    rb_pin_camera();
    // Every model in turn, on rings round the heart (spots in walls skipped).
    const int64_t models = kfx_config_state.conf.crtr_conf.model_count;
    int64_t placed = 0;
    ThingModel model = 1;
    for (int ring = 2; (ring <= 9) && (placed < vars->creatures); ring++)
        for (int spot = 0; (spot < RB_SPOTS) && (placed < vars->creatures); spot++)
        {
            const int64_t angle = spot * DEGREES_360 / RB_SPOTS + ring * 37;
            const MapSubtlCoord stl_x = (heart->mappos.x.val + ring * COORD_PER_STL * LbCosL(angle) / 65536) / COORD_PER_STL;
            const MapSubtlCoord stl_y = (heart->mappos.y.val + ring * COORD_PER_STL * LbSinL(angle) / 65536) / COORD_PER_STL;
            struct Thing *thing = ftest_util_create_creature(stl_x * COORD_PER_STL + COORD_PER_STL / 2, stl_y * COORD_PER_STL + COORD_PER_STL / 2,
                PLAYER0, 1, model);
            if (thing_is_invalid(thing))
                continue;
            placed++;
            if (++model >= models)
                model = 1;
        }
    JUSTLOG("RBENCH: %lld creatures placed (of %lld), renderer %s, shadow style %lld, asset pack '%s'", (long long)placed,
        (long long)vars->creatures, (RendererGetActiveType() == RENDERER_GPU3D) ? "vulkan" : "software",
        (long long)keeperfx_ui_config.shadow_style, keeperfx_ui_config.asset_pack);
    if (placed < vars->creatures / 2)
        FTEST_FAIL_TEST("Only %lld creatures could be placed", (long long)placed);
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult rb_start(struct FTestActionArgs* const args)
{
    struct ftest_render_bench__variables* const vars = args->data;
    rb_pin_camera();
    vars->start_turn = get_gameturn();
    vars->start_ticks = SDL_GetPerformanceCounter();
    vars->last_ticks = vars->start_ticks;
    vars->max_turn_ms = 0.0;
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult rb_measure(struct FTestActionArgs* const args)
{
    struct ftest_render_bench__variables* const vars = args->data;
    const uint64_t now = SDL_GetPerformanceCounter();
    const double freq = (double)SDL_GetPerformanceFrequency();
    const double turn_ms = (double)(now - vars->last_ticks) * 1000.0 / freq;
    if (turn_ms > vars->max_turn_ms)
        vars->max_turn_ms = turn_ms;
    vars->last_ticks = now;
    const int64_t done = (int64_t)(get_gameturn() - vars->start_turn);
    if (done < vars->turns)
        return FTRs_Repeat_Current_Action;
    const double secs = (double)(now - vars->start_ticks) / freq;
    JUSTLOG("RBENCH: %lld turns in %.2f s: %.1f turns/s, %.2f ms/turn average, %.2f ms worst", (long long)done, secs,
        (double)done / secs, secs * 1000.0 / (double)done, vars->max_turn_ms);
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult rb_end(struct FTestActionArgs* const args)
{
    (void)args;
    terrain_audit_set_enabled(0);
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_render_bench_init()
{
    memset(&ftest_render_bench__vars, 0, sizeof(ftest_render_bench__vars));
    ftest_append_action(rb_setup, 10, &ftest_render_bench__vars);
    ftest_append_action(rb_start, RB_WARMUP_TURNS, &ftest_render_bench__vars);
    ftest_append_action(rb_measure, 0, &ftest_render_bench__vars);
    ftest_append_action(rb_end, 0, &ftest_render_bench__vars);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
