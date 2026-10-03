// kfx_sim "creature" cluster: creature_states_rsrch.c's research-queue
// bookkeeping -- get_next_research_item() and has_new_rooms_to_research()
// both take a bare `const struct Dungeon *`, no Thing/CreatureControl
// involved at all, simpler than every other creature_states_*.c test in
// this plan so far. at_research_room() itself (the actual state-machine
// entry point) needs the usual fuller Room/job-system context and is
// deliberately not attempted here (docs/Architecture/testing-harness.md
// §10), same call as creature_states_barck.c/guard.c/etc.
#include <catch2/catch_test_macros.hpp>

#include "creature_states_rsrch.h"
#include "dungeon_data.h"
#include "config_creature.h"
#include "kfx_sim_test_fixtures.h"
#include "thing_stats.h"

#include <cstring>

namespace {
struct ZeroedDungeon {
    ZeroedDungeon() { std::memset(&dungeon, 0, sizeof(dungeon)); }
    struct Dungeon dungeon;
};
}

TEST_CASE_METHOD(ZeroedDungeon, "get_next_research_item returns -1 when research_num is zero", "[kfx_sim][creature_states_rsrch]") {
    dungeon.research_num = 0;
    CHECK(get_next_research_item(&dungeon) == -1);
}

TEST_CASE_METHOD(ZeroedDungeon, "get_next_research_item finds an unresearched power (magic_resrchable set, magic_level still zero)", "[kfx_sim][creature_states_rsrch]") {
    dungeon.research_num = 1;
    dungeon.research[0].rtyp = RsCat_Power;
    dungeon.research[0].rkind = 3;
    dungeon.magic_resrchable[3] = 1;
    dungeon.magic_level[3] = 0;
    CHECK(get_next_research_item(&dungeon) == 0);
}

TEST_CASE_METHOD(ZeroedDungeon, "get_next_research_item skips a power once its magic_level is nonzero", "[kfx_sim][creature_states_rsrch]") {
    dungeon.research_num = 1;
    dungeon.research[0].rtyp = RsCat_Power;
    dungeon.research[0].rkind = 3;
    dungeon.magic_resrchable[3] = 1;
    dungeon.magic_level[3] = 1; // already researched
    CHECK(get_next_research_item(&dungeon) == -1);
}

TEST_CASE_METHOD(ZeroedDungeon, "get_next_research_item finds an unallowed creature type", "[kfx_sim][creature_states_rsrch]") {
    dungeon.research_num = 1;
    dungeon.research[0].rtyp = RsCat_Creature;
    dungeon.research[0].rkind = 2;
    dungeon.creature_allowed[2] = 0;
    CHECK(get_next_research_item(&dungeon) == 0);
}

TEST_CASE_METHOD(ZeroedDungeon, "get_next_research_item skips a creature type once it's already allowed", "[kfx_sim][creature_states_rsrch]") {
    dungeon.research_num = 1;
    dungeon.research[0].rtyp = RsCat_Creature;
    dungeon.research[0].rkind = 2;
    dungeon.creature_allowed[2] = 1;
    CHECK(get_next_research_item(&dungeon) == -1);
}

TEST_CASE_METHOD(ZeroedDungeon, "get_next_research_item finds a room that still needs research (room_resrchable 1)", "[kfx_sim][creature_states_rsrch]") {
    dungeon.research_num = 1;
    dungeon.research[0].rtyp = RsCat_Room;
    dungeon.research[0].rkind = 4;
    dungeon.room_buildable[4] = 0;
    dungeon.room_resrchable[4] = 1;
    CHECK(get_next_research_item(&dungeon) == 0);
}

TEST_CASE_METHOD(ZeroedDungeon, "get_next_research_item skips a room once it's already buildable (bit 0 set)", "[kfx_sim][creature_states_rsrch]") {
    dungeon.research_num = 1;
    dungeon.research[0].rtyp = RsCat_Room;
    dungeon.research[0].rkind = 4;
    dungeon.room_buildable[4] = 1; // bit 0 set: buildable already
    dungeon.room_resrchable[4] = 1;
    CHECK(get_next_research_item(&dungeon) == -1);
}

