/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_text.h
 *     docs/refactor/editor/fx-plans/09-text-strings-editor.md -- the Text editor: a language's strings for a campaign
 *     or level, with the layer each comes from, where each is used, and an edit box (word wrap is view only).
 */
#ifndef DK_CONTENT_TEXT_H
#define DK_CONTENT_TEXT_H

#ifdef __cplusplus
extern "C" {
#endif

void content_text_open(bool map_host);
void content_text_frame(void);
bool content_text_is_open(void);
/** Selects a language ("eng") and a string id on the next frame. For tests and deep links (a NameTextID field). */
void content_text_show(const char *lang, int id);

#ifdef __cplusplus
}
#endif
#endif
