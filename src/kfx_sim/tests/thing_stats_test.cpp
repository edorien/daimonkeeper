// kfx_sim "thing" cluster depth increment, per docs/refactor/testing/
// comprehensive/stage-08b-kfx-sim-clusters.md's "still open" list:
// thing_stats.c's pure computation functions. get_radially_decaying_value/
// get_radially_growing_value/compute_controlled_speed_increase/_decrease
// are self-contained integer arithmetic, no state at all; is_neutral_thing/
// is_hero_thing and compute_creature_max_health are pattern A on
// kfx_sim_state/kfx_config_state, same fixture used throughout this plan.
//
// Depth increment: the sibling compute_creature_max_*(base_param, exp_level)
// family (strength/armour/defense/dexterity/loyalty/pay/training_cost/
// scavenging_cost) -- all pattern A on kfx_config_state.conf.crtr_conf.exp,
// sharing compute_creature_max_health's "base+percent-per-level, clamped
// at CREATURE_MAX_LEVEL-1" shape but each varying its own base_param
// range-clamp and overflow-saturation width. compute_creature_max_strength
// is a genuine outlier, confirmed by reading the body rather than assumed
// from family resemblance: it has neither a base_param<=0 floor nor an
// upper base_param clamp, unlike every other sibling.
#include <catch2/catch_test_macros.hpp>

#include "thing_stats.h"
#include "thing_data.h"
#include "player_data.h"
#include "slab_data.h"
#include "kfx_sim_state.h"
#include "kfx_config_state.h"
#include "config_creature.h"

#include <cstring>

namespace {
struct ResetSimState {
    ResetSimState() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
    }
};
}

TEST_CASE("get_radially_decaying_value returns full magnitude before decay_start", "[kfx_sim][thing_stats]") {
    CHECK(get_radially_decaying_value(100, 10, 10, 5) == 100);
    CHECK(get_radially_decaying_value(100, 10, 10, 10) == 100); // at the boundary, still full
}

TEST_CASE("get_radially_decaying_value decays linearly through the decay region", "[kfx_sim][thing_stats]") {
    CHECK(get_radially_decaying_value(100, 10, 10, 15) == 50); // halfway through
}

TEST_CASE("get_radially_decaying_value is zero at and past decay_start + decay_length", "[kfx_sim][thing_stats]") {
    CHECK(get_radially_decaying_value(100, 10, 10, 20) == 0);
    CHECK(get_radially_decaying_value(100, 10, 10, 999) == 0);
}

TEST_CASE("get_radially_growing_value is zero past the max range", "[kfx_sim][thing_stats]") {
    CHECK(get_radially_growing_value(100, 10, 10, 20, 1) == 0);
}

TEST_CASE("get_radially_growing_value returns the decayed magnitude unchanged when the push distance doesn't overshoot", "[kfx_sim][thing_stats]") {
    // A huge friction shrinks the intended push distance (5) to no more
    // than the actual distance (5), so the function falls through to
    // returning magnitude as-is rather than clamping.
    CHECK(get_radially_growing_value(10, 1000, 100, 5, 1000000) == 10);
}

TEST_CASE("get_radially_growing_value returns a negative pull-back value when the push would overshoot the epicenter", "[kfx_sim][thing_stats]") {
    CHECK(get_radially_growing_value(100, 0, 1000, 500, 1) == -2);
}

TEST_CASE("compute_controlled_speed_increase steps by 1 below speed_limit 4, else by speed_limit/4", "[kfx_sim][thing_stats]") {
    CHECK(compute_controlled_speed_increase(0, 3) == 1);
    CHECK(compute_controlled_speed_increase(0, 20) == 5);
}

TEST_CASE("compute_controlled_speed_increase clamps into [-speed_limit, speed_limit]", "[kfx_sim][thing_stats]") {
    CHECK(compute_controlled_speed_increase(5, 3) == 3);     // 6 clamped down to 3
    CHECK(compute_controlled_speed_increase(-30, 20) == -20); // -25 clamped up to -20
}

TEST_CASE("compute_controlled_speed_decrease steps by 1 below speed_limit 4, else by speed_limit/4", "[kfx_sim][thing_stats]") {
    CHECK(compute_controlled_speed_decrease(0, 3) == -1);
    CHECK(compute_controlled_speed_decrease(0, 20) == -5);
}

