/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_message_helper.cpp
 *     Script > Objective / Message window.
 * @par Purpose:
 *     See editor_message_helper.h.
 * @par Comment:
 *     Inserts one line into the user part of the script (never the managed
 *     setup region -- see editor_script_insert_block_at_cursor()). If the
 *     script editor window is open the line lands at its cursor, otherwise
 *     it is appended to the script. All formatting/numbering logic is in
 *     editor_script_message.cpp so it is unit-tested without a session.
 */
#include "pre_inc.h"
#include "editor_message_helper.h"
#include "editor_script.h"
#include "editor_script_message.h"
#include "kfx_editor.h"
#include "frontgui_widgets.h"
#include <imgui.h>
#include <cstdio>
#include <string>
#include "post_inc.h"

/******************************************************************************/
namespace {

bool s_show = false;
int s_kind = MsgKind_Objective;
int s_number = 0;
char s_text[kScriptMessageMaxChars + 1];
char s_location[48];
std::string s_status;

const char *const kKindLabels[MsgKind_Count] = {
    "Objective (QUICK_OBJECTIVE)",
    "Information (QUICK_INFORMATION)",
};

} // namespace

extern "C" void editor_dialogs_open_message_helper(void)
{
    int next = editor_script_next_message_number(editor_current_level_script_text());
    s_number = (next >= 0) ? next : 0;
    s_text[0] = '\0';
    s_location[0] = '\0';
    s_status.clear();
    s_show = true;
}

extern "C" void editor_message_helper_frame(void)
{
    if (!s_show)
        return;

    ImGui::SetNextWindowSize(ImVec2(520, 400), ImGuiCond_FirstUseEver);
    bool open = s_show;
    if (ImGui::Begin("Objective / Message", &open, ImGuiWindowFlags_NoSavedSettings))
    {
        editor_lua_override_banner();
        FeCombo("Kind", &s_kind, kKindLabels, MsgKind_Count);

        ImGui::InputInt("Number (0-255)", &s_number);
        if (s_number < 0)
            s_number = 0;
        if (s_number >= kScriptMessageCount)
            s_number = kScriptMessageCount - 1;
        if (editor_script_message_number_used(editor_current_level_script_text(), s_number))
            FeCaption("This number is already used in the script; reusing it makes the engine warn and keep the later text.");

        FeBodyText("Text:");
        ImGui::InputTextMultiline("##MsgText", s_text, sizeof(s_text), ImVec2(-1, 130));

        ImGui::InputText("Location (optional)", s_location, sizeof(s_location));
        FeCaption("A map location the message points at, e.g. PLAYER0. Leave blank for none.");

        std::string line = editor_script_format_message(s_kind, s_number, s_text, s_location);
        FeCaption("Will insert:");
        ImGui::TextWrapped("%s", line.c_str());

        ImGui::BeginDisabled(s_text[0] == '\0');
        if (FeButton("Insert", ImVec2(140, 0)))
        {
            bool into_editor = editor_script_insert_block_at_cursor(line.c_str());
            s_status = into_editor
                ? "Inserted at the cursor in the script editor -- press Apply there to keep it."
                : "Appended to the end of the script.";
            int next = editor_script_next_message_number(editor_current_level_script_text());
            if (!into_editor && next >= 0)
                s_number = next;
            else if (s_number + 1 < kScriptMessageCount)
                s_number++;
            s_text[0] = '\0';
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (FeButton("Close", ImVec2(140, 0)))
            open = false;
        if (!s_status.empty())
            FeBodyText(s_status.c_str());
    }
    ImGui::End();
    s_show = open;
}
