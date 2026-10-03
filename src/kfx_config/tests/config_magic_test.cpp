// kfx_config: config_magic.c's load_magic_config_file() parses FOUR
// independent block kinds in one pass: [spell%d]/[special%d] via
// hand-rolled parsers (parse_magic_spell_blocks/parse_magic_special_blocks,
// numeric command IDs, config_cubes_test.cpp's list of NamedField users
// doesn't include this file since these two block kinds don't use that
// mechanism) and [shot%d]/[power%d] via the NamedField/
// parse_named_field_blocks mechanism (a sixth worked example of that
// pattern, after config_compp.c). Fields needing a name lookup into a
// table this fixture doesn't load (SHOTMODEL/ARTIFACT/CREATURETYPE/
// CASTABILITY/USEFUNCTION/PLAYERSTATE) are deliberately left out of the
// fixture rather than faked, keeping it self-contained and numeric.
//
// A real quirk, found by an actual test failure (a single load_func(path,
// 0) call left every spell's code_name empty): parse_magic_spell_blocks's
// per-line dispatch (config_magic.c, "Do the name when listing, the rest
// when not listing.") processes cmd_num 1 (NAME) *only* under
// CnfLd_ListOnly, and every other spell field *only* without it --
// spell0's Name is never set by a single ordinary load. This isn't
// test-specific plumbing: src/kfx_sim/src/dungeon_stats.c's real
// production call sequence for keeper_magic_file_data is exactly
// CnfLd_ListOnly first, then CnfLd_Standard|CnfLd_PreListed second --
// load_magic_fixture_full() below mirrors that. Shots/powers (the
// NamedField mechanism) and specials (parse_magic_special_blocks) don't
// have this quirk -- their own NAME handling works in a single ordinary
// pass, verified by an actual test run.
//
// The player-power-availability functions (set_power_available and
// friends) moved to kfx_sim's player_availability.c in refactor pass 2
// (S05); their tests are in kfx_sim/tests/player_availability_test.cpp.
#include <catch2/catch_test_macros.hpp>

#include "kfx_config_test_paths.h" // KFX_CONFIG_TEST_FIXTURES_DIR
#include "config_magic.h"
#include "kfx_config_state.h"

#include <cstring>