TEST_CASE("compute_controlled_speed_decrease clamps into [-speed_limit, speed_limit]", "[kfx_sim][thing_stats]") {
    CHECK(compute_controlled_speed_decrease(-5, 3) == -3);  // -6 clamped up to -3
    CHECK(compute_controlled_speed_decrease(30, 20) == 20); // 25 clamped down to 20
}

TEST_CASE_METHOD(ResetSimState, "compute_creature_max_health applies the configured per-level percentage", "[kfx_sim][thing_stats]") {
    kfx_config_state.conf.crtr_conf.exp.health_increase_on_exp = 10; // 10% per level
    CHECK(compute_creature_max_health(100, 0) == 100);
    CHECK(compute_creature_max_health(100, 5) == 150);
}

TEST_CASE_METHOD(ResetSimState, "compute_creature_max_health clamps exp_level to CREATURE_MAX_LEVEL - 1", "[kfx_sim][thing_stats]") {
    kfx_config_state.conf.crtr_conf.exp.health_increase_on_exp = 10;
    CHECK(compute_creature_max_health(100, CREATURE_MAX_LEVEL) == compute_creature_max_health(100, CREATURE_MAX_LEVEL - 1));
    CHECK(compute_creature_max_health(100, CREATURE_MAX_LEVEL + 5) == compute_creature_max_health(100, CREATURE_MAX_LEVEL - 1));
}

TEST_CASE_METHOD(ResetSimState, "is_neutral_thing compares owner against the configured neutral player number", "[kfx_sim][thing_stats]") {
    kfx_config_state.neutral_player_num = 5;
    struct Thing *thing = thing_get(1);
    thing->owner = 5;
    CHECK(is_neutral_thing(thing));
    thing->owner = 2;
    CHECK_FALSE(is_neutral_thing(thing));
}

TEST_CASE_METHOD(ResetSimState, "is_hero_thing reads the owning player's roaming player_type", "[kfx_sim][thing_stats]") {
    struct Thing *thing = thing_get(1);
    thing->owner = 3;
    struct PlayerInfo *owner = get_player(3);

    owner->player_type = PT_Keeper;
    CHECK_FALSE(is_hero_thing(thing));

    owner->player_type = PT_Roaming;
    CHECK(is_hero_thing(thing));
}

TEST_CASE_METHOD(ResetSimState, "compute_creature_max_strength scales by percent-per-level with no base_param floor or ceiling", "[kfx_sim][thing_stats]") {
    kfx_config_state.conf.crtr_conf.exp.strength_increase_on_exp = 10; // 10% per level
    CHECK(compute_creature_max_strength(100, 0) == 100);
    CHECK(compute_creature_max_strength(100, 5) == 150);
    // Unlike every other compute_creature_max_* sibling, a non-positive
    // base_param is neither floored to 0 nor otherwise special-cased.
    CHECK(compute_creature_max_strength(-10, 5) == -15);
}

TEST_CASE_METHOD(ResetSimState, "compute_creature_max_strength clamps to UCHAR_MAX+1 only under ClscBug_Overflow8bitVal", "[kfx_sim][thing_stats]") {
    kfx_config_state.conf.crtr_conf.exp.strength_increase_on_exp = 1000; // guarantees overflow past 256
    CHECK(compute_creature_max_strength(100, 5) > 256); // unclamped by default

    kfx_config_state.conf.rules[0].gameplay.classic_bugs_flags = ClscBug_Overflow8bitVal;
    CHECK(compute_creature_max_strength(100, 5) == 256);
}

TEST_CASE_METHOD(ResetSimState, "compute_creature_max_armour floors a non-positive base_param to 0 and clamps the input at 60000", "[kfx_sim][thing_stats]") {
    kfx_config_state.conf.crtr_conf.exp.armour_increase_on_exp = 10;
    CHECK(compute_creature_max_armour(0, 5) == 0);
    CHECK(compute_creature_max_armour(-5, 5) == 0);
    // base_param above 60000 is clamped to 60000 before scaling.
    CHECK(compute_creature_max_armour(70000, 0) == 60000);
}

TEST_CASE_METHOD(ResetSimState, "compute_creature_max_defense clamps to 255 by default once the scaled value reaches the 8-bit overflow threshold", "[kfx_sim][thing_stats]") {
    kfx_config_state.conf.crtr_conf.exp.defense_increase_on_exp = 10;
    CHECK(compute_creature_max_defense(0, 5) == 0);
    CHECK(compute_creature_max_defense(100, 1) == 110); // 100 + 10%*1

    kfx_config_state.conf.crtr_conf.exp.defense_increase_on_exp = 1000;
    CHECK(compute_creature_max_defense(100, 5) == 255); // default (non-emulating) EmulateIntegerOverflowFunc
}

