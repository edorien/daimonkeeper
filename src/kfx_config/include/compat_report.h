/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file compat_report.h
 *     Content features this build doesn't support, collected while loading.
 * @par Purpose:
 *     Level scripts and config files written for a newer KeeperFX can use
 *     commands, keys, values or names this build doesn't know. The parsers
 *     log and skip them; this collects them too, so a level load can say
 *     what it skipped (log "COMPAT:" lines, and a warning before play)
 *     instead of the player only seeing the crash or broken level after.
 * @par Comment:
 *     Only "unknown feature" events belong here, not ordinary authoring
 *     mistakes (bad argument counts, out-of-range numbers).
 *     Plan: docs/rebadge/02-content-compatibility.md (local notes) §3.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_COMPAT_REPORT_H
#define DK_COMPAT_REPORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum CompatIssueKind {
    CompatIssue_ScriptCommand,  // unknown level script command
    CompatIssue_ScriptName,     // unknown creature/room/slab name in a script argument
    CompatIssue_ConfigKey,      // unknown key in a config file
    CompatIssue_ConfigValue,    // unknown named value (or flag) for a known key
    CompatIssue_LuaFunction,    // Lua called a function this build doesn't have
    CompatIssue_Limit,          // more of something than this build has room for
};

#define COMPAT_ISSUES_MAX     64
#define COMPAT_WHAT_LEN       64
#define COMPAT_WHERE_LEN      64

struct CompatIssue {
    enum CompatIssueKind kind;
    char what[COMPAT_WHAT_LEN];   // the unsupported command/key/name, "Key=Value", or what overflowed
    char where[COMPAT_WHERE_LEN]; // file name (no directory), or "" if unknown
    uint64_t line;                // first occurrence
    uint64_t count;               // occurrences, deduplicated by (kind, what)
};

/** Forgets everything collected so far (start of a level load). */
void compat_report_clear(void);
/** Records one unsupported feature. where may be a full path (only its file name is kept),
 *  or NULL for the file compat_report_set_source() named, if any. */
void compat_report_add(enum CompatIssueKind kind, const char *what, const char *where, uint64_t line);
/** The config file being parsed, for issues whose parser doesn't know it (the
 *  legacy command parsers); NULL once it's done. The path is copied. */
void compat_report_set_source(const char *path);

struct NamedCommand;
struct LongNamedCommand;
/** A named value (e.g. a flag) that didn't resolve against its name table when
 *  parsed. That can be load order -- the name may come from a config loaded
 *  later -- so it only becomes a CompatIssue_ConfigValue if it still doesn't
 *  resolve at compat_report_resolve_pending(), once everything has loaded. */
void compat_report_add_value(const char *field, const char *value, const char *where, uint64_t line,
    const struct NamedCommand *names);
void compat_report_add_long_value(const char *field, const char *value, const char *where, uint64_t line,
    const struct LongNamedCommand *names);
/** Re-checks the pending values; the ones that still don't resolve become issues. */
void compat_report_resolve_pending(void);
/** Number of distinct issues held (at most COMPAT_ISSUES_MAX). */
int64_t compat_report_count(void);
/** Distinct issues that didn't fit in COMPAT_ISSUES_MAX. */
int64_t compat_report_overflow(void);
const struct CompatIssue *compat_report_get(int64_t idx);
/** Human-readable kind, e.g. "script command". */
const char *compat_issue_kind_name(enum CompatIssueKind kind);
/** One line for people: "script command 'X' (map00001.txt, line 12)", leaving out
 *  the file or line when unknown. */
void compat_issue_describe(const struct CompatIssue *issue, char *buf, size_t buflen);
/** Writes the report to the log as "COMPAT:" lines (nothing when empty). */
void compat_report_log(const char *context);
/** Asks the in-game GUI to show the report to the player before play (set by
 *  kfx_game after a level load, cleared by the GUI once answered). */
void compat_report_set_review_pending(int pending);
int compat_report_review_pending(void);

#ifdef __cplusplus
}
#endif
#endif
