// Human actions an External seat can now take too: taking dig marks off (unmark_dig), and mark_dig starting on a slab
// already marked (a pickaxe drag untags when its first slab is marked, so the seat starts on another corner); locking
// and unlocking a door; ending Call to Arms and Must Obey early (power_off); slapping its own traps -- a TNT goes off
// where it is, a boulder rolls the way the order says (and is refused without a direction); and chat: a short message
// reaches the log, a second one straight after is TOO_SOON, a game command or a control character is BAD_MESSAGE, and
// another player's message raises the chat decision reason.
#include "ftest_ai_seat_parity_actions.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>
#include <json.h>
#include <json-dom.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "api_seat_decision.h"
#include "api_seat_view.h"
#include "config_keeperfx.h"
#include "config_magic.h"
#include "config_trapdoor.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "kfx_config_state.h"
#include "net_game.h"
#include "player_availability.h"
#include "player_data.h"
#include "power_process.h"
#include "slab_data.h"
#include "tasks_list.h"
#include "thing_doors.h"
#include "thing_list.h"
#include "thing_traps.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_failures = 0;
#define SOFT_FAIL(...) do { s_failures++; FTESTLOG("CHECK FAILED: " __VA_ARGS__); } while (0)
#define CHECK_TRUE(what, cond) do { if (!(cond)) SOFT_FAIL("%s", what); } while (0)

static PlayerNumber P = -1;
static NetUserId U = -1;
static MapSlabCoord hsx, hsy;
static ThingIndex s_boulder = 0, s_tnt = 0;
static MapSubtlCoord s_boulder_x = 0, s_boulder_y = 0;
static int64_t s_slap_turn = 0;

static struct ExtSeatVerb V(enum ExtSeatVerbKind k) { struct ExtSeatVerb v; memset(&v, 0, sizeof(v)); v.kind = k; return v; }
static struct ExtSeatVerb rect(enum ExtSeatVerbKind k, int64_t x0, int64_t y0, int64_t x1, int64_t y1)
{
    struct ExtSeatVerb v = V(k); v.has_rect = true; v.slab_x0 = x0; v.slab_y0 = y0; v.slab_x1 = x1; v.slab_y1 = y1; return v;
}
static struct ExtSeatVerb at_slab(enum ExtSeatVerbKind k, const char* name, MapSlabCoord sx, MapSlabCoord sy)
{
    struct ExtSeatVerb v = V(k); if (name) snprintf(v.name, sizeof(v.name), "%s", name);
    v.has_pos = true; v.stl_x = slab_subtile_center(sx); v.stl_y = slab_subtile_center(sy); return v;
}
static const char* submit(struct ExtSeatVerb v) { return extseat_submit_verb_ex(U, P, &v, true, NULL); }
static void ok(const char* what, struct ExtSeatVerb v) { const char* e = submit(v); if (e) SOFT_FAIL("%s: refused %s", what, e); }
static void expect(const char* what, struct ExtSeatVerb v, const char* code)
{
    const char* e = submit(v);
    if (e == NULL || strcmp(e, code) != 0) SOFT_FAIL("%s: expected %s, got %s", what, code, e ? e : "success");
}
static int marked(MapSlabCoord x0, MapSlabCoord y0, MapSlabCoord x1, MapSlabCoord y1)
{
    int n = 0;
    for (MapSlabCoord y = y0; y <= y1; y++) for (MapSlabCoord x = x0; x <= x1; x++) if (find_from_task_list_by_slab(P, x, y) != -1) n++;
    return n;
}
static TbBool idle_for(const struct FTestActionArgs* args) { return extseat_idle(U) || ((args->intended_start_at_game_turn > 0) && ((int64_t)get_gameturn() > args->intended_start_at_game_turn + 400)); }
static struct Thing* door_at(MapSlabCoord sx, MapSlabCoord sy) { return get_door_for_position(slab_subtile_center(sx), slab_subtile_center(sy)); }
static struct Thing* trap_at(MapSlabCoord sx, MapSlabCoord sy) { return get_trap_for_slab_position(sx, sy); }

