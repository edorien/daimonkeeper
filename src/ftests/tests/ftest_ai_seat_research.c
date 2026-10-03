// own.research: what's currently being researched and how far off (dungeon->research[]/current_research_idx/
// research_progress, all already-tracked engine state -- research_needed() is the same test the engine's own research
// job uses to decide what a research_room creature works on next), and the rest of the queue.
#include "ftest_ai_seat_research.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>
#include <stdlib.h>
#include <json.h>
#include <json-dom.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "api_seat_view.h"
#include "config_keeperfx.h"
#include "config_magic.h"
#include "config_terrain.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "net_game.h"
#include "player_data.h"
#include "room_data.h"
#include "room_library.h"
#include "thing_list.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_failures = 0;
#define SOFT_FAIL(...) do { s_failures++; FTESTLOG("CHECK FAILED: " __VA_ARGS__); } while (0)
#define CHECK_TRUE(what, cond) do { if (!(cond)) SOFT_FAIL("%s", what); } while (0)

static PlayerNumber P = -1;

static TbBool array_has_str(VALUE* arr, const char* s)
{
    for (size_t i = 0; arr && i < value_array_size(arr); i++) if (strcmp(value_string(value_array_get(arr, i)), s) == 0) return true;
    return false;
}

FTestActionResult rs01_setup(struct FTestActionArgs* const args);
FTestActionResult rs02_check(struct FTestActionArgs* const args);

void ftest_ai_seat_research_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_research_init()
{
    s_failures = 0;
    ftest_append_action(rs01_setup, 0, NULL);
    ftest_append_action(rs02_check, 1, NULL);
    return true;
}

FTestActionResult rs01_setup(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0 || net_add_external_seat(P) < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    struct Dungeon* d = get_players_dungeon(get_player(P));
    const RoomKind research_room = (RoomKind)get_rid(room_desc, "RESEARCH");
    const PowerKind speed = (PowerKind)power_model_id("POWER_SPEED");

    d->room_resrchable[research_room] = 1;
    d->room_buildable[research_room] &= ~1; // not yet unlocked, so it is still needed
    d->magic_resrchable[speed] = 1;
    d->magic_level[speed] = 0; // not yet unlocked

    d->research[0].rtyp = RsCat_Room; d->research[0].rkind = research_room; d->research[0].req_amount = 1000;
    d->research[1].rtyp = RsCat_Power; d->research[1].rkind = speed; d->research[1].req_amount = 500;
    d->research_num = 2;
    d->current_research_idx = 0;
    d->research_progress = 400LL << 8; // 40% of the current (research_room) item

    CHECK_TRUE("the test set up a genuinely needed room item", research_needed(&d->research[0], d));
    CHECK_TRUE("and a genuinely needed power item", research_needed(&d->research[1], d));
    return FTRs_Go_To_Next_Action;
}

FTestActionResult rs02_check(struct FTestActionArgs* const args)
{
    VALUE v; api_seat_build_view(&v, P);
    VALUE* rs = value_dict_get(value_dict_get(&v, "own"), "research");
    VALUE* cur = value_dict_get(rs, "current");
    CHECK_TRUE("the current item is the room being researched", cur && strcmp(value_string(value_dict_get(cur, "category")), "room") == 0
        && strcmp(value_string(value_dict_get(cur, "name")), "RESEARCH") == 0);
    CHECK_TRUE("its progress is reported as a percentage (40%)", cur && value_int64(value_dict_get(cur, "progress_pct")) == 40);
    VALUE* q = value_dict_get(rs, "queue");
    CHECK_TRUE("the queue lists the current room item", array_has_str(q, "RESEARCH"));
    CHECK_TRUE("and the power item still waiting behind it", array_has_str(q, "POWER_SPEED"));
    value_fini(&v);

    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " research check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: own.research reports what is currently being researched, its progress, and the rest of the queue");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
