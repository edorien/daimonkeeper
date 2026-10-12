#include "ftest_texture_hd_switch.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "player_data.h"
#include "thing_data.h"
#include "kfx_sim_state.h"
#include "config.h"
#include "config_keeperfx.h"
#include "engine_textures.h"
#include "engine_textures_hd.h"
#include "terrain_audit.h"
#include "terrain_rough.h"
#include "game_session_loop.h"
#include "bflib_fileio.h"
#include "bflib_dernc.h"
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

#define THS_PACK_NAME "ftest_hd_switch"
/** Frames to let a setting and camera settle before one counts, and updates to give up after. The simulation is
 *  suspended meanwhile (creatures, animation and the turn stand still), so the same view draws the same frame. */
#define THS_SETTLE 3
#define THS_TIMEOUT 400

/** The stops: the closest and the farthest overhead zoom, at the heart. */
static const int64_t ths_zooms[] = { CAMERA_ZOOM_MAX, CAMERA_ZOOM_MIN };
static const char *const ths_zoom_names[] = { "closest", "farthest" };
#define THS_STOPS 2

/** The states, in order: each option on, then the baseline again. */
enum ThsState { THS_Base0, THS_Pack, THS_Base1, THS_Mipmaps, THS_Base2, THS_Filter, THS_Base3, THS_States };
static const char *const ths_state_names[] = { "baseline", "pack", "baseline", "mipmaps", "baseline", "bilinear", "baseline" };

struct ftest_texture_hd_switch__variables
{
    size_t step;
    TbBool placed;
    uint64_t placed_frames;  ///< frames hashed when the stop was placed
    uint64_t placed_updates; ///< ftest updates since, to give up on a stop
    TbBool frame_hash_before;
    TbBool suspended_before;
    char pack_before[64];
    int filter_before;
    TbBool mipmaps_before;
    uint64_t hash[THS_States][THS_STOPS];
    char pack_dir[512];
};
struct ftest_texture_hd_switch__variables ftest_texture_hd_switch__vars = {
    .step = 0,
    .placed = false,
};

static void ths_path(char *out, size_t size, const char *dir, const char *file)
{
    snprintf(out, size, "%s/%s", dir, file);
}

static TbBool ths_write_file(const char *fname, const void *data, size_t len)
{
    create_directory_for_file(fname);
    FILE *f = fopen(fname, "wb");
    if (f == NULL)
        return false;
    const TbBool ok = (fwrite(data, 1, len, f) == len);
    fclose(f);
    return ok;
}

