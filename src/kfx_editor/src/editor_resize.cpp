/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_resize.cpp
 *     See editor_resize.h.
 */
#include "pre_inc.h"
#include "editor_resize.h"
#include <algorithm>
#include "post_inc.h"

namespace {
const int64_t kSlabCoord = 3 * 256; // raw map units per slab
}

bool editor_resize_content(MapContent &c, int64_t new_w, int64_t new_h, bool centered, PlayerNumber neutral_owner,
    EditorResizeReport *report)
{
    if (new_w < EDITOR_RESIZE_MIN || new_h < EDITOR_RESIZE_MIN || new_w > EDITOR_RESIZE_MAX || new_h > EDITOR_RESIZE_MAX)
        return false;
    const int64_t old_w = c.map_tiles_x, old_h = c.map_tiles_y;
    const int64_t ox = centered ? (new_w - old_w) / 2 : 0;
    const int64_t oy = centered ? (new_h - old_h) / 2 : 0;
    EditorResizeReport local;
    EditorResizeReport &rep = (report != nullptr) ? *report : local;
    rep = EditorResizeReport();

    std::vector<SlabKind> kind((size_t)(new_w * new_h), SlbT_ROCK);
    std::vector<PlayerNumber> owner((size_t)(new_w * new_h), neutral_owner);
    std::vector<unsigned char> tex((size_t)(new_w * new_h), 0);
    for (int64_t y = 0; y < old_h; y++)
    {
        for (int64_t x = 0; x < old_w; x++)
        {
            const int64_t nx = x + ox, ny = y + oy;
            if (nx < 0 || ny < 0 || nx >= new_w || ny >= new_h)
                continue;
            const size_t from = (size_t)(y * old_w + x), to = (size_t)(ny * new_w + nx);
            kind[to] = c.slab_kind[from];
            owner[to] = c.slab_owner[from];
            if (from < c.slab_texture.size())
                tex[to] = c.slab_texture[from];
        }
    }

    const int64_t dx = ox * kSlabCoord, dy = oy * kSlabCoord;
    const int64_t max_x = new_w * kSlabCoord, max_y = new_h * kSlabCoord;
    auto inside = [&](int64_t x, int64_t y) { return x >= 0 && y >= 0 && x < max_x && y < max_y; };

    std::vector<MapThingRecord> things;
    for (MapThingRecord t : c.things)
    {
        const int64_t nx = (int64_t)t.pos_x + dx, ny = (int64_t)t.pos_y + dy;
        if (!inside(nx, ny))
        {
            rep.things_dropped++;
            continue;
        }
        t.pos_x = (MapCoord)nx;
        t.pos_y = (MapCoord)ny;
        t.parent_tile = -1; // slab coded coordinates of the old map mean nothing now
        things.push_back(t);
    }
    std::vector<MapLightRecord> lights;
    for (MapLightRecord l : c.lights)
    {
        const int64_t nx = (int64_t)l.pos_x + dx, ny = (int64_t)l.pos_y + dy;
        if (!inside(nx, ny))
        {
            rep.lights_dropped++;
            continue;
        }
        l.pos_x = (MapCoord)nx;
        l.pos_y = (MapCoord)ny;
        l.parent_tile = 0;
        lights.push_back(l);
    }
    std::vector<MapActionPointRecord> aps;
    for (MapActionPointRecord a : c.action_points)
    {
        const int64_t nx = (int64_t)a.pos_x + dx, ny = (int64_t)a.pos_y + dy;
        if (!inside(nx, ny))
        {
            rep.action_points_dropped++;
            continue;
        }
        a.pos_x = (MapCoord)nx;
        a.pos_y = (MapCoord)ny;
        aps.push_back(a);
    }

    c.map_tiles_x = new_w;
    c.map_tiles_y = new_h;
    c.slab_kind = kind;
    c.slab_owner = owner;
    c.slab_texture = tex;
    c.things = things;
    c.lights = lights;
    c.action_points = aps;
    c.derived_clm.clear();
    c.derived_dat.clear();
    c.derived_wib.clear();
    return true;
}
