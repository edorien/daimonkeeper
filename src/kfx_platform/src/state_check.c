/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file state_check.c
 *     The bookkeeping the state validators share. See state_check.h.
 */
/******************************************************************************/
#include "pre_inc.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "state_check.h"
#include "post_inc.h"

int state_check_note(struct StateCheck *c, const char *what, int64_t i, int64_t value, const char *fmt, ...)
{
    if (c->survey != NULL)
    {
        for (int64_t k = 0; k < c->survey_used; k++)
        {
            if (strcmp(c->survey[k].what, what) == 0)
            {
                c->survey[k].count++;
                return 1;
            }
        }
        if (c->survey_used < STATE_SURVEY_MAX)
        {
            struct StateSurveyEntry *e = &c->survey[c->survey_used++];
            e->what = what;
            e->count = 1;
            e->first_index = i;
            e->first_value = value;
        }
        return 1;
    }
    if (c->why != NULL && c->why_size > 0)
    {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(c->why, c->why_size, fmt, ap);
        va_end(ap);
    }
    return 0;
}

int64_t state_check_report(const struct StateCheck *c, char *report, size_t report_size)
{
    if ((report != NULL) && (report_size > 0))
    {
        size_t used = 0;
        report[0] = '\0';
        for (int64_t k = 0; k < c->survey_used; k++)
        {
            int n = snprintf(report + used, report_size - used, "%s x%lld (first at [%lld] = %lld)\n", c->survey[k].what,
                (long long)c->survey[k].count, (long long)c->survey[k].first_index, (long long)c->survey[k].first_value);
            if ((n < 0) || ((size_t)n >= report_size - used))
                break;
            used += (size_t)n;
        }
    }
    return c->survey_used;
}
