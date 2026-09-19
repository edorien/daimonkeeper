// kfx_sim coverage: map_is_legacy_compatible() (map_content_compat.h) --
// see docs/refactor/editor/phase3/01-slice2-classic-save.md for the
// criteria this checks and why (each one traced to a real, confirmed
// classic .tng format limitation, not assumed from the doc's original
// pre-implementation list).
#include <catch2/catch_test_macros.hpp>

#include "map_content.h"
#include "map_content_compat.h"

namespace {

MapContent base_compatible_content()
{
    MapContent c;
    c.map_tiles_x = 85;
    c.map_tiles_y = 85;
    return c;
}

} // namespace

TEST_CASE("map_is_legacy_compatible accepts a minimal 85x85 map with nothing else set", "[kfx_sim][map_content_compat]") {
    MapContent c = base_compatible_content();
    CHECK(map_is_legacy_compatible(c));
}

TEST_CASE("map_is_legacy_compatible rejects a map that isn't 85x85", "[kfx_sim][map_content_compat]") {
    MapContent c = base_compatible_content();
    c.map_tiles_x = 40;
    c.map_tiles_y = 40;
    std::vector<std::string> reasons;
    CHECK_FALSE(map_is_legacy_compatible(c, &reasons));
    CHECK(reasons.size() == 1);
}

TEST_CASE("map_is_legacy_compatible rejects a creature with custom gold/health/name", "[kfx_sim][map_content_compat]") {
    MapThingRecord creature;
    creature.thing_class = TCls_Creature;

    SECTION("custom gold") {
        MapContent c = base_compatible_content();
        creature.creature_gold = 500;
        c.things.push_back(creature);
        CHECK_FALSE(map_is_legacy_compatible(c));
    }
    SECTION("custom health percentage") {
        MapContent c = base_compatible_content();
        creature.creature_health_percent = 50;
        c.things.push_back(creature);
        CHECK_FALSE(map_is_legacy_compatible(c));
    }
    SECTION("custom name") {
        MapContent c = base_compatible_content();
        creature.creature_name = "Bob";
        c.things.push_back(creature);
        CHECK_FALSE(map_is_legacy_compatible(c));
    }
    SECTION("none set -- stays compatible") {
        MapContent c = base_compatible_content();
        creature.creature_level = 3; // representable, doesn't disqualify
        c.things.push_back(creature);
        CHECK(map_is_legacy_compatible(c));
    }
}

TEST_CASE("map_is_legacy_compatible rejects a custom orientation on Object/Creature/Trap, but not Door", "[kfx_sim][map_content_compat]") {
    SECTION("object orientation") {
        MapContent c = base_compatible_content();
        MapThingRecord t;
        t.thing_class = TCls_Object;
        t.orientation = 512;
        c.things.push_back(t);
        CHECK_FALSE(map_is_legacy_compatible(c));
    }
    SECTION("creature orientation") {
        MapContent c = base_compatible_content();
        MapThingRecord t;
        t.thing_class = TCls_Creature;
        t.orientation = 512;
        c.things.push_back(t);
        CHECK_FALSE(map_is_legacy_compatible(c));
    }
    SECTION("trap orientation") {
        MapContent c = base_compatible_content();
        MapThingRecord t;
        t.thing_class = TCls_Trap;
        t.orientation = 512;
        c.things.push_back(t);
        CHECK_FALSE(map_is_legacy_compatible(c));
    }
    SECTION("door orientation is fine -- classic .tng has a real field for it") {
        MapContent c = base_compatible_content();
        MapThingRecord t;
        t.thing_class = TCls_Door;
        t.door_orientation = 1;
        c.things.push_back(t);
        CHECK(map_is_legacy_compatible(c));
    }
}

TEST_CASE("map_is_legacy_compatible rejects a gold pile with a custom value", "[kfx_sim][map_content_compat]") {
    MapContent c = base_compatible_content();
    MapThingRecord t;
    t.thing_class = TCls_Object;
    t.gold_value = 1000;
    c.things.push_back(t);
    CHECK_FALSE(map_is_legacy_compatible(c));
}

TEST_CASE("map_is_legacy_compatible rejects more than 255 creatures", "[kfx_sim][map_content_compat]") {
    MapContent c = base_compatible_content();
    for (int i = 0; i < 256; i++)
    {
        MapThingRecord t;
        t.thing_class = TCls_Creature;
        c.things.push_back(t);
    }
    CHECK_FALSE(map_is_legacy_compatible(c));
}

TEST_CASE("map_is_legacy_compatible accepts exactly 255 creatures", "[kfx_sim][map_content_compat]") {
    MapContent c = base_compatible_content();
    for (int i = 0; i < 255; i++)
    {
        MapThingRecord t;
        t.thing_class = TCls_Creature;
        c.things.push_back(t);
    }
    CHECK(map_is_legacy_compatible(c));
}
