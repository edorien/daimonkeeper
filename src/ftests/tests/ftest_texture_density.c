#include "ftest_texture_density.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "player_data.h"
#include "player_instances.h"
#include "thing_creature.h"
#include "thing_data.h"
#include "kfx_sim_state.h"
#include "config_settings.h"
#include "config_keeperfx.h"
#include "engine_camera.h"
#include "engine_redraw.h"
#include "local_camera.h"
#include "packet_data.h"
#include "terrain_audit.h"
#include "terrain_rough.h"
#include "scrcapt.h"
#include "engine_textures_hd.h"
#include "vidfade.h"
#include "bflib_video.h"
#include "renderer/RendererManager.h"
#include "renderer/WorldFrame.h"

#include <stdio.h>
#include <string.h>

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Turns to let a camera settle before its frame counts, and to give up on one. */
#define TD_SETTLE 2
#define TD_TIMEOUT 200
/** Turns each palette phase is watched for. */
#define TD_PALETTE_TURNS 24

/** Overhead stops: slabs from the heart, the view, the zoom (0: the camera's own) and rough terrain. */
struct TdIsoStop {
    int dx, dy;
    unsigned char view_mode;
    unsigned char cluedo;
    int64_t zoom;
    int rough;
};
#define TD_ZOOM_DEFAULT 0
static const int64_t td_iso_zooms[] = { CAMERA_ZOOM_MAX, TD_ZOOM_DEFAULT, CAMERA_ZOOM_MIN };
static const char *const td_iso_zoom_names[] = { "closest", "default", "farthest" };
#define TD_ISO_ZOOMS (sizeof(td_iso_zooms) / sizeof(td_iso_zooms[0]))
static const int td_iso_offsets[][2] = { { 0, 0 }, { -6, -4 } };
#define TD_ISO_OFFSETS (sizeof(td_iso_offsets) / sizeof(td_iso_offsets[0]))
/** Views: wibble and cluedo (low walls). */
#define TD_ISO_VIEWS 2
/** Rough terrain off, and as the game's default (walls recessed, rounded corners). */
#define TD_ROUGHS 2
#define TD_ISO_STEPS (TD_ISO_ZOOMS * TD_ISO_OFFSETS * TD_ISO_VIEWS * TD_ROUGHS)

static const int64_t td_front_zooms[] = { FRONTVIEW_CAMERA_ZOOM_MAX, TD_ZOOM_DEFAULT, FRONTVIEW_CAMERA_ZOOM_MIN };
#define TD_FRONT_STEPS (sizeof(td_front_zooms) / sizeof(td_front_zooms[0]))

/** First-person stops (half subtiles from the heart, eye height, yaw, pitch), rough off and on. */
struct TdFirstPersonStop {
    int dx2, dy2;
    int64_t z;
    int64_t yaw, pitch;
    const char *name;
};
static const struct TdFirstPersonStop td_fp_stops[] = {
    { -10,   0, 300,    0,    0, "heart-w" },
    {  10,  10, 300, 1280,   96, "heart-se-up" },
    {   8,  -8, 600,  768, 1952, "heart-ne-down" },
    {   0, -10, 300,  512,    0, "heart-n" },
};
#define TD_FP_STEPS ((sizeof(td_fp_stops) / sizeof(td_fp_stops[0])) * TD_ROUGHS)

/** Palette phases watched on the GPU's texture cache. */
static const char *const td_palette_names[] = { "none", "pain", "freeze", "lightning", "white" };
#define TD_PALETTE_PHASES (sizeof(td_palette_names) / sizeof(td_palette_names[0]))

struct ftest_texture_density__variables
{
    struct Thing *creature;
    size_t step;
    TbBool placed;
    GameTurn placed_turn;
    uint64_t placed_frame;
    GameTurn phase_start;
    struct WorldFrameTextureStats tex_before;
    uint64_t hd_draws_before;
    unsigned char cluedo_before;
    unsigned char view_mode_before;
    int64_t zoom_before;
    TbBool shot_pending;
};
struct ftest_texture_density__variables ftest_texture_density__vars = {
    .creature = NULL,
    .step = 0,
    .placed = false,
};

