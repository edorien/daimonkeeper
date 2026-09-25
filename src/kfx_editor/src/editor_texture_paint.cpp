/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_texture_paint.cpp
 *     See editor_texture_paint.h.
 */
#include "pre_inc.h"
#include "editor_texture_paint.h"
#include "kfx_editor.h"
#include "kfx_sim_state.h"
#include "kfx_config_state.h"
#include "slab_data.h"
#include <algorithm>
#include <vector>
#include "post_inc.h"

namespace {
bool in_map(MapSlabCoord x, MapSlabCoord y)
{
    return x >= 0 && y >= 0 && x < kfx_sim_state.map_tiles_x && y < kfx_sim_state.map_tiles_y;
}
}

void editor_texture_paint_slab(MapSlabCoord x, MapSlabCoord y, unsigned char pack)
{
    if (!in_map(x, y))
        return;
    kfx_config_state.slab_ext_data[get_slab_number(x, y)] = pack;
    editor_mark_dirty();
}

void editor_texture_paint_rect(MapSlabCoord x0, MapSlabCoord y0, MapSlabCoord x1, MapSlabCoord y1, unsigned char pack)
{
    for (MapSlabCoord y = std::min(y0, y1); y <= std::max(y0, y1); y++)
        for (MapSlabCoord x = std::min(x0, x1); x <= std::max(x0, x1); x++)
            editor_texture_paint_slab(x, y, pack);
}

int64_t editor_texture_paint_fill(MapSlabCoord x, MapSlabCoord y, unsigned char pack)
{
    if (!in_map(x, y))
        return 0;
    const unsigned char from_tex = kfx_config_state.slab_ext_data[get_slab_number(x, y)];
    const SlabKind from_kind = get_slabmap_block(x, y)->kind;
    if (from_tex == pack)
        return 0;
    std::vector<std::pair<MapSlabCoord, MapSlabCoord>> todo;
    todo.push_back(std::make_pair(x, y));
    int64_t changed = 0;
    while (!todo.empty())
    {
        const auto p = todo.back();
        todo.pop_back();
        if (!in_map(p.first, p.second))
            continue;
        unsigned char &tex = kfx_config_state.slab_ext_data[get_slab_number(p.first, p.second)];
        if (tex != from_tex || get_slabmap_block(p.first, p.second)->kind != from_kind)
            continue;
        tex = pack;
        changed++;
        todo.push_back(std::make_pair(p.first + 1, p.second));
        todo.push_back(std::make_pair(p.first - 1, p.second));
        todo.push_back(std::make_pair(p.first, p.second + 1));
        todo.push_back(std::make_pair(p.first, p.second - 1));
    }
    if (changed > 0)
        editor_mark_dirty();
    return changed;
}
