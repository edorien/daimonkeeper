#include "ftest_room_storage_reposition.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "game_legacy.h"
#include "config_keeperfx.h"
#include "config_creature.h"
#include "config_objects.h"
#include "config_terrain.h"
#include "config_trapdoor.h"
#include "dungeon_data.h"
#include "list_walk.h"
#include "player_availability.h"
#include "room_data.h"
#include "room_garden.h"
#include "room_graveyard.h"
#include "room_library.h"
#include "room_util.h"
#include "room_workshop.h"
#include "thing_corpses.h"
#include "thing_objects.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

// Refactor pass 3, S03 folds the garden, graveyard, workshop and library's
// "put the contents back after the room changed shape" functions into one.
// No other ftest sells or rebuilds a storage room with things in it, so this
// one does, logging the rooms and their contents after every step ("RST:"
// lines) for a before/after comparison.

#define RST_SIZE 3 /* rooms are RST_SIZE x RST_SIZE slabs */

struct RstRoomSpot {
    SlabKind slab_kind;
    RoomKind room_kind;
    MapSlabCoord dx; /* offset of the room's first slab from the heart */
    MapSlabCoord dy;
};

static const struct RstRoomSpot rst_spots[] = {
    { SlbT_GARDEN,    RoK_GARDEN,    3, -7 },
    { SlbT_GRAVEYARD, RoK_GRAVEYARD, 3, -3 },
    { SlbT_WORKSHOP,  RoK_WORKSHOP,  3,  1 },
    { SlbT_LIBRARY,   RoK_LIBRARY,  -7, -3 },
    { SlbT_LIBRARY,   RoK_LIBRARY,  -7,  1 },
};
#define RST_SPOTS (sizeof(rst_spots) / sizeof(rst_spots[0]))

struct ftest_room_storage_reposition__variables {
    MapSlabCoord heart_slb_x;
    MapSlabCoord heart_slb_y;
};
struct ftest_room_storage_reposition__variables ftest_room_storage_reposition__vars = { 0, 0 };

static void rst_log_thing(const char *label, const struct Thing *thing)
{
    const struct Room *room = subtile_room_get(thing->mappos.x.stl.num, thing->mappos.y.stl.num);
    JUSTLOG("RST: %s  thing cls=%d mdl=%d own=%d stl=(%d,%d) room=%d", label, (int)thing->class_id, (int)thing->model,
        (int)thing->owner, (int)thing->mappos.x.stl.num, (int)thing->mappos.y.stl.num,
        room_is_invalid(room) ? -1 : (int)room->kind);
}

/** Logs every storage room of player 0 and every object and corpse in the test's area. */
static void rst_log_state(struct ftest_room_storage_reposition__variables *vars, const char *label)
{
    struct Dungeon *dungeon = get_players_num_dungeon(PLAYER0);
    const RoomKind kinds[] = { RoK_GARDEN, RoK_GRAVEYARD, RoK_WORKSHOP, RoK_LIBRARY };
    for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++)
    {
        FOR_EACH_ROOM(room, room_walk_owner(dungeon->room_list_start[kinds[k]]))
        {
            JUSTLOG("RST: %s t=%lld room kind=%d slabs=%lld total=%lld used=%lld storage=%lld centre=(%d,%d)", label,
                (long long)get_gameturn(), (int)room->kind, (long long)room->slabs_count, (long long)room->total_capacity,
                (long long)room->used_capacity, (long long)room->capacity_used_for_storage,
                (int)room->central_stl_x, (int)room->central_stl_y);
        }
    }
    // Everything in the area, whether in a room or dropped outside one
    const MapSubtlCoord x0 = slab_subtile(vars->heart_slb_x - 9, 0);
    const MapSubtlCoord x1 = slab_subtile(vars->heart_slb_x + 7, 2);
    const MapSubtlCoord y0 = slab_subtile(vars->heart_slb_y - 9, 0);
    const MapSubtlCoord y1 = slab_subtile(vars->heart_slb_y + 5, 2);
    for (MapSubtlCoord y = y0; y <= y1; y++)
    {
        for (MapSubtlCoord x = x0; x <= x1; x++)
        {
            FOR_EACH_THING(thing, thing_walk_map_block(get_map_block_at(x, y)))
            {
                if ((thing->class_id == TCls_Object) || (thing->class_id == TCls_DeadCreature))
                    rst_log_thing(label, thing);
            }
        }
    }
}

/**
 * Logs any storage room holding more than its capacity, and fails the test for one: since
 * refactor pass 3 finding F9 was fixed, a library can end up one book over only with the
 * LIBRARY_EXTRA_BOOK classic bug, and the other rooms never could.
 */
