/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_creature.h
 *     docs/refactor/editor/fx-plans/04-creature-editor.md -- the Creature editor: every creature's model file
 *     (creatrs/<name>.cfg, its layers in the campaign's creature folder and per level) as tabbed forms, the
 *     levels 1-10 preview, a comparison of all creatures, and the global experience percentages (creature.cfg).
 */
#ifndef DK_CONTENT_CREATURE_H
#define DK_CONTENT_CREATURE_H

#include <cstdint>
#include <string>
#include <vector>


#ifdef __cplusplus
extern "C" {
#endif

/** A stat at experience level `level_index` (0 for level 1): the engine's own scaling,
 *  base + percent * base * level / 100 (thing_stats.c compute_creature_max_*). */
int64_t creature_level_value(int64_t base, int64_t percent, int64_t level_index);

void content_creature_open(bool map_host);
void content_creature_frame(void);
bool content_creature_is_open(void);
/** Brings a tab ("Attributes", ..., "Preview", "Compare all", "Global") to the front on the next frame, and picks
 *  creature number `creature_index` in the list (-1: leave). For tests and deep links. */
void content_creature_show(const char *tab, int creature_index);

#ifdef __cplusplus
}
#endif
#endif
