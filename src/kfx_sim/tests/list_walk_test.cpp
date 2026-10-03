// list_walk.h (refactor pass 3, S01): the iterators that replace the
// hand-written list walks. Each test builds a list directly in
// kfx_sim_state and checks what the walk hands out, in which order, and
// where it stops -- the behaviour the converted loops rely on.
#include <catch2/catch_test_macros.hpp>

#include "list_walk.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "thing_list.h"
#include "config_creature.h"

#include <cstring>
#include <vector>

namespace {
struct ResetState {
    ResetState() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
    }
};

/* Links things idx[0] -> idx[1] -> ... -> 0 through next_of_class. */
void link_class(const std::vector<ThingIndex> &idx)
{
    for (size_t n = 0; n < idx.size(); n++)
        kfx_sim_state.things_data[idx[n]].next_of_class = (n + 1 < idx.size()) ? idx[n + 1] : 0;
}

/* Links things through next_on_mapblk, head in mapblk. */
void link_map_block(struct Map *mapblk, const std::vector<ThingIndex> &idx)
{
    mapblk->mapwho = idx.empty() ? 0 : idx[0];
    for (size_t n = 0; n < idx.size(); n++)
        kfx_sim_state.things_data[idx[n]].next_on_mapblk = (n + 1 < idx.size()) ? idx[n + 1] : 0;
}
}

TEST_CASE_METHOD(ResetState, "FOR_EACH_THING over a class list hands out every thing in list order", "[kfx_sim][list_walk]") {
    link_class({4, 9, 2});
    std::vector<ThingIndex> seen;
    FOR_EACH_THING(thing, thing_walk_list(4, THINGS_COUNT))
    {
        seen.push_back((ThingIndex)(thing - kfx_sim_state.things_data));
    }
    CHECK(seen == std::vector<ThingIndex>{4, 9, 2});
}

TEST_CASE_METHOD(ResetState, "an empty list hands out nothing and counts nothing", "[kfx_sim][list_walk]") {
    int bodies = 0;
    FOR_EACH_THING(thing, thing_walk_list(0, THINGS_COUNT))
    {
        (void)thing;
        bodies++;
    }
    CHECK(bodies == 0);
}

TEST_CASE_METHOD(ResetState, "the next index is read before the body, so the body may unlink the current thing", "[kfx_sim][list_walk]") {
    link_class({1, 2, 3});
    std::vector<ThingIndex> seen;
    FOR_EACH_THING(thing, thing_walk_list(1, THINGS_COUNT))
    {
        ThingIndex idx = (ThingIndex)(thing - kfx_sim_state.things_data);
        seen.push_back(idx);
        thing->next_of_class = 0; // what deleting it does to its own link
    }
    CHECK(seen == std::vector<ThingIndex>{1, 2, 3});
}

TEST_CASE_METHOD(ResetState, "break, continue and the walk's count behave as in a plain loop", "[kfx_sim][list_walk]") {
    link_class({1, 2, 3, 4});
    int bodies = 0;
    uint64_t handed_out = 0;
    FOR_EACH_THING(thing, thing_walk_list(1, THINGS_COUNT))
    {
        ThingIndex idx = (ThingIndex)(thing - kfx_sim_state.things_data);
        handed_out = thing_walk.count;
        if (idx == 2)
            continue;
        bodies++;
        if (idx == 3)
            break;
    }
    CHECK(bodies == 2);
    CHECK(handed_out == 3);
}

TEST_CASE_METHOD(ResetState, "a walk stops at an invalid index", "[kfx_sim][list_walk]") {
    kfx_sim_state.things_data[1].next_of_class = THINGS_COUNT + 5; // out of the things array
    int bodies = 0;
    FOR_EACH_THING(thing, thing_walk_list(1, THINGS_COUNT))
    {
        (void)thing;
        bodies++;
    }
    CHECK(bodies == 1);
}

TEST_CASE_METHOD(ResetState, "a cycle stops after limit+1 elements, as the hand-written k > limit guard did", "[kfx_sim][list_walk]") {
    link_class({1, 2});
    kfx_sim_state.things_data[2].next_of_class = 1; // 1 -> 2 -> 1 -> ...
    int bodies = 0;
    FOR_EACH_THING(thing, thing_walk_list(1, 5))
    {
        (void)thing;
        bodies++;
    }
    CHECK(bodies == 6);
}