/** The pack: the level's own tmapa file (variation 0) at 64 texels, each block's colours inverted. */
static TbBool ths_write_pack(struct ftest_texture_hd_switch__variables *vars)
{
    const struct TextureMapFile *file = &texture_map_files[0][0];
    if (!file->loaded)
        return false;
    unsigned char palette[768];
    const char *pal_name = prepare_file_fmtpath(FGrp_StdData, "palette.dat");
    if (LbFileLoadAtMax(pal_name, palette, sizeof(palette)) != (int64_t)sizeof(palette))
        return false;
    asset_pack_dir(THS_PACK_NAME, "textures", vars->pack_dir, sizeof(vars->pack_dir));
    char fname[600];
    ths_path(fname, sizeof(fname), vars->pack_dir, "pack.toml");
    const char toml[] = "name = \"ftest_texture_hd_switch\"\nsize = 64\n";
    if (!ths_write_file(fname, toml, sizeof(toml) - 1))
        return false;
    // The sheet: 8 blocks of 64 to a row, as the atlas has them; the filelist names each; the keys.
    const int side = 64, cols = 8, rows = TEXTURE_BLOCKS_STAT_COUNT_A / 8;
    const size_t w = (size_t)side * cols, h = (size_t)side * rows;
    unsigned char *rgba = (unsigned char *)calloc(w * h, 4);
    uint64_t *keys = (uint64_t *)calloc(TEXTURE_BLOCKS_STAT_COUNT_A, sizeof(uint64_t));
    size_t list_len = 0, list_cap = 64 * TEXTURE_BLOCKS_STAT_COUNT_A;
    char *list = (char *)malloc(list_cap);
    if ((rgba == NULL) || (keys == NULL) || (list == NULL))
    {
        free(rgba); free(keys); free(list);
        return false;
    }
    list_len += (size_t)snprintf(list + list_len, list_cap - list_len, "ftest_hd_switch\t8\t%d\t64\t64\n", rows);
    for (int i = 0; i < TEXTURE_BLOCKS_STAT_COUNT_A; i++)
    {
        const unsigned char *block = block_ptrs[i];
        const int bx = (i % cols) * side, by = (i / cols) * side;
        list_len += (size_t)snprintf(list + list_len, list_cap - list_len, "a.png\t%d\t%d\t64\t64\n", bx, by);
        TbBool uniform = true;
        for (int t = 0; (t < 32 * 32) && uniform; t++)
            uniform = (block[(t / 32) * 256 + (t % 32)] == block[0]);
        keys[i] = uniform ? 0 : texture_hd_block_key(block);
        for (int y = 0; y < side; y++)
            for (int x = 0; x < side; x++)
            {
                const unsigned char index = block[(y / 2) * 256 + (x / 2)];
                unsigned char *o = rgba + (((size_t)(by + y) * w) + (size_t)(bx + x)) * 4;
                for (int c = 0; c < 3; c++)
                    o[c] = (unsigned char)(255 - chan6_to_8(palette[3 * index + c]));
                o[3] = 255;
            }
    }
    char stem_file[64];
    snprintf(stem_file, sizeof(stem_file), "filelist_%s.txt", file->stem);
    ths_path(fname, sizeof(fname), vars->pack_dir, stem_file);
    TbBool ok = ths_write_file(fname, list, list_len);
    snprintf(stem_file, sizeof(stem_file), "keys/%s.idx", file->stem);
    ths_path(fname, sizeof(fname), vars->pack_dir, stem_file);
    unsigned char le[8 * TEXTURE_BLOCKS_STAT_COUNT_A];
    for (int i = 0; i < TEXTURE_BLOCKS_STAT_COUNT_A; i++)
        for (int k = 0; k < 8; k++)
            le[i * 8 + k] = (unsigned char)((keys[i] >> (8 * k)) & 0xFF);
    ok = ok && ths_write_file(fname, le, sizeof(le));
    // PNG
    spng_ctx *ctx = spng_ctx_new(SPNG_CTX_ENCODER);
    if (ctx != NULL)
    {
        struct spng_ihdr ihdr;
        memset(&ihdr, 0, sizeof(ihdr));
        ihdr.width = (uint32_t)w;
        ihdr.height = (uint32_t)h;
        ihdr.bit_depth = 8;
        ihdr.color_type = SPNG_COLOR_TYPE_TRUECOLOR_ALPHA;
        spng_set_option(ctx, SPNG_ENCODE_TO_BUFFER, 1);
        spng_set_ihdr(ctx, &ihdr);
        size_t png_len = 0;
        int err = 0;
        void *png = NULL;
        if (spng_encode_image(ctx, rgba, w * h * 4, SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE) == 0)
            png = spng_get_png_buffer(ctx, &png_len, &err);
        ths_path(fname, sizeof(fname), vars->pack_dir, "a.png");
        ok = ok && (png != NULL) && ths_write_file(fname, png, png_len);
        free(png);
        spng_ctx_free(ctx);
    }
    else
        ok = false;
    free(rgba);
    free(keys);
    free(list);
    return ok;
}

static void ths_rmdir(const char *dir)
{
#ifdef _WIN32
    _rmdir(dir);
#else
    rmdir(dir);
#endif
}