static void rst_check_capacity(const char *label)
{
    struct Dungeon *dungeon = get_players_num_dungeon(PLAYER0);
    const RoomKind kinds[] = { RoK_GARDEN, RoK_GRAVEYARD, RoK_WORKSHOP, RoK_LIBRARY };
    for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++)
    {
        FOR_EACH_ROOM(room, room_walk_owner(dungeon->room_list_start[kinds[k]]))
        {
            if (room->used_capacity > room->total_capacity)
            {
                JUSTLOG("RST: %s over capacity: %s holds %lld, capacity %lld", label, room_code_name(room->kind),
                    (long long)room->used_capacity, (long long)room->total_capacity);
                const TbBool extra_book = (room->kind == RoK_LIBRARY) &&
                    flag_is_set(kfx_config_state.conf.rules[room->owner].gameplay.classic_bugs_flags, ClscBug_LibraryExtraBook);
                if (!extra_book)
                    FTEST_FAIL_TEST("%s: the %s holds %lld, over its capacity %lld", label, room_code_name(room->kind),
                        (long long)room->used_capacity, (long long)room->total_capacity);
            }
        }
    }
}

static struct Room *rst_room_at_spot(struct ftest_room_storage_reposition__variables *vars, size_t spot)
{
    return slab_room_get(vars->heart_slb_x + rst_spots[spot].dx + RST_SIZE - 1, vars->heart_slb_y + rst_spots[spot].dy + RST_SIZE - 1);
}

static FTestActionResult rst_setup(struct FTestActionArgs *const args)
{
    struct ftest_room_storage_reposition__variables *const vars = args->data;
    ftest_util_reveal_map(PLAYER0);
    struct Dungeon *dungeon = get_players_num_dungeon(PLAYER0);
    struct Thing *heart = thing_get(dungeon->dnheart_idx);
    if (thing_is_invalid(heart))
    {
        FTEST_FAIL_TEST("Failed to find PLAYER0's dungeon heart");
        return FTRs_Go_To_Next_Action;
    }
    vars->heart_slb_x = subtile_slab(heart->mappos.x.stl.num);
    vars->heart_slb_y = subtile_slab(heart->mappos.y.stl.num);
    if ((vars->heart_slb_x < 10) || (vars->heart_slb_y < 10) ||
        (vars->heart_slb_x + 8 >= kfx_sim_state.map_tiles_x) || (vars->heart_slb_y + 6 >= kfx_sim_state.map_tiles_y))
    {
        FTEST_FAIL_TEST("The heart at slab (%d,%d) is too close to the map's edge", (int)vars->heart_slb_x, (int)vars->heart_slb_y);
        return FTRs_Go_To_Next_Action;
    }
    for (size_t i = 0; i < RST_SPOTS; i++)
    {
        const MapSlabCoord sx = vars->heart_slb_x + rst_spots[i].dx;
        const MapSlabCoord sy = vars->heart_slb_y + rst_spots[i].dy;
        set_room_available(PLAYER0, rst_spots[i].room_kind, 1, 1);
        if (!ftest_util_replace_slabs(sx, sy, sx + RST_SIZE - 1, sy + RST_SIZE - 1, rst_spots[i].slab_kind, PLAYER0))
        {
            FTEST_FAIL_TEST("Failed to build room %d at slab (%d,%d)", (int)rst_spots[i].room_kind, (int)sx, (int)sy);
            return FTRs_Go_To_Next_Action;
        }
    }
    // Fill each room: one item per slab centre and one more beside it, as far as capacity allows
    const ThingModel crate = trap_crate_object_model(1);
    // Late-game powers player 0 doesn't have yet: create_spell_in_library() refuses a book whose power
    // add_power_to_player() won't grant, and one book per power avoids duplicates.
    static const char *const books[] = { "SPELLBOOK_ARMG", "SPELLBOOK_DWAL", "SPELLBOOK_TBMB", "SPELLBOOK_FRZ",
        "SPELLBOOK_SLOW", "SPELLBOOK_FLGT", "SPELLBOOK_VSN", "SPELLBOOK_TUNLR", "SPELLBOOK_RBND", "SPELLBOOK_CHKN",
        "SPELLBOOK_DISEASE", NULL };
    size_t next_book = 0;
    const ThingModel corpse_model = (ThingModel)creature_model_id("ORC");
    for (size_t i = 0; i < RST_SPOTS; i++)
    {
        struct Room *room = rst_room_at_spot(vars, i);
        if (room_is_invalid(room))
        {
            FTEST_FAIL_TEST("Room %d at spot %d wasn't created", (int)rst_spots[i].room_kind, (int)i);
            return FTRs_Go_To_Next_Action;
        }
        const MapSlabCoord sx = vars->heart_slb_x + rst_spots[i].dx;
        const MapSlabCoord sy = vars->heart_slb_y + rst_spots[i].dy;
        for (MapSlabCoord y = sy; y < sy + RST_SIZE; y++)
        {
            for (MapSlabCoord x = sx; x < sx + RST_SIZE; x++)
            {
                for (int n = 0; n < 2; n++)
                {
                    const MapSubtlCoord stl_x = slab_subtile_center(x) + n;
                    const MapSubtlCoord stl_y = slab_subtile_center(y);
                    switch (rst_spots[i].room_kind)
                    {
                    case RoK_GARDEN:
                        room_create_new_food_at(room, stl_x, stl_y);
                        break;
                    case RoK_WORKSHOP:
                        create_crate_in_workshop(room, crate, stl_x, stl_y);
                        break;
                    case RoK_LIBRARY:
                        if ((i == 3) && (next_book < sizeof(books) / sizeof(books[0]) - 1)) // the second library stays empty
                            create_spell_in_library(room, object_model_id(books[next_book++]), stl_x, stl_y);
                        break;
                    case RoK_GRAVEYARD: {
                        struct Coord3d pos;
                        pos.x.val = subtile_coord_center(stl_x);
                        pos.y.val = subtile_coord_center(stl_y);
                        pos.z.val = 0;
                        struct Thing *corpse = create_dead_creature(&pos, corpse_model, DCrSt_Dead, PLAYER0, 0);
                        if (!thing_is_invalid(corpse) && (room->used_capacity < room->total_capacity))
                            add_body_to_graveyard(corpse, room);
                        break;
                    }
                    default:
                        break;
                    }
                }
            }
        }
    }
    count_food_in_room(rst_room_at_spot(vars, 0));
    count_bodies_in_room(rst_room_at_spot(vars, 1));
    count_crates_in_room(rst_room_at_spot(vars, 2));
    count_books_in_room(rst_room_at_spot(vars, 3));
    rst_log_state(vars, "filled");
    rst_check_capacity("filled");
    return FTRs_Go_To_Next_Action;
}

