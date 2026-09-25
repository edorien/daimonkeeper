/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_spells.h
 *     docs/refactor/editor/fx-plans/06-spell-ability-editor.md -- the Spell and Ability editor: powers, spells,
 *     shots and specials of magic.cfg as forms (the shared entity window), with the per-level cost and strength
 *     arrays as boxes with a curve. Abilities (creature instances) are edited in creature.cfg through the same window.
 */
#ifndef DK_CONTENT_SPELLS_H
#define DK_CONTENT_SPELLS_H

#include <string>
#include <vector>

/** Which tab a key of a block kind ("power", "spell", "shot", "special") belongs to; the key's case does not matter. */
std::string spells_group_of(const std::string &basename, const std::string &key);
/** The tabs of a block kind, in order. */
const std::vector<std::string> &spells_groups(const std::string &basename);

#ifdef __cplusplus
extern "C" {
#endif

void content_spells_open(bool map_host);
void content_spells_frame(void);
bool content_spells_is_open(void);
/** Brings a mode (0 powers, 1 spells, 2 shots, 3 specials, 4 abilities) and a group tab to the front on the next frame. */
void content_spells_show(int mode, const char *group);

#ifdef __cplusplus
}
#endif
#endif
