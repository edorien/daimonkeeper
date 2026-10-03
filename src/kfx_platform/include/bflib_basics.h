/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_basics.h
 *     Header file for bflib_basics.c.
 * @par Purpose:
 *     Integrates all elements of the library with a common toolkit.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     10 Feb 2008 - 22 Dec 2008
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef BFLIB_BASICS_H
#define BFLIB_BASICS_H

#include <time.h>
#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// Buffer sizes
// Disk path max length
#define DISKPATH_SIZE    144
#define LINEMSG_SIZE     160
#define READ_BUFSIZE     256
#define LOOPED_FILE_LEN 4096
#define COMMAND_WORD_LEN  64

// Max length of any processed string
#define MAX_TEXT_LENGTH 4096
// Smaller buffer, also widely used
#define TEXT_BUFFER_LENGTH 2048

#define MAX_CONSOLE_LOG_COUNT 1000   // Maximum number of log messages

enum TbErrorLogFlags {
        Lb_ERROR_LOG_APPEND = 0,
        Lb_ERROR_LOG_NEW    = 1,
};

enum TbLogFlags {
        LbLog_DateInHeader = 0x0010,
        LbLog_TimeInHeader = 0x0020,
        LbLog_DateInLines  = 0x0040,
        LbLog_TimeInLines  = 0x0080,
        LbLog_LoopedFile   = 0x0100,
};

enum TbErrorCode {
    Lb_FAIL                 = -1,
    Lb_OK                   =  0,
    Lb_SUCCESS              =  1,
};

/******************************************************************************/
#pragma pack(1)

// These types should be deprecated because we have stdint.h now.
typedef unsigned char uchar;

struct TbTime {
        unsigned char Hour;
        unsigned char Minute;
        unsigned char Second;
        unsigned char HSecond;
};
struct TbDate {
        unsigned char Day;
        unsigned char Month;
        int64_t Year;
        unsigned char DayOfWeek;
};
typedef int64_t TbClockMSec;
typedef time_t TbTimeSec;

// 32-bit on purpose: the checksums are a rotate-by-5 accumulate that only means something at 32 bits wide,
// and Packet.checksum is a 4-byte wire field.
typedef uint32_t TbBigChecksum;
typedef int64_t Offset;
typedef FILE * TbFileHandle;
typedef unsigned char TbBool;
typedef int64_t TbScreenPos;

#define LOG_PREFIX_LEN 32

struct TbLog {
        char filename[DISKPATH_SIZE];
        char prefix[LOG_PREFIX_LEN];
        uint64_t flags;
        TbBool Initialised;
        TbBool Created;
        TbBool Suspended;
        int64_t position;
};

struct TbNetworkCallbackData;
/** Command function result, alias for TbResult. */
typedef int64_t TbError;
/** Command function result, valid values are of TbErrorCode enumeration. */
typedef int64_t TbResult;
typedef size_t TbSize;

struct DebugMessage {
  struct DebugMessage * next;
  char text[0];
};

extern struct DebugMessage * debug_messages_head;
extern struct DebugMessage ** debug_messages_tail;



#pragma pack()
/******************************************************************************/
extern const char *log_file_name;
extern int64_t debug_display_consolelog;
extern char consoleLogArray[MAX_CONSOLE_LOG_COUNT][MAX_TEXT_LENGTH];
extern size_t consoleLogArraySize;

// Moved from kfx_apploop's game_session_loop.h (stage 13.3) -- genuinely
// cross-cutting session-exit signals read/written from nearly every
// layer (kfx_net on a fatal protocol error, kfx_frontend on a menu-
// driven quit, kfx_render on a fatal video-mode failure, kfx_sim's
// script QUIT_GAME command, etc.). No single layer above kfx_platform
// is the "owner"; kfx_apploop's game_loop()/keeper_gameplay_loop() are
// what actually read them to decide whether to keep looping.
extern unsigned char exit_keeper;
extern unsigned char quit_game;
extern int64_t FatalError;

// High level functions - DK specific
int64_t warning_dialog(const char *codefile,const int64_t ecode,const char *message) __attribute__ ((nonnull(1, 3)));
int64_t error_dialog(const char *codefile,const int64_t ecode,const char *message) __attribute__ ((nonnull(1, 3)));
int64_t error_dialog_fatal(const char *codefile,const int64_t ecode,const char *message) __attribute__ ((nonnull(1, 3)));
int64_t str_append(char * buffer, int64_t size, const char * str) __attribute__ ((nonnull(1, 3)));
/** LbStrToI32()/LbAtoI32() saturating at the int32 range on every platform. The C functions return `long` and clamp at
 *  LONG_MAX/LONG_MIN: +-2^31 on the 32-bit Windows build, +-2^63 on 64-bit Linux, so an out-of-range number in
 *  a script/config/console command would parse to different values (and then truncate differently). */