static void td_set_rough(int rough)
{
    terrain_rough_settings.from_config = 0;
    terrain_rough_settings.params = terrain_rough_default_params;
    terrain_rough_settings.mode = rough ? TSM_Gated : TSM_Off;
    terrain_rough_settings.walls = 1;
    terrain_rough_settings.floors = 0;
}

/** Logs the last audited frame's texel density; fails the test if the stop drew nothing. */
static void td_log_density(const char *view, int64_t zoom, int rough)
{
    const struct TerrainAuditStats *st = terrain_audit_last();
    JUSTLOG("TRNTEX: view=%s zoom=%lld rough=%s res=%lldx%lld upp=%lld turn=%llu polys=%llu area=%.0f"
        " size_p10=%.3f size_p50=%.3f size_p90=%.3f size_p99=%.3f size_max=%.3f rate_p50=%.3f rate_p90=%.3f rate_p99=%.3f",
        view, (long long)zoom, rough ? "walls" : "off",
        (long long)lbDisplay.PhysicalScreenWidth, (long long)lbDisplay.PhysicalScreenHeight, (long long)units_per_pixel,
        (unsigned long long)st->gameturn, (unsigned long long)st->texel_polys, st->texel_area,
        terrain_texel_percentile(st->texel_size_hist, 0.10), terrain_texel_percentile(st->texel_size_hist, 0.50),
        terrain_texel_percentile(st->texel_size_hist, 0.90), terrain_texel_percentile(st->texel_size_hist, 0.99),
        terrain_texel_percentile(st->texel_size_hist, 1.0),
        terrain_texel_percentile(st->texel_rate_hist, 0.50), terrain_texel_percentile(st->texel_rate_hist, 0.90),
        terrain_texel_percentile(st->texel_rate_hist, 0.99));
    if (st->texel_polys == 0)
        FTEST_FAIL_TEST("%s (zoom %lld): no textured polygon was drawn", view, (long long)zoom);
}

/** Logs the GPU texture cache's counters since `before` (the Software renderer has none). */
static void td_log_gpu(const char *phase, const struct WorldFrameTextureStats *before)
{
    struct WorldFrameTextureStats now;
    if (!RendererGetWorldTextureStats(&now))
    {
        JUSTLOG("GPUTEX: n/a phase=%s (not the Vulkan renderer)", phase);
        return;
    }
    JUSTLOG("GPUTEX: phase=%s turn=%llu frames=%llu blocks=%llu uploads=%llu upload_bytes=%llu palette_flushes=%llu reclaims=%llu"
        " last_frame_blocks=%llu last_frame_uploads=%llu",
        phase, (unsigned long long)get_gameturn(), (unsigned long long)(now.frames - before->frames),
        (unsigned long long)(now.blocks - before->blocks), (unsigned long long)(now.uploads - before->uploads),
        (unsigned long long)(now.upload_bytes - before->upload_bytes),
        (unsigned long long)(now.palette_flushes - before->palette_flushes), (unsigned long long)(now.reclaims - before->reclaims),
        (unsigned long long)now.last_frame_blocks, (unsigned long long)now.last_frame_uploads);
}

/** Logs how many terrain block draws used an HD version since `before` (0 without a store, or when the
 *  palette effect is one the HD colours can't follow). */
static void td_log_hd(const char *phase, uint64_t before)
{
    const struct TextureHdStats *hd = texture_hd_stats();
    JUSTLOG("HDTEX: phase=%s turn=%llu blocks=%llu draws=%llu", phase, (unsigned long long)get_gameturn(),
        (unsigned long long)hd->blocks, (unsigned long long)(hd->draws - before));
}

