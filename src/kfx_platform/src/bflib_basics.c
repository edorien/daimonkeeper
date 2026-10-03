/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_basics.c
 *     Basic definitions and global routines for all library files.
 * @par Purpose:
 *     Integrates all elements of the library with a common toolkit.
 * @par Comment:
 *     Only simple, basic functions which can be used in every library file.
 * @author   Tomasz Lis
 * @date     10 Feb 2008 - 22 Dec 2008
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "bflib_basics.h"
#include "globals.h"

#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <SDL3/SDL.h>

#include "bflib_datetm.h"
#include "bflib_fileio.h"
#include "post_inc.h"


char consoleLogArray[MAX_CONSOLE_LOG_COUNT][MAX_TEXT_LENGTH];
size_t consoleLogArraySize = 0;
int64_t debug_display_consolelog = 0;

// Defined here (not steam_api.cpp) since that file is excluded entirely
// from non-Windows builds (see src/kfx_platform/CMakeLists.txt), but
// this flag needs to exist and default to false on every platform.
unsigned char is_running_under_wine = false;

unsigned char exit_keeper;
unsigned char quit_game;
int64_t FatalError;

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// See EmulateIntegerOverflowFunc (bflib_basics.h) and docs/refactor/todo/
// check-layering-symbol-level-blind-spot.md.
static TbBool default_emulate_integer_overflow(int64_t nbits) { return false; }
EmulateIntegerOverflowFunc emulate_integer_overflow_provider = &default_emulate_integer_overflow;

void set_emulate_integer_overflow_provider(EmulateIntegerOverflowFunc provider)
{
    emulate_integer_overflow_provider = provider ? provider : &default_emulate_integer_overflow;
}

// See get_gameturn() (globals.h).
static const GameTurn unwired_gameturn = 0;
const GameTurn *lb_gameturn_source = &unwired_gameturn;

void set_gameturn_source(const GameTurn *source)
{
    lb_gameturn_source = source ? source : &unwired_gameturn;
}

/**
 * Returns ID of given item using NamedCommands list, or any item if the string is 'RANDOM'.
 * Similar to recognize_conf_parameter(), but for use only if the buffer stores
 * one word, ended with "\0".
 * If not found, returns -1.
 */
int64_t get_rid(const struct NamedCommand *desc, const char *itmname)
{
  int64_t i;
  if ((desc == NULL) || (itmname == NULL))
    return -1;
  for (i=0; desc[i].name != NULL; i++)
  {
    if (strcasecmp(desc[i].name, itmname) == 0)
      return desc[i].num;
  }
  if (strcasecmp("RANDOM", itmname) == 0)
  {
      i = (rand() % i);
      return desc[i].num;
  }
  return -1;
}

// Functions which were previously defined as Inline,
// but redefined for compatibility with both Ansi-C and C++.

/** Return the little-endian longword at p. */
uint64_t llong (unsigned char *p)
{
    uint64_t n = p[3];
    n = (n << 8) + p[2];
    n = (n << 8) + p[1];
    n = (n << 8) + p[0];
    return n;
}

/* Return the little-endian word at p. */
uint64_t lword (unsigned char *p)
{
    uint64_t n = p[1];
    n = (n << 8) + p[0];
    return n;
}

/**
 * Returns a signed value, which is equal to val if it fits in nbits.
 * Otherwise, returns max value that can fit in nbits.
 * @param val the value to be saturated.
 * @param nbits Max bits size, including sign bit.
 */
int64_t saturate_set_signed(long long val,int64_t nbits)
{
  long long maximum_value = (1 << (nbits-1)) - 1;
  if (val >= maximum_value)
    return maximum_value;
  if (val <= -maximum_value)
    return -maximum_value;
  return val;
}

/**
 * Returns an unsigned value, which is equal to val if it fits in nbits.
 * Otherwise, returns max value that can fit in nbits.
 * @param val the value to be saturated.
 * @param nbits Max bits size, including sign bit.
 */
uint64_t saturate_set_unsigned(unsigned long long val,int64_t nbits)
{
    unsigned long long maximum_value = (1 << (nbits)) - 1;
    if (emulate_integer_overflow_provider(nbits))
        return (val & maximum_value);
    if (val >= maximum_value)
        return maximum_value;
    return val;
}

