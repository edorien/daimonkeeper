#include "pre_inc.h"
#include "frontgui_ingame_debug.h"

#include "frontgui_widgets.h"
#include "frontgui_style.h"
#include "frontgui_sprite_tex.h" // FeGuiPanelTexture / FeGuiPanelSpriteAvailable
#include "config_keeperfx.h" // ingame_gui_use_classic_hud

#include "globals.h"
#include "frontmenu_ingame_evnt.h" // *_enabled() predicates, TimerTurns, debug_display_network_stats
#include "front_input.h"           // update_time
#include "player_data.h"           // get_my_player, my_player_number
#include "dungeon_data.h"          // get_dungeon, turn_timers
#include "lvl_script.h"            // struct ScriptVariableDetails
#include "lvl_script_conditions.h" // get_condition_details
#include "sprites.h"               // GPS_message_rpanel_msg_blank_std
#include "bflib_video.h"           // units_per_pixel (get_condition_details' icon offsets)
#include "game_merge.h"            // GGUI_ScriptTimer, game_flags2
#include "kfx_game_state.h"        // kfx_game_state.script_*/timer_real/bonus_time/flags_gui
#include "kfx_sim_state.h"         // kfx_sim_state.Timer / TimerGame / turns_per_second / armageddon_cast_turn
#include "kfx_net_state.h"         // kfx_net_state.input_lag_turns
#include "bflib_datetm.h"          // frametime_measurements, debug_display_frametime, stutter_detection_*
#include "bflib_basics.h"          // consoleLogArray / consoleLogArraySize / debug_display_consolelog
#include "bflib_enet.h"            // GetPing / GetPacketLoss / rate getters
#include "net_input_lag.h"         // input_lag_get_stats, INPUT_LAG_INCREASE_SAMPLE_MS
#include "net_exchange_gameplay.h" // multiplayer_speed_adjustment_ns
#include "vidmode.h"               // MyScreenWidth / MyScreenHeight

#include "post_inc.h"

#include <cinttypes> // PRId64
#include <cstdio>
#include <imgui.h>

namespace {

constexpr ImGuiWindowFlags kOverlayFlags =
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav
    | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
    | ImGuiWindowFlags_AlwaysAutoResize;

const double kPad = 12.0;

// A borderless overlay panel pinned to a screen corner. pivot picks the
// anchored corner: (0,0) top-left ... (1,1) bottom-right.
bool begin_corner_overlay(const char *id, ImVec2 pivot)
{
    const ImGuiIO &io = ImGui::GetIO();
    const ImVec2 pos(kPad + pivot.x * (io.DisplaySize.x - 2.0 * kPad),
                     kPad + pivot.y * (io.DisplaySize.y - 2.0 * kPad));
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowBgAlpha(0.62); // legible over the live 3D view
    return ImGui::Begin(id, nullptr, kOverlayFlags);
}

// --- script / player-facing readouts (top-right stack) -----------------

void real_time_clock(char *buf, size_t n, int64_t nturns)
{
    if (nturns < 0) { std::snprintf(buf, n, "00:00:00"); return; }
    uint64_t total_seconds = ((uint64_t)nturns / kfx_sim_state.turns_per_second) + 1;
    uint64_t total_minutes = total_seconds / 60;
    std::snprintf(buf, n, "%02" PRIu64 ":%02" PRIu64 ":%02" PRIu64,
                  (uint64_t)(total_minutes / 60), (uint64_t)(total_minutes % 60), (uint64_t)(total_seconds % 60));
}

bool bonus_timer_line(char *buf, size_t n)
{
    if (!bonus_timer_enabled())
        return false;
    int64_t nturns = kfx_game_state.bonus_time - (int64_t)get_gameturn();
    if (kfx_game_state.timer_real)
        real_time_clock(buf, n, nturns);
    else
    {
        if (nturns < 0) nturns = 0;
        else if (nturns > 99999) nturns = 99999;
        std::snprintf(buf, n, "%05" PRId64, (int64_t)(nturns / 2));
    }
    return true;
}

bool script_timer_line(char *buf, size_t n)
{
    if (!script_timer_enabled())
        return false;
    const struct Dungeon *dungeon = get_dungeon(kfx_game_state.script_timer_player);
    const uint64_t limit = kfx_game_state.script_timer_limit;
    const int64_t base = (int64_t)get_gameturn() - (int64_t)dungeon->turn_timers[kfx_game_state.script_timer_id].count;
    const int64_t nturns = (limit > 0) ? (int64_t)limit - base : base;
    if (nturns < 0)
    {
        // Same self-hide the legacy draw_script_timer() does when a
        // SET_TIMER runs out.
        kfx_game_state.flags_gui &= ~GGUI_ScriptTimer;
        return false;
    }
    if (kfx_game_state.timer_real)
        real_time_clock(buf, n, (int64_t)nturns);
    else
        std::snprintf(buf, n, "%08" PRId64, (int64_t)(nturns));
    return true;
}

} // namespace

