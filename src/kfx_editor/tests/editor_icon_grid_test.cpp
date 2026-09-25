// Smoke test for the toolbox's icon palette widget (editor_icon_grid.cpp):
// runs it inside a headless ImGui frame (no backend, default font) so its
// layout and cell code execute at all in CI -- the ftest sweep never draws
// ImGui, so nothing else does.
#include <inttypes.h>
#include <catch2/catch_test_macros.hpp>

#include <imgui.h>

#include "editor_icon_grid.h"

#include <cstdio>
#include <string>

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
        editor_icon_grid_test_use_default_font(true);
    }
    ~HeadlessImGui()
    {
        editor_icon_grid_test_use_default_font(false);
        ImGui::DestroyContext(ctx);
    }
};

} // namespace

TEST_CASE("icon grid lays out text tiles and headings without asserting", "[kfx_editor][icon_grid]") {
    HeadlessImGui gui;
    ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(300, 400));
    ImGui::Begin("test");

    editor_icon_grid_begin("##g", 200.0, 5);
    editor_icon_grid_heading("Group A");
    int64_t clicks = 0;
    for (int64_t i = 0; i < 12; i++)
    {
        char id[16];
        snprintf(id, sizeof(id), "t%" PRId64, (int64_t)(i));
        EditorIconTile t;
        t.id = id;
        t.label = "SOME LONG TILE NAME";
        t.tooltip = "tip";
        t.selected = (i == 3);
        if (editor_icon_grid_tile(t))
            clicks++;
    }
    editor_icon_grid_heading("Group B");
    EditorIconTile t;
    t.id = "last";
    t.label = "X";
    editor_icon_grid_tile(t);
    editor_icon_grid_end();

    ImGui::End();
    ImGui::Render();
    CHECK(clicks == 0); // no mouse input
}

TEST_CASE("icon grid works with three text columns and an empty grid", "[kfx_editor][icon_grid]") {
    HeadlessImGui gui;
    ImGui::NewFrame();
    ImGui::Begin("test");
    editor_icon_grid_begin("##a", 100.0, 3);
    editor_icon_grid_end(); // no tiles at all
    editor_icon_grid_begin("##b", 100.0, 3);
    EditorIconTile t;
    t.id = "one";
    t.label = "DENSE GOLD";
    editor_icon_grid_tile(t);
    editor_icon_grid_end();
    ImGui::End();
    ImGui::Render();
}

TEST_CASE("pretty names replace underscores", "[kfx_editor][icon_grid]") {
    CHECK(std::string(editor_icon_grid_pretty("TORCH_WALL")) == "TORCH WALL");
    CHECK(std::string(editor_icon_grid_pretty("HARD")) == "HARD");
    CHECK(std::string(editor_icon_grid_pretty("")) == "");
}
