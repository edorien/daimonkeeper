// Smoke test for the online lobbies screen (frontgui_screens.cpp's frontgui_netsession_frame()): with
// lobbies of every kind upstream #5373's metadata describes (open, in game, another version, full, no
// roster), the screen draws inside a headless ImGui frame (no backend, default font) -- its table,
// ID and Begin/End balance run at all in CI, where ImGui asserts on a mismatch.
#include <catch2/catch_test_macros.hpp>

#include <imgui.h>

#include <cstdio>
#include <cstring>

#include "frontgui_style.h"
#include "frontgui_screens.h"
#include "frontend.h"
#include "front_network.h"
#include "net_main.h"
#include "bflib_netsession.h"
#include "config_strings.h"

namespace {
struct HeadlessImGui
{
    ImGuiContext *ctx;
    HeadlessImGui()
    {
        ctx = ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1280, 720);
        io.DeltaTime = 1.0 / 60.0;
        unsigned char *pixels;
        int w, h;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
        FeStyleTestUseDefaultFont(true);
    }
    ~HeadlessImGui()
    {
        FeStyleTestUseDefaultFont(false);
        ImGui::DestroyContext(ctx);
    }
};

struct TbNetworkSessionNameEntry lobbies[5];

void make_lobby(int64_t i, const char *name, enum NetSessionPhase phase, int players, int max_players, const char *version, bool roster)
{
    struct TbNetworkSessionNameEntry *s = &lobbies[i];
    std::memset(s, 0, sizeof(*s));
    std::snprintf(s->text, sizeof(s->text), "%s", name);
    s->phase = phase;
    s->joinable = 1;
    s->roster_known = roster;
    s->player_count = (unsigned char)players;
    s->max_players = (unsigned char)max_players;
    std::snprintf(s->version, sizeof(s->version), "%s", version);
    for (int p = 0; p < players && roster; p++)
        std::snprintf(s->players[p], sizeof(s->players[p]), "Keeper %d", p);
    net_session[i] = s;
}
}

TEST_CASE("the lobbies screen draws open, started, other-version, full and roster-less lobbies", "[kfx_frontend][netsession_ui]") {
    HeadlessImGui gui;
    // no language file in a unit test: every GUI string reads as its own placeholder
    char *saved_strings[GUI_STRINGS_COUNT];
    std::memcpy(saved_strings, gui_strings, sizeof(saved_strings));
    for (auto &str : gui_strings) str = const_cast<char *>("text");
    make_lobby(0, "Open lobby", NetPhase_Lobby, 1, 4, NET_SESSION_VERSION, true);
    make_lobby(1, "Started game", NetPhase_InGame, 2, 2, NET_SESSION_VERSION, true);
    make_lobby(2, "KeeperFX lobby", NetPhase_Lobby, 1, 4, "1.4.0.5136", true);
    make_lobby(3, "Full lobby", NetPhase_InLandview, 4, 4, NET_SESSION_VERSION, true);
    make_lobby(4, "LAN host", NetPhase_Unknown, 0, 0, "", false);
    net_number_of_sessions = 5;
    const FrontendMenuState prev_state = frontend_menu_state;
    frontend_menu_state = FeSt_NET_SESSION;
    for (int64_t selected = -1; selected < 5; selected++)
    {
        INFO("selected lobby " << selected);
        net_session_index_active = selected;
        ImGui::NewFrame();
        FrontendImGuiFrame();
        ImGui::Render();
    }
    frontend_menu_state = prev_state;
    net_number_of_sessions = 0;
    net_session_index_active = -1;
    std::memset(net_session, 0, sizeof(net_session));
    std::memcpy(gui_strings, saved_strings, sizeof(saved_strings));
    CHECK(true);
}
