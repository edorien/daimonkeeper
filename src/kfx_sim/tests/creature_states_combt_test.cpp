// kfx_sim "creature" cluster, hard-tail depth increment: the first test
// for creature_states_combt.c. Almost all of this file is line-of-sight/
// navigation-dependent combat-target selection (combat_has_line_of_sight,
// creature_can_have_combat_with_creature, ranged_combat_move, etc.) --
// genuinely the hard tail, left for a later increment. The battle-list
// linked-list bookkeeping is a different story: insert_thing_in_battle_list/
// remove_thing_from_battle_list/count_creatures_really_in_combat/
// cleanup_battle all operate purely on real Thing+CreatureControl slots
// and a CreatureBattle's first_creatr/last_creatr/battle_next_creatr/
// battle_prev_creatr fields -- the same shape as
// creature_battle_test.cpp's find_first_battle_of_mine coverage, just
// with insert/remove instead of read-only traversal.
#include <catch2/catch_test_macros.hpp>

#include "creature_states_combt.h"
#include "creature_battle.h"
#include "creature_control.h"
#include "creature_instances.h"
#include "kfx_config_state.h"
#include "config_magic.h"
#include "config_creature.h"
#include "kfx_sim_test_fixtures.h"

using namespace kfx_test;

TEST_CASE_METHOD(ResetSimAndConfig, "insert_thing_in_battle_list builds the battle's doubly-linked creature chain in insertion order", "[kfx_sim][creature_states_combt]") {
    struct Thing *thing1 = make_creature(1, 1, 0);
    struct Thing *thing2 = make_creature(2, 2, 0);

    insert_thing_in_battle_list(thing1, 5);
    struct CreatureBattle *battle = creature_battle_get(5);
    CHECK(battle->first_creatr == 1);
    CHECK(battle->last_creatr == 1);
    CHECK(battle->fighters_num == 1);

    insert_thing_in_battle_list(thing2, 5);
    CHECK(battle->first_creatr == 1); // unchanged -- thing1 is still first
    CHECK(battle->last_creatr == 2);
    CHECK(battle->fighters_num == 2);

    struct CreatureControl *cctrl1 = creature_control_get(1);
    struct CreatureControl *cctrl2 = creature_control_get(2);
    CHECK(cctrl1->battle_prev_creatr == 2); // linked to the newer arrival
    CHECK(cctrl2->battle_next_creatr == 1);
    CHECK(cctrl1->battle_id == 5);
    CHECK(cctrl2->battle_id == 5);
}

TEST_CASE_METHOD(ResetSimAndConfig, "count_creatures_really_in_combat counts only fighters with a nonzero combat_flags", "[kfx_sim][creature_states_combt]") {
    struct Thing *thing1 = make_creature(1, 1, 0);
    struct Thing *thing2 = make_creature(2, 2, 0);
    insert_thing_in_battle_list(thing1, 5);
    insert_thing_in_battle_list(thing2, 5);

    CHECK(count_creatures_really_in_combat(5) == 0);

    creature_control_get(1)->combat_flags = 1;
    CHECK(count_creatures_really_in_combat(5) == 1);
}

TEST_CASE_METHOD(ResetSimAndConfig, "remove_thing_from_battle_list unlinks a fighter and relinks its neighbors", "[kfx_sim][creature_states_combt]") {
    struct Thing *thing1 = make_creature(1, 1, 0);
    struct Thing *thing2 = make_creature(2, 2, 0);
    insert_thing_in_battle_list(thing1, 5);
    insert_thing_in_battle_list(thing2, 5);

    remove_thing_from_battle_list(thing1);

    struct CreatureBattle *battle = creature_battle_get(5);
    CHECK(battle->first_creatr == 2); // thing2 is now the sole/first fighter
    CHECK(battle->last_creatr == 2);
    CHECK(battle->fighters_num == 1);

    struct CreatureControl *cctrl1 = creature_control_get(1);
    CHECK(cctrl1->battle_id == 0);
    CHECK(cctrl1->battle_prev_creatr == 0);
    CHECK(cctrl1->battle_next_creatr == 0);
}

TEST_CASE_METHOD(ResetSimAndConfig, "cleanup_battle dissolves a battle once nobody in it is really fighting", "[kfx_sim][creature_states_combt]") {
    struct Thing *thing1 = make_creature(1, 1, 0);
    struct Thing *thing2 = make_creature(2, 2, 0);
    insert_thing_in_battle_list(thing1, 5);
    insert_thing_in_battle_list(thing2, 5);
    // Neither has combat_flags set -- count_creatures_really_in_combat is 0.

    CHECK(cleanup_battle(5));

    struct CreatureBattle *battle = creature_battle_get(5);
    CHECK(battle->first_creatr == 0);
    CHECK(battle->fighters_num == 0);
}

TEST_CASE_METHOD(ResetSimAndConfig, "cleanup_battle leaves an active battle alone", "[kfx_sim][creature_states_combt]") {
    struct Thing *thing1 = make_creature(1, 1, 0);
    insert_thing_in_battle_list(thing1, 5);
    creature_control_get(1)->combat_flags = 1;

    CHECK_FALSE(cleanup_battle(5));
    CHECK(creature_battle_get(5)->fighters_num == 1); // untouched
}