int64_t script_variable_rows(struct ScriptVariableRow *rows, int64_t max)
{
    if (!display_variable_enabled())
        return 0;
    int64_t n = 0;
    // get_condition_details() gives the icon offsets in screen pixels for a tile 22 units high, drawn 2.5 lower
    const double tile_px = 22.0 * (double)units_per_pixel / 16.0;
    for (int64_t i = 0; (i < kfx_game_state.active_script_var_count) && (n < max); i++)
    {
        const struct ScriptVariable *scvar = &kfx_game_state.script_variables[i];
        if (!scvar->is_active)
            continue;
        const struct ScriptVariableDetails details = get_condition_details(scvar->variable_player, scvar->value_type,
            scvar->value_id);
        int64_t value = details.value;
        const int64_t target = scvar->variable_target;
        const unsigned char tt = scvar->variable_target_type;
        if (target != 0)
        {
            if (tt == 0 || tt == 2) value = target - value;
            else if (tt == 1)       value = ((~target) + 1) + value;
        }
        if (tt != 2 && value < 0)
            value = 0;
        struct ScriptVariableRow *r = &rows[n++];
        std::snprintf(r->text, sizeof(r->text), "%" PRId64, (int64_t)(value));
        r->icon = -1;
        r->on_tile = false;
        r->dx = r->dy = 0.0;
        if (!scvar->include_icon)
            continue;
        if (scvar->icon_idx >= 0)
        {
            r->icon = scvar->icon_idx;
        }
        else if (tile_px > 0.0)
        {
            r->icon = details.icon_idx;
            r->on_tile = true;
            r->dx = (double)details.x_offset / tile_px;
            // upstream drew the tile at a whole-pixel 2.5 units down, the icon at its whole-pixel offset
            r->dy = (double)(details.y_offset - (int64_t)(2.5 * (double)units_per_pixel / 16.0)) / tile_px;
        }
    }
    return n;
}

namespace {

// An icon `h` high (keeping the sprite's aspect), at the cursor or, with `at`, drawn there without moving it;
// false if the sprite can't be drawn now.
bool panel_icon(int64_t sprite, double h, const ImVec2 *at = nullptr)
{
    if ((sprite < 0) || !FeGuiPanelSpriteAvailable(sprite))
        return false;
    int64_t sw = 0, sh = 0;
    void *tex = FeGuiPanelTexture(sprite, &sw, &sh);
    if ((tex == nullptr) || (sw <= 0) || (sh <= 0))
        return false;
    const ImVec2 size(h * (double)sw / (double)sh, h);
    if (at != nullptr)
        ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)tex, *at, ImVec2(at->x + size.x, at->y + size.y));
    else
        ImGui::Image((ImTextureID)(intptr_t)tex, size);
    return true;
}

void draw_script_variable_row(const struct ScriptVariableRow &r, double icon_h)
{
    const ImVec2 corner = ImGui::GetCursorScreenPos();
    bool icon_drawn = false;
    if (r.on_tile)
    {
        if (panel_icon(GPS_message_rpanel_msg_blank_std, icon_h))
        {
            const ImVec2 at(corner.x + r.dx * icon_h, corner.y + r.dy * icon_h);
            panel_icon(r.icon, icon_h, &at);
            icon_drawn = true;
        }
    }
    else
    {
        icon_drawn = panel_icon(r.icon, icon_h);
    }
    if (icon_drawn)
    {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
    }
    ImGui::TextUnformatted(r.text);
}