static void td_log_memory(void)
{
    const struct TextureHdStats *hd = texture_hd_stats();
    JUSTLOG("HDTEX: store blocks=%llu slots=%llu/%llu bytes=%llu load_ms=%lld", (unsigned long long)hd->blocks,
        (unsigned long long)hd->slots_with_hd, (unsigned long long)hd->slots_hashed, (unsigned long long)hd->bytes,
        (long long)(hd->load_us / 1000));
#ifdef __linux__
    char line[256];
    FILE *f = fopen("/proc/self/status", "r");
    if (f == NULL)
        return;
    while (fgets(line, sizeof(line), f) != NULL)
    {
        if (strncmp(line, "VmRSS:", 6) == 0)
        {
            line[strcspn(line, "\n")] = '\0';
            JUSTLOG("TRNMEM: after level load %s", line);
            break;
        }
    }
    fclose(f);
#else
    JUSTLOG("TRNMEM: n/a (Linux only)");
#endif
}

/** True once the audit has a frame drawn TD_SETTLE turns after the stop was placed; fails on a timeout. */
static TbBool td_frame_ready(struct ftest_texture_density__variables *vars, const char *what)
{
    const struct TerrainAuditStats *st = terrain_audit_last();
    if ((st->frame > vars->placed_frame) && (st->gameturn >= vars->placed_turn + TD_SETTLE))
        return true;
    if (get_gameturn() > vars->placed_turn + TD_TIMEOUT)
    {
        FTEST_FAIL_TEST("%s: no audited frame drawn", what);
        return true;
    }
    return false;
}

/** KFX_FTEST_TD_SHOTS=1: a screenshot of the stop (scrshots/td_<view>.png, '/' as '_'), from the next frame drawn
 *  (the camera stays): true while the step must wait for it. For the texture-resolution look check. */
static TbBool td_shot(struct ftest_texture_density__variables *vars, const char *view)
{
    if (getenv("KFX_FTEST_TD_SHOTS") == NULL)
        return false;
    if (vars->shot_pending)
    {
        vars->shot_pending = false;
        return false;
    }
    char fname[160];
    snprintf(fname, sizeof(fname), "scrshots/td_%s.png", view);
    for (char *c = fname + strlen("scrshots/"); *c != '\0'; c++)
        if (*c == '/')
            *c = '_';
    take_screenshot(fname);
    vars->shot_pending = true;
    return true;
}

static void td_mark_placed(struct ftest_texture_density__variables *vars)
{
    vars->placed = true;
    vars->placed_turn = get_gameturn();
    vars->placed_frame = terrain_audit_last()->frame;
}