TEST_CASE_METHOD(ResetSimState, "compute_creature_max_dexterity saturates to 255 by default via saturate_set_unsigned", "[kfx_sim][thing_stats]") {
    kfx_config_state.conf.crtr_conf.exp.dexterity_increase_on_exp = 10;
    CHECK(compute_creature_max_dexterity(100, 1) == 110);

    kfx_config_state.conf.crtr_conf.exp.dexterity_increase_on_exp = 1000;
    CHECK(compute_creature_max_dexterity(100, 5) == 255);
}

TEST_CASE_METHOD(ResetSimState, "compute_creature_max_loyalty floors at 0 and scales by percent-per-level", "[kfx_sim][thing_stats]") {
    kfx_config_state.conf.crtr_conf.exp.loyalty_increase_on_exp = 10;
    CHECK(compute_creature_max_loyalty(-5, 5) == 0);
    CHECK(compute_creature_max_loyalty(100, 5) == 150);
}

TEST_CASE_METHOD(ResetSimState, "compute_creature_max_pay/_training_cost/_scavenging_cost floor at 0, scale by percent-per-level, and saturate to 32767", "[kfx_sim][thing_stats]") {
    kfx_config_state.conf.crtr_conf.exp.pay_increase_on_exp = 10;
    kfx_config_state.conf.crtr_conf.exp.training_cost_increase_on_exp = 10;
    kfx_config_state.conf.crtr_conf.exp.scavenging_cost_increase_on_exp = 10000; // guarantees the 16-bit saturation

    CHECK(compute_creature_max_pay(-5, 5) == 0);
    CHECK(compute_creature_max_pay(1000, 5) == 1500);
    CHECK(compute_creature_max_training_cost(1000, 5) == 1500);
    CHECK(compute_creature_max_scavenging_cost(1000, 5) == 32767);
}

TEST_CASE_METHOD(ResetSimState, "compute_creature_max_unaffected floors at 0 and saturates to 255, no exp_level scaling at all", "[kfx_sim][thing_stats]") {
    CHECK(compute_creature_max_unaffected(-5, 10) == 0);
    CHECK(compute_creature_max_unaffected(100, 10) == 100); // exp_level is accepted but never used
    CHECK(compute_creature_max_unaffected(20000, 0) == 255); // clamped to 10000 first, then saturated to 8 bits
}

TEST_CASE_METHOD(ResetSimState, "compute_value_percentage's +49 rounding term rounds a >=.51 remainder up but a .50 tie down", "[kfx_sim][thing_stats]") {
    CHECK(compute_value_percentage(200, 50) == 100); // exact 50%, no remainder at all

    // Without the +49 term, (1*51)/100 would truncate to 0 -- confirmed by
    // hand-tracing the arithmetic and then verifying against a real run,
    // not assumed from the doc comment's "proper rounding" claim alone.
    CHECK(compute_value_percentage(1, 51) == 1);

    // An exact .50 fraction: (101*50+49)/100 == 5099/100 == 50, not 51 --
    // integer truncation means a tie rounds down, since +49 alone can't
    // push a remainder of exactly 50 over the next integer boundary.
    CHECK(compute_value_percentage(101, 50) == 50);

    // An extreme base_val is pre-clamped against INT32_MAX/(|npercent|+1) before
    // the multiply, so this must not overflow -- it returns some finite value.
    CHECK(compute_value_percentage(INT32_MAX, 100) <= INT32_MAX);
}

TEST_CASE_METHOD(ResetSimState, "calculate_damage_did_to_slab_with_single_hit picks the own-slab or enemy-slab digging rate", "[kfx_sim][thing_stats]") {
    kfx_sim_state.map_tiles_x = 4;
    kfx_sim_state.map_tiles_y = 4;
    kfx_config_state.conf.rules[0].workers.default_imp_dig_own_damage = 5;
    kfx_config_state.conf.rules[0].workers.default_imp_dig_damage = 2;
    struct Thing *diggertng = thing_get(1);
    diggertng->owner = 0;
    struct SlabMap *slb = get_slabmap_block(0, 0);

    slb->owner = 0; // own slab
    CHECK(calculate_damage_did_to_slab_with_single_hit(diggertng, slb) == 5);

    slb->owner = 3; // enemy slab
    CHECK(calculate_damage_did_to_slab_with_single_hit(diggertng, slb) == 2);
}