bool game_timer_line(char *buf, size_t n)
{
    if (!timer_enabled())
        return false;
    if (kfx_sim_state.TimerGame)
    {
        if (get_my_player()->victory_state != VicS_WonLevel)
            TimerTurns = get_gameturn();
        std::snprintf(buf, n, "%08" PRIu64, (uint64_t)(TimerTurns));
    }
    else
    {
        if (!kfx_sim_state.TimerFreeze)
            update_time();
        std::snprintf(buf, n, "%02" PRId64 ":%02" PRId64 ":%02" PRId64,
                      (int64_t)(kfx_sim_state.Timer.Hours), (int64_t)(kfx_sim_state.Timer.Minutes), (int64_t)(kfx_sim_state.Timer.Seconds));
    }
    return true;
}

void draw_script_readouts(void)
{
    char bonus[32], timer[32];
    struct ScriptVariableRow vars[DISPLAY_VARIABLES_LIMIT];
    const bool has_bonus = bonus_timer_line(bonus, sizeof(bonus)) || script_timer_line(bonus, sizeof(bonus));
    const int64_t nvars  = script_variable_rows(vars, DISPLAY_VARIABLES_LIMIT);
    const bool has_timer = game_timer_line(timer, sizeof(timer));
    if (!has_bonus && (nvars == 0) && !has_timer)
        return;

    if (begin_corner_overlay("##ingame_script_readouts", ImVec2(1.0, 0.0)))
    {
        FeStylePushFont(FeFont_Heading);
        const double icon_h = ImGui::GetFrameHeight();
        if (has_bonus) ImGui::TextUnformatted(bonus);
        for (int64_t i = 0; i < nvars; i++)
            draw_script_variable_row(vars[i], icon_h);
        if (has_timer) ImGui::TextUnformatted(timer);
        FeStylePopFont();
    }
    ImGui::End();
}

// --- dev-only overlays -------------------------------------------------

void draw_gameturn_overlay(void)
{
    if (!gameturn_timer_enabled())
        return;
    if (begin_corner_overlay("##ingame_gameturn", ImVec2(1.0, 1.0)))
        ImGui::Text("GameTurn %" PRIu64, (uint64_t)get_gameturn());
    ImGui::End();
}

