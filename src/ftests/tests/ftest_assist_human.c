// docs/newFeatures/llm/03 P5: the combat assistant for a human player. The player's profile (config_combat_assist.h) is
// sent as the player's own packets (kfx_game/keeper_assist_outbox.c) the way a level start sends it, goes live, and heals
// a wounded creature of theirs; then, with the Build Only computer assistant switched on as well (they are separate
// options), it keeps working. The profile is set in memory here, not written: the run's save folder is not the test's.
#include "ftest_assist_human.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "config_combat_assist.h"
#include "config_creature.h"
#include "config_keeperfx.h"
#include "config_magic.h"
#include "creature_control.h"
#include "dungeon_data.h"
#include "frontend.h"
#include "game_legacy.h"
#include "keeper_assist_outbox.h"
#include "keeper_assist_state.h"
#include "kfx_sim_state.h"
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

static PlayerNumber P = 0;
static ThingIndex wounded = 0;
static int64_t s_phase_start = 0, s_sent_turn = 0;
static uint64_t s_log_mark = 0;
static struct KeeperAssistProfile s_saved;

static int64_t now(void) { return (int64_t)get_gameturn(); }
static struct KeeperAssistState* st(void) { return &kfx_sim_state.keeper_assist[P]; }
static void wound(void)
{
    struct Thing* t = thing_get(wounded);
    if (!thing_is_invalid(t)) t->health = (HitPoints)(creature_control_get_from_thing(t)->max_health * 20 / 100);
}

FTestActionResult ah01_setup(struct FTestActionArgs* const args);
FTestActionResult ah02_wait_live_and_heal(struct FTestActionArgs* const args);
FTestActionResult ah03_with_autopilot(struct FTestActionArgs* const args);
FTestActionResult ah04_check_heal_with_autopilot(struct FTestActionArgs* const args);

TbBool ftest_assist_human_init()
{
    s_failures = 0;
    ftest_append_action(ah01_setup, 0, NULL);
    ftest_append_action(ah02_wait_live_and_heal, 1, NULL);
    ftest_append_action(ah03_with_autopilot, 1, NULL);
    ftest_append_action(ah04_check_heal_with_autopilot, 1, NULL);
    return true;
}

FTestActionResult ah01_setup(struct FTestActionArgs* const args)
{
    P = my_player_number;
    struct Dungeon* d = get_players_dungeon(get_player(P));
    const struct Thing* heart = find_players_dungeon_heart(P);
    if (dungeon_invalid(d) || thing_is_invalid(heart)) { FTEST_FAIL_TEST("the player has no dungeon"); return FTRs_Go_To_Next_Action; }
    d->total_money_owned = 100000;
    d->magic_level[PwrK_HEALCRTR] = 8;
    struct Thing* c = ftest_util_create_creature(heart->mappos.x.val + 3 * COORD_PER_STL, heart->mappos.y.val + 3 * COORD_PER_STL, P, 9,
                                                (ThingModel)creature_model_id("ORC"));
    if (thing_is_invalid(c)) { FTEST_FAIL_TEST("no creature"); return FTRs_Go_To_Next_Action; }
    wounded = c->index;
    wound();
    // The player's profile: heal any of their creatures under half health.
    struct KeeperAssistProfile* p = keeper_assist_profile();
    s_saved = *p;
    keeper_assist_profile_defaults(p);
    p->globals.enabled = 1;
    p->globals.charge_time = 0;
    keeper_assist_default_rule(&p->custom[0], KAA_Cast, PwrK_HEALCRTR, KAT_AllyAny);
    p->custom[0].cond[0].fact = KAF_HealthPct; p->custom[0].cond[0].op = KAO_Lt; p->custom[0].cond[0].value = 50;
    p->custom[0].cond_count = 1;
    p->custom[0].target_cooldown = 0;
    p->custom_count = 1;
    keeper_assist_outbox_reset(); // what a level start does
    s_phase_start = now();
    s_sent_turn = 0;
    s_log_mark = st()->log_total;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ah02_wait_live_and_heal(struct FTestActionArgs* const args)
{
    if ((s_sent_turn == 0) && st()->live[0].used) s_sent_turn = now();
    if (st()->log_total == s_log_mark) {
        if (now() - s_phase_start > 100) { SOFT_FAIL("the player's rule never healed (live %d, pending packets %d)", (int)st()->live[0].used, (int)keeper_assist_outbox_pending()); return FTRs_Go_To_Next_Action; }
        return FTRs_Repeat_Current_Action;
    }
    FTESTLOG("profile live at turn %" PRId64 " (%" PRId64 " after the level start), first heal at turn %" PRId64,
             s_sent_turn, s_sent_turn - s_phase_start, (int64_t)st()->log[s_log_mark % KEEPER_ASSIST_LOG].turn);
    CHECK_TRUE("the profile became the player's live rules", st()->globals.enabled && st()->live[0].used && st()->live[0].power == PwrK_HEALCRTR);
    CHECK_TRUE("it healed the player's wounded creature", st()->log[s_log_mark % KEEPER_ASSIST_LOG].target == wounded);
    CHECK_TRUE("nothing is left to send", keeper_assist_outbox_pending() == 0);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ah03_with_autopilot(struct FTestActionArgs* const args)
{
    // The Build Only computer assistant as well: a separate option, both may run.
    set_autopilot_type(P, 3);
    struct Dungeon* d = get_players_dungeon(get_player(P));
    if ((d->computer_enabled & 0x01) == 0) toggle_computer_player(P);
    CHECK_TRUE("the computer assistant is on", (d->computer_enabled & 0x01) != 0);
    wound();
    s_log_mark = st()->log_total;
    s_phase_start = now();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ah04_check_heal_with_autopilot(struct FTestActionArgs* const args)
{
    if (st()->log_total == s_log_mark) {
        if (now() - s_phase_start > 40) SOFT_FAIL("no heal with the computer assistant on");
        else return FTRs_Repeat_Current_Action;
    } else {
        CHECK_TRUE("healed again, with the computer assistant on", st()->log[s_log_mark % KEEPER_ASSIST_LOG].target == wounded);
    }
    struct Dungeon* d = get_players_dungeon(get_player(P));
    if ((d->computer_enabled & 0x01) != 0) toggle_computer_player(P);
    *keeper_assist_profile() = s_saved;
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " human combat assistant check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: the player's profile sent as their packets, live, healing; working alongside the computer assistant");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