FTestActionResult pa01_setup(struct FTestActionArgs* const args);
FTestActionResult pa02_mark_over_marked(struct FTestActionArgs* const args);
FTestActionResult pa03_unmark_and_lock(struct FTestActionArgs* const args);
FTestActionResult pa04_check_then_unlock_and_powers_off(struct FTestActionArgs* const args);
FTestActionResult pa05_check_then_slap(struct FTestActionArgs* const args);
FTestActionResult pa06_check_slaps_then_chat(struct FTestActionArgs* const args);
FTestActionResult pa07_check_chat_reason(struct FTestActionArgs* const args);

void ftest_ai_seat_parity_actions_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_parity_actions_init()
{
    s_failures = 0;
    ftest_append_action(pa01_setup, 0, NULL);
    ftest_append_action(pa02_mark_over_marked, 2, NULL);
    ftest_append_action(pa03_unmark_and_lock, 2, NULL);
    ftest_append_action(pa04_check_then_unlock_and_powers_off, 2, NULL);
    ftest_append_action(pa05_check_then_slap, 2, NULL);
    ftest_append_action(pa06_check_slaps_then_chat, 12, NULL);
    ftest_append_action(pa07_check_chat_reason, 14, NULL);
    return true;
}

FTestActionResult pa01_setup(struct FTestActionArgs* const args)
{
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0 || (U = net_add_external_seat(P)) < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    api_seat_decision_set_min_interval(10);
    const struct Thing* heart = find_players_dungeon_heart(P);
    hsx = subtile_slab(heart->mappos.x.stl.num); hsy = subtile_slab(heart->mappos.y.stl.num);
    // A long claimed hall (rows hsy+3..hsy+5) for the traps, earth below it for digging, a one-slab corridor for a door.
    ftest_util_replace_slabs(hsx - 6, hsy + 3, hsx + 8, hsy + 5, SlbT_CLAIMED, P);
    ftest_util_replace_slabs(hsx - 6, hsy + 6, hsx + 8, hsy + 12, SlbT_EARTH, kfx_config_state.neutral_player_num);
    ftest_util_replace_slabs(hsx + 7, hsy + 6, hsx + 7, hsy + 8, SlbT_CLAIMED, P);
    ftest_util_reveal_map(P);
    struct Dungeon* d = get_players_dungeon(get_player(P));
    d->offmap_money_owned += 100000; d->total_money_owned += 100000;
    set_trap_buildable_and_add_to_amount(P, (ThingModel)trap_model_id("BOULDER"), 1, 1);
    set_trap_buildable_and_add_to_amount(P, (ThingModel)trap_model_id("TNT"), 1, 1);
    set_door_buildable_and_add_to_amount(P, (ThingModel)door_model_id("WOOD"), 1, 1);
    set_power_available(P, PwrK_CALL2ARMS, 1, 1);
    set_power_available(P, PwrK_OBEY, 1, 1);
    // Mark the top-left slab of the dig area on its own first.
    ok("mark one slab", rect(ESV_MarkDig, hsx - 4, hsy + 8, hsx - 4, hsy + 8));
    ok("place the door", at_slab(ESV_PlaceDoor, "WOOD", hsx + 7, hsy + 7));
    ok("place a boulder at the west end of the hall", at_slab(ESV_PlaceTrap, "BOULDER", hsx - 6, hsy + 4));
    ok("place a TNT", at_slab(ESV_PlaceTrap, "TNT", hsx + 4, hsy + 3));
    struct ExtSeatVerb cta = at_slab(ESV_CastPower, "POWER_CALL_TO_ARMS", hsx, hsy + 4);
    ok("cast Call to Arms", cta);
    struct ExtSeatVerb obey = V(ESV_CastPower); snprintf(obey.name, sizeof(obey.name), "POWER_OBEY");
    ok("cast Must Obey", obey);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult pa02_mark_over_marked(struct FTestActionArgs* const args)
{
    if (!idle_for(args)) return FTRs_Repeat_Current_Action;
    CHECK_TRUE("the single slab was marked", marked(hsx - 4, hsy + 8, hsx - 4, hsy + 8) == 1);
    // A rectangle whose top-left slab is already marked: the seat must still mark, not unmark.
    ok("mark a 4x3 rectangle over it", rect(ESV_MarkDig, hsx - 4, hsy + 8, hsx - 1, hsy + 10));
    return FTRs_Go_To_Next_Action;
}

FTestActionResult pa03_unmark_and_lock(struct FTestActionArgs* const args)
{
    if (!idle_for(args)) return FTRs_Repeat_Current_Action;
    CHECK_TRUE("all 12 slabs are marked, the one already marked included", marked(hsx - 4, hsy + 8, hsx - 1, hsy + 10) == 12);
    ok("unmark the right half", rect(ESV_UnmarkDig, hsx - 2, hsy + 8, hsx - 1, hsy + 10));
    expect("unmark where nothing is marked", rect(ESV_UnmarkDig, hsx + 2, hsy + 10, hsx + 3, hsy + 11), "NOTHING_MARKED");
    struct Thing* door = door_at(hsx + 7, hsy + 7);
    CHECK_TRUE("the door is there, unlocked", !thing_is_invalid(door) && !door->door.is_locked);
    struct ExtSeatVerb lock = at_slab(ESV_SetDoorLock, NULL, hsx + 7, hsy + 7); lock.has_enabled = true; lock.enabled = true;
    ok("lock the door", lock);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult pa04_check_then_unlock_and_powers_off(struct FTestActionArgs* const args)
{
    if (!idle_for(args)) return FTRs_Repeat_Current_Action;
    CHECK_TRUE("the right half was unmarked, the left half kept", marked(hsx - 2, hsy + 8, hsx - 1, hsy + 10) == 0 && marked(hsx - 4, hsy + 8, hsx - 3, hsy + 10) == 6);
    struct Thing* door = door_at(hsx + 7, hsy + 7);
    CHECK_TRUE("the door is locked", !thing_is_invalid(door) && door->door.is_locked);
    struct ExtSeatVerb lock = at_slab(ESV_SetDoorLock, NULL, hsx + 7, hsy + 7); lock.has_enabled = true; lock.enabled = true;
    expect("locking a locked door", lock, "ALREADY_SET");
    lock.enabled = false;
    ok("unlock it", lock);
    CHECK_TRUE("Call to Arms and Must Obey are on", player_uses_power_call_to_arms(P) && player_uses_power_obey(P));
    struct ExtSeatVerb off = V(ESV_PowerOff); snprintf(off.name, sizeof(off.name), "POWER_CALL_TO_ARMS");
    ok("end Call to Arms", off);
    snprintf(off.name, sizeof(off.name), "POWER_OBEY");
    ok("end Must Obey", off);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult pa05_check_then_slap(struct FTestActionArgs* const args)
{
    if (!idle_for(args)) return FTRs_Repeat_Current_Action;
    struct Thing* door = door_at(hsx + 7, hsy + 7);
    CHECK_TRUE("the door is unlocked again", !thing_is_invalid(door) && !door->door.is_locked);
    CHECK_TRUE("Call to Arms and Must Obey are off", !player_uses_power_call_to_arms(P) && !player_uses_power_obey(P));
    struct ExtSeatVerb off = V(ESV_PowerOff); snprintf(off.name, sizeof(off.name), "POWER_CALL_TO_ARMS");
    expect("ending a power that is not on", off, "NOT_ACTIVE");
    snprintf(off.name, sizeof(off.name), "POWER_LIGHTNING");
    expect("a power with nothing to end", off, "UNSUPPORTED_POWER");
    struct Thing* boulder = trap_at(hsx - 6, hsy + 4);
    struct Thing* tnt = trap_at(hsx + 4, hsy + 3);
    if (thing_is_invalid(boulder) || thing_is_invalid(tnt)) { SOFT_FAIL("the traps were not placed"); return FTRs_Go_To_Next_Action; }
    s_boulder = boulder->index; s_tnt = tnt->index;
    s_boulder_x = boulder->mappos.x.stl.num; s_boulder_y = boulder->mappos.y.stl.num;
    s_slap_turn = (int64_t)get_gameturn();
    struct ExtSeatVerb slap = V(ESV_Slap); slap.has_thing = true; slap.thing_id = s_boulder;
    expect("slapping a boulder without saying which way", slap, "MISSING_DIRECTION");
    slap.has_direction = true; slap.direction_angle = ANGLE_EAST;
    ok("slap the boulder east", slap);
    struct ExtSeatVerb boom = V(ESV_Slap); boom.has_thing = true; boom.thing_id = s_tnt;
    ok("slap the TNT", boom);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult pa06_check_slaps_then_chat(struct FTestActionArgs* const args)
{
    // Both slaps played out (each waits for the last whip), and the boulder has had time to roll.
    if (!extseat_idle(U) || ((int64_t)get_gameturn() < s_slap_turn + 40)) return FTRs_Repeat_Current_Action;
    // The boulder is a shot now, rolling east along the hall: find the seat's shot and check it is east of the trap.
    TbBool east = false;
    int64_t idx = kfx_sim_state.thing_lists[TngList_Shots].index;
    for (int64_t g = 0; (idx > 0) && (g < 2000); g++) {
        const struct Thing* t = thing_get(idx); idx = t->next_of_class;
        if ((t->owner == P) && (t->mappos.x.stl.num > s_boulder_x + 2) && (llabs((int64_t)t->mappos.y.stl.num - s_boulder_y) <= 2)) east = true;
    }
    FTESTLOG("boulder shot east of its trap: %d", (int)east);
    CHECK_TRUE("the slapped boulder rolls east", east);
    const struct Thing* tnt = thing_get(s_tnt);
    CHECK_TRUE("the slapped TNT went off (the trap is gone)", thing_is_invalid(tnt) || !thing_exists(tnt) || (tnt->class_id != TCls_Trap));
    // Chat.
    struct ExtSeatVerb msg = V(ESV_SendMessage); snprintf(msg.message, sizeof(msg.message), "Hold the east corridor, I will flank.");
    ok("a short message", msg);
    expect("a second message straight after", msg, "TOO_SOON");
    return FTRs_Go_To_Next_Action;
}

FTestActionResult pa07_check_chat_reason(struct FTestActionArgs* const args)
{
    struct ExtSeatChat log[EXTSEAT_CHAT_LOG];
    const int64_t n = extseat_chat_log(log, EXTSEAT_CHAT_LOG);
    TbBool mine = false;
    for (int64_t i = 0; i < n; i++) if ((log[i].player == P) && (strcmp(log[i].text, "Hold the east corridor, I will flank.") == 0)) mine = true;
    CHECK_TRUE("the seat's message reached the chat", mine);
    // The message's content is checked before the rate limit, so these say BAD_MESSAGE even inside the window.
    struct ExtSeatVerb bad = V(ESV_SendMessage); snprintf(bad.message, sizeof(bad.message), "!win");
    const char* e = extseat_check_verb(U, P, &bad, NULL);
    CHECK_TRUE("a game command is refused", e && strcmp(e, "BAD_MESSAGE") == 0);
    snprintf(bad.message, sizeof(bad.message), "hi\x01there");
    e = extseat_check_verb(U, P, &bad, NULL);
    CHECK_TRUE("a control character is refused", e && strcmp(e, "BAD_MESSAGE") == 0);
    // Another player speaks: the seat is told.
    static TbBool said = false;
    if (!said) { extseat_note_chat(PLAYER_GOOD, "You will not pass!"); said = true; return FTRs_Repeat_Current_Action; }
    VALUE v; api_seat_build_view(&v, P);
    VALUE* reasons = value_dict_get(value_dict_get(value_dict_get(&v, "seat"), "decision"), "reasons");
    TbBool chat = false;
    for (size_t i = 0; reasons && i < value_array_size(reasons); i++) if (strcmp(value_string(value_array_get(reasons, i)), "chat") == 0) chat = true;
    value_fini(&v);
    if (!chat && (args->intended_start_at_game_turn > 0) && ((int64_t)get_gameturn() < args->intended_start_at_game_turn + 60)) return FTRs_Repeat_Current_Action;
    CHECK_TRUE("another player's message raises the chat decision reason", chat);
    api_seat_decision_set_min_interval(100);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " parity check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: unmark, safe re-mark, door locks, powers off, trap slaps and chat all work as a human's would");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
