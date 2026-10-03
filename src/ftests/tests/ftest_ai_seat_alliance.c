// set_alliance: a one-way declaration (exactly what the human alliance button does -- toggle_ally_with_player only ever
// touches the caller's own allied_players bit), the engine's own mutual-alliance rule (players_are_mutual_allies), and
// own.alliance in the view (declared vs mutual). Two External seats declare, one drops it, both directions checked.
#include "ftest_ai_seat_alliance.h"

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
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "net_game.h"
#include "player_data.h"
#include "thing_list.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_failures = 0;
#define SOFT_FAIL(...) do { s_failures++; FTESTLOG("CHECK FAILED: " __VA_ARGS__); } while (0)
#define CHECK_TRUE(what, cond) do { if (!(cond)) SOFT_FAIL("%s", what); } while (0)

static PlayerNumber A = -1, B = -1;
static NetUserId UA = -1, UB = -1;

static struct ExtSeatVerb ally(int64_t target, int en)
{
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = ESV_SetAlliance; v.has_target_player = true; v.target_player = target;
    if (en >= 0) { v.has_enabled = true; v.enabled = en != 0; }
    return v;
}
static TbBool array_has_int(VALUE* arr, int64_t x)
{
    for (size_t i = 0; arr && i < value_array_size(arr); i++) if (value_int64(value_array_get(arr, i)) == x) return true;
    return false;
}

FTestActionResult al01_setup(struct FTestActionArgs* const args);
FTestActionResult al02_check_declared_not_mutual(struct FTestActionArgs* const args);
FTestActionResult al03_check_mutual_then_withdraw(struct FTestActionArgs* const args);
FTestActionResult al04_check_withdrawn(struct FTestActionArgs* const args);

void ftest_ai_seat_alliance_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_alliance_init()
{
    s_failures = 0;
    ftest_append_action(al01_setup, 0, NULL);
    ftest_append_action(al02_check_declared_not_mutual, 2, NULL);
    ftest_append_action(al03_check_mutual_then_withdraw, 2, NULL);
    ftest_append_action(al04_check_withdrawn, 2, NULL);
    return true;
}

FTestActionResult al01_setup(struct FTestActionArgs* const args)
{
    A = B = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++) {
        if (p == my_player_number || p == PLAYER_GOOD || p == PLAYER_NEUTRAL) continue;
        if (!player_exists(get_player(p)) || thing_is_invalid(find_players_dungeon_heart(p))) continue;
        if (A < 0) A = p; else if (B < 0) B = p;
    }
    if (A < 0 || B < 0 || (UA = net_add_external_seat(A)) < 1 || (UB = net_add_external_seat(B)) < 1) {
        FTEST_FAIL_TEST("need two rival keepers with hearts"); return FTRs_Go_To_Next_Action;
    }
    // Validation, up front, before anything is declared.
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v)); v.kind = ESV_SetAlliance; v.has_enabled = true; v.enabled = true;
    { const char* e = extseat_submit_verb(UA, A, &v, NULL); CHECK_TRUE("set_alliance needs a target", e && strcmp(e, "MISSING_TARGET_PLAYER") == 0); }
    v = ally(999, 1);
    { const char* e = extseat_submit_verb(UA, A, &v, NULL); CHECK_TRUE("an out-of-range target is refused", e && strcmp(e, "INVALID_PLAYER") == 0); }
    v = ally(A, 1);
    { const char* e = extseat_submit_verb(UA, A, &v, NULL); CHECK_TRUE("allying with yourself is refused", e && strcmp(e, "INVALID_PLAYER") == 0); }
    v.target_player = B; v.has_enabled = false;
    { const char* e = extseat_submit_verb(UA, A, &v, NULL); CHECK_TRUE("set_alliance needs enabled", e && strcmp(e, "MISSING_ENABLED") == 0); }
    v = ally(B, 0);
    { const char* e = extseat_submit_verb(UA, A, &v, NULL); CHECK_TRUE("withdrawing an alliance that was never declared is ALREADY_SET", e && strcmp(e, "ALREADY_SET") == 0); }
    set_player_ally_locked(A, B, true);
    v = ally(B, 1);
    { const char* e = extseat_submit_verb(UA, A, &v, NULL); CHECK_TRUE("a locked alliance is refused", e && strcmp(e, "ALLIANCE_LOCKED") == 0); }
    set_player_ally_locked(A, B, false);
    CHECK_TRUE("refused orders left nothing queued", extseat_idle(UA));

    // A declares an alliance with B; B has not reciprocated.
    v = ally(B, 1);
    { const char* e = extseat_submit_verb(UA, A, &v, NULL); CHECK_TRUE("A declares an alliance with B", e == NULL); }
    return FTRs_Go_To_Next_Action;
}

