// docs/refactor/skirmish/01-...md §13.3.5: kfx_config's script_setup_analysis
// mirrors the win/lose variable whitelist as constants (kfx_game, which owns
// variable_desc / dk1_variable_desc, ranks above kfx_config and cannot be
// included there). This test -- in a binary that links both -- fails if an
// engine table edit ever breaks the mirrored facts.
#include <catch2/catch_test_macros.hpp>

#include "pre_inc.h"
#include "script_setup_analysis.h"
#include "lvl_script_conditions.h" // variable_desc, dk1_variable_desc
#include "lvl_script_lib.h"        // parse_get_varib
#include "lvl_script.h"            // CONDITIONS_COUNT, WIN_CONDITIONS_COUNT, SENSIBLE_GOLD
#include "script_setup_prelude.h"
#include "post_inc.h"

#include <cstring>
#include <string>

namespace {

const struct NamedCommand *find(const struct NamedCommand *table, const char *name)
{
    for (int i = 0; table[i].name != nullptr; i++)
        if (strcmp(table[i].name, name) == 0)
            return &table[i];
    return nullptr;
}

int count(const struct NamedCommand *table)
{
    int n = 0;
    while (table[n].name != nullptr)
        n++;
    return n;
}

} // namespace

TEST_CASE("identical-meaning whitelist names resolve to the same variable at v0 and v1", "[kfx_editor][script_setup]") {
    for (const char *const *p = script_setup_win_variables_identical(); *p; p++)
    {
        INFO(*p);
        const struct NamedCommand *v1 = find(variable_desc, *p);
        const struct NamedCommand *v0 = find(dk1_variable_desc, *p);
        REQUIRE(v1 != nullptr);
        REQUIRE(v0 != nullptr);
        CHECK(v0->num == v1->num);
        // ...and through the real lookup the engine uses for IF conditions.
        int32_t id0, type0, id1, type1;
        REQUIRE(parse_get_varib(*p, &id0, &type0, 0));
        REQUIRE(parse_get_varib(*p, &id1, &type1, 1));
        CHECK(type0 == type1);
    }
}

TEST_CASE("v1-only whitelist names exist only in the v1 table", "[kfx_editor][script_setup]") {
    for (const char *const *p = script_setup_win_variables_v1_only(); *p; p++)
    {
        INFO(*p);
        CHECK(find(variable_desc, *p) != nullptr);
        CHECK(find(dk1_variable_desc, *p) == nullptr);
    }
}

TEST_CASE("the whitelists cover the engine tables exactly (nothing silently added)", "[kfx_editor][script_setup]") {
    int identical = 0, v1_only = 0;
    for (const char *const *p = script_setup_win_variables_identical(); *p; p++) identical++;
    for (const char *const *p = script_setup_win_variables_v1_only(); *p; p++) v1_only++;
    // v1 table = identical + v1-only + TOTAL_CREATURES; v0 table = identical + TOTAL_CREATURES + TOTAL_IMPS.
    // A new engine variable (either table) breaks these counts and needs a deliberate whitelist decision.
    CHECK(count(variable_desc) == identical + v1_only + 1);
    CHECK(count(dk1_variable_desc) == identical + 2);
}

TEST_CASE("the special cases the reader relies on", "[kfx_editor][script_setup]") {
    // TOTAL_CREATURES differs in meaning between versions (v0 = IF_CONTROLS semantics) -> not whitelisted.
    const struct NamedCommand *a = find(variable_desc, "TOTAL_CREATURES");
    const struct NamedCommand *b = find(dk1_variable_desc, "TOTAL_CREATURES");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(a->num != b->num);
    // TOTAL_IMPS (v0) is the same variable as TOTAL_DIGGERS (v1): the reader's alias.
    const struct NamedCommand *imps = find(dk1_variable_desc, "TOTAL_IMPS");
    const struct NamedCommand *diggers = find(variable_desc, "TOTAL_DIGGERS");
    REQUIRE(imps != nullptr);
    REQUIRE(diggers != nullptr);
    CHECK(imps->num == diggers->num);
    CHECK(find(variable_desc, "TOTAL_IMPS") == nullptr);
    CHECK(find(dk1_variable_desc, "TOTAL_DIGGERS") == nullptr);
}

TEST_CASE("the prelude validators' engine limits mirror the real macros", "[kfx_editor][script_setup]") {
    CHECK(kSetupConditionsCount == CONDITIONS_COUNT);
    CHECK(kSetupWinConditionsCount == WIN_CONDITIONS_COUNT);
    CHECK(kSetupSensibleGold == SENSIBLE_GOLD);
}
