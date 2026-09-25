/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file frontmenu_selectlist.c
 *     Generic scrollable select-list pagination.
 * @par Purpose:
 *     Shared scroll/bounds/enabled-state mechanics for menus that page
 *     through a list of items (campaign, level, mappack and
 *     multiplayer-mappack select). Extracted from frontmenu_select.c,
 *     where the same ~15 functions were duplicated once per list.
 * @par Comment:
 *     None.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "frontmenu_selectlist.h"

#include "bflib_guibtns.h"
#include "bflib_keybrd.h"
#include "frontend.h"
#include "kjm_input.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
void frontend_selectlist_set_visible(struct FrontendSelectList *list)
{
    int64_t count = list->item_count();
    list->items_visible = (count < list->items_visible_max) ? count + 1 : list->items_visible_max;
}

void frontend_selectlist_scroll_up(struct FrontendSelectList *list)
{
    if (list->scroll_offset > 0)
        list->scroll_offset--;
}

void frontend_selectlist_scroll_down(struct FrontendSelectList *list)
{
    if (list->scroll_offset < list->item_count() - list->items_visible + 1)
        list->scroll_offset++;
}

void frontend_selectlist_scroll_to_offset(struct FrontendSelectList *list, struct GuiButton *gbtn)
{
    list->scroll_offset = frontend_scroll_tab_to_offset(gbtn, GetMouseY(), list->items_visible - 2, list->item_count());
}

void frontend_selectlist_up_maintain(struct FrontendSelectList *list, struct GuiButton *gbtn)
{
    if (gbtn == NULL)
        return;
    if (list->scroll_offset != 0)
        gbtn->flags |= LbBtnF_Enabled;
    else
        gbtn->flags &= ~LbBtnF_Enabled;
}

void frontend_selectlist_down_maintain(struct FrontendSelectList *list, struct GuiButton *gbtn)
{
    if (gbtn == NULL)
        return;
    if (list->scroll_offset < list->item_count() - list->items_visible + 1)
        gbtn->flags |= LbBtnF_Enabled;
    else
        gbtn->flags &= ~LbBtnF_Enabled;
}

void frontend_selectlist_row_maintain(struct FrontendSelectList *list, struct GuiButton *gbtn)
{
    if (gbtn == NULL)
        return;
    if (frontend_selectlist_row_to_item_index(list, gbtn) < list->item_count())
        gbtn->flags |= LbBtnF_Enabled;
    else
        gbtn->flags &= ~LbBtnF_Enabled;
}

void frontend_selectlist_update(struct FrontendSelectList *list)
{
    int64_t count = list->item_count();
    if (count <= 0)
    {
        list->scroll_offset = 0;
    } else
    if (list->scroll_offset < 0)
    {
        list->scroll_offset = 0;
    } else
    if (list->scroll_offset > count - list->items_visible + 1)
    {
        list->scroll_offset = count - list->items_visible + 1;
    }
    if (wheel_scrolled_down || (is_key_pressed(KC_DOWN, KMod_NONE)))
    {
        if (list->scroll_offset < count - list->items_visible + 1)
        {
            list->scroll_offset++;
        }
    }
    if (wheel_scrolled_up || (is_key_pressed(KC_UP, KMod_NONE)))
    {
        if (list->scroll_offset > 0)
        {
            list->scroll_offset--;
        }
    }
}

void frontend_selectlist_draw_scroll_tab(struct FrontendSelectList *list, struct GuiButton *gbtn)
{
    frontend_draw_scroll_tab(gbtn, list->scroll_offset, list->items_visible - 2, list->item_count());
}

int64_t frontend_selectlist_row_to_item_index(struct FrontendSelectList *list, struct GuiButton *gbtn)
{
    return gbtn->content.lval + list->scroll_offset - list->row_base;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