/** Sells the given slab offsets (within each room) of every room. */
static void rst_sell(struct ftest_room_storage_reposition__variables *vars, const MapSlabCoord (*offsets)[2], size_t count)
{
    for (size_t i = 0; i < RST_SPOTS; i++)
    {
        for (size_t n = 0; n < count; n++)
        {
            const MapSlabCoord x = vars->heart_slb_x + rst_spots[i].dx + offsets[n][0];
            const MapSlabCoord y = vars->heart_slb_y + rst_spots[i].dy + offsets[n][1];
            if (!room_is_invalid(slab_room_get(x, y)))
                delete_room_slab(x, y, false);
        }
    }
}

static FTestActionResult rst_sell_corner_and_middle(struct FTestActionArgs *const args)
{
    struct ftest_room_storage_reposition__variables *const vars = args->data;
    rst_log_state(vars, "settled");
    static const MapSlabCoord offsets[][2] = { {0, 0}, {1, 1} };
    rst_sell(vars, offsets, 2);
    rst_log_state(vars, "sold-2");
    rst_check_capacity("sold-2");
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult rst_rebuild(struct FTestActionArgs *const args)
{
    struct ftest_room_storage_reposition__variables *const vars = args->data;
    rst_log_state(vars, "after-sell");
    for (size_t i = 0; i < RST_SPOTS; i++)
    {
        const MapSlabCoord sx = vars->heart_slb_x + rst_spots[i].dx;
        const MapSlabCoord sy = vars->heart_slb_y + rst_spots[i].dy;
        ftest_util_replace_slabs(sx, sy, sx, sy, rst_spots[i].slab_kind, PLAYER0);
        ftest_util_replace_slabs(sx + 1, sy + 1, sx + 1, sy + 1, rst_spots[i].slab_kind, PLAYER0);
    }
    rst_log_state(vars, "rebuilt");
    rst_check_capacity("rebuilt");
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult rst_shrink(struct FTestActionArgs *const args)
{
    struct ftest_room_storage_reposition__variables *const vars = args->data;
    rst_log_state(vars, "after-rebuild");
    // Leave the last row only: every room ends up smaller than its contents
    static const MapSlabCoord offsets[][2] = { {0, 0}, {1, 0}, {2, 0}, {0, 1}, {1, 1}, {2, 1} };
    rst_sell(vars, offsets, 6);
    rst_log_state(vars, "shrunk");
    rst_check_capacity("shrunk");
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult rst_final(struct FTestActionArgs *const args)
{
    struct ftest_room_storage_reposition__variables *const vars = args->data;
    rst_log_state(vars, "final");
    rst_check_capacity("final");
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_room_storage_reposition_init()
{
    struct ftest_room_storage_reposition__variables *vars = &ftest_room_storage_reposition__vars;
    ftest_append_action(rst_setup, 20, vars);
    ftest_append_action(rst_sell_corner_and_middle, 10, vars);
    ftest_append_action(rst_rebuild, 10, vars);
    ftest_append_action(rst_shrink, 10, vars);
    ftest_append_action(rst_final, 10, vars);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
