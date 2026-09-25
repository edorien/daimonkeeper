// kfx_frontend: gui/FrontendImGui.cpp -- the ImGui lifecycle service moved
// here from kfx_platform's gui/ImGuiContext.cpp (docs/refactor/renderer/
// 05-imgui-linkage-consolidation.md). No real SDL window/renderer exists in
// this test binary, so FrontendImGuiEnsure() never succeeds here -- what's
// covered is exactly that no-active-context surface: every entry point
// must stay safe (no crash, sane defaults) when called before/without a
// real context, mirroring kfx_platform/tests/RendererManager_test.cpp's own
// "no active backend" boundary.
#include <catch2/catch_test_macros.hpp>

#include "gui/FrontendImGui.h"

TEST_CASE("FrontendImGuiEnsure fails cleanly with a null window/renderer", "[kfx_frontend][FrontendImGui]") {
    CHECK_FALSE(FrontendImGuiEnsure(nullptr, nullptr));
    CHECK_FALSE(FrontendImGuiIsActive());
}

TEST_CASE("FrontendImGui query functions are safe with no active context", "[kfx_frontend][FrontendImGui]") {
    CHECK_FALSE(FrontendImGuiIsActive());
    CHECK_FALSE(FrontendImGuiWantCaptureMouse());
    CHECK_FALSE(FrontendImGuiWantCaptureKeyboard());
}

TEST_CASE("FrontendImGui per-frame/lifecycle functions are safe with no active context", "[kfx_frontend][FrontendImGui]") {
    // Must not crash even though there is nothing to do.
    FrontendImGuiBeginFrame();
    FrontendImGuiRender();
    FrontendImGuiProcessEvent(nullptr);
    FrontendImGuiRendererDestroying();
    CHECK(true);
}

TEST_CASE("FrontendImGuiSetDemoVisible is safe with no active context", "[kfx_frontend][FrontendImGui]") {
    FrontendImGuiSetDemoVisible(1);
    FrontendImGuiSetDemoVisible(0);
    CHECK(true);
}

TEST_CASE("FrontendImGuiScreenOwned does not crash against default frontend state", "[kfx_frontend][FrontendImGui]") {
    (void)FrontendImGuiScreenOwned();
    CHECK(true);
}
