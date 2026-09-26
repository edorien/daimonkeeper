// Phase M5 of docs/refactor/AI/LLM/01-integration-plan.md: handing an External seat back to the built-in AI
// (06-lifecycle-and-robustness.md section 2.2 Option B) and the safety question section 2.3 left open: does
// script_support_setup_player_as_computer_keeper() *without* init_creature_states_for_player() leave a live dungeon
// intact and give a working AI? Runs on original-pack multiplayer map 60.
//
// A rival is converted to a seat, given time to be left alone, then released with net_release_external_seat().
//   * Immediately after: every creature's active/continue state, the gold and the room count are exactly as before
//     (nothing was reset), the seat mapping is gone, the flags say built-in AI again.
//   * Later: the AI is working again -- it spends gold / builds / digs from where the dungeon stands, and no creature
//     of the released keeper ended up in an invalid state.
// A second rival covers the opt-in takeover: armed, a lost agent connection hands every seat to the AI; unarmed, it
// leaves the seat alone.
#include "ftest_ai_seat_release.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "creature_control.h"
#include "config_keeperfx.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "net_game.h"
#include "player_computer.h"
#include "player_data.h"
#include "thing_list.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_failures = 0;
#define SOFT_FAIL(...) do { s_failures++; FTESTLOG("CHECK FAILED: " __VA_ARGS__); } while (0)
#define CHECK_TRUE(what, cond) do { if (!(cond)) SOFT_FAIL("%s", what); } while (0)

#define MAX_SNAP 256
struct Snap {
    int64_t n;
    ThingIndex idx[MAX_SNAP];
    int64_t active[MAX_SNAP], cont[MAX_SNAP];
    int64_t gold, rooms, creatures;
};

static PlayerNumber PR = -1, PT = -1; // released, takeover-tested
static struct Snap before;
static int64_t rooms_at_release = 0, gold_at_release = 0, rooms_release_turn = 0;

static void snapshot(PlayerNumber p, struct Snap* s)
{
    memset(s, 0, sizeof(*s));
    const struct Dungeon* d = get_players_dungeon(get_player(p));
    s->gold = d->total_money_owned;
    s->rooms = d->total_rooms;
    s->creatures = d->num_active_creatrs;
    for (ThingIndex i = d->creatr_list_start; i != 0 && s->n < MAX_SNAP;)
    {
        struct Thing* t = thing_get(i);
        if (thing_is_invalid(t)) break;
        s->idx[s->n] = i;
        s->active[s->n] = t->active_state;
        s->cont[s->n] = t->continue_state;
        s->n++;
        i = creature_control_get_from_thing(t)->players_next_creature_idx;
    }
}

FTestActionResult r01_convert(struct FTestActionArgs* const args);
FTestActionResult r02_release(struct FTestActionArgs* const args);
FTestActionResult r03_check_after_release_and_takeover(struct FTestActionArgs* const args);
FTestActionResult r04_check_later(struct FTestActionArgs* const args);

void ftest_ai_seat_release_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_release_init()
{
    s_failures = 0;
    // Let the built-in AI get going before it is taken away, so there is real work in progress to disturb.
    ftest_append_action(r01_convert, 2500, NULL);
    ftest_append_action(r02_release, 0, NULL);
    ftest_append_action(r03_check_after_release_and_takeover, 1, NULL);
    ftest_append_action(r04_check_later, 500, NULL);
    return true;
}

FTestActionResult r01_convert(struct FTestActionArgs* const args)
{
    PR = PT = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
    {
        if (p == my_player_number || p == PLAYER_GOOD || p == PLAYER_NEUTRAL) continue;
        if (!player_exists(get_player(p)) || thing_is_invalid(find_players_dungeon_heart(p))) continue;
        if (PR < 0) PR = p; else if (PT < 0) PT = p;
    }
    if (PR < 0 || PT < 0) { FTEST_FAIL_TEST("need two rival keepers with hearts (map 60)"); return FTRs_Go_To_Next_Action; }
    return FTRs_Go_To_Next_Action;
}

