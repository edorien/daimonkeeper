// docs/refactor/editor/phase3/02-slice3-dialogs-menubar.md -- Catch2
// coverage for editor_level_save_dir() (editor_session.cpp): pure path
// logic, no live editor_open() session needed. Deliberately doesn't
// hardcode the expected path -- instead re-derives the full file path the
// same way editor_level_save_dir() itself does (get_level_fgroup() +
// prepare_file_fmtpath(), the same pattern ftest_editor_save_reload.c had
// duplicated inline before this slice factored it out) and checks the
// returned directory is that path with the filename stripped off.
//
// FGrp_CmpgLvls (what get_level_fgroup() always resolves to) needs
// campaign.levels_location and keeper_runtime_directory both non-empty to
// resolve to anything at all (config.c's _resolve_file_path_internal) --
// neither is set in the bare test binary by default, so this test sets
// synthetic values for both. Safe to do so here: unlike
// config_settings_test.cpp/highscores_test.cpp's own documented gap (see
// their header comments), this is pure string construction, no file I/O.
#include <catch2/catch_test_macros.hpp>

#include "kfx_editor.h"
#include "config.h"
#include "config_keeperfx.h" // keeper_runtime_directory
#include "config_campaigns.h" // campaign.levels_location

#include <cstring>
#include <string>

namespace {

struct CampaignLevelsLocationFixture {
    std::string prev_runtime_dir;
    std::string prev_levels_location;
    CampaignLevelsLocationFixture() {
        prev_runtime_dir = keeper_runtime_directory;
        prev_levels_location = campaign.levels_location;
        std::strncpy(keeper_runtime_directory, "kfx_test_root", sizeof(keeper_runtime_directory) - 1);
        keeper_runtime_directory[sizeof(keeper_runtime_directory) - 1] = '\0';
        std::strncpy(campaign.levels_location, "levels", sizeof(campaign.levels_location) - 1);
        campaign.levels_location[sizeof(campaign.levels_location) - 1] = '\0';
    }
    ~CampaignLevelsLocationFixture() {
        std::strncpy(keeper_runtime_directory, prev_runtime_dir.c_str(), sizeof(keeper_runtime_directory) - 1);
        keeper_runtime_directory[sizeof(keeper_runtime_directory) - 1] = '\0';
        std::strncpy(campaign.levels_location, prev_levels_location.c_str(), sizeof(campaign.levels_location) - 1);
        campaign.levels_location[sizeof(campaign.levels_location) - 1] = '\0';
    }
};

} // namespace

TEST_CASE_METHOD(CampaignLevelsLocationFixture, "editor_level_save_dir strips the filename off the level's own file path", "[kfx_editor][editor_level_save_dir]") {
    LevelNumber lvnum = 1;
    char dir[512];
    editor_level_save_dir(lvnum, dir, sizeof(dir));

    short fgroup = get_level_fgroup(lvnum);
    char *full = prepare_file_fmtpath(fgroup, "map%05lu.slb", (unsigned long)lvnum);
    std::string full_str(full);
    std::string dir_str(dir);

    REQUIRE(full_str.size() > dir_str.size());
    CHECK(full_str.compare(0, dir_str.size(), dir_str) == 0);
    CHECK(full_str.substr(dir_str.size()) == "/map00001.slb");
}

TEST_CASE_METHOD(CampaignLevelsLocationFixture, "editor_level_save_dir truncates safely into a too-small buffer", "[kfx_editor][editor_level_save_dir]") {
    char tiny[4];
    editor_level_save_dir(1, tiny, sizeof(tiny));
    CHECK(std::strlen(tiny) <= 3);
}
