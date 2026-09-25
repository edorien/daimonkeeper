// Catch2 coverage for cfgc_help.cpp (plan 03 F4 / plan 10 §4.2).
#include <catch2/catch_test_macros.hpp>

#include "cfgc_help.h"
#include "kfx_config_test_paths.h" // KFX_CONFIG_TEST_REPO_ROOT

#include <fstream>
#include <sstream>

TEST_CASE("help: comment lines directly above a key become its help", "[cfgc_help]")
{
    const auto h = cfgc_extract_help(ConfigDocument::parse(
        "; file header\n\n[game]\n; Game turns between pay days.\nPayDayGap = 10000\n\n; detached\n\nNoHelp = 1\n"
        "; first line\n; second line\nTwoLines = 2\n[trap3]\n;Health of the trap.\nHealth = 5\n[trap4]\n; other\nHealth = 6\n"));
    CHECK(h.at("game/paydaygap") == "Game turns between pay days.");
    CHECK(h.count("game/nohelp") == 0);
    CHECK(h.at("game/twolines") == "first line second line");
    CHECK(h.at("trap/health") == "Health of the trap."); // numbered blocks share help; the first one wins
    CHECK(cfgc_help_key("trap12", "HEALTH") == "trap/health");
}

TEST_CASE("help: the base rules file documents most of its keys", "[cfgc_help][corpus]")
{
    std::ifstream f(std::string(KFX_CONFIG_TEST_REPO_ROOT) + "/config/fxdata/rules.cfg", std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    const auto h = cfgc_extract_help(ConfigDocument::parse(ss.str()));
    CHECK(h.at("game/paydaygap") == "Game turns between pay days.");
    CHECK(h.size() > 60);
}
