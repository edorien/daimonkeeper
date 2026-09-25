/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file frontmenu_settingctrl.c
 *     Generic value-binding plumbing for settings-menu widgets.
 * @par Purpose:
 *     Shared read/map/write/save mechanics for settings-menu sliders and
 *     checkboxes. Extracted from frontmenu_options.c, where
 *     gui_set_sound_volume/gui_set_music_volume/gui_set_mentor_volume
 *     were each a hand-written copy of the same shape.
 * @par Comment:
 *     None.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "frontmenu_settingctrl.h"

#include "bflib_guibtns.h"
#include "bflib_sprfnt.h"
#include "config_strings.h"
#include "frontend.h"
#include "frontmenu_options.h"
#include "gui_frontbtns.h"
#include "vidmode.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
void frontend_sliderctrl_init(struct GuiMenu *gmnu, int64_t bid, const struct FrontendSliderCtrl *ctrl)
{
    int64_t value = ctrl->get_value();
    if (ctrl->nonlinear)
        value = make_audio_slider_linear(value);
    get_gui_button_init(gmnu, bid)->content.lval = value;
}

void frontend_sliderctrl_apply(struct GuiButton *gbtn, const struct FrontendSliderCtrl *ctrl)
{
    int64_t value = gbtn->content.lval;
    if (ctrl->nonlinear)
        value = make_audio_slider_nonlinear(value);
    ctrl->set_value(value);
}

void frontend_checkboxctrl_toggle(struct GuiButton *gbtn, const struct FrontendCheckboxCtrl *ctrl)
{
    ctrl->toggle_value();
}

void frontend_checkboxctrl_draw(struct GuiButton *gbtn, const struct FrontendCheckboxCtrl *ctrl)
{
    int64_t font_idx = frontend_button_caption_font(gbtn, frontend_mouse_over_button);
    LbTextSetFont(frontend_font[font_idx]);
    LbTextSetWindow(gbtn->scr_pos_x, gbtn->scr_pos_y, gbtn->width, gbtn->height);
    int64_t tx_units_per_px = gbtn->height * 16 / LbTextLineHeight();
    const char *text = ctrl->get_value() ? get_string(GUIStr_On) : get_string(GUIStr_Off);
    LbTextDrawResized(0, 0, tx_units_per_px, text);
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
