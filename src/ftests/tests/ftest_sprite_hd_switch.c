#include "ftest_sprite_hd_switch.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "player_data.h"
#include "player_instances.h"
#include "thing_data.h"
#include "thing_creature.h"
#include "kfx_sim_state.h"
#include "config.h"
#include "config_keeperfx.h"
#include "config_creature.h"
#include "engine_render.h"
#include "engine_sprites_hd.h"
#include "keeper_sprites.h"
#include "engine_arrays.h"
#include "terrain_audit.h"
#include "scrcapt.h"
#include "game_session_loop.h"
#include "bflib_fileio.h"
#include "bflib_dernc.h"
#include "bflib_planar.h"
#include "kfx_config_state.h"
#include "bflib_math.h"
#include "bflib_video.h"
#include "renderer/RendererManager.h"

#include <spng.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHS_PACK_NAME "ftest_sprite_switch"
#define SHS_SCALE 2
/** The pack's frames at most. */
#define SHS_MAX_FRAMES 2048
/** Frames to let a state settle once its pack is decoded, and updates to give up after. */
#define SHS_SETTLE 3
#define SHS_TIMEOUT 600

enum ShsState { SHS_Base0, SHS_Pack, SHS_Base1, SHS_TrueColour, SHS_Base2, SHS_Filter, SHS_Base3, SHS_FilterAlone, SHS_Base4,
    SHS_States };
static const char *const shs_state_names[] = { "baseline", "pack", "baseline", "true colour", "baseline", "pack+bilinear", "baseline",
    "bilinear", "baseline" };
enum ShsStop { SHS_FirstPerson, SHS_Overhead, SHS_Stops };
static const char *const shs_stop_names[] = { "first person", "overhead closest" };

struct ftest_sprite_hd_switch__variables
{
    size_t state;
    TbBool placed;
    uint64_t placed_frames;
    uint64_t placed_updates;
    uint64_t phase_start;
    ThingIndex possessed;
    TbBool frame_hash_before;
    TbBool suspended_before;
    char pack_before[64];
    int64_t true_colour_before;
    int filter_before;
    uint64_t hash[SHS_Stops][SHS_States];
    uint64_t draws[SHS_Stops][SHS_States];
    uint64_t zip_draws[SHS_Stops][SHS_States];
    char pack_dir[512];
    int64_t frames_written;
    TbBool skipped;
};
struct ftest_sprite_hd_switch__variables ftest_sprite_hd_switch__vars = {
    .state = 0,
    .placed = false,
};

static TbBool shs_write_file(const char *fname, const void *data, size_t len)
{
    create_directory_for_file(fname);
    FILE *f = fopen(fname, "wb");
    if (f == NULL)
        return false;
    const TbBool ok = (fwrite(data, 1, len, f) == len);
    fclose(f);
    return ok;
}

static TbBool shs_write_png(const char *fname, const unsigned char *rgba, uint32_t w, uint32_t h)
{
    spng_ctx *ctx = spng_ctx_new(SPNG_CTX_ENCODER);
    if (ctx == NULL)
        return false;
    struct spng_ihdr ihdr;
    memset(&ihdr, 0, sizeof(ihdr));
    ihdr.width = w;
    ihdr.height = h;
    ihdr.bit_depth = 8;
    ihdr.color_type = SPNG_COLOR_TYPE_TRUECOLOR_ALPHA;
    spng_set_option(ctx, SPNG_ENCODE_TO_BUFFER, 1);
    spng_set_ihdr(ctx, &ihdr);
    size_t len = 0;
    int err = 0;
    void *png = NULL;
    if (spng_encode_image(ctx, rgba, (size_t)w * h * 4, SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE) == 0)
        png = spng_get_png_buffer(ctx, &len, &err);
    const TbBool ok = (png != NULL) && shs_write_file(fname, png, len);
    free(png);
    spng_ctx_free(ctx);
    return ok;
}

/** The pack: every frame of the animations the level's things show now (original frames only), x2, colours
 *  inverted; one canvas a frame (the frame's crop alone), one filelist. */