/******************************************************************************/
const char *log_file_name=DEFAULT_LOG_FILENAME;

/**
 * Appends a string to the end of a buffer.
 * Returns the total length of the resulting string.
 * @param buffer The buffer to append the formatted string to.
 * @param size The size of the buffer.
 * @param str The string to append.
 */
int64_t str_append(char * buffer, int64_t size, const char * str)
{
    const int64_t buffer_length = strlen(buffer);
    const int64_t available = size - buffer_length;
    if (available <= 0) {
        return buffer_length;
    }
    strncat(buffer, str, available);
    return strlen(buffer);
}

/**
 * Appends a formatted string to the end of a buffer.
 * Returns the total length of the resulting string.
 * @param buffer The buffer to append the formatted string to.
 * @param size The size of the buffer.
 * @param format The format string, similar to printf.
 * @param ... The values to format and append to the buffer.
 */
int64_t LbStrToI32(const char *text, char **endptr, int64_t base)
{
    long long value = strtoll(text, endptr, base);
    if (value > INT32_MAX)
        return INT32_MAX;
    if (value < INT32_MIN)
        return INT32_MIN;
    return (int64_t)value;
}

int64_t LbAtoI32(const char *text)
{
    return LbStrToI32(text, NULL, 10);
}

int64_t str_appendf(char * buffer, int64_t size, const char * format, ...)
{
    const int64_t buffer_length = strlen(buffer);
    const int64_t available = size - buffer_length;
    if (available <= 0) {
        return buffer_length;
    }
    va_list args;
    va_start(args, format);
    vsnprintf(&buffer[buffer_length], available, format, args);
    va_end(args);
    return strlen(buffer);
}

int64_t warning_dialog(const char *codefile,const int64_t ecode,const char *message)
{
  LbWarnLog("In source %s:\n %5" PRId64 " - %s\n",codefile,(int64_t)(ecode),message);

  const SDL_MessageBoxButtonData buttons[] = {
        { .flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, .buttonID = 1, .text = "Ignore" },
    { .flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, .buttonID = 0, .text = "Abort" },
    };

    const SDL_MessageBoxData messageboxdata = {
        .flags = SDL_MESSAGEBOX_WARNING,
        .window = NULL,
        .title = PROGRAM_FULL_NAME,
        .message = message,
        .numbuttons = SDL_arraysize(buttons),
        .buttons = buttons,
        .colorScheme = NULL //colorScheme not supported on windows
    };

  int button = 0;
  SDL_ShowMessageBox(&messageboxdata, &button);
  return button;
}

int64_t error_dialog(const char *codefile,const int64_t ecode,const char *message)
{
  LbErrorLog("In source %s:\n %5" PRId64 " - %s\n",codefile,(int64_t)(ecode),message);
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, PROGRAM_FULL_NAME, message, NULL);
  return 0;
}

int64_t error_dialog_fatal(const char *codefile,const int64_t ecode,const char *message)
{
  char msg_text[2048];
  if (kfx_log_level == LogLvl_Off)
  {
      // Nothing is written at Off (docs/refactor-pass2/stage-02-logging-option.md),
      // so don't point the player at a log that doesn't exist.
      snprintf(msg_text, sizeof(msg_text), "%s This error in '%s' makes the program unable to continue. "
          "Logging is off: set Logging to Normal in the options (or LOG_LEVEL=NORMAL in keeperfx.cfg) and try again to get details in '%s'.",
          message, codefile, log_file_name);
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, PROGRAM_FULL_NAME, msg_text, NULL);
      return 0;
  }
  LbErrorLog("In source %s:\n %5" PRId64 " - %s\n",codefile,(int64_t)(ecode),message);
  snprintf(msg_text, sizeof(msg_text), "%s This error in '%s' makes the program unable to continue. See '%s' for details.", message, codefile, log_file_name);
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, PROGRAM_FULL_NAME, msg_text, NULL);
  return 0;
}

/******************************************************************************/
int64_t error_log_initialised=false;
struct TbLog error_log;

