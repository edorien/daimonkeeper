// The menu has no in-game panel sprite sheet (vidmode.c LoadVResMinimal loads only the button sprites and
// frontend fonts), so FeGuiPanelTexture()/FeGuiPanelSpriteAvailable() fall back to a private copy of
// data/gui2-64.dat. Runs against the real shipped data in core_files/ (skipped when it is not there).
// The sprite numbers are the ones a live run reported as "no drawable menu icon" before the fix.
#include <catch2/catch_test_macros.hpp>

#include "frontgui_sprite_tex.h"
#include "vidmode.h" // gui_panel_sprites
#include "custom_sprites.h" // custom_sprites, GUI_PANEL_SPRITES_COUNT (sprites.h)
#include "sprites.h"
#include "bflib_sprite.h"

#include <filesystem>
#include <unistd.h>

// Defined in kfx_render/custom_sprites.c (not in a public header; the test swaps it for a synthetic sheet).
extern "C" struct TbSpriteSheet *custom_sprites;

namespace {

struct InCoreFiles
{
    std::string previous;
    bool ok = false;
    InCoreFiles()
    {
        const std::filesystem::path dir = std::filesystem::path(KFX_TEST_REPO_ROOT) / "core_files";
        if (!std::filesystem::exists(dir / "data" / "gui2-64.dat"))
            return;
        previous = std::filesystem::current_path().string();
        ok = (chdir(dir.c_str()) == 0);
    }
    ~InCoreFiles()
    {
        FeGuiPanelReleaseMenuSheet();
        if (ok && chdir(previous.c_str()) != 0)
            FAIL("could not restore the working directory");
    }
};

} // namespace

TEST_CASE("menu context: base-sheet icons are available, custom/invalid ones are not", "[kfx_frontend][menu_panel_sprites]") {
    InCoreFiles data;
    if (!data.ok)
        SKIP("core_files/data not found");
    REQUIRE(gui_panel_sprites == nullptr); // this is the menu situation: no in-game sheet

    // Barracks room and the powers reported live as checkerboards / text-tile fallbacks.
    for (int64_t idx : { (int64_t)69, (int64_t)452, (int64_t)809, (int64_t)772, (int64_t)424, (int64_t)436, (int64_t)550, (int64_t)412, (int64_t)406 })
    {
        INFO("sprite " << idx);
        CHECK(FeGuiPanelSpriteAvailable(idx));
    }
    // The four standard player symbols used by the Setup tab's player selector.
    for (int64_t idx = 488; idx <= 491; idx++)
        CHECK(FeGuiPanelSpriteAvailable(idx));

    // Nothing behind these: 0 means "no sprite"; past the base sheet's 920 entries; an unresolved icon name.
    CHECK_FALSE(FeGuiPanelSpriteAvailable(0));
    CHECK_FALSE(FeGuiPanelSpriteAvailable(-3));
    CHECK_FALSE(FeGuiPanelSpriteAvailable(920));
    CHECK_FALSE(FeGuiPanelSpriteAvailable(950)); // first custom-sprite index: only exists once a level has loaded its zips
    CHECK_FALSE(FeGuiPanelSpriteAvailable(32767));
}

TEST_CASE("menu context without game data degrades to 'not available' instead of the placeholder", "[kfx_frontend][menu_panel_sprites]") {
    // Run from a directory with no data/ folder: the private sheet cannot load, so nothing is drawable
    // (callers then show a text tile) -- and nothing crashes or hands back the checkerboard.
    FeGuiPanelReleaseMenuSheet(); // start from "nothing loaded"
    const std::filesystem::path empty = std::filesystem::temp_directory_path();
    const std::string previous = std::filesystem::current_path().string();
    REQUIRE(chdir(empty.c_str()) == 0);
    CHECK_FALSE(FeGuiPanelSpriteAvailable(69));
    int64_t w = -1, h = -1;
    CHECK(FeGuiPanelTexture(69, &w, &h) == nullptr);
    CHECK(w == 0);
    FeGuiPanelReleaseMenuSheet();
    REQUIRE(chdir(previous.c_str()) == 0);
}

TEST_CASE("menu context: custom (mod/campaign) icons are available when their sprites are loaded", "[kfx_frontend][menu_panel_sprites]") {
    // Regression: replacement creature portraits from a mod are custom sprites (index >= GUI_PANEL_SPRITES_COUNT)
    // loaded at startup, i.e. present in the menu. The menu's private base sheet only covers the base indices,
    // and an earlier version of the menu path treated every custom index as unavailable -- the mod's creature
    // icons vanished from the Skirmish Setup tab.
    REQUIRE(gui_panel_sprites == nullptr);
    struct TbSpriteSheet *saved = custom_sprites;
    struct TbSpriteSheet *fake = create_spritesheet();
    REQUIRE(fake != nullptr);
    const unsigned char rle[] = { 2, 1, 1, 0 }; // one row: two pixels, end of line
    REQUIRE(add_sprite(fake, 2, 1, (int64_t)sizeof(rle), rle));
    custom_sprites = fake;

    CHECK(FeGuiPanelSpriteAvailable((int64_t)GUI_PANEL_SPRITES_COUNT));          // the loaded custom sprite
    CHECK_FALSE(FeGuiPanelSpriteAvailable((int64_t)(GUI_PANEL_SPRITES_COUNT + 1))); // past the loaded ones
    CHECK_FALSE(FeGuiPanelSpriteAvailable(32767));                             // unresolved icon name

    custom_sprites = saved;
    free_spritesheet(&fake);
    CHECK_FALSE(FeGuiPanelSpriteAvailable((int64_t)GUI_PANEL_SPRITES_COUNT));    // gone again
}
