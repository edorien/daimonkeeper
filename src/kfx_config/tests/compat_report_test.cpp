// kfx_config: compat_report.c, and the config parsers' unknown-key hooks
// into it (config.c's recognize_conf_command() for the legacy command
// parsers, assign_conf_command_field() for the NamedField ones).
//
// The last case sweeps the shipped config/fxdata/*.cfg: every key in them
// must be known, or the report would warn about our own data on every
// level load -- that's what keeps the hooks honest (no false alarms).
#include <catch2/catch_test_macros.hpp>

#include "kfx_config_test_paths.h" // KFX_CONFIG_TEST_FIXTURES_DIR, KFX_CONFIG_TEST_REPO_ROOT
#include "compat_report.h"
#include "config.h"
#include "config_creature.h"
#include "config_crtrstates.h"
#include "config_compp.h"
#include "config_cubes.h"
#include "config_lenses.h"
#include "config_magic.h"
#include "config_objects.h"
#include "config_rules.h"
#include "config_sounds.h"
#include "config_terrain.h"
#include "config_trapdoor.h"
#include "config_players.h"
#include "config_effects.h"
#include "kfx_config_state.h"

#include <cstdio>
#include <cstring>
#include <string>

#include <unistd.h>

namespace {
struct ResetReport {
    ResetReport() {
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
        compat_report_clear();
    }
};

std::string issues_text(void)
{
    std::string text;
    for (int64_t i = 0; i < compat_report_count(); i++) {
        const struct CompatIssue *issue = compat_report_get(i);
        text += std::string(compat_issue_kind_name(issue->kind)) + " '" + issue->what + "' in "
            + issue->where + " line " + std::to_string(issue->line) + "\n";
    }
    return text;
}

// A cubes.cfg-shaped file (NamedField parser) with one key this build doesn't know.
std::string write_cubes_with_unknown_key(void)
{
    const std::string path = "compat_report_test_" + std::to_string(getpid()) + ".cfg";
    std::FILE *f = std::fopen(path.c_str(), "wb");
    REQUIRE(f);
    std::fputs("[cube0]\nName = CUBE_A\nFutureFeature = 7\nTextures = 1 2 3 4 5 6\n", f);
    std::fclose(f);
    return path;
}
}

TEST_CASE_METHOD(ResetReport, "compat_report deduplicates by kind and name, keeping the first location", "[kfx_config][compat_report]") {
    compat_report_add(CompatIssue_ScriptCommand, "SET_BOX_TOOLTIP", NULL, 12);
    compat_report_add(CompatIssue_ScriptCommand, "set_box_tooltip", NULL, 40);
    compat_report_add(CompatIssue_ScriptName, "SET_BOX_TOOLTIP", NULL, 50); // same text, other kind
    compat_report_add(CompatIssue_ConfigKey, "UpdateTime", "/some/dir/objects.cfg", 3);
    compat_report_add(CompatIssue_ConfigKey, "", NULL, 4);                // ignored

    REQUIRE(compat_report_count() == 3);
    const struct CompatIssue *cmd = compat_report_get(0);
    CHECK(std::string(cmd->what) == "SET_BOX_TOOLTIP");
    CHECK(cmd->line == 12);
    CHECK(cmd->count == 2);
    CHECK(compat_report_get(1)->kind == CompatIssue_ScriptName);
    CHECK(std::string(compat_report_get(2)->where) == "objects.cfg"); // directory dropped
    CHECK(compat_report_get(3) == NULL);

    compat_report_clear();
    CHECK(compat_report_count() == 0);
}

TEST_CASE_METHOD(ResetReport, "compat_report counts what doesn't fit instead of growing", "[kfx_config][compat_report]") {
    for (int64_t i = 0; i < COMPAT_ISSUES_MAX + 5; i++)
        compat_report_add(CompatIssue_ConfigKey, ("Key" + std::to_string(i)).c_str(), NULL, (uint64_t)i);
    CHECK(compat_report_count() == COMPAT_ISSUES_MAX);
    CHECK(compat_report_overflow() == 5);
}

