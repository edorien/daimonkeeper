/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_picker.cpp
 *     See content_picker.h.
 */
#include "pre_inc.h"
#include "content_picker.h"
#include "kfx_editor.h"
#include "frontgui_widgets.h"
#include "frontend.h"

#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include "post_inc.h"

/******************************************************************************/
namespace {

// What the user chose last within this run (plan 03 §5: "last-used target").
std::string s_last_campaign_fname;

std::string base_dir() { return content_root() + "/fxdata"; }
std::string base_crtr_dir() { return content_root() + "/creatrs"; }

// FeCombo takes a plain array of C strings; the lists here are built per frame.
struct ComboItems
{
    std::vector<std::string> text;
    std::vector<const char *> ptrs;
    void add(const std::string &t) { text.push_back(t); }
    const char *const *array()
    {
        ptrs.clear();
        for (const std::string &t : text)
            ptrs.push_back(t.c_str());
        return ptrs.data();
    }
    int64_t count() const { return (int64_t)text.size(); }
};

} // namespace

const ContentCampaign *ContentPicker::campaign() const
{
    return (campaign_idx >= 0 && (size_t)campaign_idx < campaigns.size()) ? &campaigns[(size_t)campaign_idx] : nullptr;
}

void ContentPicker::resolve()
{
    if (map_host)
    {
        const char *dir = editor_current_save_dir();
        const bool unsaved = editor_current_lvnum() == EDITOR_SCRATCH_LEVEL_NUMBER || dir == nullptr || dir[0] == '\0';
        target = content_target_for_map(base_dir(), base_crtr_dir(), unsaved ? std::string() : std::string(dir),
            unsaved ? -1 : (int64_t)editor_current_lvnum());
    }
    else
    {
        const ContentCampaign *c = campaign();
        int64_t level = -1;
        if (c != nullptr && level_idx > 0 && (size_t)(level_idx - 1) < c->levels.size())
            level = c->levels[(size_t)(level_idx - 1)];
        target = content_target_make(base_dir(), base_crtr_dir(), c, level);
    }
}

bool ContentPicker::layer_available(CfgLayer l) const
{
    if (layer_filter)
        return l == CfgLayer_Base || layer_filter(l);
    return l == CfgLayer_Base || !target.path_for("x.cfg", l).empty();
}

void ContentPicker::open(bool map_host_, CfgLayer preferred)
{
    map_host = map_host_;
    campaigns = content_list_campaigns();
    campaign_idx = 0;
    level_idx = 0;
    for (size_t i = 0; i < campaigns.size(); i++)
        if (campaigns[i].fname == s_last_campaign_fname)
            campaign_idx = (int64_t)i;
    resolve();
    layer_sel = layer_available(preferred) ? preferred : CfgLayer_Base;
    note.clear();
}

bool ContentPicker::draw_target()
{
    bool changed = false;
    if (map_host)
    {
        const char *dir = editor_current_save_dir();
        if (target.level_number < 0)
            FeCaption("This map has not been saved yet. Save it once so it has a folder and a number; until then only Base is shown.");
        else
        {
            char line[600];
            snprintf(line, sizeof(line), "Map: level %" PRId64 "   (%s)", (int64_t)target.level_number, dir != nullptr ? dir : "");
            FeBodyText(line);
        }
        return false;
    }
    FeCaption("Campaign / map pack");
    ImGui::SameLine();
    ComboItems camps;
    for (const ContentCampaign &c : campaigns)
        camps.add((c.is_mappack ? "[map pack] " : "") + c.name);
    if (camps.text.empty())
        camps.add("(none)");
    int64_t ci = std::max<int64_t>(0, campaign_idx);
    ImGui::SetNextItemWidth(340);
    if (FeCombo("##campaignpick", &ci, camps.array(), camps.count()) && ci != campaign_idx)
    {
        campaign_idx = ci;
        level_idx = 0;
        if (const ContentCampaign *c = campaign())
            s_last_campaign_fname = c->fname;
        resolve();
        changed = true;
    }
    ImGui::SameLine();
    FeCaption("Level");
    ImGui::SameLine();
    ComboItems levels;
    levels.add("(whole campaign)");
    if (const ContentCampaign *c = campaign())
        for (int64_t lv : c->levels)
            levels.add("Level " + std::to_string(lv));
    int64_t li = level_idx;
    ImGui::SetNextItemWidth(200);
    if (FeCombo("##levelpick", &li, levels.array(), levels.count()) && li != level_idx)
    {
        level_idx = li;
        resolve();
        changed = true;
    }
    return changed;
}

bool ContentPicker::draw_layer()
{
    static const char *const layer_items[] = {"Base (read only)", "Campaign", "Level"};
    FeCaption("Layer");
    ImGui::SameLine();
    int64_t layer = layer_sel;
    ImGui::SetNextItemWidth(180);
    if (FeCombo("##layerpick", &layer, layer_items, CfgLayer_Count) && layer != layer_sel)
    {
        if (layer_available((CfgLayer)layer))
        {
            layer_sel = layer;
            note.clear();
            return true;
        }
        note = std::string("This target has no ") + (layer == CfgLayer_Campaign ? "campaign" : "level") + " layer; pick a "
            + (layer == CfgLayer_Campaign ? "campaign or map pack." : "level.");
    }
    return false;
}

std::string ContentPicker::signature() const
{
    return target.campaign_cfg_dir + "|" + target.level_dir + "|" + std::to_string(target.level_number) + "|"
        + std::to_string(layer_sel);
}

void content_ui_begin_window(const char *id, bool opaque)
{
    // Same shape as the main menu's own screens (campaign, scenarios, skirmish): a centred, undecorated
    // window, no title bar or close box, no window scrolling.
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.90f, io.DisplaySize.y * 0.90f), ImGuiCond_Always);
    if (opaque)
    {
        ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        bg.w = 1.0f;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, bg);
    }
    ImGui::Begin(id, nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (opaque)
        ImGui::PopStyleColor();
}

void content_picker_set_last_campaign(const std::string &campaign_fname)
{
    s_last_campaign_fname = campaign_fname;
}
