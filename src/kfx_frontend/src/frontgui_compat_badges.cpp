/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file frontgui_compat_badges.cpp
 *     "Needs a newer KeeperFX" markers for the level and campaign lists.
 * @par Purpose:
 *     See frontgui_compat_badges.h.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "frontgui_compat_badges.h"

#include "frontgui_widgets.h"
#include "config.h"
#include "config_campaigns.h"
#include "lvl_script.h"
#include "version.h"

#include <imgui.h>

#include <cinttypes>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <unordered_map>
#include "post_inc.h"

namespace {

// Files scanned per frame at most: a long campaign list fills in over a few
// frames instead of stalling one.
const int kScansPerFrame = 16;
// How often (frames) a cached file is checked for changes (an editor save).
const int kRecheckFrames = 120;

struct CachedScan {
    struct ScriptPreflight result;
    std::filesystem::file_time_type mtime;
    std::uintmax_t size = 0;
    int checked_frame = 0;
};

std::unordered_map<std::string, CachedScan> s_cache;
int s_budget_frame = -1;
int s_budget_left = 0;

bool take_budget(void)
{
    const int frame = ImGui::GetFrameCount();
    if (frame != s_budget_frame)
    {
        s_budget_frame = frame;
        s_budget_left = kScansPerFrame;
    }
    if (s_budget_left <= 0)
        return false;
    s_budget_left--;
    return true;
}

// The level's script preflight, or nullptr when it isn't known yet this frame.
const struct ScriptPreflight *scan(const std::string &path)
{
    const int frame = ImGui::GetFrameCount();
    auto it = s_cache.find(path);
    if ((it != s_cache.end()) && (frame - it->second.checked_frame < kRecheckFrames))
        return &it->second.result;
    std::error_code ec;
    const auto mtime = std::filesystem::last_write_time(path, ec);
    const std::uintmax_t size = ec ? 0 : std::filesystem::file_size(path, ec);
    if ((it != s_cache.end()) && !ec && (it->second.mtime == mtime) && (it->second.size == size))
    {
        it->second.checked_frame = frame;
        return &it->second.result;
    }
    if (!take_budget())
        return (it != s_cache.end()) ? &it->second.result : nullptr;
    CachedScan entry;
    script_preflight_file(path.c_str(), &entry.result); // an unreadable/missing file scans as clean
    entry.mtime = mtime;
    entry.size = size;
    entry.checked_frame = frame;
    return &(s_cache[path] = entry).result;
}

std::string level_script_path(const struct GameCampaign *campgn, LevelNumber lvnum)
{
    char fname[32];
    snprintf(fname, sizeof(fname), "map%05" PRIu64 ".txt", (uint64_t)lvnum);
    char path[DISKPATH_SIZE];
    prepare_campaign_levels_path(path, sizeof(path), campgn, fname);
    return path;
}

std::string describe(const struct ScriptPreflight &r)
{
    std::string text;
    const int64_t shown = (r.unknown_count < SCRIPT_PREFLIGHT_NAMES_MAX) ? r.unknown_count : SCRIPT_PREFLIGHT_NAMES_MAX;
    for (int64_t i = 0; i < shown; i++)
        text += std::string(i ? ", " : "") + r.names[i] + " (line " + std::to_string(r.lines[i]) + ")";
    if (r.unknown_count > shown)
        text += ", and " + std::to_string(r.unknown_count - shown) + " more";
    return text;
}

} // namespace

enum FeCompatState fe_compat_level_state(const struct GameCampaign *campgn, LevelNumber lvnum, std::string *detail)
{
    const std::string path = level_script_path(campgn, lvnum);
    if (path.empty())
        return FeCompat_Supported;
    const struct ScriptPreflight *r = scan(path);
    if (r == nullptr)
        return FeCompat_NotKnownYet;
    if (r->unknown_count == 0)
        return FeCompat_Supported;
    if (detail != nullptr)
        *detail = "Its script uses commands this version doesn't know: " + describe(*r)
            + ". It may have been made for a newer KeeperFX (this version supports " KFX_COMPAT_STRING "), or have a mistake.";
    return FeCompat_Unsupported;
}

enum FeCompatState fe_compat_campaign_state(const struct GameCampaign *campgn, std::string *detail)
{
    if (campgn == nullptr)
        return FeCompat_Supported;
    int64_t levels = 0, unsupported = 0;
    bool all_known = true;
    std::string example;
    auto visit = [&](LevelNumber lvnum) {
        if (lvnum <= 0)
            return;
        levels++;
        const std::string path = level_script_path(campgn, lvnum);
        const struct ScriptPreflight *r = path.empty() ? nullptr : scan(path);
        if (path.empty())
            return;
        if (r == nullptr)
        {
            all_known = false;
            return;
        }
        if (r->unknown_count > 0)
        {
            unsupported++;
            if (example.empty())
                example = std::string(r->names[0]) + " in level " + std::to_string((int64_t)lvnum);
        }
    };
    for (uint64_t i = 0; i < campgn->single_levels_count; i++)
        visit(campgn->single_levels[i]);
    for (uint64_t i = 0; i < campgn->bonus_levels_count; i++)
        visit(campgn->bonus_levels[i]);
    for (uint64_t i = 0; i < campgn->extra_levels_count; i++)
        visit(campgn->extra_levels[i]);
    if (unsupported > 0)
    {
        if (detail != nullptr)
            *detail = std::to_string(unsupported) + " of its " + std::to_string(levels)
                + " levels use script commands this version doesn't know (e.g. " + example
                + "). It may have been made for a newer KeeperFX (this version supports " KFX_COMPAT_STRING "), or have a mistake.";
        return FeCompat_Unsupported;
    }
    return all_known ? FeCompat_Supported : FeCompat_NotKnownYet;
}

std::string fe_compat_row_label(const char *name, enum FeCompatState state, const char *id)
{
    return std::string(name != nullptr ? name : "") + ((state == FeCompat_Unsupported) ? "  [!]" : "") + "###" + id;
}

void fe_compat_row_tooltip(enum FeCompatState state, const std::string &detail)
{
    if ((state == FeCompat_Unsupported) && !detail.empty())
        FeHelpTooltip(detail.c_str());
}
