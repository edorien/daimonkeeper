#include "ftest_sprite_density.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "player_data.h"
#include "player_instances.h"
#include "thing_creature.h"
#include "thing_data.h"
#include "thing_effects.h"
#include "creature_control.h"
#include "kfx_sim_state.h"
#include "config_settings.h"
#include "config_keeperfx.h"
#include "config_creature.h"
#include "config_effects.h"
#include "config_magic.h"
#include "engine_camera.h"
#include "engine_redraw.h"
#include "engine_sprites_hd.h"
#include "keeper_sprites.h"
#include "local_camera.h"
#include "local_state.h"
#include "magic_powers.h"
#include "packet_data.h"
#include "power_hand.h"
#include "terrain_audit.h"
#include "scrcapt.h"
#include "vidfade.h"
#include "bflib_video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Turns to let a camera settle before its frame counts, and to give up on one. */
#define SD_SETTLE 2
#define SD_TIMEOUT 200
/** Turns each effect or palette stop is watched for. */
#define SD_WATCH_TURNS 24
/** Creatures placed round the heart (one of each model, as far as the level has them). */
#define SD_CREATURES 12
/** The ring they start on, map units from the heart, and the spots on it tried. */
#define SD_RING (4 * COORD_PER_STL)
#define SD_SPOTS 32

#define SD_ZOOM_DEFAULT 0
static const int64_t sd_iso_zooms[] = { CAMERA_ZOOM_MAX, SD_ZOOM_DEFAULT, CAMERA_ZOOM_MIN };
static const char *const sd_iso_zoom_names[] = { "closest", "default", "farthest" };
#define SD_ISO_STEPS (sizeof(sd_iso_zooms) / sizeof(sd_iso_zooms[0]))

static const int64_t sd_front_zooms[] = { FRONTVIEW_CAMERA_ZOOM_MAX, SD_ZOOM_DEFAULT, FRONTVIEW_CAMERA_ZOOM_MIN };
#define SD_FRONT_STEPS (sizeof(sd_front_zooms) / sizeof(sd_front_zooms[0]))

/** First-person stops round the ring (half subtiles from the heart, eye height, yaw), looking inwards. */
struct SdFirstPersonStop {
    int dx2, dy2;
    int64_t z;
    int64_t yaw;
    const char *name;
};
static const struct SdFirstPersonStop sd_fp_stops[] = {
    { -14,   0, 200,  512, "ring-w" },
    {  14,   0, 200, 1536, "ring-e" },
    {   0, -14, 200, 1024, "ring-n" },
    {   0,  14, 200,    0, "ring-s" },
};
#define SD_FP_STEPS (sizeof(sd_fp_stops) / sizeof(sd_fp_stops[0]))

/** Effect and palette stops at the default overhead zoom, creatures in view. */
static const char *const sd_effect_names[] = { "effects", "pain", "freeze", "lightning", "white" };
#define SD_EFFECT_STEPS (sizeof(sd_effect_names) / sizeof(sd_effect_names[0]))

struct ftest_sprite_density__variables
{
    ThingIndex creatures[SD_CREATURES];
    int creature_count;
    ThingIndex possessed;
    size_t step;
    TbBool placed;
    GameTurn placed_turn;
    uint64_t placed_frame;
    GameTurn phase_start;
    unsigned char cluedo_before;
    unsigned char view_mode_before;
    int64_t zoom_before;
    TbBool shot_pending;
    uint64_t kinds_seen[SprAud_KindCount];
    uint64_t flipped_seen;
    uint64_t fp_draws;
    ThingModel alpha_element;
};
struct ftest_sprite_density__variables ftest_sprite_density__vars = {
    .creature_count = 0,
    .step = 0,
    .placed = false,
};

static struct Thing *sd_creature(struct ftest_sprite_density__variables *vars, int i)
{
    if ((i < 0) || (i >= vars->creature_count))
        return INVALID_THING;
    struct Thing *thing = thing_get(vars->creatures[i]);
    return thing_is_creature(thing) ? thing : INVALID_THING;
}