TEST_CASE_METHOD(ResetReport, "recognize_conf_command reports an unknown legacy config command", "[kfx_config][compat_report]") {
    static const struct NamedCommand commands[] = { {"KNOWN", 1}, {NULL, 0} };
    const char buf[] = "FUTURECOMMAND = 3\n";
    int64_t pos = 0;
    CHECK(recognize_conf_command(buf, &pos, (int64_t)std::strlen(buf), commands) == ccr_unrecognised);
    REQUIRE(compat_report_count() == 1);
    CHECK(compat_report_get(0)->kind == CompatIssue_ConfigKey);
    CHECK(std::string(compat_report_get(0)->what) == "FUTURECOMMAND");

    pos = 0;
    const char known[] = "KNOWN = 1\n";
    CHECK(recognize_conf_command(known, &pos, (int64_t)std::strlen(known), commands) == 1);
    CHECK(compat_report_count() == 1);
}

TEST_CASE_METHOD(ResetReport, "a NamedField config reports an unknown key, with its file name", "[kfx_config][compat_report]") {
    const std::string path = write_cubes_with_unknown_key();
    CHECK(keeper_cubes_file_data.load_func(path.c_str(), 0));
    std::remove(path.c_str());
    INFO(issues_text());
    REQUIRE(compat_report_count() == 1);
    CHECK(std::string(compat_report_get(0)->what) == "FutureFeature");
    CHECK(std::string(compat_report_get(0)->where) == path);
    CHECK(compat_report_get(0)->line > 0);
    // The known keys around it still loaded.
    CHECK(std::strcmp(get_cube_model_stats(0)->code_name, "CUBE_A") == 0);
}

TEST_CASE_METHOD(ResetReport, "a list-only pass doesn't report the keys it deliberately skips", "[kfx_config][compat_report]") {
    const std::string path = write_cubes_with_unknown_key();
    CHECK(keeper_cubes_file_data.load_func(path.c_str(), CnfLd_ListOnly));
    std::remove(path.c_str());
    INFO(issues_text());
    CHECK(compat_report_count() == 0);
}

TEST_CASE_METHOD(ResetReport, "every key in the shipped config/fxdata configs is known", "[kfx_config][compat_report]") {
    const struct ConfigFileData *files[] = {
        &keeper_creaturetp_file_data, &creature_states_file_data, &keeper_keepcomp_file_data,
        &keeper_cubes_file_data, &keeper_lenses_file_data, &keeper_magic_file_data,
        &keeper_objects_file_data, &keeper_rules_file_data, &keeper_sounds_file_data,
        &keeper_terrain_file_data, &keeper_trapdoor_file_data,
    };
    for (const struct ConfigFileData *file : files) {
        const std::string path = std::string(KFX_CONFIG_TEST_REPO_ROOT "/config/fxdata/") + file->filename;
        INFO(path);
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
        compat_report_clear();
        file->load_func(path.c_str(), 0);
        INFO(issues_text());
        CHECK(compat_report_count() == 0);
    }
}

// --- Values, sources, limits (the remaining compat-check gaps) --------------

namespace {
std::string write_cfg(const char *tag, const char *text)
{
    const std::string path = std::string("compat_report_test_") + std::to_string(getpid()) + "_" + tag + ".cfg";
    std::FILE *f = std::fopen(path.c_str(), "wb");
    REQUIRE(f);
    std::fputs(text, f);
    std::fclose(f);
    return path;
}
}

TEST_CASE_METHOD(ResetReport, "a named value is only an issue if it still doesn't resolve once everything has loaded", "[kfx_config][compat_report]") {
    // A name table that gains an entry "later" -- as when the config defining a name loads after the one using it.
    struct NamedCommand names[3] = { {"KNOWN", 1}, {NULL, 0}, {NULL, 0} };
    compat_report_add_value("Kind", "LATER", "/x/objects.cfg", 7, names);
    compat_report_add_value("Kind", "NEVER", "/x/objects.cfg", 8, names);
    CHECK(compat_report_count() == 0); // nothing reported before the whole load is done
    names[1] = NamedCommand{"LATER", 2};
    compat_report_resolve_pending();
    INFO(issues_text());
    REQUIRE(compat_report_count() == 1);
    CHECK(compat_report_get(0)->kind == CompatIssue_ConfigValue);
    CHECK(std::string(compat_report_get(0)->what) == "Kind=NEVER");
    CHECK(std::string(compat_report_get(0)->where) == "objects.cfg");
    CHECK(compat_report_get(0)->line == 8);
}