static void ths_remove_pack(struct ftest_texture_hd_switch__variables *vars)
{
    const struct TextureMapFile *file = &texture_map_files[0][0];
    char fname[600], rel[64];
    ths_path(fname, sizeof(fname), vars->pack_dir, "a.png");
    remove(fname);
    ths_path(fname, sizeof(fname), vars->pack_dir, "pack.toml");
    remove(fname);
    snprintf(rel, sizeof(rel), "filelist_%s.txt", file->stem);
    ths_path(fname, sizeof(fname), vars->pack_dir, rel);
    remove(fname);
    snprintf(rel, sizeof(rel), "keys/%s.idx", file->stem);
    ths_path(fname, sizeof(fname), vars->pack_dir, rel);
    remove(fname);
    ths_path(fname, sizeof(fname), vars->pack_dir, "keys");
    ths_rmdir(fname);
    ths_rmdir(vars->pack_dir);
    // and the asset pack's own folder, which holds only that
    snprintf(fname, sizeof(fname), "%s", vars->pack_dir);
    char *slash = strrchr(fname, '/');
    if (slash != NULL)
    {
        *slash = '\0';
        ths_rmdir(fname);
    }
}

static void ths_apply_state(const struct ftest_texture_hd_switch__variables *vars, size_t state)
{
    const char *pack = (state == THS_Pack) ? THS_PACK_NAME : vars->pack_before;
    snprintf(keeperfx_ui_config.asset_pack, sizeof(keeperfx_ui_config.asset_pack), "%s", pack);
    RendererSetTextureMipmaps((state == THS_Mipmaps) ? 1 : vars->mipmaps_before);
    RendererSetTextureFilter((state == THS_Filter) ? RENDERER_TEXFILTER_BILINEAR : vars->filter_before);
}

static FTestActionResult ths_setup(struct FTestActionArgs* const args)
{
    struct ftest_texture_hd_switch__variables* const vars = args->data;
    if (thing_is_invalid(get_player_soul_container(PLAYER0)))
    {
        FTEST_FAIL_TEST("Player 0 has no dungeon heart");
        return FTRs_Go_To_Next_Action;
    }
    ftest_util_reveal_map(PLAYER0);
    vars->frame_hash_before = start_params.frame_hash;
    start_params.frame_hash = true;
    vars->suspended_before = kfx_sim_state.simulation_suspended;
    kfx_sim_state.simulation_suspended = true;
    snprintf(vars->pack_before, sizeof(vars->pack_before), "%s", keeperfx_ui_config.asset_pack);
    vars->filter_before = RendererGetTextureFilter();
    vars->mipmaps_before = RendererGetTextureMipmaps();
    terrain_rough_settings.from_config = 0;
    terrain_rough_settings.params = terrain_rough_default_params;
    terrain_rough_settings.mode = TSM_Gated;
    terrain_audit_set_enabled(1);
    if (!ths_write_pack(vars))
    {
        FTEST_FAIL_TEST("Couldn't write the test pack to %s", vars->pack_dir);
        return FTRs_Go_To_Next_Action;
    }
    vars->step = 0;
    vars->placed = false;
    return FTRs_Go_To_Next_Action;
}