TEST_CASE_METHOD(ResetState, "a structure-list walk reads the list's live count as its guard", "[kfx_sim][list_walk]") {
    link_class({1, 2, 3, 4, 5, 6});
    struct StructureList slist;
    slist.count = 6;
    slist.index = 1;
    int bodies = 0;
    FOR_EACH_THING(thing, thing_walk_structure_list(&slist))
    {
        (void)thing;
        bodies++;
        slist.count--; // each body deletes its thing
    }
    // After the 4th body, count 4 > live count 2: the guard stops the walk,
    // as `k++; if (k > slist->count) break;` did.
    CHECK(bodies == 4);
}

TEST_CASE_METHOD(ResetState, "a map-block walk follows next_on_mapblk and breaks a cycle on overflow", "[kfx_sim][list_walk]") {
    struct Map *mapblk = &kfx_sim_state.map[0];
    link_map_block(mapblk, {3, 7});
    std::vector<ThingIndex> seen;
    FOR_EACH_THING(thing, thing_walk_map_block(mapblk))
    {
        seen.push_back((ThingIndex)(thing - kfx_sim_state.things_data));
    }
    CHECK(seen == std::vector<ThingIndex>{3, 7});

    kfx_sim_state.things_data[7].next_on_mapblk = 3; // 3 -> 7 -> 3 -> ...
    int bodies = 0;
    FOR_EACH_THING(thing, thing_walk_map_block(mapblk))
    {
        (void)thing;
        bodies++;
    }
    CHECK(bodies == THINGS_COUNT + 1);
    CHECK(kfx_sim_state.things_data[7].next_on_mapblk == 0); // break_mapwho_infinite_chain() cut the cycle
}

TEST_CASE_METHOD(ResetState, "a creature walk follows players_next_creature_idx and stops on a thing without a CreatureControl", "[kfx_sim][list_walk]") {
    kfx_sim_state.things_data[5].ccontrol_idx = 1;
    kfx_sim_state.things_data[8].ccontrol_idx = 2;
    kfx_sim_state.cctrl_data[1].players_next_creature_idx = 8;
    kfx_sim_state.cctrl_data[2].players_next_creature_idx = 11; // thing 11 has no CreatureControl
    std::vector<ThingIndex> seen;
    FOR_EACH_THING(thing, thing_walk_creatures(5, CREATURES_COUNT))
    {
        seen.push_back((ThingIndex)(thing - kfx_sim_state.things_data));
    }
    CHECK(seen == std::vector<ThingIndex>{5, 8});
}

TEST_CASE_METHOD(ResetState, "room walks follow next_of_owner or next_of_kind", "[kfx_sim][list_walk]") {
    kfx_sim_state.rooms[2].next_of_owner = 6;
    kfx_sim_state.rooms[6].next_of_owner = 0;
    kfx_sim_state.rooms[2].next_of_kind = 3;
    kfx_sim_state.rooms[3].next_of_kind = 0;
    std::vector<RoomIndex> by_owner, by_kind;
    FOR_EACH_ROOM(room, room_walk_owner(2))
    {
        by_owner.push_back((RoomIndex)(room - kfx_sim_state.rooms));
    }
    FOR_EACH_ROOM(room, room_walk_kind(2))
    {
        by_kind.push_back((RoomIndex)(room - kfx_sim_state.rooms));
    }
    CHECK(by_owner == std::vector<RoomIndex>{2, 6});
    CHECK(by_kind == std::vector<RoomIndex>{2, 3});
}

TEST_CASE_METHOD(ResetState, "a room-slab walk steps after the body, so the body sees the current slab's link", "[kfx_sim][list_walk]") {
    kfx_sim_state.map_tiles_x = 10;
    kfx_sim_state.map_tiles_y = 10;
    kfx_sim_state.slabmap[12].next_in_room = 13;
    kfx_sim_state.slabmap[13].next_in_room = 20;
    kfx_sim_state.slabmap[20].next_in_room = 0;
    struct Room *room = &kfx_sim_state.rooms[1];
    room->slabs_list = 12;
    room->slabs_count = 3;
    std::vector<SlabCodedCoords> seen;
    FOR_EACH_ROOM_SLAB(slb_num, room_slab_walk(room))
    {
        seen.push_back(slb_num);
        if (slb_num == 13)
            kfx_sim_state.slabmap[13].next_in_room = 0; // relinked by the body: the walk follows it
    }
    CHECK(seen == std::vector<SlabCodedCoords>{12, 13});
}