// Normal until keeperfx.cfg's LOG_LEVEL (or a session override, main.cpp) says otherwise.
int64_t kfx_log_level = LogLvl_Normal;
int64_t kfx_debug_threshold = 0;

/** The *DBG threshold a level maps to: Off/Normal 0, Debug 10, DebugMax 20. */
static int64_t log_level_debug_threshold(int64_t level)
{
    switch (level)
    {
    case LogLvl_Debug:
        return 10;
    case LogLvl_DebugMax:
        return 20;
    default:
        return 0;
    }
}

void set_log_level(int64_t level)
{
    if ((level < LogLvl_Off) || (level > LogLvl_DebugMax))
        level = LogLvl_Normal;
    LbLogFlush(); // lines written under the old level's policy
    kfx_log_level = level;
    kfx_debug_threshold = log_level_debug_threshold(level);
}

int64_t get_log_level(void)
{
    return kfx_log_level;
}

static TbBool log_level_pinned = false;

void set_log_level_pinned(int64_t level)
{
    set_log_level(level);
    log_level_pinned = true;
}

TbBool log_level_is_pinned(void)
{
    return log_level_pinned;
}

void set_log_level_from_config(int64_t level)
{
    if (!log_level_pinned)
        set_log_level(level);
}

/** Set by LbLogForceOn(): the crash report is written even at Off. */
static TbBool log_forced_on = false;
/** Set by LbErrorLog()/LbWarnLog(): flush that line at once, whatever the level. */
static TbBool log_flush_next = false;

/** Startup buffer (LbLogStartStartupBuffering()): whole lines, prefix included. */
static TbBool log_startup_buffering = false;
static char *log_startup_buf = NULL;
static size_t log_startup_len = 0;
static size_t log_startup_cap = 0;
/** While set, LbLog() skips the on-screen list (the flushed lines are already in it). */
static TbBool log_skip_live_view = false;

static void log_startup_append(const char *text, size_t len)
{
    if (log_startup_len + len + 1 > log_startup_cap)
    {
        size_t cap = (log_startup_cap > 0) ? log_startup_cap : 4096;
        while (log_startup_len + len + 1 > cap)
            cap *= 2;
        char *nbuf = (char *)realloc(log_startup_buf, cap);
        if (nbuf == NULL)
            return; // out of memory: lose the line rather than the process
        log_startup_buf = nbuf;
        log_startup_cap = cap;
    }
    memcpy(log_startup_buf + log_startup_len, text, len);
    log_startup_len += len;
    log_startup_buf[log_startup_len] = '\0';
}

void LbLogStartStartupBuffering(void)
{
    log_startup_buffering = true;
}

void LbLogEndStartupBuffering(TbBool write)
{
    if (!log_startup_buffering)
        return;
    log_startup_buffering = false;
    if (write && (log_startup_len > 0))
    {
        // The buffered lines already carry their prefix and are already in the
        // on-screen list, so write them out verbatim.
        char saved_prefix[LOG_PREFIX_LEN];
        snprintf(saved_prefix, sizeof(saved_prefix), "%s", error_log.prefix);
        error_log.prefix[0] = '\0';
        log_skip_live_view = true;
        LbJustLog("%s", log_startup_buf);
        log_skip_live_view = false;
        snprintf(error_log.prefix, sizeof(error_log.prefix), "%s", saved_prefix);
    }
    free(log_startup_buf);
    log_startup_buf = NULL;
    log_startup_len = 0;
    log_startup_cap = 0;
}

void LbLogForceOn(void)
{
    log_forced_on = true;
    LbLogEndStartupBuffering(true);
}
/******************************************************************************/
int64_t LbLog(struct TbLog *log, const char *fmt_str, va_list arg);
/******************************************************************************/

int64_t LbErrorLog(const char *format, ...)
{
    if (!error_log_initialised)
        return -1;
    LbLogSetPrefix(&error_log, "Error: ");
    log_flush_next = true;
    va_list val;
    va_start(val, format);
    int64_t result=LbLog(&error_log, format, val);
    va_end(val);
    return result;
}

int64_t LbWarnLog(const char *format, ...)
{
    if (!error_log_initialised)
        return -1;
    LbLogSetPrefix(&error_log, "Warning: ");
    log_flush_next = true;
    va_list val;
    va_start(val, format);
    int64_t result=LbLog(&error_log, format, val);
    va_end(val);
    return result;
}

