/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_thumbs.cpp
 *     Palette thumbnails for slabs and objects.
 * @par Purpose:
 *     See editor_thumbs.h.
 * @par Comment:
 *     Slabs: a slab kind's centre column (slabset style 3, subtile 4 -- the
 *     one place_single_slab_type_on_map() starts from; the slabset stores it
 *     as a negated columns_data[] index) is a stack of cubes.
 *     The highest non-empty cube is what you see: for a solid slab its front
 *     face (cube texture slot 2, the renderer's sideoris[0].front_texture_index),
 *     for a floor slab its top (the column's floor_texture, else the cube's
 *     top slot 4). The face is a 32x32 block in block_ptrs[], addressed with
 *     the same texture-variation offset the renderer adds from slab_ext_data;
 *     the variation used is the level's most common one.
 *     Objects: config sprite_anim_idx is a first-person animation number;
 *     get_td_animation_sprite() converts it to the top-down animation the
 *     world view draws, and render_keepsprite_indexed() decodes frame 0.
 *     Pixels convert through engine_palette (6-bit entries, index 0
 *     transparent for sprites, opaque for textures). Both need the level's
 *     palette and texture memory, so they only work in a running game.
 */
#include "pre_inc.h"
#include "editor_thumbs.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "config_terrain.h"
#include "config_cubes.h"
#include "config_objects.h"
#include "config_slabsets.h"
#include "map_columns.h"
#include "engine_textures.h"
#include "engine_render.h"
#include "renderer/RendererManager.h"
#include "ports/render_port.h"
#include "post_inc.h"

#include <map>
#include <cstring>

namespace {

const int64_t kBlockPx = 32;
const int64_t kBlockStride = 256; // block_ptrs[] blocks live in an 8-per-row atlas

struct Cached
{
    EditorThumb thumb;   // texture handle kept across resets; rebuilt in place
    bool built = false;  // a build was attempted since the last reset
    bool valid = false;  // ... and produced a thumbnail
};

std::map<int64_t, Cached> s_slabs;
std::map<int64_t, Cached> s_objects;

uint64_t rgba_from_index(unsigned char idx, bool transparent_zero)
{
    if (transparent_zero && idx == 0)
        return 0;
    const unsigned char *pal = engine_palette + (size_t)idx * 3;
    // The palette holds 6-bit components.
    auto up = [](unsigned char v) -> uint64_t { return (uint64_t)((v << 2) | (v >> 4)) & 0xFF; };
    return up(pal[0]) | (up(pal[1]) << 8) | (up(pal[2]) << 16) | 0xFF000000u;
}

// The texture-variation the level mostly uses (what the renderer adds per
// slab through slab_ext_data).
int64_t level_texture_variation()
{
    int64_t counts[TEXTURE_VARIATIONS_COUNT] = {};
    int64_t total = (int64_t)kfx_sim_state.map_tiles_x * (int64_t)kfx_sim_state.map_tiles_y;
    if (total > MAX_TILES_X * MAX_TILES_Y)
        total = MAX_TILES_X * MAX_TILES_Y;
    for (int64_t i = 0; i < total; i++)
        counts[kfx_config_state.slab_ext_data[i] & 0x1F]++;
    int64_t best = 0;
    for (int64_t v = 1; v < TEXTURE_VARIATIONS_COUNT; v++)
        if (counts[v] > counts[best])
            best = v;
    return best;
}

bool block_pixels(int64_t texture_id, std::vector<uint64_t> &pixels, int64_t &width, int64_t &height)
{
    if (engine_palette == nullptr || texture_id <= 0)
        return false;
    const int64_t index = texture_id + level_texture_variation() * TEXTURE_BLOCKS_COUNT;
    if (index < 0 || index >= TEXTURE_VARIATIONS_COUNT * TEXTURE_BLOCKS_COUNT)
        return false;
    const unsigned char *block = block_ptrs[index];
    if (block == nullptr)
        return false;
    width = kBlockPx;
    height = kBlockPx;
    pixels.assign((size_t)kBlockPx * kBlockPx, 0);
    for (int64_t y = 0; y < kBlockPx; y++)
        for (int64_t x = 0; x < kBlockPx; x++)
            pixels[(size_t)y * kBlockPx + x] = rgba_from_index(block[y * kBlockStride + x], false);
    return true;
}

EditorThumb upload(Cached &c, const std::vector<uint64_t> &pixels, int64_t w, int64_t h)
{
    if (c.thumb.texture == nullptr || c.thumb.width != w || c.thumb.height != h)
        c.thumb.texture = RendererCreateDynamicTexture(w, h);
    if (c.thumb.texture == nullptr)
        return EditorThumb();
    RendererUpdateDynamicTexture(c.thumb.texture, pixels.data(), w, h);
    c.thumb.width = w;
    c.thumb.height = h;
    return c.thumb;
}

} // namespace

bool editor_thumb_slab_is_wall(SlabKind kind)
{
    const struct SlabConfigStats *stats = get_slab_kind_stats(kind);
    return (stats != NULL) && ((stats->block_flags & SlbAtFlg_Blocking) != 0);
}

bool editor_thumb_slab_pixels(SlabKind kind, std::vector<uint64_t> &pixels, int64_t &width, int64_t &height)
{
    if (kind >= kfx_config_state.conf.slab_conf.slab_types_count)
        return false;
    const int64_t slabset_id = SLABSETS_PER_SLAB * kind + 9 * 3 + 0;
    if (slabset_id >= SLABSET_COUNT)
        return false;
    // slabset col_idx holds the *negated* index into columns_data[] (see
    // copy_block_with_cube_groups(): "columns_data[-itm_idx]").
    const struct Column *col = get_column(-(int64_t)kfx_sim_state.slabset[slabset_id].col_idx[4]);
    if ((col == NULL) || column_invalid(col))
        return false;
    int64_t top = -1;
    for (int64_t i = COLUMN_STACK_HEIGHT - 1; i >= 0; i--)
    {
        if (col->cubes[i] != 0)
        {
            top = i;
            break;
        }
    }
    int64_t texture = 0;
    if (top >= 0)
    {
        const struct CubeConfigStats *cube = get_cube_model_stats(col->cubes[top]);
        if (editor_thumb_slab_is_wall(kind))
            texture = cube->texture_id[2]; // front face
        else
            texture = (col->floor_texture != 0) ? col->floor_texture : cube->texture_id[4]; // top face
    }
    else
    {
        texture = col->floor_texture;
    }
    return block_pixels(texture, pixels, width, height);
}

bool editor_thumb_object_pixels(ThingModel model, std::vector<uint64_t> &pixels, int64_t &width, int64_t &height)
{
    if (engine_palette == nullptr)
        return false;
    const struct ObjectConfigStats *ostat = get_object_model_stats(model);
    if ((ostat == NULL) || (ostat->sprite_anim_idx <= 0))
        return false;
    const int64_t anim = render_get_td_animation_sprite(ostat->sprite_anim_idx);
    if (anim <= 0)
        return false;
    static std::vector<unsigned char> scratch;
    scratch.assign((size_t)kBlockStride * 512, 0);
    if (!render_keepsprite_indexed((int64_t)anim, 0, scratch.data()))
        return false;
    // Crop to the drawn pixels.
    int64_t min_x = kBlockStride, max_x = -1, min_y = 512, max_y = -1;
    for (int64_t y = 0; y < 512; y++)
        for (int64_t x = 0; x < kBlockStride; x++)
            if (scratch[(size_t)y * kBlockStride + x] != 0)
            {
                if (x < min_x) min_x = x;
                if (x > max_x) max_x = x;
                if (y < min_y) min_y = y;
                if (y > max_y) max_y = y;
            }
    if (max_x < 0)
        return false;
    width = max_x - min_x + 1;
    height = max_y - min_y + 1;
    pixels.assign((size_t)width * height, 0);
    for (int64_t y = 0; y < height; y++)
        for (int64_t x = 0; x < width; x++)
            pixels[(size_t)y * width + x] = rgba_from_index(scratch[(size_t)(min_y + y) * kBlockStride + min_x + x], true);
    return true;
}

EditorThumb editor_thumb_slab(SlabKind kind)
{
    Cached &c = s_slabs[kind];
    if (!c.built)
    {
        std::vector<uint64_t> pixels;
        int64_t w = 0, h = 0;
        if (engine_palette == nullptr)
            return EditorThumb(); // level not up yet; retry next frame
        c.built = true;
        c.valid = false;
        if (editor_thumb_slab_pixels(kind, pixels, w, h))
            c.valid = (upload(c, pixels, w, h).texture != nullptr);
    }
    return c.valid ? c.thumb : EditorThumb();
}

EditorThumb editor_thumb_object(ThingModel model)
{
    Cached &c = s_objects[model];
    if (!c.built)
    {
        std::vector<uint64_t> pixels;
        int64_t w = 0, h = 0;
        if (engine_palette == nullptr)
            return EditorThumb();
        c.built = true;
        c.valid = false;
        if (editor_thumb_object_pixels(model, pixels, w, h))
            c.valid = (upload(c, pixels, w, h).texture != nullptr);
    }
    return c.valid ? c.thumb : EditorThumb();
}

void editor_thumbs_reset(void)
{
    // Keep the texture handles (rebuilt in place by upload()) so repeated
    // sessions don't leak dynamic textures.
    for (auto &kv : s_slabs)
        kv.second.built = false;
    for (auto &kv : s_objects)
        kv.second.built = false;
}
