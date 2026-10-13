/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_state_validate.c
 *     Whether a game state that came from outside can be used without leaving its arrays. See game_state_validate.h.
 */
/******************************************************************************/
#include "pre_inc.h"
#include <string.h>
#include "game_state_validate.h"

#include "state_check.h"
#include "kfx_game_state.h"
#include "lvl_script.h"
#include "creature_groups.h"
#include "thing_list.h"
#include "player_data.h"
#include "post_inc.h"

struct Ctx {
    const struct KfxGameState *s;
    struct StateCheck chk;
};

#define LEN(array) ((int64_t)(sizeof(array) / sizeof((array)[0])))
#define BAD(what, i, value, ...) do { if (!state_check_note(&((struct Ctx *)c)->chk, (what), (i), (value), __VA_ARGS__)) return false; } while (0)
#define RANGE(value, lo, hi, what, i) do { \
        const int64_t v_ = (int64_t)(value); \
        if ((v_ < (int64_t)(lo)) || (v_ > (int64_t)(hi))) \
            BAD((what), (long long)(i), v_, "%s[%lld] = %lld is outside %lld..%lld", (what), (long long)(i), (long long)v_, (long long)(lo), (long long)(hi)); \
    } while (0)
/** A count of used entries in an array of `max`. */
#define COUNT(value, max, what) RANGE(value, 0, max, what, 0)

static TbBool validate_script(const struct Ctx *c)
{
    const struct LevelScript *sc = &c->s->script;
    COUNT(sc->tunneller_triggers_num, LEN(sc->tunneller_triggers), "script.tunneller_triggers_num");
    COUNT(sc->party_triggers_num, LEN(sc->party_triggers), "script.party_triggers_num");
    COUNT(sc->values_num, LEN(sc->values), "script.values_num");
    COUNT(sc->conditions_num, LEN(sc->conditions), "script.conditions_num");
    COUNT(sc->creature_partys_num, LEN(sc->creature_partys), "script.creature_partys_num");
    COUNT(sc->win_conditions_num, LEN(sc->win_conditions), "script.win_conditions_num");
    COUNT(sc->lose_conditions_num, LEN(sc->lose_conditions), "script.lose_conditions_num");
    RANGE(sc->next_string_offset, 0, LEN(sc->strings), "script.next_string_offset", 0);
    if (sc->strings[LEN(sc->strings) - 1] != '\0')
        BAD("script.strings is not terminated", 0, sc->strings[LEN(sc->strings) - 1], "script.strings does not end with a NUL");

    // a condition index of one past the table (512) means "none", as -1 may
    const int64_t conditions = LEN(sc->conditions) + 1;
    for (int64_t i = 0; (i < (int64_t)sc->tunneller_triggers_num) && (i < LEN(sc->tunneller_triggers)); i++)
        RANGE(sc->tunneller_triggers[i].condit_idx, -1, conditions - 1, "script.tunneller_triggers.condit_idx", i);
    for (int64_t i = 0; (i < (int64_t)sc->party_triggers_num) && (i < LEN(sc->party_triggers)); i++)
        RANGE(sc->party_triggers[i].condit_idx, -1, conditions - 1, "script.party_triggers.condit_idx", i);
    for (int64_t i = 0; (i < (int64_t)sc->values_num) && (i < LEN(sc->values)); i++)
        RANGE(sc->values[i].condit_idx, -1, conditions - 1, "script.values.condit_idx", i);
    for (int64_t i = 0; (i < (int64_t)sc->conditions_num) && (i < LEN(sc->conditions)); i++)
        RANGE(sc->conditions[i].condit_idx, -1, conditions - 1, "script.conditions.condit_idx", i);
    for (int64_t i = 0; (i < (int64_t)sc->creature_partys_num) && (i < LEN(sc->creature_partys)); i++)
    {
        const struct Party *p = &sc->creature_partys[i];
        COUNT(p->members_num, GROUP_MEMBERS_COUNT, "script.creature_partys.members_num");
        if (p->prtname[LEN(p->prtname) - 1] != '\0')
            BAD("script.creature_partys name is not terminated", i, 0, "script.creature_partys[%lld].prtname does not end with a NUL", (long long)i);
    }
    for (int64_t i = 0; (i < (int64_t)sc->win_conditions_num) && (i < LEN(sc->win_conditions)); i++)
        RANGE(sc->win_conditions[i], 0, LEN(sc->conditions) - 1, "script.win_conditions", i);
    for (int64_t i = 0; (i < (int64_t)sc->lose_conditions_num) && (i < LEN(sc->lose_conditions)); i++)
        RANGE(sc->lose_conditions[i], 0, LEN(sc->conditions) - 1, "script.lose_conditions", i);
    return true;
}

static TbBool validate_rest(const struct Ctx *c)
{
    const struct KfxGameState *s = c->s;
    RANGE(s->script_timer_player, -1, PLAYERS_COUNT - 1, "script_timer_player", 0);
    COUNT(s->active_script_var_count, LEN(s->script_variables), "active_script_var_count");
    RANGE(s->ambient_sound_thing_idx, 0, (int64_t)THINGS_COUNT - 1, "ambient_sound_thing_idx", 0);
    if (s->campaign_fname[LEN(s->campaign_fname) - 1] != '\0')
        BAD("campaign_fname is not terminated", 0, 0, "campaign_fname does not end with a NUL");
    if (s->music_fname[LEN(s->music_fname) - 1] != '\0')
        BAD("music_fname is not terminated", 0, 0, "music_fname does not end with a NUL");
    return true;
}

static TbBool run_checks(struct Ctx *ctx)
{
    return validate_script(ctx) && validate_rest(ctx);
}

TbBool game_state_validate(const struct KfxGameState *state, char *why, size_t why_size)
{
    struct Ctx ctx = { state, { why, why_size, NULL, 0 } };
    if ((why != NULL) && (why_size > 0))
        why[0] = '\0';
    return run_checks(&ctx);
}

int64_t game_state_survey(const struct KfxGameState *state, char *report, size_t report_size)
{
    struct StateSurveyEntry entries[STATE_SURVEY_MAX];
    memset(entries, 0, sizeof(entries));
    struct Ctx ctx = { state, { NULL, 0, entries, 0 } };
    run_checks(&ctx);
    return state_check_report(&ctx.chk, report, report_size);
}
