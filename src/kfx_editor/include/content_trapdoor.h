/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_trapdoor.h
 *     docs/refactor/editor/fx-plans/05-trap-door-editor.md -- the Trap and Door editor: the workshop's traps
 *     and doors of trapdoor.cfg as forms, on the structured session (content_struct.h).
 */
#ifndef DK_CONTENT_TRAPDOOR_H
#define DK_CONTENT_TRAPDOOR_H

#include <string>
#include <vector>

/** The tabs of the item form, in order. */
const std::vector<std::string> &trapdoor_groups(void);
/** Which tab a key belongs to (plan 05 §3): "Build", "Behaviour", "Placement", "Look & sound", or "Advanced". The
 *  key's case does not matter. */
std::string trapdoor_group_of(bool is_door, const std::string &key);

#ifdef __cplusplus
extern "C" {
#endif

/** Opens the window; `map_host`: on the map being edited (map editor's Tools menu). */
void content_trapdoor_open(bool map_host);
void content_trapdoor_frame(void);
bool content_trapdoor_is_open(void);
/** Brings the Traps or Doors tab, and a group tab ("Build", ..., "Compare all"), to the front on the next frame. */
void content_trapdoor_show(bool doors, const char *group);

#ifdef __cplusplus
}
#endif
#endif