static TbBool shs_write_pack(struct ftest_sprite_hd_switch__variables *vars)
{
    unsigned char palette[768];
    if (LbFileLoadAtMax(prepare_file_fmtpath(FGrp_StdData, "palette.dat"), palette, sizeof(palette)) != (int64_t)sizeof(palette))
        return false;
    asset_pack_dir(SHS_PACK_NAME, "sprites", vars->pack_dir, sizeof(vars->pack_dir));
    // The table frames to cover.
    static unsigned char wanted[SHS_MAX_FRAMES * 4];
    size_t *frames = (size_t *)calloc(SHS_MAX_FRAMES, sizeof(size_t));
    if (frames == NULL)
        return false;
    int64_t count = 0;
    memset(wanted, 0, sizeof(wanted));
    for (ThingIndex i = 1; i < THINGS_COUNT; i++)
    {
        struct Thing *thing = thing_get(i);
        if (!thing_exists(thing))
            continue;
        // The animation and what the view draws for it (first person has its own animations).
        const int64_t anims[2] = { thing->anim_sprite, get_render_animation_sprite(thing->anim_sprite) };
        for (int a = 0; a < 2; a++)
        {
            const int64_t anim = anims[a];
            if ((anim <= 0) || (anim >= CREATURE_FRAMELIST_LENGTH))
                continue;
            const struct KeeperSprite *ks = keepersprite_array(anim);
            if (ks == NULL)
                continue;
            const size_t first = keepersprite_index(anim);
            const size_t n = (size_t)ks->FramesCount * ((ks->Rotable == 2) ? 5 : 1);
            for (size_t k = 0; (k < n) && (count < SHS_MAX_FRAMES); k++)
            {
                const size_t t = first + k;
                if ((t + 1 >= creature_table_length) || (t >= sizeof(wanted) * 8) || (wanted[t / 8] & (1 << (t % 8))))
                    continue;
                wanted[t / 8] |= (unsigned char)(1 << (t % 8));
                frames[count++] = t;
            }
        }
    }
    size_t list_cap = 32 * (size_t)(count + 1), list_len = 0;
    char *list = (char *)malloc(list_cap);
    unsigned char *keys = (unsigned char *)calloc((size_t)count + 1, 16);
    unsigned char *rle = NULL;
    size_t rle_cap = 0;
    TbBool ok = (list != NULL) && (keys != NULL);
    if (ok)
        list_len += (size_t)snprintf(list + list_len, list_cap - list_len, "ftest_sprite_switch 0 %lld 0 0\n", (long long)count);
    char fname[700];
    for (int64_t n = 0; ok && (n < count); n++)
    {
        const struct KeeperSprite *ks = &creature_table[frames[n]];
        const size_t len = creature_table[frames[n] + 1].DataOffset - ks->DataOffset;
        const int64_t w = ks->SWidth, h = ks->SHeight;
        list_len += (size_t)snprintf(list + list_len, list_cap - list_len, "f%04lld.png\n", (long long)n);
        if ((w <= 0) || (h <= 0) || (creature_table[frames[n] + 1].DataOffset <= ks->DataOffset))
            continue; // an empty frame: key 0, the line skipped
        if (len > rle_cap)
        {
            unsigned char *grown = (unsigned char *)realloc(rle, len);
            if (grown == NULL) { ok = false; break; }
            rle = grown;
            rle_cap = len;
        }
        if ((LbFileSeek(jty_file_handle, (int64_t)ks->DataOffset, Lb_FILE_SEEK_BEGINNING) < 0) ||
            (LbFileRead(jty_file_handle, rle, len) != (int64_t)len))
        {
            ok = false;
            break;
        }
        const uint64_t key = sprite_hd_frame_key(rle, len, w, h);
        unsigned char *rec = keys + n * 16;
        for (int k = 0; k < 8; k++)
            rec[k] = (unsigned char)((key >> (8 * k)) & 0xFF);
        rec[12] = (unsigned char)(w & 0xFF); rec[13] = (unsigned char)(w >> 8);
        rec[14] = (unsigned char)(h & 0xFF); rec[15] = (unsigned char)(h >> 8);
        // The frame's indices, x2, inverted colours; transparent where the original is.
        const int64_t W = w * SHS_SCALE, H = h * SHS_SCALE;
        unsigned char *rgba = (unsigned char *)calloc((size_t)(W * H), 4);
        if (rgba == NULL) { ok = false; break; }
        size_t p = 0;
        for (int64_t y = 0; y < h; y++)
        {
            int64_t x = 0;
            for (;;)
            {
                const int64_t c = (signed char)rle[p++];
                if (c == 0)
                    break;
                if (c < 0) { x -= c; continue; }
                for (int64_t k = 0; k < c; k++, x++)
                {
                    const unsigned char idx = rle[p + k];
                    if ((x < 0) || (x >= w) || (idx == 0))
                        continue;
                    for (int64_t dy = 0; dy < SHS_SCALE; dy++)
                        for (int64_t dx = 0; dx < SHS_SCALE; dx++)
                        {
                            unsigned char *o = rgba + ((size_t)((y * SHS_SCALE + dy) * W + x * SHS_SCALE + dx)) * 4;
                            for (int ch = 0; ch < 3; ch++)
                                o[ch] = (unsigned char)(255 - chan6_to_8(palette[3 * idx + ch]));
                            o[3] = 255;
                        }
                }
                p += (size_t)c;
            }
        }
        snprintf(fname, sizeof(fname), "%s/f/f%04lld.png", vars->pack_dir, (long long)n);
        ok = shs_write_png(fname, rgba, (uint32_t)W, (uint32_t)H);
        free(rgba);
    }
    if (ok)
    {
        const char toml[] = "name = \"ftest_sprite_hd_switch\"\nscale = 2\n";
        const char animlist[] = "f/list.txt\n";
        snprintf(fname, sizeof(fname), "%s/pack.toml", vars->pack_dir);
        ok = shs_write_file(fname, toml, sizeof(toml) - 1);
        snprintf(fname, sizeof(fname), "%s/animlist.txt", vars->pack_dir);
        ok = ok && shs_write_file(fname, animlist, sizeof(animlist) - 1);
        snprintf(fname, sizeof(fname), "%s/f/list.txt", vars->pack_dir);
        ok = ok && shs_write_file(fname, list, list_len);
        snprintf(fname, sizeof(fname), "%s/keys/animlist.idx", vars->pack_dir);
        ok = ok && shs_write_file(fname, keys, (size_t)count * 16);
    }
    vars->frames_written = count;
    free(frames);
    free(list);
    free(keys);
    free(rle);
    return ok && (count > 0);
}