TEST_CASE_METHOD(ResetState, "a room-slab walk from a first slab with no guard ends at slab number 0", "[kfx_sim][list_walk]") {
    kfx_sim_state.map_tiles_x = 10;
    kfx_sim_state.map_tiles_y = 10;
    kfx_sim_state.slabmap[4].next_in_room = 5;
    kfx_sim_state.slabmap[5].next_in_room = 0;
    std::vector<SlabCodedCoords> seen;
    FOR_EACH_ROOM_SLAB(slb_num, room_slab_walk_from(4, UINT64_MAX))
    {
        seen.push_back(slb_num);
    }
    CHECK(seen == std::vector<SlabCodedCoords>{4, 5});
}

TEST_CASE_METHOD(ResetState, "a read-ahead room-slab walk steps before the body, so the body may unlink the current slab", "[kfx_sim][list_walk]") {
    kfx_sim_state.map_tiles_x = 10;
    kfx_sim_state.map_tiles_y = 10;
    kfx_sim_state.slabmap[12].next_in_room = 13;
    kfx_sim_state.slabmap[13].next_in_room = 20;
    kfx_sim_state.slabmap[20].next_in_room = 0;
    struct Room *room = &kfx_sim_state.rooms[1];
    room->slabs_list = 12;
    room->slabs_count = 3;
    std::vector<SlabCodedCoords> seen;
    FOR_EACH_ROOM_SLAB(slb_num, room_slab_walk_ahead(room))
    {
        seen.push_back(slb_num);
        kfx_sim_state.slabmap[slb_num].next_in_room = 0; // what kill_all_room_slabs_and_contents() does
    }
    CHECK(seen == std::vector<SlabCodedCoords>{12, 13, 20});
}

// Defined in thing_list.c, not in its header.
extern "C" int64_t creature_of_model_find_first(ThingModel crmodel);

namespace {
/* Creatures at idx[n] of model models[n], linked as the creature list. */
void make_creatures(const std::vector<ThingIndex> &idx, const std::vector<ThingModel> &models)
{
    kfx_sim_state.thing_lists[TngList_Creatures].index = idx.empty() ? 0 : idx[0];
    link_class(idx);
    for (size_t n = 0; n < idx.size(); n++)
    {
        struct Thing *thing = &kfx_sim_state.things_data[idx[n]];
        thing->index = idx[n];
        thing->class_id = TCls_Creature;
        thing->model = models[n];
    }
}
}

TEST_CASE_METHOD(ResetState, "creature_of_model_find_first returns the first creature of the model, including the last one in the list (pass 3 F6)", "[kfx_sim][list_walk]") {
    kfx_config_state.conf.crtr_conf.model_count = 8;
    make_creatures({4, 9, 2}, {2, 3, 2});
    CHECK(creature_of_model_find_first(3) == 9);
    CHECK(creature_of_model_find_first(2) == 4);
    CHECK(creature_of_model_find_first(5) == 0);
    make_creatures({4, 2, 9}, {2, 2, 3});
    CHECK(creature_of_model_find_first(3) == 9);
}

TEST_CASE_METHOD(ResetState, "lord_of_the_land_find finds the creature whose model is the Lord of the Land (pass 3 F6)", "[kfx_sim][list_walk]") {
    kfx_config_state.conf.crtr_conf.model_count = 8;
    kfx_config_state.conf.crtr_conf.model[3].model_flags = CMF_IsLordOfLand;
    make_creatures({4, 2, 9}, {2, 2, 3});
    CHECK(lord_of_the_land_find() == &kfx_sim_state.things_data[9]);
    make_creatures({4, 2}, {2, 2});
    CHECK(lord_of_the_land_find() == INVALID_THING);
}
