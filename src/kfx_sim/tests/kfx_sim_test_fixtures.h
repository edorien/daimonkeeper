// Shared fixture helpers for kfx_sim unit tests.
//
// Extracted once several creature_states_*.c "at_X_room()"/"Xing()"
// state-machine entry points (at_barrack_room/barracking,
// at_guard_post_room/guarding, and siblings across the 13 previously
// wholly-untouched creature_states_*.c files) all turned out to need the
// same shape of setup: a real Thing+CreatureControl pair, a real
// allocated Room linked into kfx_sim_state.slabmap[] so
// get_room_thing_is_on() resolves it, and a job's config entry
// (room_role/job_flags/continue_crstate) reachable through
// get_config_for_job()'s bit-position index. Rather than re-deriving and
// re-documenting that setup in each new test file (as
// room_garden_test.cpp/creature_jobs_test.cpp/creature_states_prisn_test.cpp
// each did independently), it's collected here once other test files can
// build on.
//
// Deliberately a handful of small, composable helpers, not one
// monolithic fixture struct -- a test picks only what its function under
// test actually touches, the same "cheapest fixture that's still real"
// discipline used throughout this plan. Every helper operates on real
// kfx_sim_state/kfx_config_state slots (never a stack-local Thing/Room),
// since thing_is_invalid()/room_is_invalid() range-check against the real
// arrays (creature_groups_test.cpp/room_graveyard_test.cpp both caught a
// stack-local object failing that check the hard way).
#ifndef KFX_SIM_TEST_FIXTURES_H
#define KFX_SIM_TEST_FIXTURES_H

#include "kfx_sim_state.h"
#include "kfx_config_state.h"
#include "thing_data.h"
#include "creature_control.h"
#include "room_data.h"
#include "slab_data.h"
#include "dungeon_data.h"
#include "player_data.h"
#include "player_computer_types.h"
#include "globals.h"

#include <cstring>

namespace kfx_test {

// Resets kfx_sim_state/kfx_config_state to zero and sets a small,
// consistent map size (matches room_garden_test.cpp's original map+
// slabmap fixture). Also sets neutral_player_num to the real
// PLAYER_NEUTRAL sentinel (5): leaving it at its zeroed default silently
// collides with player/owner 0 in many functions that early-out on
// "is this the neutral player" -- caught the hard way in both
// creature_senses_test.cpp and creature_states_prisn_test.cpp.
struct ResetSimAndConfig {
    ResetSimAndConfig() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
        kfx_sim_state.map_subtiles_x = 10;
        kfx_sim_state.map_subtiles_y = 10;
        kfx_sim_state.map_tiles_x = 4;
        kfx_sim_state.map_tiles_y = 4;
        kfx_config_state.neutral_player_num = PLAYER_NEUTRAL;
        // states_count gates get_thing_active_state_info/get_thing_continue_
        // state_info/get_thing_state_info_num/get_creature_state_type_f's
        // bounds checks (upstream #5237 replaced the old compile-time
        // CREATURE_STATES_COUNT macro with this runtime, config-driven
        // count) -- left at CrSt_ListEnd, the same bound CREATURE_STATES_COUNT
        // used to be defined as, so every state id a test sets up stays
        // "in range" by default.
        kfx_config_state.conf.crtr_conf.states_count = CrSt_ListEnd;
    }
};

// Places a real, allocated Room of the given kind/owner at slab
// (slb_x, slb_y), linking kfx_sim_state.slabmap[]'s room_index so
// get_room_thing_is_on()/subtile_room_get() resolve it -- the fixture
// room_garden_test.cpp/creature_jobs_test.cpp introduced independently.
// room_index must be non-zero (index 0 is the reserved invalid-sentinel
// slot, per room_is_invalid()'s "<= &rooms[0]" check).
inline struct Room *make_room_at_slab(RoomIndex room_index, MapSlabCoord slb_x, MapSlabCoord slb_y, RoomKind kind, PlayerNumber owner)
{
    struct Room *room = room_get(room_index);
    room->index = room_index;
    room->kind = kind;
    room->owner = owner;
    room->alloc_flags |= RoF_Allocated;
    get_slabmap_block(slb_x, slb_y)->room_index = room_index;
    return room;
}

// Places a real Thing+CreatureControl pair at the given indices, cross-
// linked via ccontrol_idx, with ->index set on both (thing_get()/
// creature_control_get() never set it themselves -- allocators normally
// do; creature_groups_test.cpp caught a fixture that forgot this on the
// Thing side the hard way).
inline struct Thing *make_creature(ThingIndex thing_idx, CctrlIndex cctrl_idx, PlayerNumber owner)
{
    struct Thing *thing = thing_get(thing_idx);
    thing->index = thing_idx;
    thing->class_id = TCls_Creature;
    thing->alloc_flags = TAlF_Exists;
    thing->owner = owner;
    thing->ccontrol_idx = cctrl_idx;
    struct CreatureControl *cctrl = creature_control_get(cctrl_idx);
    cctrl->index = cctrl_idx;
    cctrl->creature_control_flags |= CCFlg_Exists;
    return thing;
}