/** Each state, each stop: settle, then the frame's hash. */
static FTestActionResult ths_walk(struct FTestActionArgs* const args)
{
    struct ftest_texture_hd_switch__variables* const vars = args->data;
    if (vars->step >= THS_States * THS_STOPS)
        return FTRs_Go_To_Next_Action;
    const size_t state = vars->step / THS_STOPS, stop = vars->step % THS_STOPS;
    if (!vars->placed)
    {
        struct PlayerInfo *player = get_player(PLAYER0);
        struct Camera *cam = &player->cameras[CamIV_Isometric];
        struct Thing *heart = get_player_soul_container(PLAYER0);
        struct TerrainAuditCamera pin;
        ths_apply_state(vars, state);
        pin.x = heart->mappos.x.val;
        pin.y = heart->mappos.y.val;
        pin.z = cam->mappos.z.val;
        pin.angle_x = 256;
        pin.angle_y = cam->rotation_angle_y;
        pin.angle_z = cam->rotation_angle_z;
        pin.zoom = ths_zooms[stop];
        pin.view_mode = PVM_IsoWibbleView;
        terrain_audit_pin_camera(&pin);
        uint64_t unused = 0;
        vars->placed_frames = 0;
        game_last_frame_hash(&unused, &vars->placed_frames);
        vars->placed_updates = 0;
        vars->placed = true;
        return FTRs_Repeat_Current_Action;
    }
    uint64_t hash = 0, frames = 0;
    if (!game_last_frame_hash(&hash, &frames) || (frames < vars->placed_frames + THS_SETTLE))
    {
        if (++vars->placed_updates > THS_TIMEOUT)
        {
            FTEST_FAIL_TEST("%s/%s: no frame drawn", ths_state_names[state], ths_zoom_names[stop]);
            return FTRs_Go_To_Next_Action;
        }
        return FTRs_Repeat_Current_Action;
    }
    vars->hash[state][stop] = hash;
    JUSTLOG("HDSWITCH: state=%s stop=%s frame=%" PRIu64 " hash=%016" PRIx64 " hd_blocks=%" PRIu64, ths_state_names[state],
        ths_zoom_names[stop], frames, hash, texture_hd_stats()->blocks);
    vars->placed = false;
    vars->step++;
    return FTRs_Repeat_Current_Action;
}

static FTestActionResult ths_check(struct FTestActionArgs* const args)
{
    struct ftest_texture_hd_switch__variables* const vars = args->data;
    // Switching back restores every frame exactly.
    const size_t bases[] = { THS_Base1, THS_Base2, THS_Base3 };
    for (size_t b = 0; b < 3; b++)
        for (size_t s = 0; s < THS_STOPS; s++)
            if (vars->hash[bases[b]][s] != vars->hash[THS_Base0][s])
                FTEST_FAIL_TEST("%s after the %s: %016" PRIx64 ", not the baseline's %016" PRIx64, ths_zoom_names[s],
                    ths_state_names[bases[b] - 1], vars->hash[bases[b]][s], vars->hash[THS_Base0][s]);
    // The pack shows up close, and not far out, where the 32x32 original is drawn as it always was.
    if (vars->hash[THS_Pack][0] == vars->hash[THS_Base0][0])
        FTEST_FAIL_TEST("closest: the HD pack changed nothing");
    if (vars->hash[THS_Pack][1] != vars->hash[THS_Base0][1])
        FTEST_FAIL_TEST("farthest: the HD pack changed the frame, where the original 32x32 art is drawn");
    // Mipmaps change the far zoom.
    if (vars->hash[THS_Mipmaps][1] == vars->hash[THS_Base0][1])
        FTEST_FAIL_TEST("farthest: TEXTURE_MIPMAPS changed nothing");
    // The Software renderer always samples nearest.
    if (RendererGetActiveType() != RENDERER_GPU3D)
        for (size_t s = 0; s < THS_STOPS; s++)
            if (vars->hash[THS_Filter][s] != vars->hash[THS_Base0][s])
                FTEST_FAIL_TEST("%s: TEXTURE_FILTER changed the Software renderer's frame", ths_zoom_names[s]);
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult ths_finish(struct FTestActionArgs* const args)
{
    struct ftest_texture_hd_switch__variables* const vars = args->data;
    ths_apply_state(vars, THS_Base0);
    terrain_audit_pin_camera(NULL);
    terrain_audit_set_enabled(0);
    start_params.frame_hash = vars->frame_hash_before;
    kfx_sim_state.simulation_suspended = vars->suspended_before;
    ths_remove_pack(vars);
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_texture_hd_switch_init()
{
    ftest_append_action(ths_setup, 20, &ftest_texture_hd_switch__vars);
    ftest_append_action(ths_walk, 0, &ftest_texture_hd_switch__vars);
    ftest_append_action(ths_check, 0, &ftest_texture_hd_switch__vars);
    ftest_append_action(ths_finish, 0, &ftest_texture_hd_switch__vars);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
