#include "pre_inc.h"
#include "frontgui_ingame_boxmenu.h"

#include "frontgui_widgets.h"
#include "frontgui_style.h"

#include "globals.h"
#include "bflib_guibtns.h" // struct GuiBox / GuiBoxOption
#include "gui_boxmenu.h"   // gui_get_*_priority_box, cheat_menu_is_active
#include "gui_soundmsgs.h" // (menu click sound via frontgui_widgets, kept parallel)
#include "config_keeperfx.h" // ingame_gui_use_classic_hud

#include "post_inc.h"

#include <cstdio>
#include <imgui.h>

namespace {

// Fire an option exactly as gui_process_option_inputs() does: only when
// is_enabled == 1 and there's a callback; btn is 1 (left) or 2 (right).
void fire_option(struct GuiBox *gbox, struct GuiBoxOption *goptn, unsigned char btn)
{
    if (goptn->is_enabled == 1 && goptn->callback != nullptr)
        goptn->callback(gbox, goptn, btn, &goptn->cb_param1);
}

void draw_one_box(struct GuiBox *gbox, int64_t stack_idx)
{
    if (gbox == nullptr || gbox->optn_list == nullptr)
        return;

    // Stable per-slot id (box_index is 1..2); the "Cheats" caption is
    // hidden (### keeps the id stable if it's ever localized).
    char win_id[32];
    std::snprintf(win_id, sizeof(win_id), "Cheats###ingame_box_%" PRId64, (int64_t)gbox->box_index);

    // First appearance uses the legacy spawn position (gui_create_box put
    // it near the cursor); afterwards ImGui owns the drag.
    ImGui::SetNextWindowPos(ImVec2((double)gbox->pos_x, (double)gbox->pos_y), ImGuiCond_Appearing);
    ImGui::SetNextWindowBgAlpha(0.90);
    ImGui::Begin(win_id, nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings
                 | ImGuiWindowFlags_AlwaysAutoResize);

    FeStylePushFont(FeFont_Body);
    for (struct GuiBoxOption *goptn = gbox->optn_list; goptn->label[0] != '!'; goptn++)
    {
        // is_enabled == 2 with an empty label is the legacy vertical spacer.
        if (goptn->is_enabled == 2 || goptn->label[0] == '\0')
        {
            ImGui::Separator();
            continue;
        }

        // active_cb decides whether the row is live this frame (legacy
        // stores the result in goptn->enabled and greys the text).
        const bool enabled = (goptn->active_cb != nullptr)
                             ? (goptn->active_cb(gbox, goptn, &goptn->acb_param1) != 0)
                             : true;
        goptn->enabled = enabled ? 1 : 0;

        ImGui::PushID((const void *)goptn); // labels aren't guaranteed unique within a list
        ImGui::BeginDisabled(!enabled);
        if (ImGui::Selectable(goptn->label, goptn->active != 0))
            fire_option(gbox, goptn, 1);
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            fire_option(gbox, goptn, 2);
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    FeStylePopFont();

    // Keep the legacy rect in sync so cheat_menu_is_active() / any
    // remaining pos_x/pos_y readers and the next spawn stay sane.
    const ImVec2 wp = ImGui::GetWindowPos();
    const ImVec2 ws = ImGui::GetWindowSize();
    gbox->pos_x = (int64_t)wp.x;
    gbox->pos_y = (int64_t)wp.y;
    gbox->width = (int64_t)ws.x;
    gbox->height = (int64_t)ws.y;

    ImGui::End();
    (void)stack_idx;
}

} // namespace

extern "C" void ingame_boxmenu_frame(void)
{
    if (ingame_gui_use_classic_hud())
        return;
    // Lowest priority first so the top box ends up focused (matches the
    // legacy gui_draw_all_boxes() order).
    int64_t i = 0;
    for (struct GuiBox *gbox = gui_get_lowest_priority_box();
         gbox != nullptr;
         gbox = gui_get_next_highest_priority_box(gbox))
    {
        draw_one_box(gbox, i++);
    }
}

extern "C" TbBool ingame_boxmenu_consumes_mouse(void)
{
    if (ingame_gui_use_classic_hud() || !cheat_menu_is_active())
        return 0;
    return ImGui::GetIO().WantCaptureMouse ? 1 : 0;
}