static void shs_rmdir(const char *dir)
{
#ifdef _WIN32
    _rmdir(dir);
#else
    rmdir(dir);
#endif
}

static void shs_remove_pack(struct ftest_sprite_hd_switch__variables *vars)
{
    char fname[700];
    for (int64_t n = 0; n < vars->frames_written; n++)
    {
        snprintf(fname, sizeof(fname), "%s/f/f%04lld.png", vars->pack_dir, (long long)n);
        remove(fname);
    }
    const char *files[] = { "f/list.txt", "keys/animlist.idx", "pack.toml", "animlist.txt" };
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++)
    {
        snprintf(fname, sizeof(fname), "%s/%s", vars->pack_dir, files[i]);
        remove(fname);
    }
    snprintf(fname, sizeof(fname), "%s/f", vars->pack_dir);
    shs_rmdir(fname);
    snprintf(fname, sizeof(fname), "%s/keys", vars->pack_dir);
    shs_rmdir(fname);
    shs_rmdir(vars->pack_dir);
    // and the asset pack's own folder, which holds only that
    snprintf(fname, sizeof(fname), "%s", vars->pack_dir);
    char *slash = strrchr(fname, '/');
    if (slash != NULL)
    {
        *slash = '\0';
        shs_rmdir(fname);
    }
}

static void shs_apply_state(const struct ftest_sprite_hd_switch__variables *vars, size_t state)
{
    const TbBool pack = (state == SHS_Pack) || (state == SHS_Filter);
    snprintf(keeperfx_ui_config.asset_pack, sizeof(keeperfx_ui_config.asset_pack), "%s", pack ? SHS_PACK_NAME : vars->pack_before);
    keeperfx_ui_config.sprite_true_colour = (state == SHS_TrueColour) ? 1 : vars->true_colour_before;
    RendererSetTextureFilter(((state == SHS_Filter) || (state == SHS_FilterAlone)) ? RENDERER_TEXFILTER_BILINEAR : vars->filter_before);
}