TEST_CASE_METHOD(ResetReport, "an unknown flag in a config is reported as a value", "[kfx_config][compat_report]") {
    const std::string path = write_cfg("flags", "[cube0]\nName = CUBE_A\nProperties = LAVA FUTURE_FLAG\n");
    CHECK(keeper_cubes_file_data.load_func(path.c_str(), 0));
    std::remove(path.c_str());
    compat_report_resolve_pending();
    INFO(issues_text());
    REQUIRE(compat_report_count() == 1);
    CHECK(std::string(compat_report_get(0)->what) == "Properties=FUTURE_FLAG");
    CHECK((get_cube_model_stats(0)->properties_flags & CPF_IsLava) != 0); // the known flag still applied
}

TEST_CASE_METHOD(ResetReport, "blocks beyond this build's table size are reported, not silently skipped", "[kfx_config][compat_report]") {
    const std::string path = write_cfg("limit", "[cube0]\nName = CUBE_A\n[cube5000]\nName = CUBE_FAR\n");
    CHECK(keeper_cubes_file_data.load_func(path.c_str(), 0));
    std::remove(path.c_str());
    INFO(issues_text());
    REQUIRE(compat_report_count() == 1);
    CHECK(compat_report_get(0)->kind == CompatIssue_Limit);
    CHECK(std::string(compat_report_get(0)->what).rfind("[cube5000]", 0) == 0);
}

TEST_CASE_METHOD(ResetReport, "issues from a parser that doesn't know its file get the file being loaded", "[kfx_config][compat_report]") {
    static const struct NamedCommand commands[] = { {"KNOWN", 1}, {NULL, 0} };
    const char buf[] = "FUTURECOMMAND = 3\n";
    int64_t pos = 0;
    compat_report_set_source("/some/campaign/rules.cfg");
    recognize_conf_command(buf, &pos, (int64_t)std::strlen(buf), commands);
    compat_report_set_source(NULL);
    REQUIRE(compat_report_count() == 1);
    CHECK(std::string(compat_report_get(0)->where) == "rules.cfg");
    char text[256];
    compat_issue_describe(compat_report_get(0), text, sizeof(text));
    CHECK(std::string(text).find("config key 'FUTURECOMMAND' (rules.cfg") == 0);
}

TEST_CASE_METHOD(ResetReport, "every named value in the shipped config/fxdata resolves once they've all loaded", "[kfx_config][compat_report]") {
    // The same passes, order and flags as the game's load_stats_files() (kfx_sim):
    // a list-only pass to learn the names, sounds, then the full loads.
    auto load = [](const struct ConfigFileData *file, int64_t flags) {
        file->load_func((std::string(KFX_CONFIG_TEST_REPO_ROOT "/config/fxdata/") + file->filename).c_str(), flags);
    };
    for (const struct ConfigFileData *file : {&keeper_creaturetp_file_data, &keeper_terrain_file_data,
            &keeper_objects_file_data, &keeper_trapdoor_file_data, &keeper_effects_file_data,
            &keeper_lenses_file_data, &keeper_magic_file_data, &creature_states_file_data,
            &keeper_playerstates_file_data})
        load(file, CnfLd_ListOnly);
    load(&keeper_sounds_file_data, CnfLd_Standard);
    for (const struct ConfigFileData *file : {&keeper_terrain_file_data, &keeper_objects_file_data,
            &keeper_trapdoor_file_data, &keeper_effects_file_data, &keeper_lenses_file_data,
            &keeper_magic_file_data, &keeper_creaturetp_file_data, &creature_states_file_data})
        load(file, CnfLd_Standard | CnfLd_PreListed);
    for (const struct ConfigFileData *file : {&keeper_rules_file_data, &keeper_cubes_file_data})
        load(file, CnfLd_Standard);
    load(&keeper_playerstates_file_data, CnfLd_Standard | CnfLd_PreListed);
    load(&keeper_keepcomp_file_data, CnfLd_Standard);
    compat_report_resolve_pending();
    INFO(issues_text());
    CHECK(compat_report_count() == 0);
}
