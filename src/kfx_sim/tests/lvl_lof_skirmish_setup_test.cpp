// docs/refactor/skirmish/ (S4): the .lof SKIRMISH_SETUP keyword (AUTO | ALLOW | LOCKED),
// parsed by level_lof_file_parse() into LevelInformation::skirmish_setup.
#include <catch2/catch_test_macros.hpp>

#include "lvl_filesdk1.h"
#include "config_campaigns.h"

#include <cstring>
#include <string>

// Defined in lvl_filesdk1.c but not part of its public header (only find_and_load_lof_files() uses it).
extern "C" TbBool level_lof_file_parse(const char *fname, char *buf, int64_t len);

namespace {

// level_lof_file_parse() wants a writable, null-terminated buffer.
LevelInformation *parse_lof(const char *fname, LevelNumber lvnum, const std::string &text)
{
    std::string buf = text;
    buf.push_back('\0');
    REQUIRE(level_lof_file_parse(fname, &buf[0], (int64_t)text.size()));
    return get_level_info(lvnum);
}

} // namespace

TEST_CASE("SKIRMISH_SETUP is read from a .lof file", "[kfx_sim][lof]") {
    LevelInformation *locked = parse_lof("map09901.lof", 9901, "NAME_TEXT = A\nSKIRMISH_SETUP = LOCKED\n");
    REQUIRE(locked != nullptr);
    CHECK(locked->skirmish_setup == SkirmishSetup_Locked);
    LevelInformation *allow = parse_lof("map09902.lof", 9902, "SKIRMISH_SETUP = allow\n");
    REQUIRE(allow != nullptr);
    CHECK(allow->skirmish_setup == SkirmishSetup_Allow);
    LevelInformation *none = parse_lof("map09903.lof", 9903, "NAME_TEXT = C\n");
    REQUIRE(none != nullptr);
    CHECK(none->skirmish_setup == SkirmishSetup_Auto);
    // A bad value is ignored (warned), not fatal.
    LevelInformation *bad = parse_lof("map09904.lof", 9904, "SKIRMISH_SETUP = MAYBE\n");
    REQUIRE(bad != nullptr);
    CHECK(bad->skirmish_setup == SkirmishSetup_Auto);
}
