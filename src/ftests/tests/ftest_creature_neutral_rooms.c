// A neutral creature takes no job in a neutral room. A level's neutral creatures get their first job from where
// they stand (init_creature_state(), at the level's start or when one is added with the initialise spawn type): one
// in a neutral torture chamber was an "owned creature" there, so it went on a device (painful torture, or a Dark
// Mistress's kinky torture) and stayed; one in a neutral prison went into a cell, one in a neutral guard post stood
// guard. The neutral rooms are the map's, not the neutrals'.
//
// What stays: a neutral creature placed in a *player's* prison or torture chamber is that player's captive (the
// PASSIVE_NEUTRALS classic bug off): the controls here. KeeperFX's way is the NEUTRAL_ROOM_JOBS classic bug.
//
// Setup: in a block of solid rock (no keeper's creature can see in, which would claim the neutrals), three neutral
// rooms -- a torture chamber with a neutral Dark Mistress and orc, a prison and a guard post with a neutral orc each --
// PLAYER0's torture chamber and prison with a neutral orc each, and the three neutral rooms again, whose creatures get
// their first job with NEUTRAL_ROOM_JOBS set. Each creature gets its first job the way a level start gives it, and 150
// turns later the neutral rooms' creatures must not be working in them, the controls must be held captive, and with
// NEUTRAL_ROOM_JOBS the neutrals must be working in their neutral rooms.
#include "ftest_creature_neutral_rooms.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "config_keeperfx.h"
#include "../ftest.h"
#include "../ftest_util.h"

#include "config_creature.h"
#include "config_crtrstates.h"
#include "creature_control.h"
#include "creature_states.h"
#include "creature_states_tortr.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "room_data.h"
#include "slab_data.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ROOM_SIZE 3   // slabs
#define ROOM_GAP 2    // rock between two rooms
#define BLOCK_MARGIN 2
#define ROOMS_PER_ROW 3

enum NeutralRoomsCase {
    NRC_NeutralTortureMistress = 0,
    NRC_NeutralTortureOrc,
    NRC_NeutralPrisonOrc,
    NRC_NeutralGuardPostOrc,
    NRC_PlayerTortureOrc,
    NRC_PlayerPrisonOrc,
    NRC_FlagTortureMistress,
    NRC_FlagTortureOrc,
    NRC_FlagPrisonOrc,
    NRC_FlagGuardPostOrc,
    NRC_COUNT,
};

struct NeutralRoomsCaseDef {
    const char *name;
    int room;            // index into the rooms below
    const char *model;
    TbBool works;        // the expected outcome: held or on guard in the room, or working in no room
    TbBool room_jobs;    // given its first job with NEUTRAL_ROOM_JOBS set
};

struct NeutralRoomsRoomDef {
    SlabKind kind;
    TbBool player_owned; // PLAYER0's, or neutral
    int row, col;
};

static const struct NeutralRoomsRoomDef rooms[] = {
    {SlbT_TORTURE,   false, 0, 0},
    {SlbT_PRISON,    false, 0, 1},
    {SlbT_GUARDPOST, false, 0, 2},
    {SlbT_TORTURE,   true,  1, 0},
    {SlbT_PRISON,    true,  1, 1},
    {SlbT_TORTURE,   false, 2, 0},
    {SlbT_PRISON,    false, 2, 1},
    {SlbT_GUARDPOST, false, 2, 2},
};

static const struct NeutralRoomsCaseDef cases[NRC_COUNT] = {
    {"Dark Mistress in a neutral torture chamber", 0, "DARK_MISTRESS", false, false},
    {"orc in a neutral torture chamber",           0, "ORC",           false, false},
    {"orc in a neutral prison",                    1, "ORC",           false, false},
    {"orc in a neutral guard post",                2, "ORC",           false, false},
    {"orc in PLAYER0's torture chamber",           3, "ORC",           true,  false},
    {"orc in PLAYER0's prison",                    4, "ORC",           true,  false},
    {"Dark Mistress in a neutral torture chamber, NEUTRAL_ROOM_JOBS", 5, "DARK_MISTRESS", true, true},
    {"orc in a neutral torture chamber, NEUTRAL_ROOM_JOBS",           5, "ORC",           true, true},
    {"orc in a neutral prison, NEUTRAL_ROOM_JOBS",                    6, "ORC",           true, true},
    {"orc in a neutral guard post, NEUTRAL_ROOM_JOBS",                7, "ORC",           true, true},
};

