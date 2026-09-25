// kfx_sim coverage: verify_map_content() (map_content_verify.h) -- see
// docs/refactor/editor/phase3/06-slice7-verify-map.md for the checks this
// runs and why (each one traced to a real, checkable MapContent-only
// primitive, no room/portal/script/classic-ID-range checks -- explicitly
// out of scope, see that doc). block_flags/model_flags are set directly on
// kfx_config_state (not loaded from real terrain.cfg/objects.cfg), same
// synthetic-config convention slab_data_test.cpp/power_hand_test.cpp
// already established for Catch2 fixtures.
#include <catch2/catch_test_macros.hpp>

#include "map_content.h"
#include "map_content_verify.h"
#include "kfx_config_state.h"

#include <cstring>

namespace {

// Slab kind 0 = blocking (rock-like); slab kind 1 = not blocking
// (path/claimed-like). Object model 1 = a Dungeon Heart; model 2 = a
// plain, non-heart object.
const SlabKind kBlockingKind = 0;
const SlabKind kOpenKind = 1;
const ThingModel kHeartModel = 1;
const ThingModel kPlainObjectModel = 2;

struct ResetConfig {
    ResetConfig() {
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
        kfx_config_state.conf.slab_conf.slab_types_count = 2;
        kfx_config_state.conf.slab_conf.slab_cfgstats[kBlockingKind].block_flags = SlbAtFlg_Blocking;
        kfx_config_state.conf.slab_conf.slab_cfgstats[kOpenKind].block_flags = 0;
        // get_object_model_stats() falls back to object_cfgstats[0] for any
        // model >= object_types_count (config_objects.c) -- found live: an
        // unset object_types_count silently made every Heart-recognition
        // check below resolve to model 0's own (unset) flags instead.
        kfx_config_state.conf.object_conf.object_types_count = kPlainObjectModel + 1;
        kfx_config_state.conf.object_conf.object_cfgstats[kHeartModel].model_flags = OMF_Heart;
        kfx_config_state.conf.object_conf.object_cfgstats[kPlainObjectModel].model_flags = 0;
    }
};

// A minimal, otherwise-clean 5x5 map: every slab open except a blocking
// border ring, one player (0) with a Heart at slab (2,2). Individual
// SECTIONs mutate this to trigger one check at a time.
MapContent base_content()
{
    MapContent c;
    c.map_tiles_x = 5;
    c.map_tiles_y = 5;
    c.slab_kind.assign(25, kOpenKind);
    c.slab_owner.assign(25, 0);
    for (int64_t y = 0; y < 5; y++)
        for (int64_t x = 0; x < 5; x++)
            if ((x == 0) || (y == 0) || (x == 4) || (y == 4))
                c.slab_kind[c.slab_index(x, y)] = kBlockingKind;

    MapThingRecord heart;
    heart.thing_class = TCls_Object;
    heart.model = kHeartModel;
    heart.owner = 0;
    heart.pos_x = (2 * 3 + 1) * 256; // slab (2,2), centered
    heart.pos_y = (2 * 3 + 1) * 256;
    c.things.push_back(heart);

    return c;
}

MapVerifyIssueSeverity max_severity(const std::vector<MapVerifyIssue> &issues)
{
    MapVerifyIssueSeverity worst = MVI_Info;
    for (const MapVerifyIssue &i : issues)
        if (i.severity > worst)
            worst = i.severity;
    return worst;
}

bool any_message_contains(const std::vector<MapVerifyIssue> &issues, const char *substr)
{
    for (const MapVerifyIssue &i : issues)
        if (i.message.find(substr) != std::string::npos)
            return true;
    return false;
}

} // namespace

TEST_CASE_METHOD(ResetConfig, "verify_map_content flags a missing Dungeon Heart as ERROR", "[kfx_sim][map_content_verify]") {
    MapContent c = base_content();
    c.things.clear(); // no Heart at all, but slab_owner still claims player 0

    auto issues = verify_map_content(c);
    CHECK(any_message_contains(issues, "no Dungeon Heart"));
    CHECK(max_severity(issues) == MVI_Error);
}

TEST_CASE_METHOD(ResetConfig, "verify_map_content is silent on the Heart check when every player has one", "[kfx_sim][map_content_verify]") {
    MapContent c = base_content();
    c.slab_owner.assign(25, PLAYER_NEUTRAL); // only the Heart's own owner (0) counts as "present"

    auto issues = verify_map_content(c);
    CHECK_FALSE(any_message_contains(issues, "no Dungeon Heart"));
}

TEST_CASE_METHOD(ResetConfig, "verify_map_content flags a thing embedded in a blocking slab as ERROR", "[kfx_sim][map_content_verify]") {
    MapContent c = base_content();
    MapThingRecord embedded;
    embedded.thing_class = TCls_Trap;
    embedded.model = 1;
    embedded.owner = 0;
    embedded.pos_x = 1 * 256; // slab (0, y) -- the blocking border ring
    embedded.pos_y = (2 * 3 + 1) * 256;
    c.things.push_back(embedded);

    auto issues = verify_map_content(c);
    CHECK(any_message_contains(issues, "embedded in an impenetrable slab"));
    CHECK(max_severity(issues) == MVI_Error);
}