TEST_CASE_METHOD(ZeroedDungeon, "get_next_research_item finds a captured room needing research (room_resrchable 4, buildable bit 1 set)", "[kfx_sim][creature_states_rsrch]") {
    dungeon.research_num = 1;
    dungeon.research[0].rtyp = RsCat_Room;
    dungeon.research[0].rkind = 4;
    dungeon.room_buildable[4] = 2; // bit 1 set, bit 0 clear
    dungeon.room_resrchable[4] = 4;
    CHECK(get_next_research_item(&dungeon) == 0);
}

TEST_CASE_METHOD(ZeroedDungeon, "get_next_research_item skips RsCat_None entries and moves to the next", "[kfx_sim][creature_states_rsrch]") {
    dungeon.research_num = 2;
    dungeon.research[0].rtyp = RsCat_None;
    dungeon.research[1].rtyp = RsCat_Creature;
    dungeon.research[1].rkind = 1;
    dungeon.creature_allowed[1] = 0;
    CHECK(get_next_research_item(&dungeon) == 1);
}

TEST_CASE_METHOD(ZeroedDungeon, "has_new_rooms_to_research is false when research_num is zero", "[kfx_sim][creature_states_rsrch]") {
    dungeon.research_num = 0;
    CHECK_FALSE(has_new_rooms_to_research(&dungeon));
}

TEST_CASE_METHOD(ZeroedDungeon, "has_new_rooms_to_research is true for an unbuilt, researchable room entry", "[kfx_sim][creature_states_rsrch]") {
    dungeon.research_num = 1;
    dungeon.research[0].rtyp = RsCat_Room;
    dungeon.research[0].rkind = 6;
    dungeon.room_buildable[6] = 0;
    dungeon.room_resrchable[6] = 2;
    CHECK(has_new_rooms_to_research(&dungeon));
}

TEST_CASE_METHOD(ZeroedDungeon, "has_new_rooms_to_research ignores non-room research categories", "[kfx_sim][creature_states_rsrch]") {
    dungeon.research_num = 1;
    dungeon.research[0].rtyp = RsCat_Power;
    dungeon.research[0].rkind = 6;
    dungeon.room_buildable[6] = 0;
    dungeon.room_resrchable[6] = 2;
    CHECK_FALSE(has_new_rooms_to_research(&dungeon));
}

TEST_CASE_METHOD(ZeroedDungeon, "has_new_rooms_to_research is false once the room is already buildable", "[kfx_sim][creature_states_rsrch]") {
    dungeon.research_num = 1;
    dungeon.research[0].rtyp = RsCat_Room;
    dungeon.research[0].rkind = 6;
    dungeon.room_buildable[6] = 1; // bit 0 set
    dungeon.room_resrchable[6] = 2;
    CHECK_FALSE(has_new_rooms_to_research(&dungeon));
}

// creature_can_do_research needs a real Thing (is_neutral_thing/
// get_dungeon by ->owner), so uses kfx_sim_test_fixtures.h's
// ResetSimAndConfig/make_creature rather than this file's own
// ZeroedDungeon. creature_stats_get_from_thing() always resolves to
// model[0] in this test binary (the same indirection noted throughout
// this session), so research_value is configured on model[0].
TEST_CASE_METHOD(kfx_test::ResetSimAndConfig, "creature_can_do_research requires a non-neutral owner, a positive research_value, and an active research item", "[kfx_sim][creature_states_rsrch]") {
    struct Thing *thing = kfx_test::make_creature(1, 1, 0);
    CHECK_FALSE(creature_can_do_research(thing)); // research_value defaults to 0

    kfx_config_state.conf.crtr_conf.model[0].research_value = 1;
    CHECK(creature_can_do_research(thing)); // current_research_idx defaults to 0, which is >= 0

    get_dungeon(0)->current_research_idx = -1;
    CHECK_FALSE(creature_can_do_research(thing));

    get_dungeon(0)->current_research_idx = 0;
    struct Thing *neutral_thing = kfx_test::make_creature(2, 2, PLAYER_NEUTRAL);
    CHECK_FALSE(creature_can_do_research(neutral_thing));
}
