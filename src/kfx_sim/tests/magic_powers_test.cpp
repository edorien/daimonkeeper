// kfx_sim: magic_powers.c's power-price computation functions, per
// docs/refactor/testing/comprehensive/stage-08-comprehensive-library-
// passes.md's kfx_sim row. Pattern A on kfx_config_state
// (get_power_model_stats() reads
// kfx_config_state.conf.magic_conf.power_cfgstats[]). compute_power_price
// is only exercised for its Cost_Default branch here -- Cost_Digger/
// Cost_Dwarf additionally reach into a real dungeon's creature counts
// and a SimPort provider (get_players_special_digger_model),
// a bigger increment left open.
//
// compute_power_price_scaled_with_amount had no header declaration
// anywhere (only ever called from within magic_powers.c itself) --
// added to magic_powers.h, the same "add the missing declaration" fix
// used for lvl_script_conditions.h/dungeon_stats.h/value_util.h earlier
// in this plan.
#include <catch2/catch_test_macros.hpp>

#include "magic_powers.h"
#include "config_magic.h"
#include "kfx_config_state.h"
#include "player_availability.h"

#include <cstring>

namespace {
struct ResetConfigState {
    ResetConfigState() {
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
        kfx_config_state.conf.magic_conf.power_types_count = MAGIC_ITEMS_MAX;
    }
};
}

TEST_CASE_METHOD(ResetConfigState, "compute_power_price_scaled_with_amount adds the per-unit base cost times the amount", "[kfx_sim][magic_powers]") {
    kfx_config_state.conf.magic_conf.power_cfgstats[2].cost[0] = 100; // base (unscaled) cost
    kfx_config_state.conf.magic_conf.power_cfgstats[2].cost[1] = 250; // cost at overcharge level 1
    CHECK(compute_power_price_scaled_with_amount(0, 2, 1, 3) == 550); // 250 + 100*3
}

TEST_CASE_METHOD(ResetConfigState, "compute_power_price_scaled_with_amount treats a negative amount as zero, giving the base level cost", "[kfx_sim][magic_powers]") {
    kfx_config_state.conf.magic_conf.power_cfgstats[2].cost[0] = 100;
    kfx_config_state.conf.magic_conf.power_cfgstats[2].cost[1] = 250;
    CHECK(compute_power_price_scaled_with_amount(0, 2, 1, -5) == 250);
    CHECK(compute_power_price_scaled_with_amount(0, 2, 1, 0) == 250); // same result, amount 0
}

TEST_CASE_METHOD(ResetConfigState, "compute_power_price for the Cost_Default formula is just the configured level cost", "[kfx_sim][magic_powers]") {
    kfx_config_state.conf.magic_conf.power_cfgstats[3].cost_formula = Cost_Default;
    kfx_config_state.conf.magic_conf.power_cfgstats[3].cost[2] = 777;
    CHECK(compute_power_price(0, 3, 2) == 777);
}

TEST_CASE_METHOD(ResetConfigState, "compute_lowest_power_price is the configured level cost for a non-digger power", "[kfx_sim][magic_powers]") {
    kfx_config_state.conf.magic_conf.power_cfgstats[3].cost[2] = 777;
    CHECK(compute_lowest_power_price(0, 3, 2) == 777);
}

TEST_CASE_METHOD(ResetConfigState, "compute_lowest_power_price for PwrK_MKDIGGER also reduces to the configured level cost at amount 0", "[kfx_sim][magic_powers]") {
    kfx_config_state.conf.magic_conf.power_cfgstats[PwrK_MKDIGGER].cost[0] = 100;
    kfx_config_state.conf.magic_conf.power_cfgstats[PwrK_MKDIGGER].cost[2] = 777;
    // Goes through compute_power_price_scaled_with_amount(..., 0), which
    // adds cost[0]*0 -- so it converges on the same value the plain
    // lookup gives, confirmed rather than assumed.
    CHECK(compute_lowest_power_price(0, PwrK_MKDIGGER, 2) == 777);
}