int64_t LbNetLog(const char *format, ...)
{
    if (!error_log_initialised)
        return -1;
    LbLogSetPrefix(&error_log, "Net: ");
    va_list val;
    va_start(val, format);
    int64_t result=LbLog(&error_log, format, val);
    va_end(val);
    return result;
}

int64_t LbSyncLog(const char *format, ...)
{
    if (!error_log_initialised)
        return -1;
    LbLogSetPrefix(&error_log, "Sync: ");
    va_list val;
    va_start(val, format);
    int64_t result=LbLog(&error_log, format, val);
    va_end(val);
    return result;
}

int64_t LbNaviLog(const char *format, ...)
{
    if (!error_log_initialised)
        return -1;
    LbLogSetPrefix(&error_log, "Navi: ");
    va_list val;
    va_start(val, format);
    int64_t result=LbLog(&error_log, format, val);
    va_end(val);
    return result;
}

#ifdef FUNCTESTING
int64_t LbFTestLog(const char *format, ...)
{
    if (!error_log_initialised)
        return -1;
    LbLogSetPrefix(&error_log, "FTest: ");
    va_list val;
    va_start(val, format);
    int64_t result=LbLog(&error_log, format, val);
    va_end(val);
    return result;
}
#endif

/*
 * Logs script-related message.
 */
int64_t LbScriptLog(uint64_t line,const char *format, ...)
{
    if (!error_log_initialised)
        return -1;
    LbLogSetPrefixFmt(&error_log, "Script(line %" PRIu64 "): ",(uint64_t)(line));
    va_list val;
    va_start(val, format);
    int64_t result=LbLog(&error_log, format, val);
    va_end(val);
    return result;
}

/*
 * Logs config file related message.
 */
int64_t LbConfigLog(uint64_t line,const char *format, ...)
{
    if (!error_log_initialised)
        return -1;
    LbLogSetPrefixFmt(&error_log, "Config(line %" PRIu64 "): ",(uint64_t)(line));
    va_list val;
    va_start(val, format);
    int64_t result=LbLog(&error_log, format, val);
    va_end(val);
    return result;
}

int64_t LbJustLog(const char *format, ...)
{
    if (!error_log_initialised)
        return -1;
    LbLogSetPrefix(&error_log, "");
    va_list val;
    va_start(val, format);
    int64_t result=LbLog(&error_log, format, val);
    va_end(val);
    return result;
}

int64_t LbErrorLogSetup(const char *directory, const char *filename, TbBool flag)
{
  if ( error_log_initialised ) return -1;
  if ((filename == NULL) || (strlen(filename) == 0)) {
    filename = "error.log";
  }
  char log_filename[DISKPATH_SIZE];
  int64_t result;
  if ( LbFileMakeFullPath(true, directory, filename, log_filename, DISKPATH_SIZE) != 1 ) {
    return -1;
  }
  uint64_t flags = (flag == 0) + 1;
  flags |= LbLog_TimeInHeader | LbLog_DateInHeader | 0x04;
  if ( LbLogSetup(&error_log, log_filename, flags) == 1 )
  {
    error_log_initialised = 1;
    result = 1;
  } else
  {
    result = -1;
  }
  return result;
}

int64_t LbErrorLogClose(void)
{
    if (!error_log_initialised)
        return -1;
    // Closing before the level was known (an early exit): keep the lines.
    LbLogEndStartupBuffering(true);
    LbLogFlush();
    return LbLogClose(&error_log);
}

FILE *file = NULL;

void LbLogFlush(void)
{
    if (file != NULL)
        fflush(file);
}

