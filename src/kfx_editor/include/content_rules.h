/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_rules.h
 *     docs/refactor/editor/fx-plans/03-content-editors-foundation.md §9 (F4) -- the Rules editor:
 *     rules.cfg as a form, grouped as the file groups it, on the structured session
 *     (content_struct.h) so every edit goes through the kfx_config writer.
 */
#ifndef DK_CONTENT_RULES_H
#define DK_CONTENT_RULES_H

#ifdef __cplusplus
extern "C" {
#endif

/** Opens the window; `map_host`: on the map being edited (map editor's Tools menu). */
void content_rules_open(bool map_host);
void content_rules_frame(void);
bool content_rules_is_open(void);
/** Brings a block's tab to the front on the next frame ("game", "sacrifices", ...). For tests and deep links. */
void content_rules_show_tab(const char *block);

#ifdef __cplusplus
}
#endif

#endif