int64_t LbStrToI32(const char *text, char **endptr, int64_t base);
int64_t LbAtoI32(const char *text);
int64_t str_appendf(char * buffer, int64_t size, const char * format, ...) __attribute__ ((format(printf, 3, 4), nonnull(1, 3)));

// Generic name/id lookup pair and its lookup function. Owned here (not
// kfx_config's config.h, where they used to live) because get_rid() has
// no config-state coupling at all -- it's a pure algorithm over whatever
// array is passed in. Moved down so kfx_platform code (e.g.
// sound_manager.cpp) can call it without a bare same-file extern
// forward-declaration. See docs/refactor/todo/
// check-layering-symbol-level-blind-spot.md.
struct NamedCommand {
    const char *name;
    int64_t num;
};
int64_t get_rid(const struct NamedCommand *desc, const char *itmname);

// Registered from main.cpp with kfx_config's emulate_integer_overflow()
// (config_rules.c, reads kfx_config_state's classic_bugs_flags) --
// saturate_set_unsigned() below can't include config.h directly. See
// docs/refactor/todo/check-layering-symbol-level-blind-spot.md.
typedef TbBool (*EmulateIntegerOverflowFunc)(int64_t nbits);
extern EmulateIntegerOverflowFunc emulate_integer_overflow_provider;
void set_emulate_integer_overflow_provider(EmulateIntegerOverflowFunc provider);
/******************************************************************************/
/**
 * How much the game writes to keeperfx.log: the LOG_LEVEL game option.
 * See docs/refactor-pass2/stage-02-logging-option.md.
 * - Off: nothing, except crash reports.
 * - Normal: the always-on lines (ERRORLOG, WARNLOG, SYNCLOG, JUSTLOG, ...).
 * - Debug: Normal plus every *DBG(lv, ...) line with lv < 10.
 * - DebugMax: Normal plus every *DBG line, whatever its level.
 */
enum LogLevel {
    LogLvl_Off = 0,
    LogLvl_Normal,
    LogLvl_Debug,
    LogLvl_DebugMax,
};
/** Current level, an enum LogLevel. Change it with set_log_level(). */
extern int64_t kfx_log_level;
/** Derived from kfx_log_level: a *DBG(lv, ...) line prints when lv < this. */
extern int64_t kfx_debug_threshold;
void set_log_level(int64_t level);
int64_t get_log_level(void);
/**
 * Pins the level for this session: keeperfx.cfg's LOG_LEVEL (set_log_level_from_config())
 * no longer changes it, and nothing is written back. Used for functional-test runs
 * (main.cpp). An explicit change on the options screen still applies (set_log_level()).
 */
void set_log_level_pinned(int64_t level);
TbBool log_level_is_pinned(void);
/** keeperfx.cfg's LOG_LEVEL: applied unless the session level is pinned. */
void set_log_level_from_config(int64_t level);
/** The *DBG threshold a level maps to: Off/Normal 0, Debug 10, DebugMax 20. */
int64_t log_level_debug_threshold(int64_t level);
/**
 * Makes every later log call write, whatever the level. The crash handlers
 * (bflib_crash.c) call it first, so a crash report is written even at Off.
 */
void LbLogForceOn(void);
/**
 * Startup buffering: until the configuration has set the level, log lines
 * go into memory instead of keeperfx.log. LbLogEndStartupBuffering(write)
 * then writes them out (write=true) or drops them (Off). Forcing the log on
 * or closing it while buffering writes the buffer out.
 */
void LbLogStartStartupBuffering(void);
void LbLogEndStartupBuffering(TbBool write);
/**
 * Writes buffered log lines to disk. At Off and Normal every line is flushed as
 * it is written; at Debug and above lines are flushed on ERRORLOG/WARNLOG, once
 * per presented frame (RendererPresentFrame()), on a level change and when the
 * log closes, so heavy debug output doesn't stall on the disk.
 */
void LbLogFlush(void);

int64_t LbErrorLog(const char *format, ...) __attribute__ ((format(printf, 1, 2), nonnull(1)));
int64_t LbWarnLog(const char *format, ...) __attribute__ ((format(printf, 1, 2), nonnull(1)));
int64_t LbSyncLog(const char *format, ...) __attribute__ ((format(printf, 1, 2), nonnull(1)));
int64_t LbNetLog(const char *format, ...) __attribute__ ((format(printf, 1, 2), nonnull(1)));
int64_t LbJustLog(const char *format, ...) __attribute__ ((format(printf, 1, 2), nonnull(1)));
int64_t LbNaviLog(const char *format, ...) __attribute__ ((format(printf, 1, 2), nonnull(1)));

#ifdef FUNCTESTING
int64_t LbFTestLog(const char *format, ...) __attribute__ ((format(printf, 1, 2), nonnull(1)));
#endif
int64_t LbScriptLog(uint64_t line,const char *format, ...) __attribute__ ((format(printf, 2, 3), nonnull(2)));
int64_t LbConfigLog(uint64_t line,const char *format, ...) __attribute__ ((format(printf, 2, 3), nonnull(2)));

