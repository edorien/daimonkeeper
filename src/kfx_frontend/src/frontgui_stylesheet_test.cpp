#include "pre_inc.h"
#include "frontgui_stylesheet_test.h"
#include "frontgui_widgets.h"
#include "frontgui_style.h"
#include "config_strings.h" // get_string, GUIStr_*
#include <cstdio>
#include <cstring>
#include "post_inc.h"

namespace {
    bool s_visible = false;

    // Hardcoded, not sourced from the shipped .dat translations (get_string()
    // only has the current language's table loaded -- switching languages at
    // runtime to sample every shipped one would mean a full reload mid-frame,
    // out of scope for this proving screen). These exist purely to show how
    // non-Latin scripts behave against the current (Latin-only, §4.2 CJK/
    // Cyrillic merge not yet landed) font set -- expect visible tofu boxes
    // for Cyrillic/CJK/Arabic until that merge lands; that gap is real
    // information, not a bug in this screen.
    const char *const SCRIPT_SAMPLES[] = {
        "Latin: The quick brown Beetle scuttles past the Bridge.",
        "Diacritics: Zoë's Prison café, à la Française.",
        "Cyrillic: Библиотека Тёмного Лорда (библиотека)",
        "CJK: 地牢看守者 / ダンジョンキーパー / 죄수",
        "Arabic: حارس الزنزانة",
    };

    int64_t s_slider_val_percent = 50;
    bool s_checkbox_invert_mouse = false;
    int64_t s_combo_index = 0;
    char s_text_buf[64] = "Keeper";
    bool s_capturing_key = false;
    int64_t s_selected_row = 0;
    int64_t s_selected_nav = 0;

    void draw_type_scale()
    {
        FeHeading("KeeperFX ImGui Style Sheet");
        FeSubheading(FeStyleUsingExocet() ? "Display face: Exocet (installed)" : "Display face: Cinzel (bundled fallback)");
        FeBodyText("This screen exercises every frontgui_widgets.h wrapper against the current "
                    "window resolution -- resize the window (or launch at a different resolution) "
                    "to check legibility from 640x480 through 4K. Font size is re-derived from the "
                    "window height every frame (no baked atlas), so it should stay proportional.");
        FeCaption("Phase B proving ground -- docs/refactor/renderer/04-imgui-gui-foundation.md");
        FeSeparator();
    }

    void draw_controls_panel()
    {
        if (FeBeginPanel("Settings controls", ImVec2(560, 280)))
        {
            double slider_f = (double)s_slider_val_percent;
            if (FeSlider("Mouse sensitivity", &slider_f, 0.0, 100.0, "%.0f%%"))
                s_slider_val_percent = (int64_t)slider_f;

            FeCheckbox("Invert mouse", &s_checkbox_invert_mouse);

            static const char *const items[] = { "Off", "Normal", "Always" };
            FeCombo("Tag mode", &s_combo_index, items, IM_ARRAYSIZE(items));

            FeTextInput("Player name", s_text_buf, sizeof(s_text_buf));

            if (FeKeybindRow("Rotate camera", s_capturing_key ? "..." : "R", s_capturing_key))
                s_capturing_key = !s_capturing_key;

            FeSeparator();
            FeButton("A button");
            ImGui::SameLine();
            FeIconButton(">");
        }
        FeEndPanel();
    }

    void draw_nav_and_list()
    {
        if (FeBeginPanel("Nav buttons + list box", ImVec2(560, 280)))
        {
            static const char *const nav_items[] = {
                "Start New Game", "Continue Game", "Free Play Levels", "Load Game", "Multiplayer",
            };
            for (int64_t i = 0; i < IM_ARRAYSIZE(nav_items); i++)
            {
                if (FeNavButton(nav_items[i], s_selected_nav == i))
                    s_selected_nav = i;
            }

            FeSeparator();

            bool open = FeBeginListBox("##stylesheet_listbox", ImVec2(0, 100));
            if (open)
            {
                for (int64_t i = 0; i < 8; i++)
                {
                    char label[32];
                    std::snprintf(label, sizeof(label), "List row %" PRId64, (int64_t)(i));
                    if (FeListRow(label, s_selected_row == i))
                        s_selected_row = i;
                }
            }
            FeEndListBox(open);
        }
        FeEndPanel();
    }

    void draw_scroll_area_and_scripts()
    {
        if (FeBeginPanel("Scroll area: script samples"))
        {
            bool open = FeBeginScrollArea("##stylesheet_scroll", ImVec2(0, 160));
            if (open)
            {
                for (const char *sample : SCRIPT_SAMPLES)
                {
                    FeBodyText(sample);
                    FeSeparator();
                }

                char label[128];
                std::snprintf(label, sizeof(label), "Current language, live from get_string(): \"%s\" / \"%s\"",
                    get_string(GUIStr_MnuMainMenu), get_string(GUIStr_MnuStartNewGame));
                FeBodyText(label);
            }
            FeEndScrollArea();
        }
        FeEndPanel();
    }

    void draw_tabs()
    {
        if (FeBeginPanel("Tabs"))
        {
            bool tabbar_open = FeBeginTabBar("##stylesheet_tabs");
            if (tabbar_open)
            {
                if (FeTab("Game"))
                {
                    FeBodyText("Game tab content.");
                    FeEndTab();
                }
                if (FeTab("Graphics"))
                {
                    FeBodyText("Graphics tab content.");
                    FeEndTab();
                }
                if (FeTab("Sound"))
                {
                    FeBodyText("Sound tab content.");
                    FeEndTab();
                }
            }
            FeEndTabBar(tabbar_open);
        }
        FeEndPanel();
    }

    void draw_modal()
    {
        if (FeButton("Open modal"))
            FeOpenModal("StyleSheetModal");

        bool modal_open = FeBeginModal("StyleSheetModal");
        if (modal_open)
        {
            FeBodyText("FeBeginModal/FeEndModal -- error box / add-session box / \"press a key\" all use this.");
            if (FeButton("Close"))
                ImGui::CloseCurrentPopup();
        }
        FeEndModal(modal_open);
    }
}

void FeStyleSheetSetVisible(bool visible)
{
    s_visible = visible;
}

void FeStyleSheetFrame()
{
    if (!s_visible)
        return;

    FeStyleEnsureInit();

    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(620, 640), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("##FeStyleSheetTest", nullptr, ImGuiWindowFlags_NoCollapse))
    {
        draw_type_scale();
        draw_controls_panel();
        draw_nav_and_list();
        draw_scroll_area_and_scripts();
        draw_tabs();
        draw_modal();
    }
    ImGui::End();
}