void draw_frametime_overlay(void)
{
    if (!frametime_enabled())
        return;
    const bool detail = debug_display_frametime == 2;
    if (begin_corner_overlay("##ingame_frametime", ImVec2(1.0, 0.5)))
    {
        const struct FrametimeMeasurements &m = frametime_measurements;
        static const char *ft_names[TOTAL_FRAMETIME_KINDS] = { "Frame", "Logic", "Draw", "Sleep" };
        static const char *fr_names[TOTAL_FRAMERATE_KINDS] = { "Frame FPS", "Logic FPS", "Draw FPS" };
        if (ImGui::BeginTable("ft", detail ? 4 : 2, ImGuiTableFlags_SizingFixedFit))
        {
            for (int64_t i = 0; i < TOTAL_FRAMETIME_KINDS; i++)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(ft_names[i]);
                ImGui::TableNextColumn(); ImGui::Text("%.3f ms", m.frametime_display[i]);
                if (detail)
                {
                    ImGui::TableNextColumn(); ImGui::Text("%.3f", m.frametime_get_min[i]);
                    ImGui::TableNextColumn(); ImGui::Text("%.3f", m.frametime_get_max[i]);
                }
            }
            for (int64_t i = 0; i < TOTAL_FRAMERATE_KINDS; i++)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(fr_names[i]);
                ImGui::TableNextColumn(); ImGui::Text("%" PRId64, (int64_t)(m.framerate_display[i]));
                if (detail)
                {
                    ImGui::TableNextColumn(); ImGui::Text("%" PRId64, (int64_t)(m.framerate_min[i]));
                    ImGui::TableNextColumn(); ImGui::Text("%" PRId64, (int64_t)(m.framerate_max[i]));
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void draw_network_stats_overlay(void)
{
    if (debug_display_network_stats == 0)
        return;
    if (begin_corner_overlay("##ingame_netstats", ImVec2(0.0, 0.0)))
    {
        const uint64_t ping = GetPing(my_player_number, my_player_number);
        const uint64_t in_kb10 = (GetDownloadRateBytesPerSecond() * 10) / 1024;
        const uint64_t out_kb10 = (GetUploadRateBytesPerSecond() * 10) / 1024;
        int64_t inc_wait, inc_turn, dec_wait, dec_sample;
        input_lag_get_stats(&inc_wait, &inc_turn, &dec_wait, &dec_sample);
        int64_t turn_ns = 0;
        if (kfx_sim_state.turns_per_second > 0)
        {
            turn_ns = 1000000000 / kfx_sim_state.turns_per_second + multiplayer_speed_adjustment_ns;
            if (turn_ns < 0) turn_ns = 0;
        }
        ImGui::Text("Full ping: %" PRIu64 "ms", (uint64_t)(ping));
        ImGui::Text("Half ping: %" PRIu64 "ms", (uint64_t)(ping / 2));
        ImGui::Text("Input lag: %" PRId64, (int64_t)(kfx_net_state.input_lag_turns));
        ImGui::Text("Packet wait increase: %" PRId64 "/%" PRId64 "ms in %" PRId64 "ms", (int64_t)(inc_wait), (int64_t)(inc_turn), (int64_t)(INPUT_LAG_INCREASE_SAMPLE_MS));
        ImGui::Text("Packet wait decrease: %" PRId64 "/%" PRId64 "ms", (int64_t)(dec_wait), (int64_t)(dec_sample));
        ImGui::Text("Download: %" PRIu64 ".%" PRIu64 " KB/s", (uint64_t)(in_kb10 / 10), (uint64_t)(in_kb10 % 10));
        ImGui::Text("Upload: %" PRIu64 ".%" PRIu64 " KB/s", (uint64_t)(out_kb10 / 10), (uint64_t)(out_kb10 % 10));
        ImGui::Text("Congestion: %" PRIu64 " bytes", (uint64_t)(GetClientDataInTransit()));
        ImGui::Text("Loss rate: %" PRIu64 "%%", (uint64_t)(GetPacketLoss(my_player_number, my_player_number)));
        ImGui::Text("Lost packets: %" PRIu64, (uint64_t)(GetClientPacketsLost()));
        ImGui::Text("Stutter: %" PRId64 "ms (avg %" PRId64 "ms, max %" PRId64 "ms)",
                    (int64_t)(stutter_detection_current), (int64_t)(stutter_detection_average), (int64_t)(stutter_detection_max));
        ImGui::Text("Turn length: %" PRId64 "ns", (int64_t)(turn_ns));
        ImGui::Text("Gameturn: %" PRIu64, (uint64_t)get_gameturn());
    }
    ImGui::End();
}

void draw_consolelog_overlay(void)
{
    if (!consolelog_enabled())
        return;
    const ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y * 0.5), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55);
    if (ImGui::Begin("##ingame_consolelog", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav
                     | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing))
    {
        FeStylePushFont(FeFont_Body);
        const size_t start = (consoleLogArraySize > 21) ? consoleLogArraySize - 21 : 0;
        for (size_t i = start; i < consoleLogArraySize; i++)
            ImGui::TextUnformatted(consoleLogArray[i]);
        FeStylePopFont();
    }
    ImGui::End();
}

} // namespace

extern "C" void ingame_debug_overlays_frame(void)
{
    if (ingame_gui_use_classic_hud())
        return;
    draw_script_readouts();
    draw_gameturn_overlay();
    draw_frametime_overlay();
    draw_network_stats_overlay();
    draw_consolelog_overlay();
}