TEST_CASE_METHOD(ResetConfig, "verify_map_content is silent on the embedded-thing check for a thing on open ground", "[kfx_sim][map_content_verify]") {
    MapContent c = base_content(); // the Heart itself sits on open ground (slab 2,2)
    auto issues = verify_map_content(c);
    CHECK_FALSE(any_message_contains(issues, "embedded in an impenetrable slab"));
}

TEST_CASE_METHOD(ResetConfig, "verify_map_content flags creature count at the engine cap as ERROR, near it as WARN", "[kfx_sim][map_content_verify]") {
    SECTION("well under the cap -- no issue") {
        MapContent c = base_content();
        auto issues = verify_map_content(c);
        CHECK_FALSE(any_message_contains(issues, "creatures"));
    }
    SECTION("at the cap -- ERROR") {
        MapContent c = base_content();
        for (int64_t i = 0; i < CREATURES_COUNT; i++) {
            MapThingRecord cr;
            cr.thing_class = TCls_Creature;
            cr.model = 1;
            // Default (0,0) would land on the blocking border ring and
            // spuriously trip the embedded-thing check too -- stack every
            // test creature on the same open ground the Heart sits on.
            cr.pos_x = (2 * 3 + 1) * 256;
            cr.pos_y = (2 * 3 + 1) * 256;
            c.things.push_back(cr);
        }
        auto issues = verify_map_content(c);
        CHECK(any_message_contains(issues, "creatures exceeds the engine limit"));
        CHECK(max_severity(issues) == MVI_Error);
    }
    SECTION("near the cap (95%) -- WARN, not ERROR") {
        MapContent c = base_content();
        int64_t near_cap = (CREATURES_COUNT * 95) / 100;
        for (int64_t i = 0; i < near_cap; i++) {
            MapThingRecord cr;
            cr.thing_class = TCls_Creature;
            cr.model = 1;
            cr.pos_x = (2 * 3 + 1) * 256;
            cr.pos_y = (2 * 3 + 1) * 256;
            c.things.push_back(cr);
        }
        auto issues = verify_map_content(c);
        CHECK(any_message_contains(issues, "creatures is close to the engine limit"));
        CHECK(max_severity(issues) == MVI_Warn);
    }
}

TEST_CASE_METHOD(ResetConfig, "verify_map_content flags light count at the engine cap as ERROR, near it as WARN", "[kfx_sim][map_content_verify]") {
    SECTION("a normal handful of lights -- no issue") {
        MapContent c = base_content();
        c.lights.resize(10);
        CHECK_FALSE(any_message_contains(verify_map_content(c), "lights"));
    }
    SECTION("at the cap -- ERROR") {
        MapContent c = base_content();
        c.lights.resize(2048);
        auto issues = verify_map_content(c);
        CHECK(any_message_contains(issues, "lights exceeds the engine limit"));
        CHECK(max_severity(issues) == MVI_Error);
    }
    SECTION("near the cap (95%) -- WARN, not ERROR") {
        MapContent c = base_content();
        c.lights.resize((2048 * 95) / 100);
        auto issues = verify_map_content(c);
        CHECK(any_message_contains(issues, "lights is close to the engine limit"));
        CHECK(max_severity(issues) == MVI_Warn);
    }
}

TEST_CASE_METHOD(ResetConfig, "verify_map_content flags duplicate action point numbers as WARN", "[kfx_sim][map_content_verify]") {
    MapContent c = base_content();
    MapActionPointRecord a1; a1.point_number = 7;
    MapActionPointRecord a2; a2.point_number = 7;
    c.action_points.push_back(a1);
    c.action_points.push_back(a2);

    auto issues = verify_map_content(c);
    CHECK(any_message_contains(issues, "Action point number 7 is used more than once"));
}

TEST_CASE_METHOD(ResetConfig, "verify_map_content flags duplicate hero gate numbers as WARN", "[kfx_sim][map_content_verify]") {
    MapContent c = base_content();
    MapThingRecord gate1; gate1.thing_class = TCls_Object; gate1.model = kPlainObjectModel; gate1.herogate_number = 3;
    MapThingRecord gate2; gate2.thing_class = TCls_Object; gate2.model = kPlainObjectModel; gate2.herogate_number = 3;
    c.things.push_back(gate1);
    c.things.push_back(gate2);

    auto issues = verify_map_content(c);
    CHECK(any_message_contains(issues, "Hero gate number 3 is used more than once"));
}

TEST_CASE_METHOD(ResetConfig, "verify_map_content flags a breached border as WARN", "[kfx_sim][map_content_verify]") {
    MapContent c = base_content();
    c.slab_kind[c.slab_index(0, 2)] = kOpenKind; // punch a hole in the left border

    auto issues = verify_map_content(c);
    CHECK(any_message_contains(issues, "border isn't fully impenetrable"));
}

TEST_CASE_METHOD(ResetConfig, "verify_map_content is silent on the border check for an intact border", "[kfx_sim][map_content_verify]") {
    MapContent c = base_content();
    auto issues = verify_map_content(c);
    CHECK_FALSE(any_message_contains(issues, "border isn't fully impenetrable"));
}

TEST_CASE_METHOD(ResetConfig, "verify_map_content always reports a classic-compatibility INFO line", "[kfx_sim][map_content_verify]") {
    MapContent c = base_content();
    auto issues = verify_map_content(c);
    CHECK(any_message_contains(issues, "Classic-compatible:"));
}
