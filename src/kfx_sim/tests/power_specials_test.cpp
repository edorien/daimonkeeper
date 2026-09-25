// kfx_sim: power_specials.c -- targeted per the user's request to focus
// coverage around scripts/check_layering.py's CURRENTLY-FAILING (not
// accepted) kfx_sim -> kfx_script violation: this file #includes
// kfx_script's api.h, but only for one symbol, script_hooks (the
// ScriptHookCallbacks global). api.h itself just re-includes
// kfx_config's own script_hooks.h (see that header's own comment: "the
// type is defined there and just used here" -- kfx_config is already a
// legitimate lower-ranked dependency of kfx_sim), so the fix path is the
// same shape as ariadne_regions_test.cpp's PLAYERS_COUNT finding: swap
// the include for the header that actually defines the symbol.
//
// activate_dungeon_special(), the one script_hooks->lua_on_special_box_
// activate() call site, is a large orchestration function (dungeon/
// thing/config state throughout) -- not attempted here, the same
// "process_*-shaped, needs a fuller context" call this whole plan has
// made for similar functions elsewhere. Instead: box_thing_to_special()
// (pattern A, self-contained, called from several other files too) and
// activate_bonus_level() -- kfx_sim's first SimFeedbackCallbacks
// pattern-B test in this whole plan, despite sim_feedback being called
// through pervasively across kfx_sim.
#include <catch2/catch_test_macros.hpp>

#include "power_specials.h"
#include "thing_objects.h" // box_thing_to_special
#include "thing_data.h"
#include "kfx_sim_state.h"
#include "kfx_config_state.h"
#include "sim_feedback.h"
#include "player_data.h"

#include <cstring>

namespace {
struct ResetSimState {
    ResetSimState() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
        kfx_config_state.conf.object_conf.object_types_count = OBJECT_TYPES_MAX;
    }
};
}

TEST_CASE_METHOD(ResetSimState, "box_thing_to_special is 0 for an invalid thing", "[kfx_sim][power_specials]") {
    CHECK(box_thing_to_special(thing_get(0)) == 0);
}

TEST_CASE_METHOD(ResetSimState, "box_thing_to_special is 0 for a thing that isn't a TCls_Object", "[kfx_sim][power_specials]") {
    struct Thing *thing = thing_get(1);
    thing->alloc_flags |= TAlF_Exists;
    thing->class_id = TCls_Shot;
    thing->model = 5;
    kfx_config_state.conf.object_conf.object_to_special_artifact[5] = SpcKind_Resurrect;
    CHECK(box_thing_to_special(thing) == 0); // right model, wrong class -- must not match anyway
}

TEST_CASE_METHOD(ResetSimState, "box_thing_to_special is 0 once the model is out of the configured range", "[kfx_sim][power_specials]") {
    struct Thing *thing = thing_get(1);
    thing->alloc_flags |= TAlF_Exists;
    thing->class_id = TCls_Object;
    thing->model = 100;
    kfx_config_state.conf.object_conf.object_types_count = 50; // model 100 is now out of range
    CHECK(box_thing_to_special(thing) == 0);
}

TEST_CASE_METHOD(ResetSimState, "box_thing_to_special looks up the configured special kind for an in-range object model", "[kfx_sim][power_specials]") {
    struct Thing *thing = thing_get(1);
    thing->alloc_flags |= TAlF_Exists;
    thing->class_id = TCls_Object;
    thing->model = 5;
    kfx_config_state.conf.object_conf.object_to_special_artifact[5] = SpcKind_TrnsfrCrtr;
    CHECK(box_thing_to_special(thing) == SpcKind_TrnsfrCrtr);
}

namespace {
LevelNumber g_fake_loaded_level = 0;
LevelNumber fake_get_loaded_level_number(void) { return g_fake_loaded_level; }

TbBool g_fake_activate_result = false;
LevelNumber g_captured_sp_lvnum = -1;
TbBool fake_activate_bonus_level_for_singleplayer(struct PlayerInfo *player, uint64_t sp_lvnum) {
    (void)player;
    g_captured_sp_lvnum = sp_lvnum;
    return g_fake_activate_result;
}

struct SimFeedbackFixture : ResetSimState {
    struct SimFeedbackCallbacks callbacks{};
    struct PlayerInfo player{};

    SimFeedbackFixture() {
        g_fake_loaded_level = 0;
        g_fake_activate_result = false;
        g_captured_sp_lvnum = -1;
        callbacks.get_loaded_level_number = fake_get_loaded_level_number;
        callbacks.activate_bonus_level_for_singleplayer = fake_activate_bonus_level_for_singleplayer;
        set_sim_feedback_callbacks(&callbacks);
        kfx_sim_state.operation_flags |= GOF_SingleLevel;
    }
    ~SimFeedbackFixture() { set_sim_feedback_callbacks(nullptr); } // restores the default no-op table
};
}

TEST_CASE_METHOD(SimFeedbackFixture, "activate_bonus_level passes the loaded level number through to the provider and returns its result", "[kfx_sim][power_specials]") {
    g_fake_loaded_level = 7;
    g_fake_activate_result = true;
    CHECK(activate_bonus_level(&player));
    CHECK(g_captured_sp_lvnum == 7);
}

TEST_CASE_METHOD(SimFeedbackFixture, "activate_bonus_level clears GOF_SingleLevel unconditionally, even when the provider fails", "[kfx_sim][power_specials]") {
    g_fake_activate_result = false;
    CHECK_FALSE(activate_bonus_level(&player));
    CHECK((kfx_sim_state.operation_flags & GOF_SingleLevel) == 0);
}
