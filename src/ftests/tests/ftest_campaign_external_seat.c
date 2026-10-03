// M10: net_add_external_seat(my_player_number) -- the local player's own campaign/scenario seat handed to an
// External agent (an LLM via the TCP API), not just the built-in AI (spectator_handoff.c's job). Three things
// need proving that no existing test covers, since every prior External-seat test used a rival: (1) the
// packets.c dispatch guard actually discriminates between the seat's own NetUserId and the stale one
// front_input.c still writes for local user 0 every frame (get_net_user_player_number(SOLO_HUMAN_ID) always
// resolves to my_player_number, so both a real click and a submitted verb would otherwise land on the same
// player); (2) a real, multi-turn extseat_submit_verb(ESV_MarkDig) actually mutates the seat's own dungeon end
// to end, not just that its steps get dispatched; and (3) releasing it hands control back to the human, not the
// built-in AI.
//
// (2) uncovered a second real bug, on top of the M10 commit's own get_local_user()/my_local_user_id fix:
// front_input.c's per-frame set_players_packet_control(get_my_player(), PCtr_Gui) (game_is_busy_doing_gui()) and
// the other "for player" writers (set_players_packet_action, unset_players_packet_control,
// get_players_packet_action) all resolved their packet via player->user_id directly, same as get_local_user()
// used to -- so once net_add_external_seat() reassigned the local player's own user_id to the seat, EVERY ONE of
// front_input.c's ~90 write call sites (not just get_local_packet()'s own callers) silently redirected onto the
// seat's packet instead of staying on user 0's. The observable effect: PCtr_Gui ended up set on the seat's
// packet turn after turn, and process_dungeon_control_packet_clicks()'s own early
// `if (flag_is_set(pckt->control_flags,PCtr_Gui)) return false;` silently no-opped the seat's every submitted
// click -- accepted, drained to idle, zero reported error, and zero dig-tagged slabs. Fixed the same way as
// get_local_user(): a get_players_own_packet() helper (packet_data.c) and set_players_packet_action's own
// packet_data.c body now resolve to get_local_packet() whenever the target player is my_player_number, instead
// of trusting player->user_id (which net_add_external_seat() is now allowed to have reassigned).
#include "ftest_campaign_external_seat.h"
#include "game_commands.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_packet_inject.h"
#include "../ftest_util.h"

#include "config_keeperfx.h"
#include "config_players.h"
#include "config_terrain.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "map_data.h"
#include "net_game.h"
#include "player_data.h"
#include "slab_data.h"
#include "tasks_list.h"
#include "thing_list.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_failures = 0;
#define SOFT_FAIL(...) do { s_failures++; FTESTLOG("CHECK FAILED: " __VA_ARGS__); } while (0)
#define CHECK_TRUE(what, cond) do { if (!(cond)) SOFT_FAIL("%s", what); } while (0)

static NetUserId s_seat_user = -1;
static MapSlabCoord s_hsx, s_hsy; // the local player's own dungeon heart, in slab coords

FTestActionResult ces01_claim(struct FTestActionArgs* const args);
FTestActionResult ces02_submit_markdig(struct FTestActionArgs* const args);
FTestActionResult ces03_check_markdig(struct FTestActionArgs* const args);
FTestActionResult ces04_inject_local_toggle(struct FTestActionArgs* const args);
FTestActionResult ces05_check_blocked_inject_seat_toggle(struct FTestActionArgs* const args);
FTestActionResult ces06_check_allowed_and_release(struct FTestActionArgs* const args);
FTestActionResult ces07_inject_local_toggle_again(struct FTestActionArgs* const args);
FTestActionResult ces08_check_restored(struct FTestActionArgs* const args);

void ftest_campaign_external_seat_pre_start() { fe_computer_players = 1; }

TbBool ftest_campaign_external_seat_init()
{
    s_failures = 0;
    ftest_packet_inject_reset();
    ftest_append_action(ces01_claim, 0, NULL);
    ftest_append_action(ces02_submit_markdig, 0, NULL);
    ftest_append_action(ces03_check_markdig, 15, NULL);
    ftest_append_action(ces04_inject_local_toggle, 1, NULL);
    ftest_append_action(ces05_check_blocked_inject_seat_toggle, 3, NULL);
    ftest_append_action(ces06_check_allowed_and_release, 3, NULL);
    ftest_append_action(ces07_inject_local_toggle_again, 1, NULL);
    ftest_append_action(ces08_check_restored, 3, NULL);
    return true;
}

FTestActionResult ces01_claim(struct FTestActionArgs* const args)
{
    struct PlayerInfo* player = get_player(my_player_number);
    CHECK_TRUE("the local player starts as a genuine human (no CompCtrl, no ExternalSeat)",
        !flag_is_set(player->allocflags, PlaF_CompCtrl) && !flag_is_set(player->allocflags, PlaF_ExternalSeat));

    s_seat_user = net_add_external_seat(my_player_number);
    CHECK_TRUE("the local player's own seat can be claimed as External (M10)", s_seat_user >= 0);
    CHECK_TRUE("the seat is flagged External, not CompCtrl", flag_is_set(player->allocflags, PlaF_ExternalSeat) && !flag_is_set(player->allocflags, PlaF_CompCtrl));
    CHECK_TRUE("the player's user_id points at the new seat", player->user_id == s_seat_user);

    struct Thing* heart = find_players_dungeon_heart(my_player_number);
    s_hsx = subtile_slab(heart->mappos.x.stl.num);
    s_hsy = subtile_slab(heart->mappos.y.stl.num);
    return FTRs_Go_To_Next_Action;
}