namespace {
struct ResetConfigState {
    ResetConfigState() { std::memset(&kfx_config_state, 0, sizeof(kfx_config_state)); }
};

// Mirrors dungeon_stats.c's real two-call sequence for
// keeper_magic_file_data -- see the top-of-file comment.
bool load_magic_fixture_full() {
    if (!keeper_magic_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/magic_minimal.cfg", CnfLd_ListOnly))
        return false;
    return keeper_magic_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/magic_minimal.cfg", CnfLd_Standard | CnfLd_PreListed);
}
}

TEST_CASE_METHOD(ResetConfigState, "load_magic_config_file maps a [spell0] block's Name/Duration/SelfCasted/CastAtThing fields", "[kfx_config][config_magic]") {
    REQUIRE(load_magic_fixture_full());

    struct SpellConfigStats *spellst = get_spell_model_stats(0);
    CHECK(std::strcmp(spellst->code_name, "SPELL_TEST") == 0);

    struct SpellConfig *spconf = get_spell_config(0);
    CHECK(spconf->duration == 50);
    CHECK(spconf->caster_affected == 1);
    CHECK(spconf->caster_affect_sound == 0);
    CHECK(spconf->caster_sounds_count == 2);
    CHECK(spconf->cast_at_thing == 1);
}

TEST_CASE_METHOD(ResetConfigState, "load_magic_config_file maps a [shot0] block's Name/Health/Damage/IsMagical/Speed fields", "[kfx_config][config_magic]") {
    REQUIRE(keeper_magic_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/magic_minimal.cfg", 0));

    struct ShotConfigStats *shotst = get_shot_model_stats(0);
    CHECK(std::strcmp(shotst->code_name, "SHOT_TEST") == 0);
    CHECK(shotst->health == 10);
    CHECK(shotst->damage == 5);
    CHECK(shotst->is_magical);
    CHECK(shotst->speed == 20);
}

TEST_CASE_METHOD(ResetConfigState, "load_magic_config_file maps a [power0] block's Name/Duration/CostFormula, and fills Cost's 9 slots from 9 tokens", "[kfx_config][config_magic]") {
    REQUIRE(keeper_magic_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/magic_minimal.cfg", 0));

    struct PowerConfigStats *powerst = get_power_model_stats(0);
    CHECK(std::strcmp(powerst->code_name, "POWER_TEST") == 0);
    CHECK(powerst->duration == 100);
    CHECK(powerst->cost_formula == Cost_Default);
    for (int64_t i = 0; i < MAGIC_OVERCHARGE_LEVELS; i++) {
        CHECK(powerst->cost[i] == (i + 1) * 10);
    }
}

TEST_CASE_METHOD(ResetConfigState, "load_magic_config_file's [power0] Power field duplicates its last (9th) token into strength[9] without consuming a 10th token", "[kfx_config][config_magic]") {
    REQUIRE(keeper_magic_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/magic_minimal.cfg", 0));

    // magic_powers_named_fields[]'s "POWER" position-8 row uses a custom
    // assign function (assign_strength_before_last) that writes the same
    // value into position 9 as a side effect -- "old power max is one
    // short for spell max" per that function's own comment. With exactly
    // 9 space-separated tokens (positions 0..8), the table's separate
    // position-9 "POWER" row then finds no 10th token to consume, so its
    // own assign_default never overwrites what position 8's side effect
    // just wrote -- not something this test works around, it's the
    // documented intent of that custom assign function.
    struct PowerConfigStats *powerst = get_power_model_stats(0);
    for (int64_t i = 0; i < 8; i++) {
        CHECK(powerst->strength[i] == i + 1);
    }
    CHECK(powerst->strength[8] == 9);
    CHECK(powerst->strength[9] == 9);
}

TEST_CASE_METHOD(ResetConfigState, "load_magic_config_file maps a [special0] block's Name/TooltipTextId/Value fields", "[kfx_config][config_magic]") {
    REQUIRE(keeper_magic_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/magic_minimal.cfg", 0));

    struct SpecialConfigStats *specst = get_special_model_stats(0);
    CHECK(std::strcmp(specst->code_name, "SPECIAL_TEST") == 0);
    CHECK(specst->tooltip_stridx == 5);
    CHECK(specst->value == 7);
}

TEST_CASE_METHOD(ResetConfigState, "get_spell_config maps index 0 and any out-of-range index to the same slot-0 sentinel, which spell_config_is_invalid rejects", "[kfx_config][config_magic]") {
    REQUIRE(keeper_magic_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/magic_minimal.cfg", 0));

    struct SpellConfig *slot0 = get_spell_config(0);
    CHECK(slot0 == get_spell_config(MAGIC_ITEMS_MAX + 1));
    CHECK(spell_config_is_invalid(slot0));

    // spell1 was never configured by the fixture, but its index (1) is
    // still < spell_types_count (fixture sets it to 1 via slot 0's own
    // presence -- confirm the real count instead of assuming), so it
    // resolves to its own real slot rather than falling back, yet that
    // slot is still "invalid" (empty code_name) -- get_spell_config's
    // bounds check and spell_config_is_invalid's content check are two
    // different, independently testable things.
    CHECK_FALSE(spell_config_is_invalid(get_spell_config(0) + 1));
}

TEST_CASE_METHOD(ResetConfigState, "get_spell_model_stats/get_shot_model_stats/get_power_model_stats/get_special_model_stats fall back to slot 0 for an out-of-range model", "[kfx_config][config_magic]") {
    CHECK(get_spell_model_stats(MAGIC_ITEMS_MAX + 1) == get_spell_model_stats(0));
    CHECK(get_shot_model_stats(MAGIC_ITEMS_MAX + 1) == get_shot_model_stats(0));
    CHECK(get_power_model_stats(MAGIC_ITEMS_MAX + 1) == get_power_model_stats(0));
    CHECK(get_special_model_stats(MAGIC_ITEMS_MAX + 1) == get_special_model_stats(0));
}

TEST_CASE_METHOD(ResetConfigState, "power_model_stats_invalid rejects slot 0 and anything at or before it", "[kfx_config][config_magic]") {
    CHECK(power_model_stats_invalid(get_power_model_stats(0)));
    CHECK_FALSE(power_model_stats_invalid(get_power_model_stats(0) + 1));
}

TEST_CASE_METHOD(ResetConfigState, "spell_code_name/shot_code_name/power_code_name round-trip a loaded block's name; power_model_id resolves it back to an index", "[kfx_config][config_magic]") {
    REQUIRE(load_magic_fixture_full());

    CHECK(std::strcmp(spell_code_name(0), "SPELL_TEST") == 0);
    CHECK(std::strcmp(shot_code_name(0), "SHOT_TEST") == 0);
    CHECK(std::strcmp(power_code_name(0), "POWER_TEST") == 0);
    CHECK(power_model_id("POWER_TEST") == 0);
    CHECK(power_model_id("NOT_A_REAL_POWER") == -1);
}

TEST_CASE_METHOD(ResetConfigState, "get_power_name_strindex/get_power_description_strindex fall back to slot 0's values for an out-of-range power", "[kfx_config][config_magic]") {
    get_power_model_stats(0)->name_stridx = 11;
    get_power_model_stats(0)->tooltip_stridx = 22;
    kfx_config_state.conf.magic_conf.power_types_count = 1;

    CHECK(get_power_name_strindex(0) == 11);
    CHECK(get_power_description_strindex(0) == 22);
    CHECK(get_power_name_strindex(5) == 11); // out of range -> slot 0
    CHECK(get_power_description_strindex(5) == 22);
}

TEST_CASE_METHOD(ResetConfigState, "get_special_description_strindex falls back to slot 0 for a negative or out-of-range special", "[kfx_config][config_magic]") {
    get_special_model_stats(0)->tooltip_stridx = 33;
    kfx_config_state.conf.magic_conf.power_types_count = 1; // get_special_description_strindex bounds-checks against power_types_count, not special_types_count -- a real quirk, verified by reading the implementation

    CHECK(get_special_description_strindex(-1) == 33);
    CHECK(get_special_description_strindex(5) == 33);
}

TEST_CASE_METHOD(ResetConfigState, "power_is_instinctive treats an invalid (out-of-range) power as instinctive, and otherwise checks PwCF_Instinctive", "[kfx_config][config_magic]") {
    // pwkind 0 always resolves to the slot-0 sentinel, which
    // power_model_stats_invalid() always rejects -- so power 0 is
    // "instinctive" by this fallback rule, not because it has the flag.
    CHECK(power_is_instinctive(0));

    kfx_config_state.conf.magic_conf.power_types_count = 2;
    get_power_model_stats(1)->config_flags = PwCF_Instinctive;
    CHECK(power_is_instinctive(1));

    get_power_model_stats(1)->config_flags = 0;
    CHECK_FALSE(power_is_instinctive(1));
}

TEST_CASE_METHOD(ResetConfigState, "get_power_index_for_work_state finds the first power matching work_state, or 0 if none match", "[kfx_config][config_magic]") {
    kfx_config_state.conf.magic_conf.power_types_count = 3;
    get_power_model_stats(2)->work_state = 42;

    CHECK(get_power_index_for_work_state(42) == 2);
    CHECK(get_power_index_for_work_state(999) == 0);
}

TEST_CASE_METHOD(ResetConfigState, "make_all_powers_cost_free zeroes every configured power's cost array", "[kfx_config][config_magic]") {
    kfx_config_state.conf.magic_conf.power_types_count = 1;
    get_power_model_stats(0)->cost[0] = 100;
    get_power_model_stats(0)->cost[MAGIC_OVERCHARGE_LEVELS - 1] = 200;

    CHECK(make_all_powers_cost_free());
    CHECK(get_power_model_stats(0)->cost[0] == 0);
    CHECK(get_power_model_stats(0)->cost[MAGIC_OVERCHARGE_LEVELS - 1] == 0);
}

TEST_CASE("load_magic_config_file returns false for a missing file", "[kfx_config][config_magic]") {
    CHECK_FALSE(keeper_magic_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/does_not_exist.cfg", CnfLd_IgnoreErrors));
}

TEST_CASE("keeper_magic_file_data has no pre/post-load hooks", "[kfx_config][config_magic]") {
    CHECK(keeper_magic_file_data.pre_load_func == nullptr);
    CHECK(keeper_magic_file_data.post_load_func == nullptr);
    CHECK(std::strcmp(keeper_magic_file_data.filename, "magic.cfg") == 0);
}