/** Logs the last audited frame's sprite magnification; fails the test if `required` and it drew no sprite. */
static void sd_log_density(const char *view, int64_t zoom, TbBool required)
{
    const struct SpriteAuditStats *st = sprite_audit_last();
    JUSTLOG("SPRDEN: view=%s zoom=%lld res=%lldx%lld turn=%llu draws=%llu flipped=%llu solid=%llu remap=%llu ghost=%llu alpha=%llu"
        " area=%.0f mag_p10=%.3f mag_p50=%.3f mag_p90=%.3f mag_p99=%.3f mag_max=%.3f hd_draws=%llu hd_frames=%llu hd_kib=%llu zip_draws=%llu zip_frames=%llu zip_kib=%llu alpha_hd_draws=%llu draw_us=%lld",
        view, (long long)zoom, (long long)lbDisplay.PhysicalScreenWidth, (long long)lbDisplay.PhysicalScreenHeight,
        (unsigned long long)st->gameturn, (unsigned long long)st->draws, (unsigned long long)st->flipped,
        (unsigned long long)st->kind[SprAud_Solid], (unsigned long long)st->kind[SprAud_Remap],
        (unsigned long long)st->kind[SprAud_Ghost], (unsigned long long)st->kind[SprAud_Alpha], st->area,
        terrain_texel_percentile(st->mag_hist, 0.10), terrain_texel_percentile(st->mag_hist, 0.50),
        terrain_texel_percentile(st->mag_hist, 0.90), terrain_texel_percentile(st->mag_hist, 0.99), st->mag_max,
        (unsigned long long)sprite_hd_stats()->draws, (unsigned long long)sprite_hd_stats()->frames,
        (unsigned long long)(sprite_hd_stats()->bytes / 1024), (unsigned long long)sprite_hd_stats()->zip_draws,
        (unsigned long long)sprite_hd_stats()->zip_frames, (unsigned long long)(sprite_hd_stats()->zip_bytes / 1024),
        (unsigned long long)sprite_hd_stats()->alpha_draws, (long long)terrain_audit_last()->draw_us);
    if (required && (st->draws == 0))
        FTEST_FAIL_TEST("%s (zoom %lld): no sprite was drawn", view, (long long)zoom);
}

/** True once a frame drawn SD_SETTLE turns after the stop was placed has been audited; fails on a timeout. */
static TbBool sd_frame_ready(struct ftest_sprite_density__variables *vars, const char *what)
{
    const struct SpriteAuditStats *st = sprite_audit_last();
    if ((st->frame > vars->placed_frame) && (st->gameturn >= vars->placed_turn + SD_SETTLE))
        return true;
    if (get_gameturn() > vars->placed_turn + SD_TIMEOUT)
    {
        FTEST_FAIL_TEST("%s: no audited frame drawn", what);
        return true;
    }
    return false;
}

/** KFX_FTEST_SD_SHOTS=1: a screenshot of the stop (scrshots/sd_<view>.png, '/' as '_') from the next frame drawn:
 *  true while the step must wait for it. For the HD sprite look check. */
static TbBool sd_shot(struct ftest_sprite_density__variables *vars, const char *view)
{
    if (getenv("KFX_FTEST_SD_SHOTS") == NULL)
        return false;
    if (vars->shot_pending)
    {
        vars->shot_pending = false;
        return false;
    }
    char fname[160];
    snprintf(fname, sizeof(fname), "scrshots/sd_%s.png", view);
    for (char *c = fname + strlen("scrshots/"); *c != '\0'; c++)
        if (*c == '/')
            *c = '_';
    take_screenshot(fname);
    vars->shot_pending = true;
    return true;
}

static void sd_mark_placed(struct ftest_sprite_density__variables *vars)
{
    vars->placed = true;
    vars->placed_turn = get_gameturn();
    vars->placed_frame = sprite_audit_last()->frame;
}

