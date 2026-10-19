// A library that lifts its books out and lays them down again (after an expansion, or to move them to another
// library) must not switch off a Must Obey the player has cast, since the player ends up with the same book.
// The dropped control has nowhere to put the book back: the player really loses the power and it must go off.
#include "ftest_bug_library_reposition_toggle.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "config_keeperfx.h"
#include "dungeon_data.h"
#include "game_legacy.h"
#include "list_walk.h"
#include "magic_powers.h"
#include "player_availability.h"
#include "room_data.h"
#include "room_library.h"
#include "room_list.h"
#include "thing_objects.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

// A rock corner of the level, left alone by the other tests.
#define LIB_X 1
#define LIB_Y 80
#define LIB_W 2
#define LIB_H 3

enum LibraryScenario {
    LibExpand,
    LibMoved,
    LibDropped,
};

static enum LibraryScenario scenario = LibExpand;

static int64_t count_obey_books(void)
{
    int64_t count = 0;
    for (ThingIndex i = 1; i < THINGS_COUNT; i++)
    {
        const struct Thing* thing = thing_get(i);
        if (thing_exists(thing) && thing_is_spellbook(thing) && (thing->owner == PLAYER0) && (book_thing_to_power_kind(thing) == PwrK_OBEY))
            count++;
    }
    return count;
}

static FTestActionResult action001__setup(struct FTestActionArgs* const args)
{
    struct Dungeon* dungeon = get_players_num_dungeon(PLAYER0);
    if (dungeon->magic_level[PwrK_OBEY] != 0)
    {
        FTEST_FAIL_TEST("Player 0 already has Must Obey");
        return FTRs_Go_To_Next_Action;
    }
    set_room_available(PLAYER0, RoK_LIBRARY, 1, 1);
    if (!ftest_util_replace_slabs(LIB_X, LIB_Y, LIB_X + LIB_W - 1, LIB_Y + LIB_H - 1, SlbT_LIBRARY, PLAYER0))
    {
        FTEST_FAIL_TEST("Failed to build the library");
        return FTRs_Go_To_Next_Action;
    }
    struct Room* room = slab_room_get(LIB_X, LIB_Y);
    if (room_is_invalid(room) || (room->kind != RoK_LIBRARY))
    {
        FTEST_FAIL_TEST("The library wasn't created");
        return FTRs_Go_To_Next_Action;
    }
    struct Thing* book = create_spell_in_library(room, object_model_id("SPELLBOOK_OBEY"),
        slab_subtile_center(LIB_X + LIB_W - 1), slab_subtile_center(LIB_Y + 1));
    if (thing_is_invalid(book))
    {
        FTEST_FAIL_TEST("Failed to place the Must Obey book");
        return FTRs_Go_To_Next_Action;
    }
    dungeon->offmap_money_owned = 1000000;
    dungeon->total_money_owned = 1000000;
    dungeon->must_obey_turn = get_gameturn();
    if ((dungeon->magic_level[PwrK_OBEY] != 1) || !player_uses_power_obey(PLAYER0))
    {
        FTEST_FAIL_TEST("Must Obey should be owned and active, level %" PRId64, (int64_t)dungeon->magic_level[PwrK_OBEY]);
    }
    return FTRs_Go_To_Next_Action;
}

/** Fills every other library of the player (so the book has nowhere to go), and returns how many there are. */
static int64_t fill_other_libraries(const struct Room* library, TbBool fill)
{
    struct Dungeon* dungeon = get_players_num_dungeon(PLAYER0);
    int64_t others = 0;
    FOR_EACH_ROOM(room, room_walk_owner(dungeon->room_list_start[RoK_LIBRARY]))
    {
        if (room == library)
            continue;
        others++;
        if (fill)
        {
            room->used_capacity = room->total_capacity;
            room->capacity_used_for_storage = room->total_capacity;
        }
    }
    return others;
}

static FTestActionResult action002__reposition(struct FTestActionArgs* const args)
{
    struct Dungeon* dungeon = get_players_num_dungeon(PLAYER0);
    struct Room* room = slab_room_get(LIB_X, LIB_Y);
    if (room_is_invalid(room))
    {
        FTEST_FAIL_TEST("The library is gone");
        return FTRs_Go_To_Next_Action;
    }
    if (!player_uses_power_obey(PLAYER0) || (count_obey_books() != 1))
    {
        FTEST_FAIL_TEST("Not set up: Must Obey active %d, books %" PRId64, (int)player_uses_power_obey(PLAYER0), count_obey_books());
        return FTRs_Go_To_Next_Action;
    }
    switch (scenario)
    {
    case LibExpand:
        ftest_util_replace_slabs(LIB_X + LIB_W, LIB_Y, LIB_X + LIB_W, LIB_Y + LIB_H - 1, SlbT_LIBRARY, PLAYER0);
        break;
    case LibMoved:
    case LibDropped:
        if ((fill_other_libraries(room, scenario == LibDropped) == 0) && (scenario == LibMoved))
        {
            FTEST_FAIL_TEST("The player has no other library to move the book to");
            return FTRs_Go_To_Next_Action;
        }
        room->total_capacity = 0;
        count_books_in_room(room);
        break;
    }
    const TbBool lost = (scenario == LibDropped);
    const int64_t books = count_obey_books();
    FTESTLOG("scenario %d: books %" PRId64 ", level %" PRId64 ", Must Obey active %d", (int)scenario, books,
        (int64_t)dungeon->magic_level[PwrK_OBEY], (int)player_uses_power_obey(PLAYER0));
    if (books != (lost ? 0 : 1))
        FTEST_FAIL_TEST("Expected %d Must Obey book(s), found %" PRId64, lost ? 0 : 1, books);
    if (dungeon->magic_level[PwrK_OBEY] != (lost ? 0 : 1))
        FTEST_FAIL_TEST("Expected Must Obey level %d, found %" PRId64, lost ? 0 : 1, (int64_t)dungeon->magic_level[PwrK_OBEY]);
    if (player_uses_power_obey(PLAYER0) == lost)
        FTEST_FAIL_TEST("Must Obey should be %s after the book was %s", lost ? "off" : "still on", lost ? "lost" : "laid down again");
    return FTRs_Go_To_Next_Action;
}

static TbBool init_scenario(enum LibraryScenario which)
{
    scenario = which;
    ftest_append_action(action001__setup, 20, NULL);
    ftest_append_action(action002__reposition, 10, NULL);
    return true;
}

TbBool ftest_bug_library_reposition_toggle__expand_init()  { return init_scenario(LibExpand); }
TbBool ftest_bug_library_reposition_toggle__moved_init()   { return init_scenario(LibMoved); }
TbBool ftest_bug_library_reposition_toggle__dropped_init() { return init_scenario(LibDropped); }

#ifdef __cplusplus
}
#endif

#endif
