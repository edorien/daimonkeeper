/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file compat_report.c
 *     Content features this build doesn't support, collected while loading.
 * @par Purpose:
 *     See compat_report.h.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "compat_report.h"
#include "config.h"
#include "globals.h"
#include "version.h"

#include <string.h>
#include <stdio.h>
#include "post_inc.h"

static struct CompatIssue compat_issues[COMPAT_ISSUES_MAX];
static int64_t compat_issues_num = 0;
static int64_t compat_issues_overflow = 0;
static int compat_review_pending = 0;
static char compat_source[COMPAT_WHERE_LEN] = "";

// Values waiting for compat_report_resolve_pending() (see compat_report.h).
struct PendingValue {
    char what[COMPAT_WHAT_LEN];
    char value[COMPAT_WHAT_LEN];
    char where[COMPAT_WHERE_LEN];
    uint64_t line;
    const struct NamedCommand *names;
    const struct LongNamedCommand *long_names;
};
static struct PendingValue compat_pending[COMPAT_ISSUES_MAX];
static int64_t compat_pending_num = 0;
static int64_t compat_pending_overflow = 0;

void compat_report_clear(void)
{
    memset(compat_issues, 0, sizeof(compat_issues));
    compat_issues_num = 0;
    compat_issues_overflow = 0;
    compat_review_pending = 0;
    compat_source[0] = '\0';
    memset(compat_pending, 0, sizeof(compat_pending));
    compat_pending_num = 0;
    compat_pending_overflow = 0;
}

void compat_report_set_source(const char *path)
{
    compat_source[0] = '\0';
    if (path == NULL)
        return;
    const char *name = path;
    for (const char *p = path; *p != '\0'; p++)
        if ((*p == '/') || (*p == '\\'))
            name = p + 1;
    snprintf(compat_source, sizeof(compat_source), "%s", name);
}

static void add_pending(const char *field, const char *value, const char *where, uint64_t line,
    const struct NamedCommand *names, const struct LongNamedCommand *long_names)
{
    if ((value == NULL) || (value[0] == '\0'))
        return;
    char what[COMPAT_WHAT_LEN];
    snprintf(what, sizeof(what), "%s=%s", (field != NULL) ? field : "", value);
    for (int64_t i = 0; i < compat_pending_num; i++)
        if (strcasecmp(compat_pending[i].what, what) == 0)
            return;
    if (compat_pending_num >= COMPAT_ISSUES_MAX)
    {
        compat_pending_overflow++;
        return;
    }
    struct PendingValue *pv = &compat_pending[compat_pending_num++];
    memcpy(pv->what, what, sizeof(pv->what));
    snprintf(pv->value, sizeof(pv->value), "%s", value);
    const char *src = (where != NULL) ? where : compat_source;
    const char *name = src;
    for (const char *p = src; *p != '\0'; p++)
        if ((*p == '/') || (*p == '\\'))
            name = p + 1;
    snprintf(pv->where, sizeof(pv->where), "%s", name);
    pv->line = line;
    pv->names = names;
    pv->long_names = long_names;
}

void compat_report_add_value(const char *field, const char *value, const char *where, uint64_t line,
    const struct NamedCommand *names)
{
    add_pending(field, value, where, line, names, NULL);
}

void compat_report_add_long_value(const char *field, const char *value, const char *where, uint64_t line,
    const struct LongNamedCommand *names)
{
    add_pending(field, value, where, line, NULL, names);
}

void compat_report_resolve_pending(void)
{
    for (int64_t i = 0; i < compat_pending_num; i++)
    {
        const struct PendingValue *pv = &compat_pending[i];
        const TbBool resolves = (pv->names != NULL) ? (get_id(pv->names, pv->value) >= 0)
            : (pv->long_names != NULL) ? (get_long_id(pv->long_names, pv->value) >= 0) : false;
        if (!resolves)
            compat_report_add(CompatIssue_ConfigValue, pv->what, pv->where, pv->line);
    }
    // Pending values that didn't fit were never checked; they aren't counted as issues.
    compat_pending_num = 0;
    compat_pending_overflow = 0;
}

