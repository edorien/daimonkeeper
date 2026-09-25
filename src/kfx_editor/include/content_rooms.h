/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_rooms.h
 *     docs/refactor/editor/fx-plans/07-room-editor.md -- the Room editor: the rooms, slabs and slab health table of
 *     terrain.cfg as forms (the shared entity window).
 */
#ifndef DK_CONTENT_ROOMS_H
#define DK_CONTENT_ROOMS_H

#include <string>
#include <vector>

/** The tabs of a block kind ("room", "slab", "block_health"), in order. */
std::vector<std::string> rooms_groups(const std::string &basename);
/** Which tab a key of that block kind belongs to; the key's case does not matter. */
std::string rooms_group_of(const std::string &basename, const std::string &key);

#ifdef __cplusplus
extern "C" {
#endif

void content_rooms_open(bool map_host);
void content_rooms_frame(void);
bool content_rooms_is_open(void);
/** Brings a mode (0 rooms, 1 terrain, 2 health table) and a tab to the front on the next frame. */
void content_rooms_show(int mode, const char *group);

#ifdef __cplusplus
}
#endif
#endif
