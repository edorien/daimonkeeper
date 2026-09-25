// kfx_frontend: frontmenu_settingctrl.c's pure value-binding plumbing,
// extracted from what used to be a hand-written copy per settings-menu
// control (gui_set_sound_volume/gui_set_music_volume/gui_set_mentor_volume
// etc. in frontmenu_options.c -- see docs/refactor/gui/00-overview.md
// Phase 2). frontend_checkboxctrl_draw() isn't covered here: it does real
// text rendering (LbTextSetFont/LbTextDrawResized), not pure.
#include <catch2/catch_test_macros.hpp>

#include "frontmenu_settingctrl.h"
#include "bflib_guibtns.h"

#include <cstring>

namespace {
int64_t fake_setting_value = 0;
int64_t fake_get_value() { return fake_setting_value; }
void fake_set_value(int64_t value) { fake_setting_value = value; }

bool fake_checkbox_value = false;
int64_t fake_toggle_calls = 0;
TbBool fake_checkbox_get() { return fake_checkbox_value; }
void fake_checkbox_toggle() { fake_checkbox_value = !fake_checkbox_value; fake_toggle_calls++; }

struct SliderCtrlFixture {
    struct GuiButtonInit buttons[2]{};
    struct GuiMenu gmnu{};
    struct GuiButton gbtn{};

    SliderCtrlFixture() {
        std::memset(buttons, 0, sizeof(buttons));
        std::memset(&gmnu, 0, sizeof(gmnu));
        std::memset(&gbtn, 0, sizeof(gbtn));
        buttons[0].gbtype = LbBtnT_HorizSlider;
        buttons[0].id_num = BID_DEFAULT + 1;
        buttons[1].gbtype = -1; // terminator, see get_gui_button_init()
        gmnu.buttons = buttons;
        fake_setting_value = 0;
    }
};

struct CheckboxCtrlFixture {
    struct GuiButton gbtn{};

    CheckboxCtrlFixture() {
        std::memset(&gbtn, 0, sizeof(gbtn));
        fake_checkbox_value = false;
        fake_toggle_calls = 0;
    }
};
}

TEST_CASE_METHOD(SliderCtrlFixture, "frontend_sliderctrl_init sets the button's content.lval to the raw setting for a non-mapped control", "[kfx_frontend][frontmenu_settingctrl]") {
    struct FrontendSliderCtrl ctrl = { fake_get_value, fake_set_value, false };
    fake_setting_value = 4;
    frontend_sliderctrl_init(&gmnu, BID_DEFAULT + 1, &ctrl);
    CHECK(buttons[0].content.lval == 4);
}

TEST_CASE_METHOD(SliderCtrlFixture, "frontend_sliderctrl_init maps the setting through the linear curve for a nonlinear control", "[kfx_frontend][frontmenu_settingctrl]") {
    struct FrontendSliderCtrl ctrl = { fake_get_value, fake_set_value, true };
    fake_setting_value = 0; // bottom of range maps to itself under make_audio_slider_linear
    frontend_sliderctrl_init(&gmnu, BID_DEFAULT + 1, &ctrl);
    CHECK(buttons[0].content.lval == 0);
}

TEST_CASE_METHOD(SliderCtrlFixture, "frontend_sliderctrl_apply writes the button's raw value through for a non-mapped control", "[kfx_frontend][frontmenu_settingctrl]") {
    struct FrontendSliderCtrl ctrl = { fake_get_value, fake_set_value, false };
    gbtn.content.lval = 3;
    frontend_sliderctrl_apply(&gbtn, &ctrl);
    CHECK(fake_setting_value == 3);
}

TEST_CASE_METHOD(SliderCtrlFixture, "frontend_sliderctrl_apply maps the button's value through the nonlinear curve for a nonlinear control", "[kfx_frontend][frontmenu_settingctrl]") {
    struct FrontendSliderCtrl ctrl = { fake_get_value, fake_set_value, true };
    gbtn.content.lval = 0; // bottom of range maps to itself under make_audio_slider_nonlinear
    frontend_sliderctrl_apply(&gbtn, &ctrl);
    CHECK(fake_setting_value == 0);
}

TEST_CASE_METHOD(CheckboxCtrlFixture, "frontend_checkboxctrl_toggle calls through to the bound toggle function", "[kfx_frontend][frontmenu_settingctrl]") {
    struct FrontendCheckboxCtrl ctrl = { fake_checkbox_get, fake_checkbox_toggle };
    frontend_checkboxctrl_toggle(&gbtn, &ctrl);
    CHECK(fake_checkbox_value == true);
    CHECK(fake_toggle_calls == 1);
}