int64_t LbErrorLogSetup(const char *directory, const char *filename, TbBool flag);
int64_t LbErrorLogClose(void);

int64_t LbLogClose(struct TbLog *log) __attribute__ ((nonnull(1)));
int64_t LbLogSetup(struct TbLog *log, const char *filename, uint64_t flags) __attribute__ ((nonnull(1, 2)));
int64_t LbLogSetPrefix(struct TbLog *log, const char *prefix) __attribute__ ((nonnull(1, 2)));
int64_t LbLogSetPrefixFmt(struct TbLog *log, const char *format, ...) __attribute__ ((format(printf, 2, 3), nonnull(1, 2)));

/******************************************************************************/
typedef void (*TbNetworkCallbackFunc)(struct TbNetworkCallbackData *, void *);
/******************************************************************************/
uint64_t llong (unsigned char *p) __attribute__ ((nonnull(1)));
uint64_t lword (unsigned char *p) __attribute__ ((nonnull(1)));
int64_t saturate_set_signed(long long val,int64_t nbits);
uint64_t saturate_set_unsigned(unsigned long long val,int64_t nbits);
void make_lowercase(char *) __attribute__ ((nonnull(1)));
void make_uppercase(char *) __attribute__ ((nonnull(1)));
int64_t natoi(const char * str, int64_t len) __attribute__ ((nonnull(1))); // like atoi but stops after len bytes

/**
 * Converts an index number to a flag - by creating a bitmask where only the nth bit is set to 1.
 * For example: idx=0 returns 0b000001, idx=1 returns 0b000010, idx=2 returns 0b000100.
 *
 * @param idx The index number of the flag, or n, used to set the nth bit of the mask to 1.
 * @return Returns a bitmask with a single masked bit (aka "flag", "bitflag").
 */
#define to_flag(idx) (1 << idx)

/**
 * Set a flag* - by setting the given masked bit(s) to 1 in the given flags variable. *Can set multiple flags.
 *
 * @param flags The flags variable we want to change.
 * @param mask Bitmask, containing 1 (or more) masked bits, representing the flag(s) we want to set.
 */
#define set_flag(flags,mask) flags |= mask

/**
 * Clear a flag* - by setting the given masked bit(s) to 0 in the given flags variable. *Can clear multiple flags.
 *
 * @param flags The flags variable we want to change.
 * @param mask Bitmask, containing 1 (or more) masked bits, representing the flag(s) we want to clear.
 */
#define clear_flag(flags,mask) flags &= ~(mask)

/**
 * Toggle a given flag* between set/cleared - by toggling the given masked bit(s) in the given flags variable. *Can toggle multiple flags.
 *
 * @param flags The flags variable we want to change.
 * @param mask Bitmask, containing 1 (or more) masked bits, representing the flag(s) we want to toggle.
 */
#define toggle_flag(flags,mask) flags ^= mask

/**
 * Check if the given flag* is set - by checking if the given masked bits are set to 1 in the given flags variable. *Can check for multiple flags.
 *
 * @param flags The flags variable we want to check.
 * @param mask Bitmask, containing 1 (or more) masked bits, representing the flag(s) we want to check in the "flags" parameter.
 * @return Returns TRUE if the given masked bits are set to 1 in the given flags variable.
 */
#define flag_is_set(flags,mask) ((flags & (mask)) == (mask))

/**
 * Check if any of the given flags is set - by checking if any of the given masked bits are set to 1 in the given flags variable.
 *
 * @param flags The flags variable we want to check.
 * @param mask Bitmask, containing 1 (or more) masked bits, representing the bit flags we want to check in the "flags" parameter.
 * @return Returns TRUE if any of the given masked bits are set to 1 in the given flags variable.
 */
#define any_flag_is_set(flags,mask) ((flags & (mask)) != 0)

/**
 * Check if all of the flags are set - by checking if all of the bits are set to 1 in the given flags variable.
 * For example: all 6 players set in a flags variable would be 0b111111.
 *
 * @param flags The flags variable we want to check.
 * @param count The number of bits used by the flags variable, i.e the count of "all of the bits".
 * @return Returns TRUE if all bits are set to 1 in the given flags variable.
 */
#define all_flags_are_set(flags,count) ((1 << count) - flags == 1)

/**
 * Set a flag* - by setting the given masked bit(s) to "bool value" in the given flags variable. *Can set multiple flags.
 *
 * @param flags The flags variable we want to change.
 * @param mask Bitmask, containing 1 (or more) masked bits, representing the flag(s) we want to set.
 * @param value If value == 0, then set the masked bit(s) to 0 in "flags". If value != 0, then set the masked bit(s) to 1 in "flags".
 */
#define set_flag_value(flags,mask,value) ((value) ? (set_flag(flags,mask)) : (clear_flag(flags,mask)))
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
