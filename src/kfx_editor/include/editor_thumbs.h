/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_thumbs.h
 *     Header file for editor_thumbs.cpp.
 * @par Purpose:
 *     Thumbnails for the toolbox palette items the game has no icon for:
 *     objects show their in-world sprite (the top-down/"front view"
 *     animation, as drawn in the game view), solid slabs (walls, rock, gold,
 *     doors) the front face of their wall cube, and floor slabs (paths,
 *     water, lava, claimed floor) their top face -- the same 32x32 texture
 *     blocks the renderer uses, through the level's current texture set.
 *     Internal to kfx_editor.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_EDITOR_THUMBS_H
#define DK_EDITOR_THUMBS_H

#include "globals.h"

#ifdef __cplusplus
#include <cstdint>
#include <vector>

struct EditorThumb
{
    void *texture = nullptr; // renderer texture handle; nullptr if there is no thumbnail
    int64_t width = 0;
    int64_t height = 0;
};

// Cached, built on first use. Safe to call every frame; a failed build is
// remembered (and retried after editor_thumbs_reset()).
EditorThumb editor_thumb_slab(SlabKind kind);
EditorThumb editor_thumb_object(ThingModel model);

// The RGBA pixels behind a thumbnail (0xAABBGGRR, alpha 0 = transparent), for
// tests and for the caches above. False if the item has nothing to show.
bool editor_thumb_slab_pixels(SlabKind kind, std::vector<uint64_t> &pixels, int64_t &width, int64_t &height);
bool editor_thumb_object_pixels(ThingModel model, std::vector<uint64_t> &pixels, int64_t &width, int64_t &height);

// Drops cached thumbnails so they rebuild against the level / palette now
// loaded (called when an editor session opens).
void editor_thumbs_reset(void);

// Whether a slab is drawn as a wall (front face) rather than a floor (top).
bool editor_thumb_slab_is_wall(SlabKind kind);

#endif // __cplusplus
#endif
