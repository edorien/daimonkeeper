/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file state_check.h
 *     The bookkeeping the state validators share (security pass S06): record the first thing found wrong, or, when
 *     calibrating, every kind of thing found wrong and how often.
 * @par Purpose:
 *     A validator (sim_state_validate(), game_state_validate(), ariadne_saved_state_validate()) checks a state that came
 *     from a file or a peer before it is used. It reports through a StateCheck: normally the first violation ends the
 *     check with a message saying which field; in survey mode (state_check_survey) the checks go on, so a run over
 *     real states lists every kind of violation at once -- how the bounds were calibrated against real levels.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_STATE_CHECK_H
#define DK_STATE_CHECK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STATE_SURVEY_MAX 96

struct StateSurveyEntry {
    const char *what;
    int64_t count;
    int64_t first_index;
    int64_t first_value;
};

struct StateCheck {
    char *why;               /**< where the first violation is described, or NULL */
    size_t why_size;
    struct StateSurveyEntry *survey; /**< survey mode: STATE_SURVEY_MAX entries to fill, else NULL */
    int64_t survey_used;
};

/**
 * Notes a violation of `what` at element `i` holding `value`; `fmt` is the message for `why` (first violation only).
 * @return 1 if checking should go on (survey mode), 0 to leave with the reason.
 */
int state_check_note(struct StateCheck *c, const char *what, int64_t i, int64_t value, const char *fmt, ...);

/** Writes one line per kind of violation a survey found into `report`. @return how many kinds. */
int64_t state_check_report(const struct StateCheck *c, char *report, size_t report_size);

#ifdef __cplusplus
}
#endif

#endif
