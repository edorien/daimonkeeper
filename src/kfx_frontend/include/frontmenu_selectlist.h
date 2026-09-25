/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file frontmenu_selectlist.h
 *     Header file for frontmenu_selectlist.c.
 * @par Purpose:
 *     Generic scrollable select-list pagination, shared by the campaign,
 *     level, mappack and multiplayer-mappack select screens (and future
 *     scrollable lists, e.g. the planned settings list).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_FRONTMENU_SELECTLIST_H
#define DK_FRONTMENU_SELECTLIST_H

#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

struct GuiButton;

/** Row buttons in a select-list's GuiButtonInit array store their row number in
 *  content.lval, biased by this base (see frontmenu_select_data.cpp's {45}..{51}
 *  rows) so slot 0 stays reserved/unused for "no row". */
#define FE_SELECTLIST_ROW_BASE 45

/**
 * Pagination state and item-count source for one scrollable select-list.
 * Deliberately doesn't know what a row displays or does when selected -
 * only the scrolling/bounds arithmetic is shared; per-list code keeps its
 * own draw and on-select behaviour.
 */
struct FrontendSelectList {
    int64_t scroll_offset;
    int64_t items_visible;
    int64_t items_visible_max;
    int64_t (*item_count)(void);
    // Row buttons' content.lval is also used as a hover-tracking key
    // (frontend_over_button sets the single global frontend_mouse_over_button
    // to it directly, gui_frontbtns.c) -- two simultaneously-visible lists
    // using the same content.lval range hover-highlight together, which is
    // exactly what happened when mappack_select_list and level_select_list
    // both used FE_SELECTLIST_ROW_BASE (45) on the merged Free play screen
    // (docs/refactor/gui/04-phase2-landview-panel-investigation.md). Every
    // list still visible alongside another one needs its own non-overlapping
    // row_base; defaults to FE_SELECTLIST_ROW_BASE for lists that are always
    // the only one on their screen.
    int64_t row_base;
};

void frontend_selectlist_set_visible(struct FrontendSelectList *list);
void frontend_selectlist_scroll_up(struct FrontendSelectList *list);
void frontend_selectlist_scroll_down(struct FrontendSelectList *list);
void frontend_selectlist_scroll_to_offset(struct FrontendSelectList *list, struct GuiButton *gbtn);
void frontend_selectlist_up_maintain(struct FrontendSelectList *list, struct GuiButton *gbtn);
void frontend_selectlist_down_maintain(struct FrontendSelectList *list, struct GuiButton *gbtn);
void frontend_selectlist_row_maintain(struct FrontendSelectList *list, struct GuiButton *gbtn);
void frontend_selectlist_update(struct FrontendSelectList *list);
void frontend_selectlist_draw_scroll_tab(struct FrontendSelectList *list, struct GuiButton *gbtn);
int64_t frontend_selectlist_row_to_item_index(struct FrontendSelectList *list, struct GuiButton *gbtn);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
