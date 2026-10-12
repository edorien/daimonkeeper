// Smoke test for the Options screen's "Combat assistant" section (frontgui_screens.cpp, docs/newFeatures/llm/01 section
// 10.2): drawn opened inside a headless ImGui frame (no backend, default font) from the main menu and in game, with no
// presets file, with a hand-written rule list, and in a multiplayer game -- so its ID and Begin/End balance run at all
// in CI, where ImGui asserts on a mismatch. What a change does is ftest assist_human's.
#include <catch2/catch_test_macros.hpp>

#include <imgui.h>

#include "frontgui_style.h"
#include "frontgui_screens.h"
#include "config_combat_assist.h"
#include "kfx_sim_state.h"

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

void draw(bool in_game)
{
    ImGui::NewFrame();
    ImGui::Begin("##assist_test");
    ImGui::SetNextItemOpen(true);
    frontgui_combat_assistant_section(in_game);
    ImGui::End();
    ImGui::Render();
}
}

TEST_CASE("the combat assistant section draws in every state it has", "[kfx_frontend][assist_ui]") {
    HeadlessImGui gui;
    KeeperAssistProfile saved = *keeper_assist_profile();
    const enum GameKinds saved_kind = kfx_sim_state.game_kind;
    kfx_sim_state.game_kind = GKind_LocalGame;
    keeper_assist_profile_defaults(keeper_assist_profile());
    draw(false);                                   // the main menu, a preset profile (no presets file in a unit test)
    draw(true);                                    // in game
    KeeperAssistProfile *p = keeper_assist_profile();
    keeper_assist_default_rule(&p->custom[0], KAA_Cast, PwrK_HEALCRTR, KAT_AllyAny);
    p->custom_count = 1;
    draw(true);                                    // hand-written rules
    kfx_sim_state.game_kind = GKind_MultiGame;
    draw(true);                                    // multiplayer: shown disabled
    kfx_sim_state.game_kind = saved_kind;
    *keeper_assist_profile() = saved;
    CHECK(true);
}
