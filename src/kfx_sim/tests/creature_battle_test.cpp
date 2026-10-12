// kfx_sim "creature" cluster, hard-tail depth increment: creature_battle.c
// was at 0% coverage. Its accessor/predicate family follows the same
// reserved-index-0-sentinel shape as actionpt.c/creature_control.c,
// already covered elsewhere in this plan -- creature_battle_get/_invalid/
// _exists/_get_from_thing, the melee/ranged opponent-count gates, and
// find_first_battle_of_mine/_last_battle_of_mine (which walk a real
// battle -> creature -> CreatureControl chain via battle_prev_creatr,
// the same linked-list-through-CreatureControl shape room_lair_test.cpp's
// calculate_free_lair_space used). The battle-list maintenance/combat
// dispatch functions (maintain_my_battle_list, set_creature_in_combat,
// get_flee_position) need a fuller Room/navigation fixture and are left
// for a later increment.
#include <catch2/catch_test_macros.hpp>

#include "creature_battle.h"
#include "creature_control.h"
#include "thing_data.h"
#include "dungeon_data.h"
#include "kfx_sim_state.h"

#include <cstring>

namespace {
struct ResetState {
    ResetState() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
    }
};
}

TEST_CASE_METHOD(ResetState, "creature_battle_get bounds-checks battle_id against 1..BATTLES_COUNT-1, reserving index 0", "[kfx_sim][creature_battle]") {
    CHECK(creature_battle_get(0) == INVALID_CRTR_BATTLE);
    CHECK(creature_battle_get(-1) == INVALID_CRTR_BATTLE);
    CHECK(creature_battle_get(BATTLES_COUNT) == INVALID_CRTR_BATTLE);
    CHECK(creature_battle_get(1) == &kfx_sim_state.battles[1]);
}

TEST_CASE_METHOD(ResetState, "creature_battle_invalid checks pointer identity against the reserved slot 0", "[kfx_sim][creature_battle]") {
    CHECK(creature_battle_invalid(INVALID_CRTR_BATTLE));
    CHECK(creature_battle_invalid(nullptr));
    CHECK_FALSE(creature_battle_invalid(&kfx_sim_state.battles[1]));
}

TEST_CASE_METHOD(ResetState, "creature_battle_exists is false for an invalid battle_id, else reads fighters_num", "[kfx_sim][creature_battle]") {
    CHECK_FALSE(creature_battle_exists(0));
    CHECK_FALSE(creature_battle_exists(5)); // fighters_num defaults to 0

    kfx_sim_state.battles[5].fighters_num = 2;
    CHECK(creature_battle_exists(5));
}

TEST_CASE_METHOD(ResetState, "creature_battle_get_from_thing requires a creature with a valid battle_id", "[kfx_sim][creature_battle]") {
    struct Thing *thing = thing_get(1);
    thing->class_id = TCls_Object;
    CHECK(creature_battle_get_from_thing(thing) == INVALID_CRTR_BATTLE); // wrong class

    thing->class_id = TCls_Creature;
    thing->ccontrol_idx = 0; // no CreatureControl assigned
    CHECK(creature_battle_get_from_thing(thing) == INVALID_CRTR_BATTLE);

    thing->ccontrol_idx = 1;
    struct CreatureControl *cctrl = creature_control_get(1);
    cctrl->battle_id = 0; // not in a battle
    CHECK(creature_battle_get_from_thing(thing) == INVALID_CRTR_BATTLE);

    cctrl->battle_id = 3;
    CHECK(creature_battle_get_from_thing(thing) == &kfx_sim_state.battles[3]);
}

TEST_CASE_METHOD(ResetState, "has/can_add_melee_combat_attacker read opponents_melee_count against COMBAT_MELEE_OPPONENTS_LIMIT", "[kfx_sim][creature_battle]") {
    struct Thing *victim = thing_get(1);
    victim->ccontrol_idx = 1;
    struct CreatureControl *cctrl = creature_control_get(1);

    cctrl->opponents_melee_count = 0;
    CHECK_FALSE(has_melee_combat_attackers(victim));
    CHECK(can_add_melee_combat_attacker(victim));

    cctrl->opponents_melee_count = COMBAT_MELEE_OPPONENTS_LIMIT;
    CHECK(has_melee_combat_attackers(victim));
    CHECK_FALSE(can_add_melee_combat_attacker(victim)); // at the limit, not below it
}

TEST_CASE_METHOD(ResetState, "has/can_add_ranged_combat_attacker read opponents_ranged_count against COMBAT_RANGED_OPPONENTS_LIMIT", "[kfx_sim][creature_battle]") {
    struct Thing *victim = thing_get(1);
    victim->ccontrol_idx = 1;
    struct CreatureControl *cctrl = creature_control_get(1);

    cctrl->opponents_ranged_count = 0;
    CHECK_FALSE(has_ranged_combat_attackers(victim));
    CHECK(can_add_ranged_combat_attacker(victim));

    cctrl->opponents_ranged_count = COMBAT_RANGED_OPPONENTS_LIMIT;
    CHECK(has_ranged_combat_attackers(victim));
    CHECK_FALSE(can_add_ranged_combat_attacker(victim));
}

TEST_CASE_METHOD(ResetState, "find_first_battle_of_mine/_last_battle_of_mine find the lowest/highest active battle involving the player", "[kfx_sim][creature_battle]") {
    // Battle 3: one fighter, owned by player 2.
    kfx_sim_state.battles[3].fighters_num = 1;
    kfx_sim_state.battles[3].first_creatr = 1;
    struct Thing *fighter3 = thing_get(1);
    fighter3->owner = 2;
    fighter3->ccontrol_idx = 1;
    creature_control_get(1)->battle_prev_creatr = 0; // end of this battle's creature list

    // Battle 7: one fighter, owned by player 2 as well.
    kfx_sim_state.battles[7].fighters_num = 1;
    kfx_sim_state.battles[7].first_creatr = 2;
    struct Thing *fighter7 = thing_get(2);
    fighter7->owner = 2;
    fighter7->ccontrol_idx = 2;
    creature_control_get(2)->battle_prev_creatr = 0;

    CHECK(find_first_battle_of_mine(2) == 3);
    CHECK(find_last_battle_of_mine(2) == 7);
    CHECK(find_first_battle_of_mine(5) == 0); // no battles involve player 5
}
