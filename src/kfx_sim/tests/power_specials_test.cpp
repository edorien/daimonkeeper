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
#include "kfx_sim_test_fixtures.h"
#include "creature_control.h"
#include "dungeon_data.h"
#include "config_magic.h"

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

namespace {
// Player 0 with a creature (thing 10) and a special digger (thing 11), both at level 3, and a familiar
// (thing 12) summoned by the creature with a spell whose summon level is the summoner's own.
struct IncreaseLevelFixture : kfx_test::ResetSimAndConfig {
    struct Thing *creature, *digger, *familiar;
    IncreaseLevelFixture() {
        kfx_test::make_player_active(0);
        struct Dungeon *dungeon = get_dungeon(0);
        for (int64_t m = 0; m < CREATURE_TYPES_MAX; m++)
            dungeon->creature_max_level[m] = CREATURE_MAX_LEVEL;
        creature = kfx_test::make_creature(10, 1, 0);
        digger = kfx_test::make_creature(11, 2, 0);
        familiar = kfx_test::make_creature(12, 3, 0);
        kfx_test::link_creature_into_player_list(&dungeon->creatr_list_start, 10);
        kfx_test::link_creature_into_player_list(&dungeon->digger_list_start, 11);
        dungeon->summon_list[0] = 12;
        dungeon->num_summon = 1;
        struct CreatureControl *famcctrl = creature_control_get_from_thing(familiar);
        famcctrl->summoner_idx = 10;
        famcctrl->summon_spl_idx = 1;
        kfx_config_state.conf.magic_conf.spell_config[1].crtr_summon_level = 0; // relative: the summoner's level
        for (struct Thing *t : {creature, digger, familiar})
            creature_control_get_from_thing(t)->exp_level = 3;
    }
    int64_t level(const struct Thing *t) { return creature_control_get_from_thing(t)->exp_level; }
};
}

TEST_CASE_METHOD(IncreaseLevelFixture, "a negative increase-level count lowers creatures, special diggers and familiars alike (pass 4 P4-F1)", "[kfx_sim][power_specials]") {
    // The Halloween maps' "Trick-or-Treat decreased the level of all minions!": the digger went up instead,
    // and the familiar stayed above its summoner.
    script_use_special_increase_level(0, -1);
    CHECK(level(creature) == 2);
    CHECK(level(digger) == 2);
    CHECK(level(familiar) == 2);
}

TEST_CASE_METHOD(IncreaseLevelFixture, "an increase-level count is limited to -9..9 whoever calls it (Lua did not limit it)", "[kfx_sim][power_specials]") {
    script_use_special_increase_level(0, -300); // a char used to make this 212 in the Lua binding
    CHECK(level(creature) == 0);
    CHECK(level(digger) == 0);
    CHECK(level(familiar) == 0);
}

TEST_CASE_METHOD(IncreaseLevelFixture, "an increase-level count of 1 or 0 flags every creature and special digger for one level up", "[kfx_sim][power_specials]") {
    for (int64_t count : {1, 0}) {
        for (struct Thing *t : {creature, digger})
            creature_control_get_from_thing(t)->exp_level_up = false;
        script_use_special_increase_level(0, count);
        CHECK(creature_control_get_from_thing(creature)->exp_level_up);
        CHECK(creature_control_get_from_thing(digger)->exp_level_up);
        CHECK(level(creature) == 3);
    }
}