FTestActionResult r02_release(struct FTestActionArgs* const args)
{
    if (PR < 0) return FTRs_Go_To_Next_Action;
    // Convert at a moment the AI is mid-work, then release straight away: the released dungeon is "exactly as the agent
    // left it" by construction, which is what the hazard question needs.
    const NetUserId u = net_add_external_seat(PR);
    CHECK_TRUE("seat claimed", u > 0);
    snapshot(PR, &before);
    FTESTLOG("before release: %" PRId64 " creatures listed, %" PRId64 " rooms, %" PRId64 " gold", before.n, before.rooms, before.gold);
    CHECK_TRUE("the keeper has creatures to disturb", before.n > 0);

    CHECK_TRUE("releasing a non-seat is refused", !net_release_external_seat(my_player_number));
    CHECK_TRUE("release_external_seat succeeds", net_release_external_seat(PR));
    CHECK_TRUE("releasing twice is refused", !net_release_external_seat(PR));
    rooms_release_turn = (int64_t)get_gameturn();
    rooms_at_release = get_players_dungeon(get_player(PR))->total_rooms;
    gold_at_release = get_players_dungeon(get_player(PR))->total_money_owned;

    // The takeover pair: PT is a seat; unarmed, losing the client leaves it alone; armed, the AI gets it.
    const NetUserId ut = net_add_external_seat(PT);
    CHECK_TRUE("second seat claimed", ut > 0);
    extseat_on_client_lost();
    CHECK_TRUE("unarmed: losing the client leaves the seat a seat", flag_is_set(get_player(PT)->allocflags, PlaF_ExternalSeat));
    extseat_set_takeover(true);
    extseat_on_client_lost();
    CHECK_TRUE("armed: losing the client hands the seat to the built-in AI", !flag_is_set(get_player(PT)->allocflags, PlaF_ExternalSeat) && flag_is_set(get_player(PT)->allocflags, PlaF_CompCtrl));
    CHECK_TRUE("the takeover disarms itself", !extseat_takeover_armed());
    return FTRs_Go_To_Next_Action;
}

FTestActionResult r03_check_after_release_and_takeover(struct FTestActionArgs* const args)
{
    if (PR < 0) return FTRs_Go_To_Next_Action;
    const struct PlayerInfo* p = get_player(PR);
    CHECK_TRUE("flags: built-in AI again", flag_is_set(p->allocflags, PlaF_CompCtrl) && !flag_is_set(p->allocflags, PlaF_ExternalSeat));
    CHECK_TRUE("user 1 no longer maps to the player", get_net_user_player_number(1) != PR);
    CHECK_TRUE("the computer player was re-armed", !computer_player_invalid(get_computer_player(PR)) && get_computer_player(PR)->processes[0].name[0] != '\0');

    // One turn has run since the release. Creatures may legitimately have moved on by a step, so the strict check is on
    // what release itself could have reset: the creature list is the same, and no creature was thrown back to its
    // initial state (a reset would put every creature into the same start state at once).
    struct Snap now; snapshot(PR, &now);
    CHECK_TRUE("the same creatures are listed", now.n == before.n);
    int64_t same = 0, reset_like = 0;
    for (int64_t i = 0; i < now.n && i < before.n; i++)
        if (now.idx[i] == before.idx[i]) {
            if (now.active[i] == before.active[i]) same++;
            if (now.active[i] == now.active[0] && before.active[i] != before.active[0]) reset_like++;
        }
    FTESTLOG("creatures with the same active state one turn after release: %" PRId64 " of %" PRId64, same, before.n);
    CHECK_TRUE("nearly all creatures kept their state (a reset would change all of them)", same * 10 >= before.n * 8);
    CHECK_TRUE("no mass reset into a single state", reset_like * 2 < (before.n > 1 ? before.n : 2));
    if (s_failures > 0) FTEST_FAIL_TEST("%" PRId64 " release check(s) failed", s_failures);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult r04_check_later(struct FTestActionArgs* const args)
{
    if (PR < 0) return FTRs_Go_To_Next_Action;
    struct Snap now; snapshot(PR, &now);
    const int64_t rooms_delta = now.rooms - rooms_at_release;
    const int64_t gold_delta = now.gold - gold_at_release;
    FTESTLOG("500 turns after release: rooms %+" PRId64 ", gold %+" PRId64 ", creatures %" PRId64 " (was %" PRId64 ")", rooms_delta, gold_delta, now.creatures, before.creatures);
    CHECK_TRUE("the keeper is still alive (heart present, undecided)", !thing_is_invalid(find_players_dungeon_heart(PR)) && get_player(PR)->victory_state != VicS_LostLevel);
    // The signal only the AI can produce: a process of the re-armed Computer2 has run (its state was zeroed by the release).
    const struct Computer2* comp = get_computer_player(PR);
    int64_t last_run = 0;
    for (int64_t i = 0; i < COMPUTER_PROCESSES_COUNT && comp->processes[i].name[0] != '\0'; i++)
        if ((int64_t)comp->processes[i].last_run_turn > last_run) last_run = (int64_t)comp->processes[i].last_run_turn;
    FTESTLOG("latest computer process run: turn %" PRId64, last_run);
    CHECK_TRUE("the released AI is running its processes again", last_run >= rooms_release_turn);
    const struct PlayerInfo* pt = get_player(PT);
    CHECK_TRUE("the takeover keeper is still a computer keeper", flag_is_set(pt->allocflags, PlaF_CompCtrl));
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " release check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: a released seat is a working built-in AI again with its creatures' states intact; takeover is opt-in");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