struct ftest_creature_neutral_rooms__variables
{
    ThingIndex creature_idx[NRC_COUNT];
};
static struct ftest_creature_neutral_rooms__variables ftest_creature_neutral_rooms__vars;

static FTestActionResult ftest_creature_neutral_rooms_action001__setup(struct FTestActionArgs* const args);
static FTestActionResult ftest_creature_neutral_rooms_action002__verify(struct FTestActionArgs* const args);

TbBool ftest_creature_neutral_rooms_init()
{
    ftest_append_action(ftest_creature_neutral_rooms_action001__setup, 0, &ftest_creature_neutral_rooms__vars);
    ftest_append_action(ftest_creature_neutral_rooms_action002__verify, 150, &ftest_creature_neutral_rooms__vars);
    return true;
}

/** A block of rock w x h slabs (plus the margin) to build in: found by scanning the map. */
static TbBool find_rock_block(MapSlabCoord w, MapSlabCoord h, MapSlabCoord *out_x, MapSlabCoord *out_y)
{
    for (MapSlabCoord y = 1; y + h + 2 * BLOCK_MARGIN < kfx_sim_state.map_tiles_y - 1; y++)
    {
        for (MapSlabCoord x = 1; x + w + 2 * BLOCK_MARGIN < kfx_sim_state.map_tiles_x - 1; x++)
        {
            TbBool all_rock = true;
            for (MapSlabCoord j = y; (j < y + h + 2 * BLOCK_MARGIN) && all_rock; j++)
                for (MapSlabCoord i = x; (i < x + w + 2 * BLOCK_MARGIN) && all_rock; i++)
                    all_rock = (get_slabmap_block(i, j)->kind == SlbT_ROCK);
            if (all_rock)
            {
                *out_x = x + BLOCK_MARGIN;
                *out_y = y + BLOCK_MARGIN;
                return true;
            }
        }
    }
    return false;
}

