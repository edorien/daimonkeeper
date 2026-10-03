// kfx_config: config_rules.c -- sacrifice-recipe bookkeeping and a
// couple of pure lookups, all reachable via direct kfx_config_state
// field writes rather than the full rules.cfg NamedField loader.
// player_code_name() needs no fixture at all -- player_desc[] is a
// real compile-time const table (config_players.c), unlike the
// dynamically-populated *_desc tables most other loaders in this
// library use. clear_sacrifice_recipes()/add_sacrifice_victim() had
// real external linkage but no header declaration; added.
#include <catch2/catch_test_macros.hpp>

#include "compat_report.h"
#include "config.h"
#include "config_rules.h"
#include "kfx_config_state.h"

#include <cstdio>
#include <cstring>
#include <string>

#include <unistd.h>

namespace {
struct ResetConfigState {
    ResetConfigState() { std::memset(&kfx_config_state, 0, sizeof(kfx_config_state)); }
};
}

TEST_CASE_METHOD(ResetConfigState, "get_unused_sacrifice_recipe_slot finds the first unused slot, starting at index 1", "[kfx_config][config_rules]") {
    // ResetConfigState already zeroed everything, so every recipe's
    // action is SacA_None (0) -- slot 1 is the first one scanned.
    struct SacrificeRecipe *slot = get_unused_sacrifice_recipe_slot();
    CHECK(slot == &kfx_config_state.conf.rules[0].sacrifices.sacrifice_recipes[1]);
}

TEST_CASE_METHOD(ResetConfigState, "get_unused_sacrifice_recipe_slot falls back to slot 0 once every slot 1.. is used", "[kfx_config][config_rules]") {
    for (int64_t i = 1; i < MAX_SACRIFICE_RECIPES; i++) {
        kfx_config_state.conf.rules[0].sacrifices.sacrifice_recipes[i].action = SacA_MkCreature; // anything != SacA_None
    }
    struct SacrificeRecipe *slot = get_unused_sacrifice_recipe_slot();
    CHECK(slot == &kfx_config_state.conf.rules[0].sacrifices.sacrifice_recipes[0]);
}

TEST_CASE_METHOD(ResetConfigState, "emulate_integer_overflow checks the ClscBug_Overflow8bitVal flag only for nbits==8", "[kfx_config][config_rules]") {
    kfx_config_state.conf.rules[0].gameplay.classic_bugs_flags = ClscBug_Overflow8bitVal;
    CHECK(emulate_integer_overflow(8));
    CHECK_FALSE(emulate_integer_overflow(16)); // only nbits==8 is ever checked
}

TEST_CASE_METHOD(ResetConfigState, "clear_sacrifice_recipes resets every slot's action to SacA_None", "[kfx_config][config_rules]") {
    kfx_config_state.conf.rules[0].sacrifices.sacrifice_recipes[5].action = SacA_MkCreature;
    kfx_config_state.conf.rules[0].sacrifices.sacrifice_recipes[5].victims[0] = 42;

    clear_sacrifice_recipes();
    CHECK(kfx_config_state.conf.rules[0].sacrifices.sacrifice_recipes[5].action == SacA_None);
    CHECK(kfx_config_state.conf.rules[0].sacrifices.sacrifice_recipes[5].victims[0] == 0);
}

TEST_CASE("sac_compare_fn returns a plain bool (0/1), not a real three-way qsort comparator", "[kfx_config][config_rules]") {
    // A real quirk found while testing, not fixed: qsort's comparator
    // contract needs negative/zero/positive, but this returns only
    // `a < b` -- 0 or 1, never negative. Documented here as the actual
    // observed return values, not the three-way contract a caller might
    // assume from the name/usage. add_sacrifice_victim() below is
    // deliberately not asserting a specific sorted order as a result --
    // with a non-conforming comparator, qsort's output is
    // implementation-defined, not something this test should bake in as
    // "the" expected behavior.
    ThingModel a = 5, b = 10;
    CHECK(sac_compare_fn(&a, &b) == 1); // a < b -> true(1)
    CHECK(sac_compare_fn(&b, &a) == 0); // b < a is false -> 0
    CHECK(sac_compare_fn(&a, &a) == 0); // equal -> false(0), same as "b < a" above -- not a real tri-state result
}

TEST_CASE("add_sacrifice_victim inserts a new victim into the first free slot", "[kfx_config][config_rules]") {
    struct SacrificeRecipe sac;
    std::memset(&sac, 0, sizeof(sac));

    CHECK(add_sacrifice_victim(&sac, 30));
    // qsort's non-conforming comparator (see sac_compare_fn's own test)
    // means the exact post-sort array order is implementation-defined,
    // not asserted here -- only that the inserted value is present
    // somewhere and the free-slot search advances correctly.
    bool found_30 = false;
    for (int64_t i = 0; i < MAX_SACRIFICE_VICTIMS; i++) {
        if (sac.victims[i] == 30) found_30 = true;
    }
    CHECK(found_30);

    CHECK(add_sacrifice_victim(&sac, 10));
    int64_t nonzero_count = 0;
    for (int64_t i = 0; i < MAX_SACRIFICE_VICTIMS; i++) {
        if (sac.victims[i] != 0) nonzero_count++;
    }
    CHECK(nonzero_count == 2); // both victims present, no slot lost or duplicated
}

TEST_CASE("add_sacrifice_victim rejects a new victim once every slot is full", "[kfx_config][config_rules]") {
    struct SacrificeRecipe sac;
    std::memset(&sac, 0, sizeof(sac));
    for (int64_t i = 0; i < MAX_SACRIFICE_VICTIMS; i++) {
        sac.victims[i] = i + 1; // fill every slot with a nonzero model
    }
    CHECK_FALSE(add_sacrifice_victim(&sac, 99));
}

