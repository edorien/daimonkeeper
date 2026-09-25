/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file frontmenu_settingctrl.h
 *     Header file for frontmenu_settingctrl.c.
 * @par Purpose:
 *     Generic value-binding plumbing for settings-menu widgets (sliders,
 *     checkboxes) that read and write a single `settings.<field>`. Widget
 *     rendering itself (frontend_draw_slider, frontend_draw_small_slider,
 *     ...) is untouched - this only replaces the hand-written
 *     read/map/write/save body each control used to duplicate.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_FRONTMENU_SETTINGCTRL_H
#define DK_FRONTMENU_SETTINGCTRL_H

#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

struct GuiMenu;
struct GuiButton;

/**
 * Binds one horizontal-slider button to a settings.<field>.
 * `nonlinear`, when set, applies the same slider-space <-> settings-space
 * mapping (make_audio_slider_linear/make_audio_slider_nonlinear) the
 * volume sliders already used, so the slider feels non-linear to drag
 * while the stored value stays linear; leave clear for a raw 1:1 slider
 * (e.g. mouse sensitivity).
 */
struct FrontendSliderCtrl {
    int64_t (*get_value)(void);
    void (*set_value)(int64_t value);
    TbBool nonlinear;
};

/** Sets a menu's slider button (by id_num) to reflect the bound settings
 *  value; call from a GuiMenu's create_cb, once per bound slider. */
void frontend_sliderctrl_init(struct GuiMenu *gmnu, int64_t bid, const struct FrontendSliderCtrl *ctrl);
/** A slider button's click_event body: reads the button's current
 *  position and writes it through to the bound setting. */
void frontend_sliderctrl_apply(struct GuiButton *gbtn, const struct FrontendSliderCtrl *ctrl);

/** Binds one ON/OFF checkbox button to a settings.<field>. */
struct FrontendCheckboxCtrl {
    TbBool (*get_value)(void);
    void (*toggle_value)(void);
};

/** A checkbox button's click_event body. */
void frontend_checkboxctrl_toggle(struct GuiButton *gbtn, const struct FrontendCheckboxCtrl *ctrl);
/** A checkbox button's draw_call body: draws the bound setting's current
 *  value as localized ON/OFF text. */
void frontend_checkboxctrl_draw(struct GuiButton *gbtn, const struct FrontendCheckboxCtrl *ctrl);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