static FTestActionResult td_setup(struct FTestActionArgs* const args)
{
    struct ftest_texture_density__variables* const vars = args->data;
    struct PlayerInfo *player = get_player(PLAYER0);
    if (thing_is_invalid(get_player_soul_container(PLAYER0)))
    {
        FTEST_FAIL_TEST("Player 0 has no dungeon heart");
        return FTRs_Go_To_Next_Action;
    }
    ftest_util_reveal_map(PLAYER0);
    vars->cluedo_before = settings.video_cluedo_mode;
    vars->view_mode_before = player->cameras[CamIV_Isometric].view_mode;
    vars->zoom_before = player->cameras[CamIV_Isometric].zoom;
    terrain_audit_set_enabled(1);
    if (!terrain_audit_is_enabled())
        FTEST_FAIL_TEST("Terrain audit could not be enabled");
    td_log_memory();
    vars->step = 0;
    vars->placed = false;
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult td_walk_iso(struct FTestActionArgs* const args)
{
    struct ftest_texture_density__variables* const vars = args->data;
    struct PlayerInfo *player = get_player(PLAYER0);
    size_t s = vars->step;
    if (s >= TD_ISO_STEPS)
    {
        vars->step = 0;
        vars->placed = false;
        return FTRs_Go_To_Next_Action;
    }
    const size_t zoom_i = s % TD_ISO_ZOOMS;   s /= TD_ISO_ZOOMS;
    const size_t view_i = s % TD_ISO_VIEWS;   s /= TD_ISO_VIEWS;
    const int rough = (int)(s % TD_ROUGHS);   s /= TD_ROUGHS;
    const size_t off_i = s;
    struct Camera *cam = &player->cameras[CamIV_Isometric];
    const int64_t zoom = (td_iso_zooms[zoom_i] == TD_ZOOM_DEFAULT) ? vars->zoom_before : td_iso_zooms[zoom_i];
    if (!vars->placed)
    {
        struct Thing *heart = get_player_soul_container(PLAYER0);
        struct TerrainAuditCamera pin;
        td_set_rough(rough);
        settings.video_cluedo_mode = (view_i == 1);
        pin.x = heart->mappos.x.val + td_iso_offsets[off_i][0] * STL_PER_SLB * COORD_PER_STL;
        pin.y = heart->mappos.y.val + td_iso_offsets[off_i][1] * STL_PER_SLB * COORD_PER_STL;
        pin.z = cam->mappos.z.val;
        pin.angle_x = 256;
        pin.angle_y = cam->rotation_angle_y;
        pin.angle_z = cam->rotation_angle_z;
        pin.zoom = zoom;
        pin.view_mode = PVM_IsoWibbleView;
        terrain_audit_pin_camera(&pin);
        td_mark_placed(vars);
        return FTRs_Repeat_Current_Action;
    }
    char view[96];
    snprintf(view, sizeof(view), "iso/%s/%s/stop%d", (view_i == 1) ? "cluedo" : "wibble", td_iso_zoom_names[zoom_i], (int)off_i);
    if (!td_frame_ready(vars, view))
        return FTRs_Repeat_Current_Action;
    if (!vars->shot_pending)
        td_log_density(view, zoom, rough);
    if (td_shot(vars, view))
        return FTRs_Repeat_Current_Action;
    vars->placed = false;
    vars->step++;
    return FTRs_Repeat_Current_Action;
}

static void td_apply_palette_phase(size_t phase)
{
    struct PlayerInfo *player = get_player(PLAYER0);
    const NetUserId user = get_local_user();
    PaletteSetUserViewPalette(user, VPal_Engine);
    switch (phase)
    {
    case 1: PaletteApplyPainToPlayer(player, 10); break;
    case 2: PaletteSetUserViewPalette(user, VPal_Freeze); break;
    case 3: lightning_palette_on(user); break;
    case 4: PaletteSetUserViewPalette(user, VPal_White); break;
    default: break;
    }
}

/** Overhead view at the default zoom, each palette effect in turn: the GPU texture cache's traffic per turn. */
static FTestActionResult td_walk_palettes(struct FTestActionArgs* const args)
{
    struct ftest_texture_density__variables* const vars = args->data;
    const size_t phase = vars->step;
    if (phase >= TD_PALETTE_PHASES)
    {
        PaletteSetUserViewPalette(get_local_user(), VPal_Engine);
        vars->step = 0;
        vars->placed = false;
        return FTRs_Go_To_Next_Action;
    }
    if (!vars->placed)
    {
        if (phase == 0)
        {
            // Overhead, default zoom, at the heart: a typical view, not the last stop's farthest zoom.
            struct PlayerInfo *player = get_player(PLAYER0);
            struct Camera *cam = &player->cameras[CamIV_Isometric];
            struct Thing *heart = get_player_soul_container(PLAYER0);
            struct TerrainAuditCamera pin;
            td_set_rough(1);
            settings.video_cluedo_mode = vars->cluedo_before;
            pin.x = heart->mappos.x.val;
            pin.y = heart->mappos.y.val;
            pin.z = cam->mappos.z.val;
            pin.angle_x = 256;
            pin.angle_y = cam->rotation_angle_y;
            pin.angle_z = cam->rotation_angle_z;
            pin.zoom = vars->zoom_before;
            pin.view_mode = PVM_IsoWibbleView;
            terrain_audit_pin_camera(&pin);
        }
        td_apply_palette_phase(phase);
        RendererGetWorldTextureStats(&vars->tex_before);
        vars->hd_draws_before = texture_hd_stats()->draws;
        vars->phase_start = get_gameturn();
        vars->placed = true;
        return FTRs_Repeat_Current_Action;
    }
    // Pain is applied once (one hit) and fades out turn by turn, a new palette each turn, as in play.
    td_log_gpu(td_palette_names[phase], &vars->tex_before);
    td_log_hd(td_palette_names[phase], vars->hd_draws_before);
    if ((get_gameturn() == vars->phase_start + 2) && (getenv("KFX_FTEST_TD_SHOTS") != NULL))
    {
        char shot[64];
        snprintf(shot, sizeof(shot), "palette/%s", td_palette_names[phase]);
        vars->shot_pending = false;
        td_shot(vars, shot);
        vars->shot_pending = false; // the camera stays for the phase's turns: no wait
    }
    RendererGetWorldTextureStats(&vars->tex_before);
    vars->hd_draws_before = texture_hd_stats()->draws;
    if (get_gameturn() >= vars->phase_start + TD_PALETTE_TURNS)
    {
        vars->placed = false;
        vars->step++;
    }
    return FTRs_Repeat_Current_Action;
}

static FTestActionResult td_walk_front(struct FTestActionArgs* const args)
{
    struct ftest_texture_density__variables* const vars = args->data;
    struct PlayerInfo *player = get_player(PLAYER0);
    struct Camera *cam = &player->cameras[CamIV_FrontView];
    if (vars->step >= TD_FRONT_STEPS)
    {
        set_engine_view(player, PVM_IsoWibbleView);
        vars->step = 0;
        vars->placed = false;
        return FTRs_Go_To_Next_Action;
    }
    const int64_t zoom = (td_front_zooms[vars->step] == TD_ZOOM_DEFAULT) ? FRONTVIEW_CAMERA_ZOOM_MAX / 2 : td_front_zooms[vars->step];
    if (!vars->placed)
    {
        struct Thing *heart = get_player_soul_container(PLAYER0);
        terrain_audit_pin_camera(NULL);
        td_set_rough(0);
        settings.video_cluedo_mode = vars->cluedo_before;
        set_engine_view(player, PVM_FrontView);
        cam->mappos.x.val = heart->mappos.x.val;
        cam->mappos.y.val = heart->mappos.y.val;
        cam->zoom = zoom;
        sync_local_camera(player);
        td_mark_placed(vars);
        return FTRs_Repeat_Current_Action;
    }
    char view[64];
    snprintf(view, sizeof(view), "front/zoom%d", (int)vars->step);
    if (!td_frame_ready(vars, view))
        return FTRs_Repeat_Current_Action;
    if (!vars->shot_pending)
        td_log_density(view, zoom, 0);
    if (td_shot(vars, view))
        return FTRs_Repeat_Current_Action;
    vars->placed = false;
    vars->step++;
    return FTRs_Repeat_Current_Action;
}

static FTestActionResult td_possess(struct FTestActionArgs* const args)
{
    struct ftest_texture_density__variables* const vars = args->data;
    struct PlayerInfo *player = get_player(PLAYER0);
    struct Thing *heart = get_player_soul_container(PLAYER0);
    terrain_audit_pin_camera(NULL);
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
    return FTRs_Go_To_Next_Action;
}

static TbBool td_reached_view(struct ftest_texture_density__variables *vars, int64_t view, const char *what)
{
    struct PlayerInfo *player = get_player(PLAYER0);
    if ((player->view_type == view) && (player->instance_num == PI_Unset))
        return true;
    if (get_gameturn() > vars->phase_start + TD_TIMEOUT)
    {
        FTEST_FAIL_TEST("%s: view %d not reached (view %d, instance %d)", what, (int)view, (int)player->view_type, (int)player->instance_num);
        return true;
    }
    return false;
}

static FTestActionResult td_walk_first_person(struct FTestActionArgs* const args)
{
    struct ftest_texture_density__variables* const vars = args->data;
    if (thing_is_invalid(vars->creature))
        return FTRs_Go_To_Next_Action;
    if (!td_reached_view(vars, PVT_CreatureContrl, "possession"))
        return FTRs_Repeat_Current_Action;
    if (vars->step >= TD_FP_STEPS)
        return FTRs_Go_To_Next_Action;
    const struct TdFirstPersonStop *fp = &td_fp_stops[vars->step % (TD_FP_STEPS / TD_ROUGHS)];
    const int rough = (int)(vars->step / (TD_FP_STEPS / TD_ROUGHS));
    const int64_t zoom = get_player(PLAYER0)->cameras[CamIV_FirstPerson].zoom;
    if (!vars->placed)
    {
        struct Thing *heart = get_player_soul_container(PLAYER0);
        struct TerrainAuditCamera pin;
        td_set_rough(rough);
        pin.x = heart->mappos.x.val + fp->dx2 * COORD_PER_STL / 2;
        pin.y = heart->mappos.y.val + fp->dy2 * COORD_PER_STL / 2;
        pin.z = fp->z;
        pin.angle_x = fp->yaw;
        pin.angle_y = fp->pitch;
        pin.angle_z = 0;
        pin.zoom = zoom;
        pin.view_mode = PVM_CreatureView;
        terrain_audit_pin_camera(&pin);
        td_mark_placed(vars);
        return FTRs_Repeat_Current_Action;
    }
    char view[64];
    snprintf(view, sizeof(view), "first-person/%s", fp->name);
    if (!td_frame_ready(vars, view))
        return FTRs_Repeat_Current_Action;
    if (!vars->shot_pending)
        td_log_density(view, zoom, rough);
    if (td_shot(vars, view))
        return FTRs_Repeat_Current_Action;
    vars->placed = false;
    vars->step++;
    return FTRs_Repeat_Current_Action;
}

static FTestActionResult td_leave_possession(struct FTestActionArgs* const args)
{
    struct ftest_texture_density__variables* const vars = args->data;
    terrain_audit_pin_camera(NULL);
    if (!thing_is_invalid(vars->creature))
        set_player_instance(get_player(PLAYER0), PI_DirctCtLeave, false);
    vars->phase_start = get_gameturn();
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult td_finish(struct FTestActionArgs* const args)
{
    struct ftest_texture_density__variables* const vars = args->data;
    struct PlayerInfo *player = get_player(PLAYER0);
    if (!thing_is_invalid(vars->creature) && !td_reached_view(vars, PVT_DungeonTop, "leaving possession"))
        return FTRs_Repeat_Current_Action;
    terrain_rough_settings.mode = TSM_Off;
    terrain_rough_settings.from_config = 1;
    terrain_rough_settings.params = terrain_rough_default_params;
    terrain_audit_pin_camera(NULL);
    terrain_audit_set_enabled(0);
    settings.video_cluedo_mode = vars->cluedo_before;
    player->cameras[CamIV_Isometric].view_mode = vars->view_mode_before;
    player->cameras[CamIV_Isometric].zoom = vars->zoom_before;
    sync_local_camera(player);
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_texture_density_init()
{
    ftest_append_action(td_setup, 20, &ftest_texture_density__vars);
    ftest_append_action(td_walk_iso, 0, &ftest_texture_density__vars);
    ftest_append_action(td_walk_palettes, 0, &ftest_texture_density__vars);
    ftest_append_action(td_walk_front, 0, &ftest_texture_density__vars);
    ftest_append_action(td_possess, 0, &ftest_texture_density__vars);
    ftest_append_action(td_walk_first_person, 0, &ftest_texture_density__vars);
    ftest_append_action(td_leave_possession, 0, &ftest_texture_density__vars);
    ftest_append_action(td_finish, 0, &ftest_texture_density__vars);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