// Makes thing_idx available to the real thing allocator
// (allocate_free_thing_structure/create_*) by pushing it onto the
// synced/unsynced free-thing-index stack it pops from (LIFO: the most
// recently pushed index is the next one handed out). ResetSimAndConfig's
// zero-fill leaves both stacks' counts at 0, so without this every
// allocator-backed create_*() call fails with "no free slots" -- unlike
// every other fixture helper here, which builds a Thing/Room/etc. by
// writing its slot directly rather than going through the allocator at
// all. "Synced" vs. "unsynced" matches is_non_synchronized_thing_class()
// (TCls_EffectElem/_AmbientSnd/_Effect are unsynced; everything else,
// including creatures/objects/shots/traps/doors/cave-ins, is synced).
inline void make_synced_thing_index_available(ThingIndex thing_idx)
{
    kfx_sim_state.synced_free_things[kfx_sim_state.synced_free_things_count++] = thing_idx;
}

inline void make_unsynced_thing_index_available(ThingIndex thing_idx)
{
    kfx_sim_state.unsynced_free_things[kfx_sim_state.unsynced_free_things_count++] = thing_idx;
}

// Registers a real (non-RoRoF_None) room_role/job_flags/continue_crstate
// for the given job bitmask, at get_config_for_job's own bit-position
// index -- job value (1<<n) resolves to jobs[n+1], traced by hand from
// get_config_for_job's `while(k){k>>=1;i++;}` loop and confirmed
// empirically in creature_jobs_test.cpp/room_garden_test.cpp before this
// helper existed. Grows jobs_count to cover the computed index
// automatically.
inline void configure_job(CreatureJob job, RoomRole room_role, uint64_t job_flags, CrtrStateId continue_crstate = 0)
{
    uint64_t k = job;
    int64_t i = 0;
    while (k) { k >>= 1; i++; }
    if (kfx_config_state.conf.crtr_conf.jobs_count <= i)
        kfx_config_state.conf.crtr_conf.jobs_count = i + 1;
    struct CreatureJobConfig *jobcfg = &kfx_config_state.conf.crtr_conf.jobs[i];
    jobcfg->room_role = room_role;
    jobcfg->job_flags = job_flags;
    jobcfg->continue_crstate = continue_crstate;
}

// Registers room_role in the given room kind's config (room_cfgstats[kind].roles),
// growing room_types_count to cover it. Matches room_garden_test.cpp's
// pattern, generalized into a helper.
inline void configure_room_role(RoomKind kind, RoomRole role)
{
    if (kfx_config_state.conf.slab_conf.room_types_count <= kind)
        kfx_config_state.conf.slab_conf.room_types_count = kind + 1;
    kfx_config_state.conf.slab_conf.room_cfgstats[kind].roles |= role;
}

// --- Heavier fixtures: computer-AI (Computer2/Dungeon/PlayerInfo) and
// linked-list wiring, added for player_comp*.c's "complicated logic"
// functions (calculate_number_of_creatures_to_move, computer_checks_hates,
// count_entrances, computer_check_prison_tendency, and similar). These
// functions don't walk things_data[]/rooms[] directly like the "at_X_room"
// entry points above -- they walk a *player's* linked view of those
// arrays: a Dungeon's creatr_list_start/digger_list_start/room_list_start[]
// head pointers, threaded through CreatureControl::players_next_creature_idx
// and Room::next_of_kind/next_of_owner. Every one of these is a plain
// "push onto the front of an intrusive singly-linked list" -- the same
// shape as creature_states_spdig_test.cpp's mapwho-chain wiring, just
// keyed by dungeon/room-kind instead of by map subtile.

// Makes player plyr_idx exist and be active, with id_number matching its
// own slot -- required before get_players_dungeon()/player_exists() (which
// both key off ->id_number, not the players[] array position) will treat
// it as real. Left unset by ResetSimAndConfig's zero-fill, same trap as
// neutral_player_num: every player's id_number defaults to 0, silently
// aliasing player 0 unless set explicitly per player under test.
inline struct PlayerInfo *make_player_active(PlayerNumber plyr_idx)
{
    struct PlayerInfo *player = get_player(plyr_idx);
    player->id_number = plyr_idx;
    player->allocflags |= PlaF_Allocated;
    player->is_active = 1;
    return player;
}

// Pushes thing_idx onto the front of a per-dungeon creature list
// (Dungeon::creatr_list_start or ::digger_list_start -- both are threaded
// through the same CreatureControl::players_next_creature_idx field, per
// count_creatures_in_dungeon()/count_diggers_in_dungeon()). thing_idx must
// already be a real creature (see make_creature above).
inline void link_creature_into_player_list(int64_t *list_head, ThingIndex thing_idx)
{
    struct CreatureControl *cctrl = creature_control_get_from_thing(thing_get(thing_idx));
    cctrl->players_next_creature_idx = *list_head;
    *list_head = (int64_t)thing_idx;
}

// Pushes room_index onto the front of a "rooms of a kind" list threaded
// through Room::next_of_kind (e.g. kfx_sim_state.entrance_room_id's
// entrance-room chain, walked by count_entrances()).
inline void link_room_into_kind_list(int64_t *list_head, RoomIndex room_index)
{
    struct Room *room = room_get(room_index);
    room->next_of_kind = *list_head;
    *list_head = room_index;
}

// Pushes room_index onto the front of a per-dungeon, per-kind room list
// (Dungeon::room_list_start[rkind]), threaded through Room::next_of_owner
// -- per get_room_kind_total_and_used_capacity()/
// computer_check_move_creatures_to_room()'s room sweeps.
inline void link_room_into_owner_list(int64_t *list_head, RoomIndex room_index)
{
    struct Room *room = room_get(room_index);
    room->next_of_owner = *list_head;
    *list_head = room_index;
}

} // namespace kfx_test

#endif