static FTestActionResult shs_setup(struct FTestActionArgs* const args)
{
    struct ftest_sprite_hd_switch__variables* const vars = args->data;
    struct Thing *heart = get_player_soul_container(PLAYER0);
    if (thing_is_invalid(heart))
    {
        FTEST_FAIL_TEST("Player 0 has no dungeon heart");
        return FTRs_Go_To_Next_Action;
    }
    if (getenv("KFX_SPRITE_HD_SYNTH") != NULL)
    {
        // The synthetic store stands in for every pack: nothing here would show. Not this test's case.
        JUSTLOG("SPRSWITCH: KFX_SPRITE_HD_SYNTH is set: the synthetic store replaces packs; skipped");
        vars->state = SHS_States;
        vars->skipped = true;
        return FTRs_Go_To_Next_Action;
    }
    ftest_util_reveal_map(PLAYER0);
    sprite_audit_set_enabled(1);
    terrain_audit_set_enabled(1); // its camera pin holds the stops' views
    vars->frame_hash_before = start_params.frame_hash;
    start_params.frame_hash = true;
    vars->suspended_before = kfx_sim_state.simulation_suspended;
    snprintf(vars->pack_before, sizeof(vars->pack_before), "%s", keeperfx_ui_config.asset_pack);
    vars->true_colour_before = keeperfx_ui_config.sprite_true_colour;
    vars->filter_before = RendererGetTextureFilter();
    // The imp to possess for the first-person stop.
    struct Thing *imp = create_owned_special_digger(heart->mappos.x.val - 6 * COORD_PER_STL, heart->mappos.y.val, PLAYER0);
    if (thing_is_invalid(imp))
    {
        FTEST_FAIL_TEST("Couldn't create a creature to possess");
        return FTRs_Go_To_Next_Action;
    }
    vars->possessed = imp->index;
    struct PlayerInfo *player = get_player(PLAYER0);
    player->influenced_thing_idx = imp->index;
    player->influenced_thing_creation = imp->creation_turn;
    set_player_instance(player, PI_DirctCtrl, false);
    vars->phase_start = get_gameturn();
    vars->state = 0;
    vars->placed = false;
    return FTRs_Go_To_Next_Action;
}

/** A creature of `model` on the first free subtile of a ring round (x, y) (not in a wall or the heart's pillars),
 *  starting `first` spots round. */
static TbBool shs_place(const char *model, MapCoord x, MapCoord y, int first)
{
    const int64_t m = get_id(creature_desc, model);
    for (int ring = 1; (ring <= 3) && (m > 0); ring++)
        for (int k = 0; k < 8; k++)
        {
            const int64_t angle = ((first * 3 + k) % 8) * DEGREES_360 / 8;
            const MapSubtlCoord stl_x = (x + ring * 2 * COORD_PER_STL * LbCosL(angle) / 65536) / COORD_PER_STL;
            const MapSubtlCoord stl_y = (y + ring * 2 * COORD_PER_STL * LbSinL(angle) / 65536) / COORD_PER_STL;
            if (!thing_is_invalid(ftest_util_create_creature(stl_x * COORD_PER_STL + COORD_PER_STL / 2,
                stl_y * COORD_PER_STL + COORD_PER_STL / 2, PLAYER0, 1, (ThingModel)m)))
                return true;
        }
    return false;
}

/** Waits for the player's view (the simulation running), then suspends it, puts the stop's creatures in place and
 *  writes the pack from what is near the heart. */