static void sd_pin_overhead(int64_t zoom)
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
    pin.zoom = zoom;
    pin.view_mode = PVM_IsoWibbleView;
    terrain_audit_pin_camera(&pin);
}

static FTestActionResult sd_setup(struct FTestActionArgs* const args)
{
    struct ftest_sprite_density__variables* const vars = args->data;
    struct PlayerInfo *player = get_player(PLAYER0);
    struct Thing *heart = get_player_soul_container(PLAYER0);
    if (thing_is_invalid(heart))
    {
        FTEST_FAIL_TEST("Player 0 has no dungeon heart");
        return FTRs_Go_To_Next_Action;
    }
    ftest_util_reveal_map(PLAYER0);
    vars->cluedo_before = settings.video_cluedo_mode;
    vars->view_mode_before = player->cameras[CamIV_Isometric].view_mode;
    vars->zoom_before = player->cameras[CamIV_Isometric].zoom;
    // The camera pin needs the terrain audit; the sprite audit is what this test reads.
    terrain_audit_set_enabled(1);
    sprite_audit_set_enabled(1);
    if (!terrain_audit_is_enabled() || !sprite_audit_is_enabled())
        FTEST_FAIL_TEST("The terrain or sprite audit could not be enabled");

    // The simulation's animation frame counts are the sprite tables' (keeper_sprites.h, published as they load),
    // originals and the level's custom sprites alike.
    const int64_t mismatches = keeper_sprites_frames_mismatches();
    JUSTLOG("SPRFRAMES: %lld animation ids whose published frame count differs from the sprite tables'", (long long)mismatches);
    if (mismatches != 0)
        FTEST_FAIL_TEST("%lld animation frame counts not published", (long long)mismatches);

    // KFX_FTEST_SD_KEYS=<file>: the engine's content key of every sprite frame, for scripts/spritepack.py's check.
    const char *keys = getenv("KFX_FTEST_SD_KEYS");
    if (keys != NULL)
    {
        const int64_t n = sprite_hd_dump_keys(keys);
        JUSTLOG("SPRKEYS: %lld frames written to %s", (long long)n, keys);
        if (n <= 0)
            FTEST_FAIL_TEST("sprite_hd_dump_keys(%s) wrote no frames", keys);
    }

    // One creature of each model round the heart (model 0 is none), at subtile centres on a ring; a spot in a wall
    // (the heart's pillars) is skipped. KFX_FTEST_SD_MODELS=<name>,<name>... (or + between them) places those models instead (the mods'
    // zip creatures, DRUID, TIME_MAGE, MAIDEN, BIRD, for SPRITE_TRUE_COLOUR).
    vars->creature_count = 0;
    const int64_t models = kfx_config_state.conf.crtr_conf.model_count;
    ThingModel chosen[SD_CREATURES];
    int chosen_count = 0;
    const char *model_list = getenv("KFX_FTEST_SD_MODELS");
    if (model_list != NULL)
    {
        char names[256];
        snprintf(names, sizeof(names), "%s", model_list);
        for (char *name = strtok(names, ",+"); (name != NULL) && (chosen_count < SD_CREATURES); name = strtok(NULL, ",+"))
        {
            const int64_t m = get_id(creature_desc, name);
            if ((m <= 0) || (m >= models))
                FTEST_FAIL_TEST("KFX_FTEST_SD_MODELS: no creature model %s", name);
            chosen[chosen_count++] = (ThingModel)m;
        }
    }
    int next_model = 0;
    ThingModel model = (chosen_count > 0) ? chosen[0] : 1;
    for (int spot = 0; (spot < SD_SPOTS) && (model < models) && (vars->creature_count < SD_CREATURES); spot++)
    {
        const int64_t angle = spot * DEGREES_360 / SD_SPOTS;
        const MapSubtlCoord stl_x = (heart->mappos.x.val + SD_RING * LbCosL(angle) / 65536) / COORD_PER_STL;
        const MapSubtlCoord stl_y = (heart->mappos.y.val + SD_RING * LbSinL(angle) / 65536) / COORD_PER_STL;
        struct Thing *thing = ftest_util_create_creature(stl_x * COORD_PER_STL + COORD_PER_STL / 2, stl_y * COORD_PER_STL + COORD_PER_STL / 2,
            PLAYER0, 1, model);
        if (thing_is_invalid(thing))
            continue;
        vars->creatures[vars->creature_count++] = thing->index;
        if (chosen_count > 0)
        {
            if (++next_model >= chosen_count)
                break;
            model = chosen[next_model];
        }
        else
            model++;
    }
    if (vars->creature_count < ((chosen_count > 0) ? chosen_count : 6))
        FTEST_FAIL_TEST("Only %d creatures could be placed", vars->creature_count);
    vars->alpha_element = get_id(effectelem_desc, "EFFECTELEMENT_LAVA_FLAME_STATIONARY");
    vars->step = 0;
    vars->placed = false;
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult sd_walk_iso(struct FTestActionArgs* const args)
{
    struct ftest_sprite_density__variables* const vars = args->data;
    if (vars->step >= SD_ISO_STEPS)
    {
        vars->step = 0;
        vars->placed = false;
        return FTRs_Go_To_Next_Action;
    }
    const int64_t zoom = (sd_iso_zooms[vars->step] == SD_ZOOM_DEFAULT) ? vars->zoom_before : sd_iso_zooms[vars->step];
    if (!vars->placed)
    {
        settings.video_cluedo_mode = vars->cluedo_before;
        sd_pin_overhead(zoom);
        sd_mark_placed(vars);
        return FTRs_Repeat_Current_Action;
    }
    char view[64];
    snprintf(view, sizeof(view), "iso/%s", sd_iso_zoom_names[vars->step]);
    if (!sd_frame_ready(vars, view))
        return FTRs_Repeat_Current_Action;
    if (!vars->shot_pending)
        sd_log_density(view, zoom, true);
    if (sd_shot(vars, view))
        return FTRs_Repeat_Current_Action;
    vars->placed = false;
    vars->step++;
    return FTRs_Repeat_Current_Action;
}

static void sd_apply_palette_phase(size_t phase)
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

static void sd_cast(struct Thing *thing, const char *spell)
{
    const int64_t kind = get_id(spell_desc, spell);
    if (thing_is_invalid(thing) || (kind <= 0))
    {
        FTEST_FAIL_TEST("Can't cast %s", spell);
        return;
    }
    apply_spell_effect_to_thing(thing, (SpellKind)kind, 1, PLAYER0);
}

/** The default overhead zoom with the creatures in view: first the effects (frozen, invisible, chicken, a
 *  damage flash, the hover flash, an alpha-ramp effect element), then each palette effect. Logs each stop's
 *  last frame and fails if a kind of draw the colour gates rely on was never drawn. */
static FTestActionResult sd_walk_effects(struct FTestActionArgs* const args)
{
    struct ftest_sprite_density__variables* const vars = args->data;
    const size_t phase = vars->step;
    if (phase >= SD_EFFECT_STEPS)
    {
        PaletteSetUserViewPalette(get_local_user(), VPal_Engine);
        local_state.local_thing_under_hand = 0;
        JUSTLOG("SPRDEN: effects seen solid=%llu remap=%llu ghost=%llu alpha=%llu flipped=%llu",
            (unsigned long long)vars->kinds_seen[SprAud_Solid], (unsigned long long)vars->kinds_seen[SprAud_Remap],
            (unsigned long long)vars->kinds_seen[SprAud_Ghost], (unsigned long long)vars->kinds_seen[SprAud_Alpha],
            (unsigned long long)vars->flipped_seen);
        if ((vars->kinds_seen[SprAud_Remap] == 0) || (vars->kinds_seen[SprAud_Ghost] == 0) || (vars->kinds_seen[SprAud_Alpha] == 0)
            || (vars->flipped_seen == 0))
            FTEST_FAIL_TEST("A kind of sprite draw was never seen (remap %llu, ghost %llu, alpha %llu, flipped %llu)",
                (unsigned long long)vars->kinds_seen[SprAud_Remap], (unsigned long long)vars->kinds_seen[SprAud_Ghost],
                (unsigned long long)vars->kinds_seen[SprAud_Alpha], (unsigned long long)vars->flipped_seen);
        vars->step = 0;
        vars->placed = false;
        return FTRs_Go_To_Next_Action;
    }
    if (!vars->placed)
    {
        if (phase == 0)
        {
            sd_pin_overhead(vars->zoom_before);
            sd_cast(sd_creature(vars, 0), "SPELL_FREEZE");
            sd_cast(sd_creature(vars, 1), "SPELL_INVISIBILITY");
            sd_cast(sd_creature(vars, 2), "SPELL_CHICKEN");
        }
        sd_apply_palette_phase(phase);
        vars->phase_start = get_gameturn();
        sd_mark_placed(vars);
        return FTRs_Repeat_Current_Action;
    }
    // Every turn: a damage flash on one creature, the hover flash on another, an alpha-ramp flame by the heart.
    struct Thing *hit = sd_creature(vars, 3);
    if (!thing_is_invalid(hit))
        hit->last_turn_damaged = kfx_sim_state.play_gameturn;
    struct Thing *hover = sd_creature(vars, 4);
    if (!thing_is_invalid(hover))
        local_state.local_thing_under_hand = hover->index;
    if ((vars->alpha_element > 0) && ((get_gameturn() % 4) == 0))
    {
        struct Thing *heart = get_player_soul_container(PLAYER0);
        struct Coord3d pos = heart->mappos;
        pos.x.val += COORD_PER_STL;
        create_effect_element(&pos, vars->alpha_element, PLAYER0);
    }
    const struct SpriteAuditStats *st = sprite_audit_last();
    if (st->frame > vars->placed_frame)
    {
        for (int k = 0; k < SprAud_KindCount; k++)
            vars->kinds_seen[k] += st->kind[k];
        vars->flipped_seen += st->flipped;
        vars->placed_frame = st->frame;
    }
    if ((get_gameturn() == vars->phase_start + SD_WATCH_TURNS / 2) && (getenv("KFX_FTEST_SD_SHOTS") != NULL))
    {
        char shot[64];
        snprintf(shot, sizeof(shot), "iso/%s", sd_effect_names[phase]);
        vars->shot_pending = false;
        sd_shot(vars, shot);
        vars->shot_pending = false; // the camera stays: no wait
    }
    if (get_gameturn() >= vars->phase_start + SD_WATCH_TURNS)
    {
        char view[64];
        snprintf(view, sizeof(view), "iso/%s", sd_effect_names[phase]);
        sd_log_density(view, vars->zoom_before, true);
        vars->placed = false;
        vars->step++;
    }
    return FTRs_Repeat_Current_Action;
}

static FTestActionResult sd_walk_front(struct FTestActionArgs* const args)
{
    struct ftest_sprite_density__variables* const vars = args->data;
    struct PlayerInfo *player = get_player(PLAYER0);
    struct Camera *cam = &player->cameras[CamIV_FrontView];
    if (vars->step >= SD_FRONT_STEPS)
    {
        set_engine_view(player, PVM_IsoWibbleView);
        vars->step = 0;
        vars->placed = false;
        return FTRs_Go_To_Next_Action;
    }
    const int64_t zoom = (sd_front_zooms[vars->step] == SD_ZOOM_DEFAULT) ? FRONTVIEW_CAMERA_ZOOM_MAX / 2 : sd_front_zooms[vars->step];
    if (!vars->placed)
    {
        struct Thing *heart = get_player_soul_container(PLAYER0);
        terrain_audit_pin_camera(NULL);
        set_engine_view(player, PVM_FrontView);
        cam->mappos.x.val = heart->mappos.x.val;
        cam->mappos.y.val = heart->mappos.y.val;
        cam->zoom = zoom;
        sync_local_camera(player);
        sd_mark_placed(vars);
        return FTRs_Repeat_Current_Action;
    }
    char view[64];
    snprintf(view, sizeof(view), "front/zoom%d", (int)vars->step);
    if (!sd_frame_ready(vars, view))
        return FTRs_Repeat_Current_Action;
    if (!vars->shot_pending)
        sd_log_density(view, zoom, true);
    if (sd_shot(vars, view))
        return FTRs_Repeat_Current_Action;
    vars->placed = false;
    vars->step++;
    return FTRs_Repeat_Current_Action;
}

static TbBool sd_reached_view(struct ftest_sprite_density__variables *vars, int64_t view, const char *what)
{
    struct PlayerInfo *player = get_player(PLAYER0);
    if ((player->view_type == view) && (player->instance_num == PI_Unset))
        return true;
    if (get_gameturn() > vars->phase_start + SD_TIMEOUT)
    {
        FTEST_FAIL_TEST("%s: view %d not reached (view %d, instance %d)", what, (int)view, (int)player->view_type, (int)player->instance_num);
        return true;
    }
    return false;
}

static FTestActionResult sd_possess(struct FTestActionArgs* const args)
{
    struct ftest_sprite_density__variables* const vars = args->data;
    struct PlayerInfo *player = get_player(PLAYER0);
    struct Thing *heart = get_player_soul_container(PLAYER0);
    terrain_audit_pin_camera(NULL);
    struct Thing *thing = create_owned_special_digger(heart->mappos.x.val - 6 * COORD_PER_STL, heart->mappos.y.val, PLAYER0);
    if (thing_is_invalid(thing))
    {
        FTEST_FAIL_TEST("Failed to create a creature to possess");
        return FTRs_Go_To_Next_Action;
    }
    vars->possessed = thing->index;
    player->influenced_thing_idx = thing->index;
    player->influenced_thing_creation = thing->creation_turn;
    set_player_instance(player, PI_DirctCtrl, false);
    vars->phase_start = get_gameturn();
    vars->step = 0;
    vars->placed = false;
    return FTRs_Go_To_Next_Action;
}

/** First person, looking at the ring from each side. A stop may see nothing (the creatures wander); all of them
 *  seeing nothing fails. */
static FTestActionResult sd_walk_first_person(struct FTestActionArgs* const args)
{
    struct ftest_sprite_density__variables* const vars = args->data;
    if (vars->possessed == 0)
        return FTRs_Go_To_Next_Action;
    if (!sd_reached_view(vars, PVT_CreatureContrl, "possession"))
        return FTRs_Repeat_Current_Action;
    if (vars->step >= SD_FP_STEPS)
    {
        if (vars->fp_draws == 0)
            FTEST_FAIL_TEST("first person: no stop saw a sprite");
        return FTRs_Go_To_Next_Action;
    }
    const struct SdFirstPersonStop *fp = &sd_fp_stops[vars->step];
    const int64_t zoom = get_player(PLAYER0)->cameras[CamIV_FirstPerson].zoom;
    if (!vars->placed)
    {
        struct Thing *heart = get_player_soul_container(PLAYER0);
        struct TerrainAuditCamera pin;
        pin.x = heart->mappos.x.val + fp->dx2 * COORD_PER_STL / 2;
        pin.y = heart->mappos.y.val + fp->dy2 * COORD_PER_STL / 2;
        pin.z = fp->z;
        pin.angle_x = fp->yaw;
        pin.angle_y = 0;
        pin.angle_z = 0;
        pin.zoom = zoom;
        pin.view_mode = PVM_CreatureView;
        terrain_audit_pin_camera(&pin);
        sd_mark_placed(vars);
        return FTRs_Repeat_Current_Action;
    }
    char view[64];
    snprintf(view, sizeof(view), "first-person/%s", fp->name);
    if (!sd_frame_ready(vars, view))
        return FTRs_Repeat_Current_Action;
    if (!vars->shot_pending)
    {
        sd_log_density(view, zoom, false);
        vars->fp_draws += sprite_audit_last()->draws;
    }
    if (sd_shot(vars, view))
        return FTRs_Repeat_Current_Action;
    vars->placed = false;
    vars->step++;
    return FTRs_Repeat_Current_Action;
}

static FTestActionResult sd_leave_possession(struct FTestActionArgs* const args)
{
    struct ftest_sprite_density__variables* const vars = args->data;
    terrain_audit_pin_camera(NULL);
    if (vars->possessed != 0)
        set_player_instance(get_player(PLAYER0), PI_DirctCtLeave, false);
    vars->phase_start = get_gameturn();
    return FTRs_Go_To_Next_Action;
}

/** A creature in the power hand: drawn by draw_power_hand() at the pointer, after the world. Headless, the pointer
 *  may sit where the hand isn't drawn; the stop then logs no draws (not a failure: the hand's scale is the UI's). */
static FTestActionResult sd_hand(struct FTestActionArgs* const args)
{
    struct ftest_sprite_density__variables* const vars = args->data;
    if ((vars->possessed != 0) && !sd_reached_view(vars, PVT_DungeonTop, "leaving possession"))
        return FTRs_Repeat_Current_Action;
    if (!vars->placed)
    {
        struct Thing *thing = sd_creature(vars, 5);
        if (thing_is_invalid(thing))
            return FTRs_Go_To_Next_Action;
        sd_pin_overhead(vars->zoom_before);
        ftest_util_center_cursor_over_dungeon_view();
        if (magic_use_available_power_on_thing(PLAYER0, PwrK_HAND, 0, thing->mappos.x.stl.num, thing->mappos.y.stl.num, thing, PwMod_Default) != Lb_SUCCESS)
        {
            JUSTLOG("SPRDEN: view=hand n/a (the hand couldn't pick up a creature)");
            return FTRs_Go_To_Next_Action;
        }
        sd_mark_placed(vars);
        return FTRs_Repeat_Current_Action;
    }
    if (!sd_frame_ready(vars, "hand"))
        return FTRs_Repeat_Current_Action;
    if (!vars->shot_pending)
        sd_log_density("hand", vars->zoom_before, false);
    if (sd_shot(vars, "hand"))
        return FTRs_Repeat_Current_Action;
    struct Thing *thing = sd_creature(vars, 5);
    if (!thing_is_invalid(thing))
    {
        struct Thing *heart = get_player_soul_container(PLAYER0);
        dump_first_held_thing_on_map(PLAYER0, heart->mappos.x.stl.num + 3, heart->mappos.y.stl.num, 1);
    }
    vars->placed = false;
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult sd_finish(struct FTestActionArgs* const args)
{
    struct ftest_sprite_density__variables* const vars = args->data;
    struct PlayerInfo *player = get_player(PLAYER0);
    terrain_audit_pin_camera(NULL);
    terrain_audit_set_enabled(0);
    sprite_audit_set_enabled(0);
    settings.video_cluedo_mode = vars->cluedo_before;
    player->cameras[CamIV_Isometric].view_mode = vars->view_mode_before;
    player->cameras[CamIV_Isometric].zoom = vars->zoom_before;
    sync_local_camera(player);
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_sprite_density_init()
{
    ftest_append_action(sd_setup, 20, &ftest_sprite_density__vars);
    ftest_append_action(sd_walk_iso, 0, &ftest_sprite_density__vars);
    ftest_append_action(sd_walk_effects, 0, &ftest_sprite_density__vars);
    ftest_append_action(sd_walk_front, 0, &ftest_sprite_density__vars);
    ftest_append_action(sd_possess, 0, &ftest_sprite_density__vars);
    ftest_append_action(sd_walk_first_person, 0, &ftest_sprite_density__vars);
    ftest_append_action(sd_leave_possession, 0, &ftest_sprite_density__vars);
    ftest_append_action(sd_hand, 0, &ftest_sprite_density__vars);
    ftest_append_action(sd_finish, 0, &ftest_sprite_density__vars);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