// The end-to-end proof, not just dispatch: a real ESV_MarkDig for the seat's own user must actually tag the
// target slabs, not merely drain its steps with no reported error -- ftest_ai_multi_seat.c and
// ftest_ai_bridge_reference.c already prove this mechanism for a RIVAL's External seat; this is the same
// verb, same driver, same packets.c dispatch path, for plyr_idx == my_player_number instead.
FTestActionResult ces02_submit_markdig(struct FTestActionArgs* const args)
{
    // A 5x5 earth patch around the target rect, same offsets ftest_ai_bridge_reference.c already uses on this
    // level -- guarantees real diggable terrain regardless of what the level's own layout looks like nearby.
    ftest_util_replace_slabs(s_hsx - 9, s_hsy - 3, s_hsx - 5, s_hsy + 1, SlbT_EARTH, PLAYER_NEUTRAL);
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = ESV_MarkDig;
    v.has_rect = true;
    v.slab_x0 = s_hsx - 8; v.slab_y0 = s_hsy - 2; v.slab_x1 = s_hsx - 6; v.slab_y1 = s_hsy;
    const char* e = extseat_submit_verb(s_seat_user, my_player_number, &v, NULL);
    CHECK_TRUE("mark_dig is accepted for the local player's own seat", e == NULL);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ces03_check_markdig(struct FTestActionArgs* const args)
{
    int64_t n = 0;
    for (int dx = 0; dx < 3; dx++) for (int dy = 0; dy < 3; dy++)
        if (find_from_task_list(my_player_number, get_subtile_number(slab_subtile_center(s_hsx - 8 + dx), slab_subtile_center(s_hsy - 2 + dy))) != -1) n++;
    FTESTLOG("mark_dig tagged %" PRId64 " of 9 slabs in the local player's own dungeon", n);
    CHECK_TRUE("mark_dig actually tags the target rect for the local player's own seat", n == 9);
    return FTRs_Go_To_Next_Action;
}

// The stale packet: front_input.c still writes one for user 0 (SOLO_HUMAN_ID) every frame regardless of seat
// type, and get_net_user_player_number(0) always resolves to my_player_number -- exactly the packet the
// dispatch guard must now ignore for dungeon-control purposes. PckA_ToggleComputer (flips
// dungeon->computer_enabled) stands in for a real gameplay action here: it is dispatched from the same
// PVT_DungeonTop-gated switch (packets.c) as build/dig/spell/slap, but has no drag/multi-turn machinery of its
// own, so it isolates the dispatch guard on its own from the end-to-end mutation ces02/ces03 already covered.
FTestActionResult ces04_inject_local_toggle(struct FTestActionArgs* const args)
{
    struct FtestPacketInject r = {0};
    r.action = PckA_ToggleComputer;
    ftest_packet_inject_queue_local(&r);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ces05_check_blocked_inject_seat_toggle(struct FTestActionArgs* const args)
{
    struct Dungeon* d = get_players_dungeon(get_player(my_player_number));
    CHECK_TRUE("a dungeon-control action from the stale local user (0) does not reach an External seat",
        (d->computer_enabled & 0x01) == 0);

    struct FtestPacketInject r = {0};
    r.action = PckA_ToggleComputer;
    ftest_packet_inject_queue(s_seat_user, &r);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ces06_check_allowed_and_release(struct FTestActionArgs* const args)
{
    struct Dungeon* d = get_players_dungeon(get_player(my_player_number));
    CHECK_TRUE("the same action from the seat's own user does reach it", (d->computer_enabled & 0x01) != 0);
    d->computer_enabled &= ~0x01; // known baseline before the post-release check

    CHECK_TRUE("releasing the seat succeeds", net_release_external_seat(my_player_number));
    struct PlayerInfo* player = get_player(my_player_number);
    CHECK_TRUE("release restores plain human control, not the built-in AI",
        !flag_is_set(player->allocflags, PlaF_ExternalSeat) && !flag_is_set(player->allocflags, PlaF_CompCtrl));
    CHECK_TRUE("release restores user 0 as the player's own user", player->user_id == SOLO_HUMAN_ID);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ces07_inject_local_toggle_again(struct FTestActionArgs* const args)
{
    struct FtestPacketInject r = {0};
    r.action = PckA_ToggleComputer;
    ftest_packet_inject_queue_local(&r);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ces08_check_restored(struct FTestActionArgs* const args)
{
    struct Dungeon* d = get_players_dungeon(get_player(my_player_number));
    CHECK_TRUE("after release, the local user's own dungeon-control packets reach the player again",
        (d->computer_enabled & 0x01) != 0);

    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " campaign external seat check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: the local player's own seat can be claimed as an External seat, a real mark_dig verb "
             "actually mutates its own dungeon end to end, its dispatch guard discriminates the seat's own user "
             "from the stale local one, and release restores human control.");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
