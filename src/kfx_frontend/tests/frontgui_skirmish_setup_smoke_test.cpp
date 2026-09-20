// Smoke test for the Skirmish Setup tab's drawing code
// (frontgui_skirmish_setup.cpp): runs every section inside a headless ImGui
// frame (no backend, default font, no game sprites) so its layout, ID and
// Begin/End balance execute at all in CI -- ImGui asserts on a mismatch.
#include <catch2/catch_test_macros.hpp>

#include <imgui.h>

#include "frontgui_style.h"
#include "frontgui_skirmish_setup.h"
#include "skirmish_setup.h"
#include "config_campaigns.h"
#include "level_script_override.h"

namespace {

const char *const kScript =
    "LEVEL_VERSION(1)\nSET_GENERATE_SPEED(400)\nSTART_MONEY(ALL_PLAYERS,2000)\nMAX_CREATURES(ALL_PLAYERS,20)\n"
    "ADD_CREATURE_TO_POOL(FLY,5)\nCREATURE_AVAILABLE(ALL_PLAYERS,TROLL,1,0)\nROOM_AVAILABLE(ALL_PLAYERS,LAIR,1,1)\n"
    "TRAP_AVAILABLE(ALL_PLAYERS,LAVA,1,2)\nMAGIC_AVAILABLE(PLAYER0,POWER_REAPER,1,0)\n"
    "IF(PLAYER0,GAME_TURN > 5)\n NEXT_COMMAND_REUSABLE\n MAGIC_AVAILABLE(PLAYER0,POWER_REAPER,0,0)\nENDIF\n"
    "IF(PLAYER0,ALL_DUNGEONS_DESTROYED == 1)\n WIN_GAME\nENDIF\n";

struct HeadlessImGui
{
    ImGuiContext *ctx;
    HeadlessImGui()
    {
        ctx = ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1280, 720);
        io.DeltaTime = 1.0f / 60.0f;
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

void frame(float height)
{
    ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(900, 700));
    ImGui::Begin("test");
    frontgui_skirmish_setup_draw(height);
    ImGui::End();
    ImGui::Render();
}

} // namespace

TEST_CASE("Setup tab draws for an enabled level, an edited level and a disabled level", "[kfx_frontend][skirmish_setup_ui]") {
    HeadlessImGui gui;
    // nothing loaded yet
    skirmish_setup_forget();
    frame(600.0f);
    CHECK(std::string(frontgui_skirmish_setup_status()).empty());

    skirmish_setup_load_from_text(9921, kScript, 3, false, SkirmishSetup_Auto, 0);
    frame(600.0f);
    frame(600.0f); // a second frame with the same IDs

    // edited: a win/lose Replace with no rules is an error, shown in the status line
    skirmish_setup_set_money(-1, 100);
    skirmish_setup().choices.replace_win_lose = true;
    skirmish_setup().choices.rules.clear();
    frame(600.0f);
    CHECK(std::string(frontgui_skirmish_setup_status()).find("win") != std::string::npos);
    skirmish_setup().choices.rules = skirmish_setup_template(SkirmishRule_LastKeeper, 0);
    frame(600.0f);

    // controllers and teams, with a small window
    skirmish_setup_set_controller(1, SkirmishCtl_Model, 5);
    skirmish_setup_set_team(1, 2);
    frame(120.0f);

    // a Lua companion that changes the setup, and a map with one heart missing (notes drawn in every section)
    const std::string lua = "StartMoney(PLAYER0,1)\nRoomAvailable(PLAYER0,'LAIR',1,1)\nWinGame()\nComputerPlayer(PLAYER1,3)\nAllyPlayers(PLAYER0,PLAYER1,1)\n";
    skirmish_setup_load_from_text(9923, kScript, 3, true, SkirmishSetup_Auto, 0, &lua);
    skirmish_setup().hearts = { 1, 1, 0 };
    skirmish_setup_set_controller(2, SkirmishCtl_Model, 5);
    skirmish_setup().choices.replace_win_lose = true;
    frame(600.0f);
    frame(120.0f);

    skirmish_setup_load_from_text(9922, kScript, 2, false, SkirmishSetup_Locked, 0);
    frame(600.0f); // disabled with a reason
    skirmish_setup_forget();
    level_script_override_clear();
}

namespace {

// One click of `button` (0 left, 1 right) at (x, y), as real input: move, press, release -- each in
// its own frame, like a user's click across frames.
void click(float x, float y, int button)
{
    ImGuiIO &io = ImGui::GetIO();
    io.AddMousePosEvent(x, y);
    frame(600.0f);
    io.AddMouseButtonEvent(button, true);
    frame(600.0f);
    io.AddMouseButtonEvent(button, false);
    frame(600.0f);
}

int pool_of(const char *creature)
{
    const auto it = skirmish_setup().choices.values.pool.find(creature);
    return (it == skirmish_setup().choices.values.pool.end()) ? 0 : it->second;
}

// Finds where the TROLL pool tile is by right-clicking across the window until its count drops
// (a right-click can never toggle a collapsing header or press a button, so the scan is safe).
// Returns the count change (-1 expected) and the click position.
int find_pool_tile_by_right_click(float &fx, float &fy)
{
    const int before = pool_of("TROLL");
    for (float y = 40.0f; y < 690.0f; y += 12.0f)
        for (float x = 20.0f; x < 880.0f; x += 12.0f)
        {
            click(x, y, 1);
            const int now = pool_of("TROLL");
            if (now != before)
            {
                fx = x;
                fy = y;
                return now - before;
            }
        }
    return 0;
}

} // namespace

TEST_CASE("pool tile: left-click adds one, right-click removes exactly one (no bounce-back on release)", "[kfx_frontend][skirmish_setup_ui]") {
    HeadlessImGui gui;
    skirmish_setup_load_from_text(9931, "LEVEL_VERSION(1)\nADD_CREATURE_TO_POOL(TROLL,5)\nCREATURE_AVAILABLE(ALL_PLAYERS,TROLL,1,0)\n"
        "IF(PLAYER0,ALL_DUNGEONS_DESTROYED == 1)\n WIN_GAME\nENDIF\n", 2, false, SkirmishSetup_Auto, 0);
    frame(600.0f);
    REQUIRE(pool_of("TROLL") == 5);

    // Right-click: must end at 4, i.e. the button release must not count as a left-click (+1).
    float x = 0, y = 0;
    CHECK(find_pool_tile_by_right_click(x, y) == -1);
    CHECK(pool_of("TROLL") == 4);
    frame(600.0f); // and it must stay there over the following frames
    frame(600.0f);
    CHECK(pool_of("TROLL") == 4);

    // A second right-click at the same spot goes on decreasing, one per click.
    click(x, y, 1);
    CHECK(pool_of("TROLL") == 3);

    // Left-click at the same spot adds one.
    click(x, y, 0);
    CHECK(pool_of("TROLL") == 4);
    click(x, y, 0);
    CHECK(pool_of("TROLL") == 5);
    skirmish_setup_forget();
}