void write_log_to_array_for_live_viewing(const char* fmt_str, va_list args, const char* add_log_prefix) {
    if (consoleLogArraySize >= MAX_CONSOLE_LOG_COUNT) {
        // Array is full - so clear it. This is a bit of a stopgap solution, it will lose us the older entries.
        memset(consoleLogArray, 0, sizeof(consoleLogArray));
        consoleLogArraySize = 0;
    }

    char formattedString[MAX_TEXT_LENGTH];
    va_list copy;
    va_copy(copy, args);
    vsnprintf(formattedString, sizeof(formattedString), fmt_str, copy);
    va_end(copy);

    char buffer[MAX_TEXT_LENGTH];
    snprintf(buffer, sizeof(buffer), "%s%s", add_log_prefix, formattedString); // merge prefix and formatted string

    // Add the combined message to the array
    strncpy(consoleLogArray[consoleLogArraySize], buffer, MAX_TEXT_LENGTH);
    consoleLogArray[consoleLogArraySize][MAX_TEXT_LENGTH - 1] = '\0';
    consoleLogArraySize++;
}

int64_t LbLog(struct TbLog *log, const char *fmt_str, va_list arg)
{
  enum Header {
        NONE   = 0,
        CREATE = 1,
        APPEND = 2,
  };
//  printf(fmt_str, arg);
  if (!log->Initialised)
    return -1;
  if ( log->Suspended )
    return 1;
  if (log_startup_buffering)
  {
      char line[MAX_TEXT_LENGTH];
      va_list copy;
      va_copy(copy, arg);
      int64_t len = snprintf(line, sizeof(line), "%s", log->prefix);
      if ((len >= 0) && (len < (int64_t)sizeof(line)))
          vsnprintf(line + len, sizeof(line) - len, fmt_str, copy);
      va_end(copy);
      log_startup_append(line, strlen(line));
      write_log_to_array_for_live_viewing(fmt_str, arg, log->prefix);
      return 1;
  }
  // Off writes nothing but crash reports (LbLogForceOn()).
  if ((kfx_log_level == LogLvl_Off) && !log_forced_on)
    return 1;
  char header = NONE;
  int64_t need_initial_newline = false;
  if ( !log->Created )
  {
      if (((log->flags & 0x04) == 0) || LbFileExists(log->filename))
      {
        if (((log->flags & 0x01) != 0) && ((log->flags & 0x04) != 0))
        {
          header = CREATE;
        } else
        if (((log->flags & 0x02) != 0) && ((log->flags & 0x08) != 0))
        {
          need_initial_newline = true;
          header = APPEND;
        }
      } else
      {
        header = CREATE;
      }
  }
   const char *accmode;
    if ((log->Created) || ((log->flags & 0x01) == 0))
      accmode = "a";
    else
      accmode = "w";
    // Only load log if it's not already open
    if (file == NULL)
    {
      file = fopen(log->filename, accmode);
      // Couldn't open. Abort
      if (file == NULL)
        return -1;
      // Larger buffer for the Debug levels, which flush less often (LbLogFlush()).
      setvbuf(file, NULL, _IOFBF, 64 * 1024);
    }
    log->Created = true;
    if (header != NONE)
    {
      if ( need_initial_newline )
        fprintf(file, "\n");
      const char *actn;
      if (header == CREATE)
      {
        static const char *const log_level_names[] = {"off", "normal", "debug", "debug max"};
        fprintf(file, PRODUCT_VERSION_LABEL " -- ver " VER_STRING " (log level: %s) git:%s\n",
            log_level_names[(kfx_log_level >= LogLvl_Off && kfx_log_level <= LogLvl_DebugMax) ? kfx_log_level : LogLvl_Normal], GIT_REVISION);
        actn = "CREATED";
      } else
      {
        actn = "APPENDED";
      }
      fprintf(file, "LOG %s", actn);
      int64_t at_used = 0;
      if ((log->flags & LbLog_TimeInHeader) != 0)
      {
        struct TbTime curr_time;
        if (LbTime(&curr_time) == Lb_SUCCESS)
        {
            fprintf(file, "  @ %02" PRIu64 ":%02" PRIu64 ":%02" PRIu64,
                (uint64_t)(curr_time.Hour),(uint64_t)(curr_time.Minute),(uint64_t)(curr_time.Second));
            at_used = 1;
        }
      }
      if ((log->flags & LbLog_DateInHeader) != 0)
      {
        struct TbDate curr_date;
        if (LbDate(&curr_date) == Lb_SUCCESS)
        {
            const char *sep;
            if ( at_used )
              sep = " ";
            else
              sep = "  @ ";
            fprintf(file," %s%02" PRIu64 "-%02" PRIu64 "-%" PRIu64,sep,(uint64_t)(curr_date.Day),(uint64_t)(curr_date.Month),(uint64_t)(curr_date.Year));
        }
      }
      fprintf(file, "\n\n");
    }
    if ((log->flags & LbLog_DateInLines) != 0)
    {
        struct TbDate curr_date;
        if (LbDate(&curr_date) == Lb_SUCCESS)
        {
            fprintf(file,"%02" PRIu64 "-%02" PRIu64 "-%" PRIu64 " ",(uint64_t)(curr_date.Day),(uint64_t)(curr_date.Month),(uint64_t)(curr_date.Year));
        }
    }
    if ((log->flags & LbLog_TimeInLines) != 0)
    {
        struct TbTime curr_time;
        if (LbTime(&curr_time) == Lb_SUCCESS)
        {
            fprintf(file, "%02" PRIu64 ":%02" PRIu64 ":%02" PRIu64 " ",
                (uint64_t)(curr_time.Hour),(uint64_t)(curr_time.Minute),(uint64_t)(curr_time.Second));
        }
    }
  if (log->prefix[0] != '\0') {
      fputs(log->prefix, file);
  }

  // Write formatted message to the array
  if (!log_skip_live_view)
    write_log_to_array_for_live_viewing(fmt_str, arg, log->prefix);

  vfprintf(file, fmt_str, arg);
  log->position = ftell(file);
  // fclose is slow and automatically happens on normal program exit.
  // Opening/closing every time we log something hits performance hard.
  // fclose(file);
  // Off/Normal (low volume; bug reports rely on the last line being on disk),
  // errors and warnings, and crash reports are flushed at once. Debug and
  // above are flushed by LbLogFlush().
  if ((kfx_log_level <= LogLvl_Normal) || log_flush_next || log_forced_on)
    fflush(file);
  log_flush_next = false;
  return 1;
}