static FTestActionResult shs_reach(struct FTestActionArgs* const args, enum PlayerViewType view, const char *what)
{
    struct ftest_sprite_hd_switch__variables* const vars = args->data;
    if (vars->skipped)
        return FTRs_Go_To_Next_Action;
    struct PlayerInfo *player = get_player(PLAYER0);
    if ((player->view_type != view) || (player->instance_num != 0))
    {
        if (get_gameturn() > vars->phase_start + SHS_TIMEOUT)
        {
            FTEST_FAIL_TEST("%s: view %d not reached", what, (int)view);
            return FTRs_Go_To_Next_Action;
        }
        return FTRs_Repeat_Current_Action;
    }
    // Stood still from here: a druid (a mod's zip sprite) and a wizard in the stop's view, and the pack made from
    // what the things show now (their animations changed since the last stop).
    kfx_sim_state.simulation_suspended = true;
    struct Thing *heart = get_player_soul_container(PLAYER0);
    // A druid and a wizard where the stop looks: overhead by the heart, in first person ahead of the camera.
    MapCoord cx = heart->mappos.x.val, cy = heart->mappos.y.val;
    struct Thing *imp = thing_get(vars->possessed);
    if ((view == PVT_CreatureContrl) && thing_exists(imp))
    {
        // In first person: 3 subtiles ahead of the possessed imp, which the view follows.
        cx = imp->mappos.x.val + 3 * COORD_PER_STL * LbSinL(imp->move_angle_xy) / 65536;
        cy = imp->mappos.y.val - 3 * COORD_PER_STL * LbCosL(imp->move_angle_xy) / 65536;
    }
    const char *const models[] = { "DRUID", "WIZARD" };
    for (size_t m = 0; m < sizeof(models) / sizeof(models[0]); m++)
        if (!shs_place(models[m], cx, cy, (int)m))
            FTEST_FAIL_TEST("Couldn't place a %s", models[m]);
    if (vars->frames_written > 0)
        shs_remove_pack(vars);
    if (!shs_write_pack(vars))
        FTEST_FAIL_TEST("Couldn't write the test pack to %s", vars->pack_dir);
    vars->state = 0;
    vars->placed = false;
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult shs_reach_possession(struct FTestActionArgs* const args)
{
    return shs_reach(args, PVT_CreatureContrl, "possession");
}

static FTestActionResult shs_reach_overhead(struct FTestActionArgs* const args)
{
    return shs_reach(args, PVT_DungeonTop, "leaving possession");
}

static void shs_pin(enum ShsStop stop)
{
    if (stop == SHS_FirstPerson)
    {
        // The possessed imp's own view: it doesn't move while the simulation is suspended.
        terrain_audit_pin_camera(NULL);
        return;
    }
    struct PlayerInfo *player = get_player(PLAYER0);
    struct Thing *heart = get_player_soul_container(PLAYER0);
    struct Camera *cam = &player->cameras[CamIV_Isometric];
    struct TerrainAuditCamera pin;
    pin.x = heart->mappos.x.val;
    pin.y = heart->mappos.y.val;
    pin.z = cam->mappos.z.val;
    pin.angle_x = 256;
    pin.angle_y = cam->rotation_angle_y;
    pin.angle_z = cam->rotation_angle_z;
    pin.zoom = CAMERA_ZOOM_MAX;
    pin.view_mode = PVM_IsoWibbleView;
    terrain_audit_pin_camera(&pin);
}

/** Each state at the stop: apply it, wait for its pack to be decoded, settle, then the frame's hash. */
static FTestActionResult shs_walk(struct FTestActionArgs* const args, enum ShsStop stop)
{
    struct ftest_sprite_hd_switch__variables* const vars = args->data;
    if (vars->state >= SHS_States)
        return FTRs_Go_To_Next_Action;
    const size_t state = vars->state;
    if (!vars->placed)
    {
        shs_apply_state(vars, state);
        shs_pin(stop);
        uint64_t unused = 0;
        vars->placed_frames = 0;
        game_last_frame_hash(&unused, &vars->placed_frames);
        vars->placed_updates = 0;
        vars->placed = true;
        return FTRs_Repeat_Current_Action;
    }
    const struct SpriteHdStats *st = sprite_hd_stats();
    uint64_t hash = 0, frames = 0;
    const TbBool decoding = (st->entries > 0) && (st->frames + st->failed < st->entries);
    if (decoding)
        game_last_frame_hash(&hash, &vars->placed_frames); // count the settle from when it is all decoded
    if (decoding || !game_last_frame_hash(&hash, &frames) || (frames < vars->placed_frames + SHS_SETTLE))
    {
        if (++vars->placed_updates > SHS_TIMEOUT)
        {
            FTEST_FAIL_TEST("%s/%s: no frame drawn", shs_stop_names[stop], shs_state_names[state]);
            return FTRs_Go_To_Next_Action;
        }
        return FTRs_Repeat_Current_Action;
    }
    vars->hash[stop][state] = hash;
    vars->draws[stop][state] = st->draws;
    vars->zip_draws[stop][state] = st->zip_draws;
    JUSTLOG("SPRSWITCH: stop=%s state=%s frame=%" PRIu64 " hash=%016" PRIx64 " sprite_draws=%" PRIu64 " entries=%" PRIu64 " hd_draws=%" PRIu64
        " zip_frames=%" PRIu64 " zip_draws=%" PRIu64, shs_stop_names[stop], shs_state_names[state], frames, hash, sprite_audit_last()->draws,
        st->entries, st->draws, st->zip_frames, st->zip_draws);
    if (getenv("KFX_FTEST_SHS_SHOTS") != NULL)
    {
        char fname[96];
        snprintf(fname, sizeof(fname), "scrshots/shs_%d_%d.png", (int)stop, (int)state);
        take_screenshot(fname);
    }
    vars->placed = false;
    vars->state++;
    return FTRs_Repeat_Current_Action;
}

static FTestActionResult shs_walk_first_person(struct FTestActionArgs* const args)
{
    return shs_walk(args, SHS_FirstPerson);
}

static FTestActionResult shs_walk_overhead(struct FTestActionArgs* const args)
{
    return shs_walk(args, SHS_Overhead);
}

static FTestActionResult shs_leave_possession(struct FTestActionArgs* const args)
{
    struct ftest_sprite_hd_switch__variables* const vars = args->data;
    if (vars->skipped)
        return FTRs_Go_To_Next_Action;
    shs_apply_state(vars, SHS_Base0);
    terrain_audit_pin_camera(NULL);
    kfx_sim_state.simulation_suspended = false;
    set_player_instance(get_player(PLAYER0), PI_DirctCtLeave, false);
    vars->phase_start = get_gameturn();
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult shs_check(struct FTestActionArgs* const args)
{
    struct ftest_sprite_hd_switch__variables* const vars = args->data;
    if (vars->skipped)
        return FTRs_Go_To_Next_Action;
    const size_t bases[] = { SHS_Base1, SHS_Base2, SHS_Base3, SHS_Base4 };
    for (size_t s = 0; s < SHS_Stops; s++)
    {
        // Switching back restores every frame exactly.
        for (size_t b = 0; b < sizeof(bases) / sizeof(bases[0]); b++)
            if (vars->hash[s][bases[b]] != vars->hash[s][SHS_Base0])
                FTEST_FAIL_TEST("%s: the baseline after the %s is %016" PRIx64 ", not %016" PRIx64, shs_stop_names[s],
                    shs_state_names[bases[b] - 1], vars->hash[s][bases[b]], vars->hash[s][SHS_Base0]);
        // The pack shows.
        if (vars->hash[s][SHS_Pack] == vars->hash[s][SHS_Base0])
            FTEST_FAIL_TEST("%s: the HD pack changed nothing", shs_stop_names[s]);
        // True colour: the druid in view.
        if (vars->hash[s][SHS_TrueColour] == vars->hash[s][SHS_Base0])
            FTEST_FAIL_TEST("%s: SPRITE_TRUE_COLOUR changed nothing", shs_stop_names[s]);
        // Bilinear: the HD sprites still drawn under it (the pack changes the filtered frame too). Under Vulkan it
        // filters the terrain as well, so the sprites' own filtering is gpu3d_verify's frame 24; the Software renderer
        // draws everything nearest.
        if (vars->hash[s][SHS_Filter] == vars->hash[s][SHS_FilterAlone])
            FTEST_FAIL_TEST("%s: the HD pack changed nothing under Bilinear", shs_stop_names[s]);
        if ((RendererGetActiveType() != RENDERER_GPU3D) &&
            ((vars->hash[s][SHS_Filter] != vars->hash[s][SHS_Pack]) || (vars->hash[s][SHS_FilterAlone] != vars->hash[s][SHS_Base0])))
            FTEST_FAIL_TEST("%s: TEXTURE_FILTER changed the Software renderer's frame", shs_stop_names[s]);
    }
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult shs_finish(struct FTestActionArgs* const args)
{
    struct ftest_sprite_hd_switch__variables* const vars = args->data;
    if (vars->skipped)
        return FTRs_Go_To_Next_Action;
    shs_apply_state(vars, SHS_Base0);
    terrain_audit_pin_camera(NULL);
    sprite_audit_set_enabled(0);
    terrain_audit_set_enabled(0);
    start_params.frame_hash = vars->frame_hash_before;
    kfx_sim_state.simulation_suspended = vars->suspended_before;
    shs_remove_pack(vars);
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_sprite_hd_switch_init()
{
    ftest_append_action(shs_setup, 20, &ftest_sprite_hd_switch__vars);
    ftest_append_action(shs_reach_possession, 0, &ftest_sprite_hd_switch__vars);
    ftest_append_action(shs_walk_first_person, 0, &ftest_sprite_hd_switch__vars);
    ftest_append_action(shs_leave_possession, 0, &ftest_sprite_hd_switch__vars);
    ftest_append_action(shs_reach_overhead, 0, &ftest_sprite_hd_switch__vars);
    ftest_append_action(shs_walk_overhead, 0, &ftest_sprite_hd_switch__vars);
    ftest_append_action(shs_check, 0, &ftest_sprite_hd_switch__vars);
    ftest_append_action(shs_finish, 0, &ftest_sprite_hd_switch__vars);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
