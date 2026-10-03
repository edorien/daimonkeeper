// kfx_config: config_crtrmodel.c has a tiny public surface (4 functions
// + one NamedCommand table) despite being a 3039-line file -- almost
// all of it is per-creature-model default data tables reached only
// through the real creature.cfg/<creature>.cfg loading path.
//
// load_default_creaturemodel_config()/swap_creature() both funnel into
// a static load_creaturemodel_config() that constructs real paths via
// get_game_file_path_fmt() across several FGrp_* locations and
// the current level number, with no caller-supplied path -- same
// class of gap as config_settings_test.cpp's load_settings(). Only
// swap_creature()'s upfront bounds validation (which returns false
// before any file I/O) is tested here; load_default_creaturemodel_config()
// itself is a thin wrapper straight into that same real-I/O path and
// isn't attempted.
//
// creatmodel_properties_commands is a plain data table (bit-position
// numbers for a flag parser reached only through the untested loading
// path above) -- not given a standalone test, since nothing here
// exercises the parsing logic that interprets it.
#include <catch2/catch_test_macros.hpp>

#include "config_crtrmodel.h"
#include "config_creature.h"
#include "kfx_config_state.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "config.h"

namespace {
struct ResetConfigState {
    ResetConfigState() { std::memset(&kfx_config_state, 0, sizeof(kfx_config_state)); }
};
}

TEST_CASE_METHOD(ResetConfigState, "swap_creature fails immediately for a negative or too-large crtr_id or ncrt_id, before any file loading", "[kfx_config][config_crtrmodel]") {
    kfx_config_state.conf.crtr_conf.model_count = 2;

    // A real quirk, verified by an actual test failure: unlike
    // creature_stats_get()'s `< 1` check (model[0] is the reserved
    // "NOCREATURE" sentinel), swap_creature()'s own bounds check is
    // `< 0`, so 0 passes validation here and falls through into the real
    // file-loading path -- not attempted in this test file, so only
    // genuinely negative or too-large ids are exercised below.
    CHECK_FALSE(swap_creature(1, -1)); // crtr_id negative
    CHECK_FALSE(swap_creature(1, 5));  // crtr_id >= model_count
    CHECK_FALSE(swap_creature(-1, 1)); // ncrt_id negative
    CHECK_FALSE(swap_creature(5, 1));  // ncrt_id >= model_count
}

TEST_CASE_METHOD(ResetConfigState, "make_all_creatures_free zeroes training_cost/scavenger_cost/pay for every configured model", "[kfx_config][config_crtrmodel]") {
    kfx_config_state.conf.crtr_conf.model_count = 2;
    struct CreatureModelConfig *crconf = creature_stats_get(1);
    crconf->training_cost = 10;
    crconf->scavenger_cost = 20;
    crconf->pay = 30;

    CHECK(make_all_creatures_free());
    CHECK(crconf->training_cost == 0);
    CHECK(crconf->scavenger_cost == 0);
    CHECK(crconf->pay == 0);
}

TEST_CASE_METHOD(ResetConfigState, "change_max_health_of_creature_kind fails for an invalid (unconfigured) model without touching it", "[kfx_config][config_crtrmodel]") {
    // model_count defaults to 0 after reset, so creature_stats_get(1)
    // falls back to slot 0, which creature_stats_invalid() rejects.
    CHECK_FALSE(change_max_health_of_creature_kind(1, 500));
}

TEST_CASE_METHOD(ResetConfigState, "change_max_health_of_creature_kind updates health via saturate_set_signed even though it reports failure -- SimPort's unwired do_to_all_things_of_class_and_model returns 0", "[kfx_config][config_crtrmodel]") {
    kfx_config_state.conf.crtr_conf.model_count = 2;
    struct CreatureModelConfig *crconf = creature_stats_get(1);
    crconf->health = 100;

    // Not a bug this test works around: the function's own return value
    // reflects whether any live creature's health was actually updated
    // (do_to_all_things_of_class_and_model's default stub always returns
    // 0, i.e. "no things found"), which is independent of whether the
    // config table itself got updated.
    CHECK_FALSE(change_max_health_of_creature_kind(1, 500));
    CHECK(crconf->health == 500);
}

TEST_CASE_METHOD(ResetConfigState, "a sound number in a later creature file keeps an earlier custom sound, for every sound key (pass 3 F12)", "[kfx_config][config_crtrmodel]") {
    kfx_config_state.conf.crtr_conf.model_count = 2;
    struct CreatureSounds *snd = &kfx_config_state.conf.crtr_conf.creature_sounds[1];
    // Custom sounds (file paths) are stored as negative indices; an earlier file set these.
    snd->hit.index = -3;
    snd->die.index = -4;
    snd->piss.index = -5;
    snd->sad.index = -6;
    snd->happy.index = 5;
    const std::filesystem::path path = std::filesystem::temp_directory_path() / ("kfx_crtrmodel_sounds_" + std::to_string(getpid()) + ".cfg");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "; padded so the file is never too short to be read ................................\n";
        out << "[sounds]\nHit = 12 1\nDie = 13 1\nPiss = 14 1\nSad = NONE\nHappy = 15 2\n";
    }
    load_creaturemodel_config_file(1, path.string().c_str(), CnfLd_AcceptPartial | CnfLd_IgnoreErrors);
    std::filesystem::remove(path);
    CHECK(snd->hit.index == -3);
    CHECK(snd->die.index == -4);
    CHECK(snd->piss.index == -5);
    CHECK(snd->sad.index == 0);    // NONE silences, custom or not
    CHECK(snd->happy.index == 15); // not custom: the number replaces it
    CHECK(snd->happy.count == 2);
}