void compat_report_set_review_pending(int pending)
{
    compat_review_pending = pending ? 1 : 0;
}

int compat_report_review_pending(void)
{
    return compat_review_pending;
}

static const char *file_name_only(const char *path)
{
    const char *name = path;
    for (const char *p = path; *p != '\0'; p++)
    {
        if ((*p == '/') || (*p == '\\'))
            name = p + 1;
    }
    return name;
}

void compat_report_add(enum CompatIssueKind kind, const char *what, const char *where, uint64_t line)
{
    if ((what == NULL) || (what[0] == '\0'))
        return;
    char what_buf[COMPAT_WHAT_LEN];
    snprintf(what_buf, sizeof(what_buf), "%s", what);
    for (int64_t i = 0; i < compat_issues_num; i++)
    {
        struct CompatIssue *issue = &compat_issues[i];
        if ((issue->kind == kind) && (strcasecmp(issue->what, what_buf) == 0))
        {
            issue->count++;
            return;
        }
    }
    if (compat_issues_num >= COMPAT_ISSUES_MAX)
    {
        compat_issues_overflow++;
        return;
    }
    struct CompatIssue *issue = &compat_issues[compat_issues_num++];
    issue->kind = kind;
    memcpy(issue->what, what_buf, sizeof(issue->what));
    snprintf(issue->where, sizeof(issue->where), "%s", (where != NULL) ? file_name_only(where) : compat_source);
    issue->line = line;
    issue->count = 1;
}

int64_t compat_report_count(void)
{
    return compat_issues_num;
}

int64_t compat_report_overflow(void)
{
    return compat_issues_overflow;
}

const struct CompatIssue *compat_report_get(int64_t idx)
{
    if ((idx < 0) || (idx >= compat_issues_num))
        return NULL;
    return &compat_issues[idx];
}

const char *compat_issue_kind_name(enum CompatIssueKind kind)
{
    switch (kind)
    {
    case CompatIssue_ScriptCommand: return "script command";
    case CompatIssue_ScriptName:    return "script name";
    case CompatIssue_ConfigKey:     return "config key";
    case CompatIssue_ConfigValue:   return "config value";
    case CompatIssue_LuaFunction:   return "Lua function";
    case CompatIssue_Limit:         return "over this build's limit:";
    default:                        return "feature";
    }
}

void compat_issue_describe(const struct CompatIssue *issue, char *buf, size_t buflen)
{
    char loc[COMPAT_WHERE_LEN + 32] = "";
    if ((issue->where[0] != '\0') && (issue->line > 0))
        snprintf(loc, sizeof(loc), " (%s, line %" PRIu64 ")", issue->where, issue->line);
    else if (issue->where[0] != '\0')
        snprintf(loc, sizeof(loc), " (%s)", issue->where);
    else if (issue->line > 0)
        snprintf(loc, sizeof(loc), " (line %" PRIu64 ")", issue->line);
    snprintf(buf, buflen, "%s '%s'%s%s", compat_issue_kind_name(issue->kind), issue->what, loc,
        (issue->count > 1) ? ", repeated" : "");
}

void compat_report_log(const char *context)
{
    compat_report_resolve_pending();
    if (compat_issues_num == 0)
        return;
    JUSTMSG("COMPAT: %s uses %" PRId64 " feature(s) this build (%s) doesn't support:",
        context, compat_issues_num + compat_issues_overflow, KFX_COMPAT_STRING);
    for (int64_t i = 0; i < compat_issues_num; i++)
    {
        const struct CompatIssue *issue = &compat_issues[i];
        char text[COMPAT_WHAT_LEN + COMPAT_WHERE_LEN + 96];
        compat_issue_describe(issue, text, sizeof(text));
        JUSTMSG("COMPAT:   %s", text);
    }
    if (compat_issues_overflow > 0)
        JUSTMSG("COMPAT:   ... and %" PRId64 " more", compat_issues_overflow);
}