static FTestActionResult ftest_creature_neutral_rooms_action001__setup(struct FTestActionArgs* const args)
{
    struct ftest_creature_neutral_rooms__variables* const vars = args->data;
    const PlayerNumber neutral = kfx_config_state.neutral_player_num;
    const MapSlabCoord block_w = ROOMS_PER_ROW * ROOM_SIZE + (ROOMS_PER_ROW - 1) * ROOM_GAP;
    const MapSlabCoord block_h = 3 * ROOM_SIZE + 2 * ROOM_GAP;
    MapSlabCoord bx, by;
    if (!find_rock_block(block_w, block_h, &bx, &by))
    {
        FTEST_FAIL_TEST("No block of rock %" PRId64 "x%" PRId64 " slabs to build in", (int64_t)block_w, (int64_t)block_h);
        return FTRs_Go_To_Next_Action;
    }
    FTESTLOG("building in the rock at slab (%" PRId64 ",%" PRId64 ")", (int64_t)bx, (int64_t)by);
    for (size_t r = 0; r < sizeof(rooms) / sizeof(rooms[0]); r++)
    {
        const MapSlabCoord x = bx + rooms[r].col * (ROOM_SIZE + ROOM_GAP);
        const MapSlabCoord y = by + rooms[r].row * (ROOM_SIZE + ROOM_GAP);
        const PlayerNumber owner = rooms[r].player_owned ? PLAYER0 : neutral;
        if (!ftest_util_replace_slabs(x, y, x + ROOM_SIZE - 1, y + ROOM_SIZE - 1, rooms[r].kind, owner))
        {
            FTEST_FAIL_TEST("Failed to build room %zu at slab (%" PRId64 ",%" PRId64 ")", r, (int64_t)x, (int64_t)y);
            return FTRs_Go_To_Next_Action;
        }
        struct Room *room = subtile_room_get(slab_subtile_center(x + 1), slab_subtile_center(y + 1));
        if (room_is_invalid(room) || (room->owner != owner))
        {
            FTEST_FAIL_TEST("Room %zu at slab (%" PRId64 ",%" PRId64 ") wasn't made, or not for player %" PRId64, r, (int64_t)x, (int64_t)y, (int64_t)owner);
            return FTRs_Go_To_Next_Action;
        }
    }
    for (int c = 0; c < NRC_COUNT; c++)
    {
        const struct NeutralRoomsRoomDef *rd = &rooms[cases[c].room];
        // the room's middle slab, the second creature in a room one subtile off
        const MapSlabCoord x = bx + rd->col * (ROOM_SIZE + ROOM_GAP) + 1;
        const MapSlabCoord y = by + rd->row * (ROOM_SIZE + ROOM_GAP) + 1;
        const ThingModel model = (ThingModel)creature_model_id(cases[c].model);
        if (model < 1)
        {
            FTEST_FAIL_TEST("No creature model %s", cases[c].model);
            return FTRs_Go_To_Next_Action;
        }
        const MapSubtlCoord stl_x = slab_subtile_center(x) + (((c == NRC_NeutralTortureOrc) || (c == NRC_FlagTortureOrc)) ? 1 : 0);
        struct Thing *creatng = ftest_util_create_creature(subtile_coord_center(stl_x), subtile_coord_center(slab_subtile_center(y)), neutral, 1, model);
        if (thing_is_invalid(creatng))
        {
            FTEST_FAIL_TEST("Failed to create the %s", cases[c].name);
            return FTRs_Go_To_Next_Action;
        }
        // the first job, as a level's start gives it (init_all_creature_states())
        uint64_t *bugs = &kfx_config_state.conf.rules[neutral].gameplay.classic_bugs_flags;
        const uint64_t bugs_before = *bugs;
        if (cases[c].room_jobs)
            set_flag(*bugs, ClscBug_NeutralRoomJobs);
        init_creature_state(creatng);
        *bugs = bugs_before;
        vars->creature_idx[c] = creatng->index;
    }
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult ftest_creature_neutral_rooms_action002__verify(struct FTestActionArgs* const args)
{
    struct ftest_creature_neutral_rooms__variables* const vars = args->data;
    for (int c = 0; c < NRC_COUNT; c++)
    {
        struct Thing *creatng = thing_get(vars->creature_idx[c]);
        if (thing_is_invalid(creatng) || !thing_is_creature(creatng))
        {
            FTEST_FAIL_TEST("The %s is gone", cases[c].name);
            continue;
        }
        const struct CreatureControl *cctrl = creature_control_get_from_thing(creatng);
        const CrtrStateId state = get_creature_state_besides_interruptions(creatng);
        const TbBool in_custody = creature_is_being_tortured_including_kinky(creatng) || creature_is_kept_in_prison(creatng);
        const TbBool guarding = (state == CrSt_Guarding) || (state == CrSt_AtGuardPostRoom);
        FTESTLOG("%s: owner %" PRId64 ", state %s, work room %" PRId64 ", held %d, guarding %d, health %" PRId64 "/%" PRId64,
            cases[c].name, (int64_t)creatng->owner, creature_state_code_name(state), (int64_t)cctrl->work_room_id,
            (int)in_custody, (int)guarding, (int64_t)creatng->health, (int64_t)cctrl->max_health);
        if (creatng->owner != kfx_config_state.neutral_player_num)
        {
            FTEST_FAIL_TEST("The %s is no longer neutral (player %" PRId64 ")", cases[c].name, (int64_t)creatng->owner);
            continue;
        }
        if (cases[c].works)
        {
            if (!in_custody && !guarding)
                FTEST_FAIL_TEST("The %s isn't held or on guard in its room (state %s)", cases[c].name, creature_state_code_name(state));
        }
        else if (in_custody || guarding || (cctrl->work_room_id != 0))
        {
            FTEST_FAIL_TEST("The %s works in the neutral room (state %s)", cases[c].name, creature_state_code_name(state));
        }
    }
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