FTestActionResult al02_check_declared_not_mutual(struct FTestActionArgs* const args)
{
    VALUE v; api_seat_build_view(&v, A);
    VALUE* al = value_dict_get(value_dict_get(&v, "own"), "alliance");
    CHECK_TRUE("A's declared list has B", array_has_int(value_dict_get(al, "declared"), B));
    CHECK_TRUE("but it is not yet mutual", !array_has_int(value_dict_get(al, "mutual"), B));
    value_fini(&v);
    api_seat_build_view(&v, B);
    VALUE* alb = value_dict_get(value_dict_get(&v, "own"), "alliance");
    CHECK_TRUE("B's own declared list does not have A (one-way so far)", !array_has_int(value_dict_get(alb, "declared"), A));
    value_fini(&v);

    // B reciprocates.
    struct ExtSeatVerb v2 = ally(A, 1);
    const char* e = extseat_submit_verb(UB, B, &v2, NULL);
    CHECK_TRUE("B declares back", e == NULL);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult al03_check_mutual_then_withdraw(struct FTestActionArgs* const args)
{
    VALUE v; api_seat_build_view(&v, A);
    VALUE* al = value_dict_get(value_dict_get(&v, "own"), "alliance");
    CHECK_TRUE("now it is mutual for A", array_has_int(value_dict_get(al, "mutual"), B));
    value_fini(&v);
    api_seat_build_view(&v, B);
    VALUE* alb = value_dict_get(value_dict_get(&v, "own"), "alliance");
    CHECK_TRUE("and for B", array_has_int(value_dict_get(alb, "mutual"), A));
    value_fini(&v);
    CHECK_TRUE("the engine's own rule agrees", players_are_mutual_allies(A, B));

    struct ExtSeatVerb v3 = ally(A, 1);
    { const char* e = extseat_submit_verb(UB, B, &v3, NULL); CHECK_TRUE("declaring what is already declared is ALREADY_SET", e && strcmp(e, "ALREADY_SET") == 0); }

    struct ExtSeatVerb w = ally(B, 0);
    const char* e = extseat_submit_verb(UA, A, &w, NULL);
    CHECK_TRUE("A withdraws", e == NULL);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult al04_check_withdrawn(struct FTestActionArgs* const args)
{
    VALUE v; api_seat_build_view(&v, A);
    VALUE* al = value_dict_get(value_dict_get(&v, "own"), "alliance");
    CHECK_TRUE("A no longer declares B", !array_has_int(value_dict_get(al, "declared"), B));
    CHECK_TRUE("so it is no longer mutual for A", !array_has_int(value_dict_get(al, "mutual"), B));
    value_fini(&v);
    api_seat_build_view(&v, B);
    VALUE* alb = value_dict_get(value_dict_get(&v, "own"), "alliance");
    CHECK_TRUE("B still declares A (one-way withdrawal)", array_has_int(value_dict_get(alb, "declared"), A));
    CHECK_TRUE("but it is not mutual for B either now", !array_has_int(value_dict_get(alb, "mutual"), A));
    value_fini(&v);
    CHECK_TRUE("the engine's own rule agrees it is no longer mutual", !players_are_mutual_allies(A, B));
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " alliance check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: set_alliance declares one-way, the view shows declared vs mutual, withdrawal is one-way too");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