TEST_CASE_METHOD(ResetSimState, "calculate_gold_digged_out_of_slab_with_single_hit returns the plain scaled amount mid-dig", "[kfx_sim][thing_stats]") {
    kfx_sim_state.map_tiles_x = 4;
    kfx_sim_state.map_tiles_y = 4;
    kfx_config_state.conf.slab_conf.slab_cfgstats[0].gold_held = 100;
    kfx_config_state.conf.slab_conf.slab_cfgstats[0].block_health_index = 0;
    kfx_sim_state.block_health[0] = 50;
    struct SlabMap *slb = get_slabmap_block(0, 0);
    slb->health = 5; // still standing after this hit -- neither adjustment branch applies

    CHECK(calculate_gold_digged_out_of_slab_with_single_hit(10, slb) == 20); // (10*100)/50
}

TEST_CASE_METHOD(ResetSimState, "calculate_gold_digged_out_of_slab_with_single_hit adds the remainder on the exact final hit", "[kfx_sim][thing_stats]") {
    kfx_sim_state.map_tiles_x = 4;
    kfx_sim_state.map_tiles_y = 4;
    kfx_config_state.conf.slab_conf.slab_cfgstats[0].gold_held = 100;
    kfx_config_state.conf.slab_conf.slab_cfgstats[0].block_health_index = 0;
    kfx_sim_state.block_health[0] = 30;
    struct SlabMap *slb = get_slabmap_block(0, 0);
    slb->health = 0; // this hit exactly finished the slab

    // base gold = (10*100)/30 == 33; remainder = 100 % 33 == 1
    CHECK(calculate_gold_digged_out_of_slab_with_single_hit(10, slb) == 34);
}

TEST_CASE_METHOD(ResetSimState, "calculate_gold_digged_out_of_slab_with_single_hit subtracts full-hit gold already counted on an overshooting hit", "[kfx_sim][thing_stats]") {
    kfx_sim_state.map_tiles_x = 4;
    kfx_sim_state.map_tiles_y = 4;
    kfx_config_state.conf.slab_conf.slab_cfgstats[0].gold_held = 100;
    kfx_config_state.conf.slab_conf.slab_cfgstats[0].block_health_index = 0;
    kfx_sim_state.block_health[0] = 30;
    struct SlabMap *slb = get_slabmap_block(0, 0);
    slb->health = -5; // this hit dealt more damage than the slab had left

    // base gold = (10*100)/30 == 33; gold = 100 - (30/10)*33 == 100-99 == 1
    CHECK(calculate_gold_digged_out_of_slab_with_single_hit(10, slb) == 1);
}

TEST_CASE_METHOD(ResetSimState, "calculate_gold_digged_out_of_slab_with_single_hit never returns less than 1 gold", "[kfx_sim][thing_stats]") {
    kfx_sim_state.map_tiles_x = 4;
    kfx_sim_state.map_tiles_y = 4;
    kfx_config_state.conf.slab_conf.slab_cfgstats[0].gold_held = 60;
    kfx_config_state.conf.slab_conf.slab_cfgstats[0].block_health_index = 0;
    kfx_sim_state.block_health[0] = 30;
    struct SlabMap *slb = get_slabmap_block(0, 0);
    slb->health = -5;

    // base gold = (10*60)/30 == 20; gold = 60 - (30/10)*20 == 60-60 == 0 -> clamped to 1
    CHECK(calculate_gold_digged_out_of_slab_with_single_hit(10, slb) == 1);
}

// creature_stats_get_from_thing/get_creature_model_flags moved here from
// kfx_config's config_creature.c (refactor pass 2, S05).
TEST_CASE_METHOD(ResetSimState, "creature_stats_get_from_thing/get_creature_model_flags resolve the thing's model, falling back to slot 0 and no flags when it's out of range", "[kfx_sim][thing_stats]") {
    struct Thing *thing = thing_get(1);
    kfx_config_state.conf.crtr_conf.model_count = 3;
    creature_stats_get(2)->model_flags = CMF_IsEvil;

    thing->model = 2;
    CHECK(creature_stats_get_from_thing(thing) == creature_stats_get(2));
    CHECK(get_creature_model_flags(thing) == CMF_IsEvil);

    thing->model = 0;
    CHECK(creature_stats_get_from_thing(thing) == creature_stats_get(0));
    CHECK(get_creature_model_flags(thing) == 0);

    thing->model = 3; // == model_count
    CHECK(creature_stats_get_from_thing(thing) == creature_stats_get(0));
    CHECK(get_creature_model_flags(thing) == 0);
}
