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
const long kSlabCoord = 3 * 256; // raw map units per slab
}

bool editor_resize_content(MapContent &c, long new_w, long new_h, bool centered, PlayerNumber neutral_owner,
    EditorResizeReport *report)
{
    if (new_w < EDITOR_RESIZE_MIN || new_h < EDITOR_RESIZE_MIN || new_w > EDITOR_RESIZE_MAX || new_h > EDITOR_RESIZE_MAX)
        return false;
    const long old_w = c.map_tiles_x, old_h = c.map_tiles_y;
    const long ox = centered ? (new_w - old_w) / 2 : 0;
    const long oy = centered ? (new_h - old_h) / 2 : 0;
    EditorResizeReport local;
    EditorResizeReport &rep = (report != nullptr) ? *report : local;
    rep = EditorResizeReport();

    std::vector<SlabKind> kind((size_t)(new_w * new_h), SlbT_ROCK);
    std::vector<PlayerNumber> owner((size_t)(new_w * new_h), neutral_owner);
    std::vector<unsigned char> tex((size_t)(new_w * new_h), 0);
    for (long y = 0; y < old_h; y++)
    {
        for (long x = 0; x < old_w; x++)
        {
            const long nx = x + ox, ny = y + oy;
            if (nx < 0 || ny < 0 || nx >= new_w || ny >= new_h)
                continue;
            const size_t from = (size_t)(y * old_w + x), to = (size_t)(ny * new_w + nx);
            kind[to] = c.slab_kind[from];
            owner[to] = c.slab_owner[from];
            if (from < c.slab_texture.size())
                tex[to] = c.slab_texture[from];
        }
    }

    const long dx = ox * kSlabCoord, dy = oy * kSlabCoord;
    const long max_x = new_w * kSlabCoord, max_y = new_h * kSlabCoord;
    auto inside = [&](long x, long y) { return x >= 0 && y >= 0 && x < max_x && y < max_y; };

    std::vector<MapThingRecord> things;
    for (MapThingRecord t : c.things)
    {
        const long nx = (long)t.pos_x + dx, ny = (long)t.pos_y + dy;
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
        const long nx = (long)l.pos_x + dx, ny = (long)l.pos_y + dy;
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
        const long nx = (long)a.pos_x + dx, ny = (long)a.pos_y + dy;
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