TEST_CASE("get_research_id returns -1 and logs an error for an unhandled item_type", "[kfx_config][config_rules]") {
    CHECK(get_research_id(-1, "whatever", "test") == -1);
    CHECK(get_research_id(999, "whatever", "test") == -1);
}

TEST_CASE("player_code_name resolves the real, compile-time player_desc table -- no fixture needed", "[kfx_config][config_rules]") {
    CHECK(std::strcmp(player_code_name(PLAYER0), "PLAYER0") == 0);
    CHECK(std::strcmp(player_code_name(PLAYER_NEUTRAL), "PLAYER_NEUTRAL") == 0);
    CHECK(std::strcmp(player_code_name((PlayerNumber)123), "INVALID") == 0);
}

TEST_CASE("keeper_rules_file_data has a pre_load_func but no post_load_func", "[kfx_config][config_rules]") {
    CHECK(keeper_rules_file_data.pre_load_func != nullptr); // set_rules_defaults
    CHECK(keeper_rules_file_data.post_load_func == nullptr);
    CHECK(std::strcmp(keeper_rules_file_data.filename, "rules.cfg") == 0);
}

// The rules.cfg rows upstream's "Compiler portability (#4944)" dropped from
// the field tables (WinnerTorturesLoser, InstanceDelayOnDrop), plus the
// obsolete BarrackTime row that's accepted and discarded. Each test loads a
// small rules.cfg written on the fly, through the real NamedField loader.
namespace {
const char *const kRulesWithRestoredRows =
    "[game]\nWinnerTorturesLoser = 1\n"
    "[creatures]\nInstanceDelayOnDrop = 40\n"
    "[rooms]\nBarrackTime = 100\nBarrackMaxPartySize = 5\n";

std::string write_rules(const char *text)
{
    const std::string path = "config_rules_test_" + std::to_string(getpid()) + ".cfg";
    std::FILE *f = std::fopen(path.c_str(), "wb");
    REQUIRE(f);
    std::fputs(text, f);
    std::fclose(f);
    return path;
}

// Defaults, then the file -- the order load_config() runs them in.
void load_rules(const char *text)
{
    std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
    compat_report_clear();
    keeper_rules_file_data.pre_load_func();
    const std::string path = write_rules(text);
    CHECK(keeper_rules_file_data.load_func(path.c_str(), 0));
    std::remove(path.c_str());
}

const struct NamedField *rules_row(const char *name)
{
    for (const struct NamedField *block : ruleblocks) {
        const int64_t id = get_named_field_id(block, name);
        if (id >= 0)
            return block + id;
    }
    return nullptr;
}
}

TEST_CASE("WinnerTorturesLoser and InstanceDelayOnDrop default to off", "[kfx_config][config_rules]") {
    load_rules("[game]\n");
    for (int64_t plyr = 0; plyr < PLAYERS_COUNT; plyr++) {
        CHECK(kfx_config_state.conf.rules[plyr].gameplay.winner_tortures_loser == 0);
        CHECK(kfx_config_state.conf.rules[plyr].creature.instance_delay_on_drop == 0);
    }
}

TEST_CASE("rules.cfg's WinnerTorturesLoser and InstanceDelayOnDrop load for every player", "[kfx_config][config_rules]") {
    load_rules(kRulesWithRestoredRows);
    CHECK(compat_report_count() == 0);
    for (int64_t plyr = 0; plyr < PLAYERS_COUNT; plyr++) {
        CHECK(kfx_config_state.conf.rules[plyr].gameplay.winner_tortures_loser == 1);
        CHECK(kfx_config_state.conf.rules[plyr].creature.instance_delay_on_drop == 40);
    }
}

TEST_CASE("the restored rules rows keep upstream's limits", "[kfx_config][config_rules]") {
    const struct NamedField *wtl = rules_row("WinnerTorturesLoser");
    REQUIRE(wtl != nullptr);
    CHECK(parse_named_field_value(wtl, "7", &rules_named_fields_set, 0, "test", 0) == 1);
    CHECK(parse_named_field_value(wtl, "-1", &rules_named_fields_set, 0, "test", 0) == 0);

    const struct NamedField *delay = rules_row("InstanceDelayOnDrop");
    REQUIRE(delay != nullptr);
    CHECK(parse_named_field_value(delay, "-5", &rules_named_fields_set, 0, "test", 0) == 0);
    CHECK(parse_named_field_value(delay, "250", &rules_named_fields_set, 0, "test", 0) == 250);
}

TEST_CASE("BarrackTime is accepted but changes nothing", "[kfx_config][config_rules]") {
    load_rules("[game]\nWinnerTorturesLoser = 1\n[rooms]\nBarrackMaxPartySize = 5\n");
    const struct RulesConfig without = kfx_config_state.conf.rules[0];

    load_rules("[game]\nWinnerTorturesLoser = 1\n[rooms]\nBarrackTime = 100\nBarrackMaxPartySize = 5\n");
    CHECK(compat_report_count() == 0); // known key: no compat-report warning
    CHECK(std::memcmp(&kfx_config_state.conf.rules[0], &without, sizeof(without)) == 0);

    // Setting it the way SET_GAME_RULE does is a no-op too.
    const struct NamedField *barrack = rules_row("BarrackTime");
    REQUIRE(barrack != nullptr);
    const int64_t value = parse_named_field_value(barrack, "250", &rules_named_fields_set, 0, "test", 0);
    assign_named_field_value(barrack, value, &rules_named_fields_set, 0, "test", 0);
    CHECK(std::memcmp(&kfx_config_state.conf.rules[0], &without, sizeof(without)) == 0);
}