int64_t LbLogSetPrefix(struct TbLog *log, const char *prefix)
{
    if (!log->Initialised) return -1;
    snprintf(log->prefix, LOG_PREFIX_LEN, "%s", prefix);
    return 1;
}

int64_t LbLogSetPrefixFmt(struct TbLog *log, const char *format, ...)
{
    if (!log->Initialised) return -1;
    va_list val;
    va_start(val, format);
    vsnprintf(log->prefix, sizeof(log->prefix), format, val);
    va_end(val);
    return 1;
}

int64_t LbLogSetup(struct TbLog *log, const char *filename, uint64_t flags)
{
  log->Initialised = false;
  memset(log->filename, 0, DISKPATH_SIZE);
  memset(log->prefix, 0, LOG_PREFIX_LEN);
  log->Initialised=false;
  log->Created=false;
  log->Suspended=false;
  if (strlen(filename) >= DISKPATH_SIZE || strlen(filename) == 0) {
    return -1;
  }
  snprintf(log->filename, DISKPATH_SIZE, "%s", filename);
  log->flags = flags;
  log->Initialised = true;
  log->position = 0;
  return 1;
}

int64_t LbLogClose(struct TbLog *log)
{
  if ( !log->Initialised )
    return -1;
  memset(log->filename, 0, DISKPATH_SIZE);
  memset(log->prefix, 0, LOG_PREFIX_LEN);
  log->flags = 0;
  log->Initialised = false;
  log->Created = false;
  log->Suspended = false;
  log->position = 0;
  return 1;
}

struct DebugMessage * debug_messages_head = NULL;
struct DebugMessage ** debug_messages_tail = &debug_messages_head;

void make_lowercase(char * string) {
  for (char * ptr = string; *ptr != 0; ++ptr) {
    *ptr = tolower(*ptr);
  }
}

void make_uppercase(char * string) {
  for (char * ptr = string; *ptr != 0; ++ptr) {
    *ptr = toupper(*ptr);
  }
}

int64_t natoi(const char * str, int64_t len) {
  int64_t value = -1;
  for (int64_t i = 0; i < len; ++i) {
    if (!isdigit(str[i])) {
      return value;
    } else if (value < 0) {
      value = 0;
    }
    value = (value * 10) + (str[i] - '0');
  }
  return value;
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
