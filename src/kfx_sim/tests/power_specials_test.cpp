// kfx_sim: power_specials.c.
//
// activate_dungeon_special(), the one script_lua_on_special_box_activate()
// call site, is a large orchestration function (dungeon/
// thing/config state throughout) -- not attempted here, the same
// "process_*-shaped, needs a fuller context" call this whole plan has
// made for similar functions elsewhere. Instead: box_thing_to_special()
// (pattern A, self-contained, called from several other files too) and
// activate_bonus_level(), which reaches kfx_game through GamePort (faked
// here).
#include <catch2/catch_test_macros.hpp>

#include "power_specials.h"
#include "thing_objects.h" // box_thing_to_special
#include "thing_data.h"
#include "kfx_sim_state.h"
#include "kfx_config_state.h"
#include "player_data.h"
#include "ports/game_port.h"

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

TbBool g_fake_activate_result = false;
LevelNumber g_captured_sp_lvnum = -1;
TbBool fake_activate_bonus_level_for_singleplayer(struct PlayerInfo *player, uint64_t sp_lvnum) {
    (void)player;
    g_captured_sp_lvnum = sp_lvnum;
    return g_fake_activate_result;
}

struct GamePortFixture : ResetSimState {
    struct GamePort callbacks = game_port_defaults;
    struct PlayerInfo player{};

    GamePortFixture() {
        kfx_sim_state.loaded_level_number = 0;
        g_fake_activate_result = false;
        g_captured_sp_lvnum = -1;
        callbacks.activate_bonus_level_for_singleplayer = fake_activate_bonus_level_for_singleplayer;
        set_game_port(&callbacks);
        kfx_sim_state.operation_flags |= GOF_SingleLevel;
    }
    ~GamePortFixture() { set_game_port(nullptr); } // restores the default no-op table
};
}

TEST_CASE_METHOD(GamePortFixture, "activate_bonus_level passes the loaded level number through to the provider and returns its result", "[kfx_sim][power_specials]") {
    kfx_sim_state.loaded_level_number = 7;
    g_fake_activate_result = true;
    CHECK(activate_bonus_level(&player));
    CHECK(g_captured_sp_lvnum == 7);
}

TEST_CASE_METHOD(GamePortFixture, "activate_bonus_level clears GOF_SingleLevel unconditionally, even when the provider fails", "[kfx_sim][power_specials]") {
    g_fake_activate_result = false;
    CHECK_FALSE(activate_bonus_level(&player));
    CHECK((kfx_sim_state.operation_flags & GOF_SingleLevel) == 0);
}