// add/remove_ranged_combat_attacker and add/remove_melee_combat_attacker:
// plain fixed-size array bookkeeping over CreatureControl::opponents_ranged[]/
// opponents_melee[] (COMBAT_RANGED_OPPONENTS_LIMIT/COMBAT_MELEE_OPPONENTS_LIMIT
// slots each) -- both had only a same-file forward declaration, added to
// creature_states_combt.h alongside their siblings.
TEST_CASE_METHOD(ResetSimAndConfig, "add_ranged_combat_attacker fills the first empty slot and refuses a duplicate or a full list", "[kfx_sim][creature_states_combt]") {
    struct Thing *victim = make_creature(1, 1, 0);
    struct CreatureControl *cctrl = creature_control_get(1);

    CHECK(add_ranged_combat_attacker(victim, 5));
    CHECK(cctrl->opponents_ranged[0] == 5);
    CHECK(cctrl->opponents_ranged_count == 1);

    CHECK(add_ranged_combat_attacker(victim, 5)); // already present: reports success, doesn't duplicate
    CHECK(cctrl->opponents_ranged_count == 1);

    CHECK(add_ranged_combat_attacker(victim, 6));
    CHECK(add_ranged_combat_attacker(victim, 7));
    CHECK(add_ranged_combat_attacker(victim, 8));
    CHECK(cctrl->opponents_ranged_count == 4); // COMBAT_RANGED_OPPONENTS_LIMIT

    CHECK_FALSE(add_ranged_combat_attacker(victim, 9)); // list is full
    CHECK(cctrl->opponents_ranged_count == 4); // unchanged
}

TEST_CASE_METHOD(ResetSimAndConfig, "remove_ranged_combat_attacker clears the matching slot, or reports failure if the fighter isn't present", "[kfx_sim][creature_states_combt]") {
    struct Thing *victim = make_creature(1, 1, 0);
    struct CreatureControl *cctrl = creature_control_get(1);
    add_ranged_combat_attacker(victim, 5);

    CHECK(remove_ranged_combat_attacker(victim, 5));
    CHECK(cctrl->opponents_ranged[0] == 0);
    CHECK(cctrl->opponents_ranged_count == 0);

    CHECK_FALSE(remove_ranged_combat_attacker(victim, 5)); // no longer present
}

TEST_CASE_METHOD(ResetSimAndConfig, "add_melee_combat_attacker/remove_melee_combat_attacker mirror the ranged pair over opponents_melee[]", "[kfx_sim][creature_states_combt]") {
    struct Thing *victim = make_creature(1, 1, 0);
    struct CreatureControl *cctrl = creature_control_get(1);

    CHECK(add_melee_combat_attacker(victim, 3));
    CHECK(cctrl->opponents_melee[0] == 3);
    CHECK(cctrl->opponents_melee_count == 1);

    CHECK(remove_melee_combat_attacker(victim, 3));
    CHECK(cctrl->opponents_melee[0] == 0);
    CHECK(cctrl->opponents_melee_count == 0);
    CHECK_FALSE(remove_melee_combat_attacker(victim, 3));
}

namespace {
struct Thing *make_cornered_melee_creature()
{
    kfx_config_state.conf.rules[0].creature.flee_zone_radius = 1536;
    struct Thing *creature = make_creature(1, 1, 0);
    creature->model = 1;
    creature->mappos.x.val = 5 * 256;
    creature->mappos.y.val = 5 * 256;
    struct CreatureControl *cctrl = creature_control_get(1);
    cctrl->flee_pos = creature->mappos;
    return creature;
}

struct Thing *make_enemy_creature()
{
    struct Thing *enemy = make_creature(2, 2, 1);
    enemy->model = 2;
    enemy->mappos.x.val = 6 * 256;
    enemy->mappos.y.val = 5 * 256;
    return enemy;
}
}

TEST_CASE_METHOD(ResetSimAndConfig, "creature_too_scared_for_combat: a melee creature in its flee zone still flees a time bomb carrier (upstream #5115)", "[kfx_sim][creature_states_combt]") {
    struct Thing *creature = make_cornered_melee_creature();
    struct Thing *enemy = make_enemy_creature();
    creature_control_get(2)->spell_flags |= CSAfF_Timebomb;

    CHECK(creature_too_scared_for_combat(creature, enemy));
}

TEST_CASE_METHOD(ResetSimAndConfig, "creature_too_scared_for_combat: a creature with a ranged weapon does not fear a time bomb carrier", "[kfx_sim][creature_states_combt]") {
    kfx_config_state.conf.crtr_conf.instances_count = 2;
    kfx_config_state.conf.magic_conf.instance_info[1].instance_property_flags = InstPF_RangedAttack;
    struct Thing *creature = make_cornered_melee_creature();
    struct Thing *enemy = make_enemy_creature();
    creature_control_get(1)->instance_available[1] = 1;
    creature_control_get(2)->spell_flags |= CSAfF_Timebomb;

    CHECK_FALSE(creature_too_scared_for_combat(creature, enemy));
}

TEST_CASE_METHOD(ResetSimAndConfig, "creature_too_scared_for_combat: the flee zone still exempts other fears", "[kfx_sim][creature_states_combt]") {
    struct Thing *creature = make_cornered_melee_creature();
    struct Thing *enemy = make_enemy_creature();
    kfx_config_state.conf.crtr_conf.model_count = 3;
    kfx_config_state.conf.crtr_conf.model[1].fear_wounded = 101;

    CHECK_FALSE(creature_too_scared_for_combat(creature, enemy));
}
