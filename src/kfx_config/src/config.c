/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file config.c
 *     Configuration and campaign files support.
 * @par Purpose:
 *     loading of CFG files.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     30 Jan 2009 - 11 Feb 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "config.h"
#include "port_check.h"

#include <stdarg.h>
#include <inttypes.h>
#include "globals.h"
#include "bflib_basics.h"
#include "bflib_math.h"
#include "bflib_fileio.h"
#include "bflib_dernc.h"
#include "bflib_video.h"
#include "bflib_keybrd.h"
#include "bflib_datetm.h"
#include "bflib_mouse.h"
#include "bflib_sound.h"
#include "bflib_fmvids.h"
#include "config_campaigns.h"
// script_strdup()/script_strval() (kfx_game's lvl_script_lib.h) are
// reached through SimPort instead of same-file
// bare-extern forward-declarations. See docs/refactor/todo/
// check-layering-symbol-level-blind-spot.md.
// Real usage: render_get_icon_id()/render_get_anim_id_().
// Real usage: get_string_id_by_alias().
#include "config_translation.h"
// Real usage: install_info.
#include "config_keeperfx.h"

// Real usage: effect_or_effect_element_id() -- same library, kfx_config.
#include "config_effects.h"
#include "compat_report.h"
// Real usage: kfx_sim's map_data.h COORD_PER_STL (only reachable
// transitively) -- literal-duplicated locally.
#define COORD_PER_STL 256
#include "ports/script_port.h"
#include "ports/game_port.h"
#include "ports/render_port.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

/** Line number, used when loading text files. */
uint64_t text_line_number;

// See struct SimPort and docs/refactor/stage-04-kfx-config.md
// issue A.

// See docs/refactor/todo/check-layering-symbol-level-blind-spot.md.



/******************************************************************************/
#ifdef __cplusplus
}
#endif
/******************************************************************************/

const struct NamedCommand logicval_type[] = {
  {"ENABLED",  1},
  {"DISABLED", 2},
  {"ON",       1},
  {"OFF",      2},
  {"TRUE",     1},
  {"FALSE",    2},
  {"YES",      1},
  {"NO",       2},
  {"ALWAYS",   1},
  {"NEVER",    2},
  {"1",        1},
  {"0",        2},
  {NULL,       0},
};

TbBool parameter_is_number(const char* parstr) {
    if (parstr == NULL) {
        return false;
    }

    // Trim leading spaces
    while (*parstr == ' ') {
        parstr++;
    }

    // Trim trailing spaces
    int64_t len = strlen(parstr);
    while (len > 0 && parstr[len - 1] == ' ') {
        len--;
    }

    if (len == 0) {
        return false;
    }

    // Check if the first character is a valid start for a number
    if (!(parstr[0] == '-' || isdigit((unsigned char)parstr[0]))) {
        return false;
    }

    // Check the remaining characters
    for (int64_t i = 1; i < len; ++i) {
        if (!isdigit((unsigned char)parstr[i])) {
            return false;
        }
    }

    return true;
}

int64_t get_conf_line(const char *buf, int64_t *pos, int64_t buflen, char *dst, int64_t dstlen)
{
    SYNCDBG(19,"Starting");
    if ((*pos) >= buflen) return ccr_endOfFile;
    // Skipping starting spaces
    while ((buf[*pos] == ' ') || (buf[*pos] == '\t') || (buf[*pos] == '\n') || (buf[*pos] == '\r') || (buf[*pos] == 26) || ((unsigned char)buf[*pos] < 7))
    {
        (*pos)++;
        if ((*pos) >= buflen) return ccr_endOfFile;
    }
    // Checking if this line is a comment
    if (buf[*pos] == ';')
        return ccr_comment;
    // Checking if this line is start of a block
    if (buf[*pos] == '[')
        return ccr_endOfBlock;
    int64_t i = 0;
    for (i=0; i+1 < dstlen; i++)
    {
        if ((buf[*pos]=='\r') || (buf[*pos]=='\n') || ((unsigned char)buf[*pos] < 7))
            break;
        dst[i]=buf[*pos];
        (*pos)++;
        if ((*pos) > buflen) break;
    }
    // Trim ending spaces
    for (; i>0; i--)
    {
        if ( (dst[i-1] != ' ') && (dst[i-1] != '\t') && (dst[i-1] != 26) )
            break;
    }
    dst[i]='\0';
    return i;
}


TbBool skip_conf_to_next_line(const char *buf,int64_t *pos,int64_t buflen)
{
  // Skip to end of the line
  while ((*pos) < buflen)
  {
    if ((buf[*pos]=='\r') || (buf[*pos]=='\n')) break;
    (*pos)++;
  }
  // Go to start of next line
  while ((*pos) < buflen)
  {
    if ((unsigned char)buf[*pos] > 32) break;
    if (buf[*pos]=='\n')
      text_line_number++;
    (*pos)++;
  }
  return ((*pos) < buflen);
}

TbBool skip_conf_spaces(const char *buf, int64_t *pos, int64_t buflen)
{
  while ((*pos) < buflen)
  {
    if ((buf[*pos]!=' ') && (buf[*pos]!='\t') && (buf[*pos] != 26) && ((unsigned char)buf[*pos] >= 7)) break;
    (*pos)++;
  }
  return ((*pos) < buflen);
}

/**
 * Searches for start of INI file block with given name.
 * Starts at position given with pos, and sets it to position of block data.
 * @return Returns 1 if the block is found, -1 if buffer exceeded.
 */
int64_t find_conf_block(const char *buf,int64_t *pos,int64_t buflen,const char *blockname)
{
  text_line_number = 1;
  int64_t blname_len = strlen(blockname);
  while ((*pos)+blname_len+2 < buflen)
  {
    // Skipping starting spaces
    if (!skip_conf_spaces(buf,pos,buflen))
      break;
    // Checking if this line is start of a block
    if (buf[*pos] != '[')
    {
      skip_conf_to_next_line(buf,pos,buflen);
      continue;
    }
    (*pos)++;
    // Skipping any spaces
    if (!skip_conf_spaces(buf,pos,buflen))
      break;
    if ((*pos)+blname_len+2 >= buflen)
      break;
    if (strncasecmp(&buf[*pos],blockname,blname_len) != 0)
    {
      skip_conf_to_next_line(buf,pos,buflen);
      continue;
    }
    (*pos)+=blname_len;
    // Skipping any spaces
    if (!skip_conf_spaces(buf,pos,buflen))
      break;
    if (buf[*pos] != ']')
    {
      skip_conf_to_next_line(buf,pos,buflen);
      continue;
    }
    skip_conf_to_next_line(buf,pos,buflen);
    return 1;
  }
  return -1;
}

/**
 * Reads the block name from buf, starting at pos.
 * Sets name and namelen to the block name and name length respectively.
 * Returns true on success, false when the block name is zero.
 */
TbBool conf_get_block_name(const char * buf, int64_t * pos, int64_t buflen, const char ** name, int64_t * namelen)
{
  const int64_t start = *pos;
  *name = NULL;
  *namelen = 0;
  while (true) {
    if (*pos >= buflen) {
      return false;
    } else if (isalpha((unsigned char)buf[*pos])) {
      (*pos)++;
      continue;
    } else if (isdigit((unsigned char)buf[*pos])) {
      (*pos)++;
      continue;
    } else {
      if (*pos - start > 0) {
        *name = &buf[start];
        *namelen = *pos - start;
        return true;
      } else {
        return false;
      }
    }
  }
}

/**
 * Searches for the next block in buf, starting at pos.
 * Sets name and namelen to the block name and name length respectively.
 * Returns true on success, false when no more blocks are found.
 */
TbBool iterate_conf_blocks(const char * buf, int64_t * pos, int64_t buflen, const char ** name, int64_t * namelen)
{
  text_line_number = 1;
  *name = NULL;
  *namelen = 0;
  while (true) {
    // Skip whitespace before block start
    if (!skip_conf_spaces(buf, pos, buflen)) {
      return false;
    }
    // Check if this line is start of a block
    if (*pos >= buflen) {
      return false;
    } else if (buf[*pos] != '[') {
      skip_conf_to_next_line(buf, pos, buflen);
      continue;
    }
    (*pos)++;
    // Skip whitespace before block name
    if (!skip_conf_spaces(buf, pos, buflen)) {
      return false;
    }
    // Get block name
    if (!conf_get_block_name(buf, pos, buflen, name, namelen)) {
      skip_conf_to_next_line(buf, pos, buflen);
      return false;
    }
    // Skip whitespace after block name
    if (!skip_conf_spaces(buf, pos, buflen)) {
      return false;
    } else if (buf[*pos] != ']') {
      skip_conf_to_next_line(buf, pos, buflen);
      continue;
    }
    skip_conf_to_next_line(buf,pos,buflen);
    return true;
  }
}

/**
 * Records a config key this build doesn't know in the compat report (compat_report.h):
 * content made for a newer KeeperFX, most likely. text points at the key; where is the
 * config file name, or NULL when the parser doesn't know it.
 */
static void report_unknown_config_key(const char *text, int64_t maxlen, const char *where)
{
    char key[COMPAT_WHAT_LEN];
    int64_t n = 0;
    while ((n < maxlen) && (n < (int64_t)sizeof(key) - 1) && (text[n] != '\0') && (strchr(" =\t\r\n", text[n]) == NULL))
    {
        key[n] = text[n];
        n++;
    }
    key[n] = '\0';
    compat_report_add(CompatIssue_ConfigKey, key, where, text_line_number);
}

/**
 * Recognizes config command and returns its number, or negative status code.
 * The string comparison is done by case-insensitive.
 * @param buf
 * @param pos
 * @param buflen
 * @param commands
 * @return If positive integer is returned, it is the command number recognized in the line.
 * If ccr_comment      is returned, that means the current line did not contained any command and should be skipped.
 * If ccr_endOfFile    is returned, that means we've reached end of file.
 * If ccr_unrecognised is returned, that means the command wasn't recognized.
 * If ccr_endOfBlock   is returned, that means we've reached end of the INI block.
 */
int64_t recognize_conf_command(const char *buf,int64_t *pos,int64_t buflen,const struct NamedCommand commands[])
{
    SYNCDBG(19,"Starting");
    if ((*pos) >= buflen) return ccr_endOfFile;
    // Skipping starting spaces
    while ((buf[*pos] == ' ') || (buf[*pos] == '\t') || (buf[*pos] == '\n') || (buf[*pos] == '\r') || (buf[*pos] == 26) || ((unsigned char)buf[*pos] < 7))
    {
        (*pos)++;
        if ((*pos) >= buflen) return ccr_endOfFile;
    }
    // Checking if this line is a comment
    if (buf[*pos] == ';')
        return ccr_comment;
    // Checking if this line is start of a block
    if (buf[*pos] == '[')
        return ccr_endOfBlock;
    // Finding command number
    int64_t i = 0;
    while (commands[i].num > 0)
    {
        int64_t cmdname_len = strlen(commands[i].name);
        if ((*pos)+cmdname_len > buflen) {
            i++;
            continue;
        }
        // Find a matching command
        if (strnicmp(buf+(*pos), commands[i].name, cmdname_len) == 0)
        {
            (*pos) += cmdname_len;
            // if we're not at end of input buffer..
            if ((*pos) < buflen)
            {
                // make sure it's whole command, not just start of different one
               if ((buf[(*pos)] != ' ') && (buf[(*pos)] != '\t')
                && (buf[(*pos)] != '=')  && ((unsigned char)buf[(*pos)] >= 7))
               {
                  (*pos) -= cmdname_len;
                  i++;
                  continue;
               }
               // Skipping spaces between command and parameters
               while ((buf[*pos] == ' ') || (buf[*pos] == '\t')
                || (buf[*pos] == '=')  || ((unsigned char)buf[*pos] < 7))
               {
                 (*pos)++;
                 if ((*pos) >= buflen) break;
               }
            }
            return commands[i].num;
        }
        i++;
    }
    const int64_t len = strcspn(&buf[(*pos)], " \n\r\t");
    CONFWRNLOG("Unrecognized command '%.*s'", (int)(len), &buf[(*pos)]);
    report_unknown_config_key(&buf[(*pos)], buflen - (*pos), NULL);
    return ccr_unrecognised;
}

static int64_t get_datatype_min(uchar type)
{
    switch (type)
    {
        case dt_uchar:
            return 0;
        case dt_schar:
            return SCHAR_MIN;
        case dt_char:
            return CHAR_MIN;
        case dt_short:
            return SHRT_MIN;
        case dt_ushort:
            return 0;
        case dt_int:
            return INT_MIN;
        case dt_uint:
            return 0;
        case dt_long:
            return LONG_MIN;
        case dt_ulong:
            return 0;
        case dt_longlong:
            return INT64_MIN;
        case dt_ulonglong:
            return 0;
        default:
            ERRORLOG("unexpected datatype %" PRId64, (int64_t)(type));
            break;
    }
    return 0;
}

static int64_t get_datatype_max(uchar type)
{
    switch (type)
    {
        case dt_uchar:
            return UCHAR_MAX;
        case dt_schar:
            return SCHAR_MAX;
        case dt_char:
            return CHAR_MAX;
        case dt_short:
            return SHRT_MAX;
        case dt_ushort:
            return USHRT_MAX;
        case dt_int:
            return INT_MAX;
        case dt_uint:
            return UINT_MAX;
        case dt_long:
            return LONG_MAX;
        case dt_ulong:
            return INT64_MAX; // values travel as int64_t, whose maximum is the usable range of a 64-bit unsigned field
        case dt_longlong:
            return INT64_MAX;
        case dt_ulonglong:
            return INT64_MAX;
        default:
            break;
    }
    return 0;
}

//if the parameter is a number return the number, if a value in the provided NamedCommand list return the value
int64_t value_default(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    if (parameter_is_number(value_text))
    {
        int64_t value = atoll(value_text);
        int64_t minimum = max(named_field->min, get_datatype_min(named_field->type));
        int64_t maximum = min(named_field->max, get_datatype_max(named_field->type));
        if( value < minimum)
        {
            NAMFIELDWRNLOG("field '%s' smaller than min value '%" PRId64 "', was '%" PRId64 "'",named_field->name,(int64_t)(minimum),(int64_t)(value));
            value = minimum;
        }
        else if( value > maximum)
        {
            NAMFIELDWRNLOG("field '%s' greater than max value '%" PRId64 "', was '%" PRId64 "'",named_field->name,(int64_t)(maximum),(int64_t)(value));
            value = maximum;
        }
        return value;

    }
    else if(named_field->namedCommand != NULL)
    {
        int64_t value = get_id(named_field->namedCommand, value_text);
        if(value >= 0)
        {
            return value;
        }
        NAMFIELDWRNLOG("Unrecognized parameter for field '%s', got '%s'",named_field->name,value_text);
        compat_report_add_value(named_field->name, value_text, src_str, text_line_number, named_field->namedCommand);
    }
    else
    {
        NAMFIELDWRNLOG("Expected number for field '%s', got '%s'",named_field->name,value_text);
    }
    return 0;
}

int64_t value_name(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    void* field_ptr = (char*)named_fields_set->get_struct_base() + named_fields_set->struct_size * idx + (ptrdiff_t)named_field->field;
    strncpy(field_ptr, value_text, COMMAND_WORD_LEN - 1);
    ((char*)field_ptr)[COMMAND_WORD_LEN - 1] = '\0';
    return 0;
}

//same as value_flagsfield but treats the namedCommand field as a longnamedCommand
int64_t value_longflagsfield(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    int64_t value = 0;
    char word_buf[COMMAND_WORD_LEN];
    if (parameter_is_number(value_text))
    {
        return atoll(value_text);
    }
    if(strcasecmp(value_text,"none") == 0)
    {
        return 0;
    }

    int64_t pos = 0;
    int64_t len = strlen(value_text);
    int64_t i = 0;
    while (get_conf_parameter_single(value_text,&pos,len,word_buf,sizeof(word_buf)) > 0)
    {
        if (i == 1)
        {
            //if the second value is 0 or 1, treat it as a flag toggle
            if(strcmp(word_buf, "0") == 0 || strcmp(word_buf, "1") == 0)
            {
                int64_t original_value = get_named_field_value(named_field, named_fields_set, idx);
                set_flag_value(original_value,value, atoi(word_buf));
                return original_value;
            }
        }

        int64_t k = get_long_id((struct LongNamedCommand*)named_field->namedCommand, word_buf);
        if(k >= 0)
            value |= k;
        else
        {
            NAMFIELDWRNLOG("Unexpected value for field '%s', got '%s'",named_field->name,word_buf);
            compat_report_add_long_value(named_field->name, word_buf, src_str, text_line_number,
                (const struct LongNamedCommand*)named_field->namedCommand);
        }
        i++;
    }
    return value;
}


//expects value_text to be a space seperated list of values in the named fields named command, wich can be combined with bitwise or
int64_t value_flagsfield(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    int64_t value = 0;
    char word_buf[COMMAND_WORD_LEN];
    if (parameter_is_number(value_text))
    {
        return atoll(value_text);
    }
    if(strcasecmp(value_text,"none") == 0)
    {
        return 0;
    }

    int64_t pos = 0;
    int64_t len = strlen(value_text);
    int64_t i = 0;
    while (get_conf_parameter_single(value_text,&pos,len,word_buf,sizeof(word_buf)) > 0)
    {
        if (i == 1)
        {
            //if the second value is 0 or 1, treat it as a flag toggle
            if(strcmp(word_buf, "0") == 0 || strcmp(word_buf, "1") == 0)
            {
                int64_t original_value = get_named_field_value(named_field, named_fields_set, idx);
                set_flag_value(original_value,value, atoi(word_buf));
                return original_value;
            }
        }

        int64_t k = get_id(named_field->namedCommand, word_buf);
        if(k >= 0)
            value |= k;
        else
        {
            NAMFIELDWRNLOG("Unexpected value for field '%s', got '%s'",named_field->name,word_buf);
            compat_report_add_value(named_field->name, word_buf, src_str, text_line_number, named_field->namedCommand);
        }
        i++;
    }
    return value;
}

int64_t value_icon(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    if (flag_is_set(flags,ccf_SplitExecution))
    {
        int64_t script_string_offset = game_script_strdup(value_text);
        if (script_string_offset < 0)
        {
            NAMFIELDWRNLOG("Run out script strings space");
            return -1;
        }
        return script_string_offset;
    }
    else
    {
        return render_get_icon_id(value_text);
    }
}

int64_t value_animid(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
  if (flag_is_set(flags,ccf_SplitExecution))
  {
      int64_t script_string_offset = game_script_strdup(value_text);
      if (script_string_offset < 0)
      {
          NAMFIELDWRNLOG("Run out script strings space");
          return -1;
      }
      return script_string_offset;
  }
  else
  {
      return render_get_anim_id_(value_text);
  }
}

int64_t value_stringId(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    return get_string_id_by_alias(value_text);
}

int64_t value_effOrEffEl(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    return effect_or_effect_element_id(value_text);
}

int64_t value_function(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    return script_get_lua_function_idx(value_text, named_field->namedCommand);
}

void assign_icon(const struct NamedField* named_field, int64_t value, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    if (flag_is_set(flags,ccf_SplitExecution))
    {
        int64_t icon_id = render_get_icon_id(game_script_strval(value));
        assign_default(named_field,icon_id,named_fields_set,idx,src_str,flags);
    }
    else
    {
        assign_default(named_field,value,named_fields_set,idx,src_str,flags);
    }
}

void assign_animid(const struct NamedField* named_field, int64_t value, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    if (flag_is_set(flags,ccf_SplitExecution))
    {
        int64_t anim_id = render_get_anim_id_(game_script_strval(value));
        assign_default(named_field,anim_id,named_fields_set,idx,src_str,flags);
    }
    else
    {
        assign_default(named_field,value,named_fields_set,idx,src_str,flags);
    }
}

int64_t value_transpflg(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{

    if (parameter_is_number(value_text))
    {
        return atoll(value_text) << 4;
    }
    else
    {
        NAMFIELDWRNLOG("Expected number for field '%s', got '%s'",named_field->name,value_text);
    }
    return 0;
}

int64_t value_stltocoord(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{

    if (parameter_is_number(value_text))
    {
        return atoll(value_text) * COORD_PER_STL;
    }
    else
    {
        NAMFIELDWRNLOG("Expected number for field '%s', got '%s'",named_field->name,value_text);
    }
    return 0;
}

/******************************************************************************/
// Parse functions that keep the hand-written block parsers' rules, for the
// blocks refactor pass 3 (S04) moved onto NamedField tables. They are more
// lenient than value_default: numbers go through atoi() with no range check
// and are stored with C's conversion (README finding F10), and a name that
// isn't found leaves the field as it is instead of setting 0. Switching a row
// to value_default/assign_default is the fix for F10.

/** atoi(): no range check; text reads as 0, "12abc" as 12. */
int64_t value_atoi(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    return atoi(value_text);
}

/** atoi(); a negative value leaves the field as it is. */
int64_t value_atoi_nonneg(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    const int64_t value = atoi(value_text);
    if (value < 0)
    {
        NAMFIELDWRNLOG("Incorrect value of field '%s', got '%s'", named_field->name, value_text);
        return NAMFIELD_KEEP;
    }
    return value;
}

/** atoi(); a value outside the row's min..max leaves the field as it is. */
int64_t value_atoi_in_bounds(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    const int64_t value = atoi(value_text);
    if ((value < named_field->min) || (value > named_field->max))
    {
        NAMFIELDWRNLOG("Incorrect value of field '%s', got '%s'", named_field->name, value_text);
        return NAMFIELD_KEEP;
    }
    return value;
}

/** A name from the field's list (get_id()); an unknown one leaves the field as it is. */
int64_t value_id_nonneg(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    const int64_t value = get_id(named_field->namedCommand, value_text);
    if (value < 0)
    {
        NAMFIELDWRNLOG("Incorrect value of field '%s', got '%s'", named_field->name, value_text);
        return NAMFIELD_KEEP;
    }
    return value;
}

/** As value_id_nonneg, but entry 0 (usually "NULL") also leaves the field as it is. */
int64_t value_id_positive(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    const int64_t value = get_id(named_field->namedCommand, value_text);
    if (value <= 0)
    {
        if (strcasecmp(value_text, "NULL") != 0)
            NAMFIELDWRNLOG("Incorrect value of field '%s', got '%s'", named_field->name, value_text);
        return NAMFIELD_KEEP;
    }
    return value;
}

/** A known key whose value is ignored (kept so the key isn't reported as unknown). */
int64_t value_ignored(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    return NAMFIELD_KEEP;
}

/**
 * For argnum -1 rows: the whole line is a list of names from the field's list,
 * OR'ed together (flags). Starts from 0, so an empty line clears the field.
 * Unknown names, and names whose value is 0, are skipped with a warning.
 */
int64_t value_ids_or(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    char word_buf[COMMAND_WORD_LEN];
    int64_t value = 0;
    int64_t pos = 0;
    const int64_t len = (int64_t)strlen(value_text);
    while (get_conf_parameter_single(value_text, &pos, len, word_buf, sizeof(word_buf)) > 0)
    {
        const int64_t k = get_id(named_field->namedCommand, word_buf);
        if (k > 0)
            value |= k;
        else
            NAMFIELDWRNLOG("Incorrect value of field '%s', got '%s'", named_field->name, word_buf);
    }
    return value;
}

/** A string alias or number (get_string_id_by_alias()); not found (or 0) leaves the field as it is. */
int64_t value_string_id_positive(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    const int64_t k = get_string_id_by_alias(value_text);
    if (k <= 0)
    {
        CONFWRNLOG("Incorrect value of \"%s\" parameter in %s file.", named_field->name, src_str);
        return NAMFIELD_KEEP;
    }
    return k;
}

/**
 * For -2 rows over a char[COMMAND_WORD_LEN] field: the line's first word, read
 * as from the file (an empty value clears the field).
 */
int64_t value_word(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    char* field = (char*)named_fields_set->get_struct_base() + named_fields_set->struct_size * idx + (ptrdiff_t)named_field->field;
    int64_t pos = 0;
    if (get_conf_parameter_single(value_text, &pos, (int64_t)strlen(value_text), field, COMMAND_WORD_LEN) <= 0)
    {
        NAMFIELDWRNLOG("Couldn't read field '%s'", named_field->name);
    }
    return NAMFIELD_KEEP;
}

/** Stores the value with C's conversion to the field's type; NAMFIELD_KEEP stores nothing. */
void assign_cast(const struct NamedField* named_field, int64_t value, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    if (value == NAMFIELD_KEEP)
        return;
    char* base = (char*)named_fields_set->get_struct_base();
    if ((base == NULL) || (idx < 0) || (idx >= named_fields_set->max_count))
    {
        NAMFIELDERRLOG("Field '%s' index %" PRId64 " out of bounds", named_field->name, (int64_t)(idx));
        return;
    }
    char* field = base + named_fields_set->struct_size * idx + (ptrdiff_t)named_field->field;
    switch (named_field->type)
    {
    case dt_uchar:     { unsigned char v = (unsigned char)value; memcpy(field, &v, sizeof(v)); break; }
    case dt_schar:     { signed char v = (signed char)value; memcpy(field, &v, sizeof(v)); break; }
    case dt_char:      { char v = (char)value; memcpy(field, &v, sizeof(v)); break; }
    case dt_short:     { short v = (short)value; memcpy(field, &v, sizeof(v)); break; }
    case dt_ushort:    { unsigned short v = (unsigned short)value; memcpy(field, &v, sizeof(v)); break; }
    case dt_int:       { int v = (int)value; memcpy(field, &v, sizeof(v)); break; }
    case dt_uint:      { unsigned int v = (unsigned int)value; memcpy(field, &v, sizeof(v)); break; }
    case dt_long:      { long v = (long)value; memcpy(field, &v, sizeof(v)); break; }
    case dt_ulong:     { unsigned long v = (unsigned long)value; memcpy(field, &v, sizeof(v)); break; }
    case dt_longlong:  { long long v = (long long)value; memcpy(field, &v, sizeof(v)); break; }
    case dt_ulonglong: { unsigned long long v = (unsigned long long)value; memcpy(field, &v, sizeof(v)); break; }
    default:
        assign_default(named_field, value, named_fields_set, idx, src_str, flags);
        break;
    }
}

int64_t parse_named_field_value(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    if (named_field->parse_func != NULL)
      return named_field->parse_func(named_field,value_text,named_fields_set,idx,src_str,flags);
    else
        NAMFIELDWRNLOG("No parse_func for field %s",named_field->name);
    return 0;
}

int64_t get_named_field_value(const struct NamedField* named_field, const struct NamedFieldSet* named_fields_set, int64_t idx)
{
    void* field = (char*)named_fields_set->get_struct_base() + named_fields_set->struct_size * idx + (ptrdiff_t)named_field->field;
    switch (named_field->type)
    {
    case dt_uchar:
        return *(unsigned char*)field;
    case dt_schar:
        return *(signed char*)field;
    case dt_char:
        return *(char*)field;
    case dt_short:
        return *(signed short*)field;
    case dt_ushort:
        return *(unsigned short*)field;
    case dt_int:
        return *(signed int*)field;
    case dt_uint:
        return *(unsigned int*)field;
    case dt_long:
        return *(signed long*)field;
    case dt_ulong:
        return *(unsigned long*)field;
    case dt_longlong: {
        /* Use memcpy to avoid ARM LDRD strict-alignment fault. */
        int64_t v; memcpy(&v, field, sizeof(v)); return v;
    }
    case dt_ulonglong: {
        uint64_t v; memcpy(&v, field, sizeof(v)); return (int64_t)v;
    }
    case dt_float:
        return (int64_t)(*(float*)field);
    case dt_double: {
        double v; memcpy(&v, field, sizeof(v)); return (int64_t)v;
    }
    case dt_longdouble: {
        long double v; memcpy(&v, field, sizeof(v)); return (int64_t)v;
    }
    case dt_charptr:
    case dt_default:
    case dt_void:
    default:
        ERRORLOG("unexpected datatype for field '%s', '%" PRId64 "'", named_field->name, (int64_t)(named_field->type));
        return -1;
    }
}

//for fields that are fully handled in the parse function
void assign_null(const struct NamedField* named_field, int64_t value, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
}

void assign_default(const struct NamedField* named_field, int64_t value, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    char* field = (char*)named_fields_set->get_struct_base() + named_fields_set->struct_size * idx + (ptrdiff_t)named_field->field;
    char* base = (char*)named_fields_set->get_struct_base();

    if (named_fields_set->get_struct_base() == NULL || idx < 0 || idx >= named_fields_set->max_count || field < base ||
        field >= base + named_fields_set->struct_size * named_fields_set->max_count)
    {
        NAMFIELDERRLOG("Field '%s' index %" PRId64 " out of bounds", named_field->name, (int64_t)(idx));
        return;
    }
    switch (named_field->type)
    {
    case dt_uchar:
        if (value < 0 || value > UCHAR_MAX)
            NAMFIELDWRNLOG("Value out of range for unsigned char: %" PRId64, (int64_t)(value));
        else
            *(unsigned char*)field = (unsigned char)value;
        break;
    case dt_schar:
            *(signed char*)field = (signed char)value;
        break;
    case dt_char:
        if (value < CHAR_MIN || value > CHAR_MAX)
            NAMFIELDWRNLOG("Value out of range for char: %" PRId64, (int64_t)(value));
        else
            *(char*)field = (char)value;
        break;
    case dt_short:
        if (value < SHRT_MIN || value > SHRT_MAX)
            NAMFIELDWRNLOG("Value out of range for signed short: %" PRId64, (int64_t)(value));
        else
            *(signed short*)field = (signed short)value;
        break;
    case dt_ushort:
        if (value < 0 || value > USHRT_MAX)
            NAMFIELDWRNLOG("Value out of range for unsigned short: %" PRId64, (int64_t)(value));
        else
            *(unsigned short*)field = (unsigned short)value;
        break;
    case dt_int:
        if (value < INT_MIN || value > INT_MAX)
            NAMFIELDWRNLOG("Value out of range for signed int: %" PRId64, (int64_t)(value));
        else
            *(signed int*)field = (signed int)value;
        break;
    case dt_uint:
        if (value < 0 || value > UINT_MAX)
            NAMFIELDWRNLOG("Value out of range for unsigned int: %" PRId64, (int64_t)(value));
        else
            *(unsigned int*)field = (unsigned int)value;
        break;
    case dt_long:
        if (value < LONG_MIN || value > LONG_MAX)
            NAMFIELDWRNLOG("Value out of range for signed long: %" PRId64, (int64_t)(value));
        else
            *(signed long*)field = (signed long)value;
        break;
    case dt_ulong:
        if (value < 0 || value > ULONG_MAX)
            NAMFIELDWRNLOG("Value out of range for unsigned long: %" PRId64, (int64_t)(value));
        else
            *(unsigned long*)field = (unsigned long)value;
        break;
    case dt_longlong:
        if (value < INT64_MIN || value > INT64_MAX)
            NAMFIELDWRNLOG("Value out of range for signed long long: %" PRId64, (int64_t)(value));
        else {
            /* Use memcpy to avoid ARM STRD strict-alignment fault (ARMv7 STRD requires
             * 8-byte aligned address; struct base may only be 4-byte aligned). */
            signed long long v = (signed long long)value;
            memcpy(field, &v, sizeof(v));
        }
        break;
    case dt_ulonglong:
        if (value < 0)
            NAMFIELDWRNLOG("Value out of range for unsigned long long: %" PRId64, (int64_t)(value));
        else {
            unsigned long long v = (unsigned long long)value;
            memcpy(field, &v, sizeof(v));
        }
        break;
    case dt_float:
        *(float*)field = (double)value;
        break;
    case dt_double: {
        double v = (double)value;
        memcpy(field, &v, sizeof(v));
        break;
    }
    case dt_longdouble: {
        long double v = (long double)value;
        memcpy(field, &v, sizeof(v));
        break;
    }
    case dt_charptr:
    case dt_default:
    case dt_void:
    default:
        NAMFIELDWRNLOG("unexpected datatype for field '%s', '%" PRId64 "'", named_field->name, (int64_t)(named_field->type));
        break;
    }
}

void assign_named_field_value(const struct NamedField* named_field, int64_t value, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags)
{
    if(named_field->assign_func == NULL)
    {
        ERRORLOG("No assign_func for field %s",named_field->name);
        assign_default(named_field,value,named_fields_set,idx,src_str,flags);
    }
    else
    {
        named_field->assign_func(named_field,value,named_fields_set,idx,src_str,flags);
    }
}

/**
 * Recognizes config command and returns its number, or negative status code.
 * @param buf
 * @param pos
 * @param buflen
 * @param commands
 * @return If ccr_ok is returned the field has been correctly assigned
 * If ccr_comment      is returned, that means the current line did not contained any command and should be skipped.
 * If ccr_endOfFile    is returned, that means we've reached end of file.
 * If ccr_unrecognised is returned, that means the command wasn't recognized.
 * If ccr_endOfBlock   is returned, that means we've reached end of the INI block.
 * If ccr_error        is returned, that means something went wrong.
 */

int64_t assign_conf_command_field(const char *buf,int64_t *pos,int64_t buflen,const struct NamedField commands[], const struct NamedFieldSet* named_fields_set, int64_t idx, int64_t flags, const char *config_textname)
{
    SYNCDBG(19,"Starting");
    if ((*pos) >= buflen) return -1;
    // Skipping starting spaces
    while ((buf[*pos] == ' ') || (buf[*pos] == '\t') || (buf[*pos] == '\n') || (buf[*pos] == '\r') || (buf[*pos] == 26) || ((unsigned char)buf[*pos] < 7))
    {
        (*pos)++;
        if ((*pos) >= buflen) return -1;
    }
    // Checking if this line is a comment
    if (buf[*pos] == ';')
        return ccr_comment;
    // Checking if this line is start of a block
    if (buf[*pos] == '[')
        return ccr_endOfBlock;
    // Finding command number
    int64_t i = 0;
    while (commands[i].name != NULL)
    {
        const TbBool list_skips = flag_is_set(flags,CnfLd_ListOnly) && strcasecmp(commands[i].name,"Name") != 0;
        if (list_skips && !flag_is_set(flags,CnfLd_ListKnownKeys))
        {
            i++;
            continue;
        }

        if (commands[i].argnum > 0)
        {
            i++;
            continue;
        }

        int64_t cmdname_len = strlen(commands[i].name);
        if ((*pos)+cmdname_len > buflen) {
            i++;
            continue;
        }
        // Find a matching command
        if (strnicmp(buf+(*pos), commands[i].name, cmdname_len) == 0)
        {
            (*pos) += cmdname_len;
            // if we're not at end of input buffer..
            if ((*pos) < buflen)
            {
                // make sure it's whole command, not just start of different one
               if ((buf[(*pos)] != ' ') && (buf[(*pos)] != '\t')
                && (buf[(*pos)] != '=')  && ((unsigned char)buf[(*pos)] >= 7))
               {
                  (*pos) -= cmdname_len;
                  i++;
                  continue;
               }
               // Skipping spaces between command and parameters
               while ((buf[*pos] == ' ') || (buf[*pos] == '\t')
                || (buf[*pos] == '=')  || ((unsigned char)buf[*pos] < 7))
               {
                 (*pos)++;
                 if ((*pos) >= buflen) break;
               }
            }


            if (list_skips)
                return ccr_ok; // CnfLd_ListKnownKeys: a known key, not read in the list pass
            int64_t k = 0;
            if (commands[i].argnum == NAMFIELD_WHOLE_LINE_UNLIMITED)
            {
                // As -1, without its line length limit: for lists read to the end of the line.
                // The text keeps the line's end character (if the file has one there), so a parse
                // function reading it with get_conf_parameter_single() sees what it would in the file.
                int64_t line_len = 0;
                while ((*pos) + line_len < buflen && buf[(*pos) + line_len] != '\n' && buf[(*pos) + line_len] != '\r')
                    line_len++;
                const int64_t text_len = ((*pos) + line_len < buflen) ? line_len + 1 : line_len;
                char* line_buf = (char*)KfxCalloc((size_t)text_len + 1, 1);
                if (line_buf == NULL)
                {
                    ERRORLOG("Can't allocate %" PRId64 " bytes for a conf line", (int64_t)(text_len + 1));
                    return ccr_error;
                }
                memcpy(line_buf, buf + (*pos), (size_t)text_len);
                line_buf[text_len] = '\0';
                (*pos) += line_len;
                k = parse_named_field_value(&commands[i], line_buf,named_fields_set,idx,config_textname,ccf_None);
                assign_named_field_value(&commands[i],k,named_fields_set,idx,config_textname,ccf_None);
                KfxFree(line_buf);
            }
            else if (commands[i].argnum == -1)
            {
                #define MAX_LINE_LEN 1024
                char line_buf[MAX_LINE_LEN];
                int64_t line_len = 0;

                // Copy characters until newline or end of buffer
                while ((*pos) + line_len < buflen &&
                      buf[(*pos) + line_len] != '\n' &&
                      buf[(*pos) + line_len] != '\r')
                {
                    line_buf[line_len] = buf[(*pos) + line_len];
                    line_len++;

                    // Prevent buffer overflow
                    if (line_len >= MAX_LINE_LEN - 1)
                    {
                      ERRORLOG("preventing overflow for long conf line");
                      break;
                    }
                }

                #undef MAX_LINE_LEN
                line_buf[line_len] = '\0'; // Null-terminate the string

                // Move position to the next line
                (*pos) += line_len;

                // Pass extracted string
              k = parse_named_field_value(&commands[i], line_buf,named_fields_set,idx,config_textname,ccf_None);
              assign_named_field_value(&commands[i],k,named_fields_set,idx,config_textname,ccf_None);
            }
            else
            {
                char word_buf[COMMAND_WORD_LEN];
                uchar n = 0;
                while (get_conf_parameter_single(buf,pos,buflen,word_buf,sizeof(word_buf)) > 0)
                {
                    if ((commands[i + n].name == NULL) || (strcmp(commands[i + n].name, commands[i].name) != 0)) // past the table's last row: README F11
                    {
                        CONFWRNLOG("more params than expected for command '%s' '%s'",commands[i].name, word_buf);
                    }
                    else
                    {
                        k = parse_named_field_value(&commands[i + n],word_buf,named_fields_set,idx,config_textname,ccf_None);
                        assign_named_field_value(&commands[i + n],k,named_fields_set,idx,config_textname,ccf_None);
                        n++;
                    }
                }
            }
            return ccr_ok;
        }
        i++;
    }
    // A list-only pass reads just the Name fields; every other key is expected to fall through here.
    if (!flag_is_set(flags, CnfLd_ListOnly) || flag_is_set(flags, CnfLd_ListKnownKeys))
    {
        const int64_t len = (int64_t)strcspn(&buf[(*pos)], " =\n\r\t");
        CONFWRNLOG("Unrecognized field '%.*s' in %s", (int)(len), &buf[(*pos)], config_textname);
        report_unknown_config_key(&buf[(*pos)], buflen - (*pos), config_textname);
    }
    return ccr_unrecognised;
}

TbBool parse_named_field_block(const char *buf, int64_t len, const char *config_textname, int64_t flags,const char* blockname,
                         const struct NamedField named_field[], const struct NamedFieldSet* named_fields_set, int64_t idx)
{
    int64_t pos = 0;
    int64_t k = find_conf_block(buf, &pos, len, blockname);
    if (k < 0)
    {
        if ((flags & CnfLd_AcceptPartial) == 0)
            WARNMSG("Block [%s] not found in %s file.",blockname,config_textname);
        return false;
    }

    parse_named_field_block_lines(buf, &pos, len, config_textname, flags, named_field, named_fields_set, idx);
    return true;
}

void parse_named_field_block_lines(const char *buf, int64_t *pos, int64_t len, const char *config_textname, int64_t flags,
                         const struct NamedField named_field[], const struct NamedFieldSet* named_fields_set, int64_t idx)
{
    while (*pos<len)
    {
        // Finding command number in this line.
        int64_t assignresult = assign_conf_command_field(buf, pos, len, named_field,named_fields_set,idx,flags,config_textname);
        if( assignresult == ccr_ok || assignresult == ccr_comment )
        {
            skip_conf_to_next_line(buf,pos,len);
            continue;
        }
        else if( assignresult == ccr_unrecognised)
        {
            skip_conf_to_next_line(buf,pos,len);
            continue;
        }
        else if( assignresult == ccr_endOfBlock || assignresult == ccr_error || assignresult == ccr_endOfFile)
        {
            break;
        }
    }
}

void set_defaults(const struct NamedFieldSet* named_fields_set, const char *config_textname)
{
  memset(named_fields_set->get_struct_base(), 0, named_fields_set->struct_size * named_fields_set->max_count);
  *named_fields_set->get_count() = 0;

  const struct NamedField* name_NamedField = NULL;

  for (int64_t i = 0; named_fields_set->named_fields[i].name != NULL; i++)
  {
      if (named_fields_set->named_fields[i].default_value != 0)
      {
          for (int64_t j = 0; j < named_fields_set->max_count; j++)
          {
              assign_default(&named_fields_set->named_fields[i], named_fields_set->named_fields[i].default_value, named_fields_set, j, config_textname, ccf_None);
          }
      }

      if(strcmp(named_fields_set->named_fields[i].name, "NAME") == 0)
      {
          name_NamedField = &named_fields_set->named_fields[i];
      }

  }

  if (name_NamedField != NULL && named_fields_set->names != NULL)
  {
      for (int64_t i = 0; i < named_fields_set->max_count; i++)
      {
          named_fields_set->names[i].name = (char*)named_fields_set->get_struct_base() + i * named_fields_set->struct_size + (ptrdiff_t)name_NamedField->field;
          named_fields_set->names[i].num = i;
      }
      named_fields_set->names[named_fields_set->max_count - 1].name = NULL; // must be null for get_id
  }
}


TbBool parse_named_field_blocks(char *buf, int64_t len, const char *config_textname, int64_t flags,
                               const struct NamedFieldSet* named_fields_set)
{
    int64_t pos = 0;
    // Initialize the array
    if ((flags & (CnfLd_AcceptPartial|CnfLd_PreListed)) == 0)
    {
        set_defaults(named_fields_set,config_textname);
    }

    const char * blockname = NULL;
    int64_t blocknamelen = 0;
    const int64_t basename_len = strlen(named_fields_set->block_basename);
    while (iterate_conf_blocks(buf, &pos, len, &blockname, &blocknamelen))
    {
        // look for blocks starting with block_basename, followed by one or more digits
        if (blocknamelen < basename_len + 1) {
            continue;
        } else if (memcmp(blockname, named_fields_set->block_basename, basename_len) != 0) {
            continue;
        }
        const int64_t i = natoi(&blockname[basename_len], blocknamelen - basename_len);
        if (i >= named_fields_set->max_count) {
            // Content defining more of these than this build has room for (a newer
            // KeeperFX's bigger table, most likely): skipped, so say so.
            char what[COMPAT_WHAT_LEN];
            snprintf(what, sizeof(what), "[%.*s] (room for %" PRId64 ")", (int)blocknamelen, blockname, (int64_t)named_fields_set->max_count);
            WARNLOG("%s: block %s is beyond this build's limit, skipped", config_textname, what);
            compat_report_add(CompatIssue_Limit, what, config_textname, 0);
            continue;
        }
        if (i < 0) {
            continue;
        } else if (i >= *named_fields_set->get_count()) {
            *named_fields_set->get_count() = i + 1;
        }
        char blockname_null[COMMAND_WORD_LEN];
        strncpy(blockname_null, blockname, blocknamelen);
        blockname_null[blocknamelen] = '\0';

        parse_named_field_block(buf, len, config_textname, flags, blockname_null, named_fields_set->named_fields, named_fields_set, i);
    }

    return true;
}

int64_t get_conf_parameter_whole(const char *buf,int64_t *pos,int64_t buflen,char *dst,int64_t dstlen)
{
  int64_t i;
  if ((*pos) >= buflen) return 0;
  // Skipping spaces after previous parameter
  while ((buf[*pos] == ' ') || (buf[*pos] == '\t'))
  {
    (*pos)++;
    if ((*pos) >= buflen) return 0;
  }
  for (i=0; i+1 < dstlen; i++)
  {
    if ((buf[*pos]=='\r') || (buf[*pos]=='\n') || ((unsigned char)buf[*pos] < 7))
      break;
    dst[i]=buf[*pos];
    (*pos)++;
    if ((*pos) > buflen) break;
  }
  dst[i]='\0';
  return i;
}

int64_t get_conf_parameter_single(const char *buf,int64_t *pos,int64_t buflen,char *dst,int64_t dstlen)
{
    int64_t i;
    if ((*pos) >= buflen) return 0;
    // Skipping spaces after previous parameter
    while ((buf[*pos] == ' ') || (buf[*pos] == '\t'))
    {
        (*pos)++;
        if ((*pos) >= buflen) return 0;
    }
    for (i=0; i+1 < dstlen; i++)
    {
        if ((buf[*pos] == ' ') || (buf[*pos] == '\t') || (buf[*pos] == '\r')
         || (buf[*pos] == '\n') || ((unsigned char)buf[*pos] < 7))
          break;
        dst[i]=buf[*pos];
        (*pos)++;
        if ((*pos) >= buflen) {
            i++;
            break;
        }
    }
    dst[i]='\0';
    return i;
}

/**
 * Returns parameter num from given NamedCommand array, or 0 if not found.
 */
int64_t recognize_conf_parameter(const char *buf,int64_t *pos,int64_t buflen,const struct NamedCommand commands[])
{
  if ((*pos) >= buflen) return 0;
  // Skipping spaces after previous parameter
  while ((buf[*pos] == ' ') || (buf[*pos] == '\t'))
  {
    (*pos)++;
    if ((*pos) >= buflen) return 0;
  }
  int64_t i = 0;
  while (commands[i].name != NULL)
  {
      int64_t par_len = strlen(commands[i].name);
      if (strncasecmp(&buf[(*pos)], commands[i].name, par_len) == 0)
      {
          // If EOLN found, finish and return position before the EOLN
          if ((buf[(*pos)+par_len] == '\n') || (buf[(*pos)+par_len] == '\r'))
          {
            (*pos) += par_len;
            return commands[i].num;
          }
          // If non-EOLN blank char, finish and return position after the char
          if ((buf[(*pos)+par_len] == ' ') || (buf[(*pos)+par_len] == '\t')
           || ((unsigned char)buf[(*pos)+par_len] < 7))
          {
            (*pos) += par_len+1;
            return commands[i].num;
          }
      }
      i++;
  }
  return 0;
}

/**
 * Returns name of a config parameter with given number, or empty string.
 */
const char *get_conf_parameter_text(const struct NamedCommand commands[],int64_t num)
{
    int64_t i = 0;
    while (commands[i].name != NULL)
    {
        //SYNCLOG("\"%s\", %d %d",commands[i].name,(int64_t)(commands[i].num),(int64_t)(num));
        if (commands[i].num == num)
            return commands[i].name;
        i++;
  }
  return "";
}

/**
 * Returns ID of given item using NamedField list.
 * If not found, returns -1.
 */
int64_t get_named_field_id(const struct NamedField *desc, const char *itmname)
{
  if ((desc == NULL) || (itmname == NULL))
    return -1;
  for (int64_t i = 0; desc[i].name != NULL; i++)
  {
    if (strcasecmp(desc[i].name, itmname) == 0)
      return i;
  }
  return -1;
}

/**
 * Returns ID of given item using NamedCommands list.
 * Similar to recognize_conf_parameter(), but for use only if the buffer stores
 * one word, ended with "\0".
 * If not found, returns -1.
 */
int64_t get_id(const struct NamedCommand *desc, const char *itmname)
{
  if ((desc == NULL) || (itmname == NULL))
    return -1;
  for (int64_t i = 0; desc[i].name != NULL; i++)
  {
    if (strcasecmp(desc[i].name, itmname) == 0)
      return desc[i].num;
  }
  return -1;
}

/**
 * Returns ID of given item using NamedCommands list.
 * Similar to recognize_conf_parameter(), but for use only if the buffer stores
 * one word, ended with "\0".
 * If not found, returns -1.
 */
long long get_long_id(const struct LongNamedCommand* desc, const char* itmname)
{
    if ((desc == NULL) || (itmname == NULL))
        return -1;
    for (int64_t i = 0; desc[i].name != NULL; i++)
    {
        if (strcasecmp(desc[i].name, itmname) == 0)
            return desc[i].num;
    }
    return -1;
}

// get_rid() moved to kfx_platform's bflib_basics.c (see
// docs/refactor/todo/check-layering-symbol-level-blind-spot.md) --
// declared in bflib_basics.h, included above via config.h.

char *prepare_file_path_buf(char *dst, int64_t dst_size, int64_t fgroup, const char *fname)
{
    return prepare_file_path_buf_mod(dst, dst_size, NULL, fgroup, fname);
}

/*
 * @mod_dir insert before fgroup related sdir, set NULL if no mod.
 * @fname insert after fgroup related sdir.
 */
/**
 * Internal helper: Resolve a file path with bounds checking to prevent buffer overflow.
 * Returns NULL if the constructed path would exceed dst_size.
 */
static char *_resolve_file_path_internal(char *dst, size_t dst_size, 
                                          const char *mod_dir, int64_t fgroup, 
                                          const char *fname)
{
  const char *mdir = NULL;
  const char *sdir = NULL;
  switch (fgroup)
  {
  case FGrp_StdData:
      mdir=keeper_runtime_directory;
      sdir="data";
      break;
  case FGrp_LrgData:
      mdir=keeper_runtime_directory;
      sdir="data";
      break;
  case FGrp_FxData:
      mdir=keeper_runtime_directory;
      sdir="fxdata";
      break;
  case FGrp_LoData:
      mdir=install_info.inst_path[0] ? install_info.inst_path : keeper_runtime_directory;
      sdir="ldata";
      break;
  case FGrp_HiData:
      mdir=keeper_runtime_directory;
      sdir="hdata";
      break;
  case FGrp_Music:
      mdir=keeper_runtime_directory;
      sdir="music";
      break;
  case FGrp_VarLevels:
      mdir=install_info.inst_path[0] ? install_info.inst_path : keeper_runtime_directory;
      sdir="levels";
      break;
  case FGrp_Save:
      mdir=keeper_runtime_directory;
      sdir="save";
      break;
  case FGrp_SShots:
      mdir=keeper_runtime_directory;
      sdir="scrshots";
      break;
  case FGrp_StdSound:
      mdir=keeper_runtime_directory;
      sdir="sound";
      break;
  case FGrp_LrgSound:
      mdir=keeper_runtime_directory;
      sdir="sound";
      break;
  case FGrp_AtlSound:
      if (campaign.speech_location[0] == '\0') {
          mdir=NULL; sdir=NULL;
          break;
      }
      mdir=keeper_runtime_directory;
      sdir=campaign.speech_location;
      break;
  case FGrp_Main:
      mdir=keeper_runtime_directory;
      sdir=NULL;
      break;
  case FGrp_Campgn:
      mdir=keeper_runtime_directory;
      sdir="campgns";
      break;
  case FGrp_CmpgLvls:
      if (campaign.levels_location[0] == '\0') {
          mdir=NULL; sdir=NULL;
          break;
      }
      mdir=install_info.inst_path[0] ? install_info.inst_path : keeper_runtime_directory;
      sdir=campaign.levels_location;
      break;
  case FGrp_CmpgCrtrs:
      if (campaign.creatures_location[0] == '\0') {
          mdir=NULL; sdir=NULL;
          break;
      }
      mdir=install_info.inst_path[0] ? install_info.inst_path : keeper_runtime_directory;
      sdir=campaign.creatures_location;
      break;
  case FGrp_CmpgConfig:
      if (campaign.configs_location[0] == '\0') {
          mdir=NULL; sdir=NULL;
          break;
      }
      mdir=install_info.inst_path[0] ? install_info.inst_path : keeper_runtime_directory;
      sdir=campaign.configs_location;
      break;
  case FGrp_CmpgMedia:
      if (campaign.media_location[0] == '\0') {
          mdir=NULL; sdir=NULL;
          break;
      }
      mdir=install_info.inst_path[0] ? install_info.inst_path : keeper_runtime_directory;
      sdir=campaign.media_location;
      break;
  case FGrp_LandView:
      if (campaign.land_location[0] == '\0') {
          mdir=NULL; sdir=NULL;
          break;
      }
      mdir=install_info.inst_path[0] ? install_info.inst_path : keeper_runtime_directory;
      sdir=campaign.land_location;
      break;
  case FGrp_CrtrData:
      mdir=keeper_runtime_directory;
      sdir="creatrs";
      break;
  case FGrp_MpLevels:
      mdir=keeper_runtime_directory;
      sdir="multiplayer";
      break;
  case FGrp_Replays:
      mdir=keeper_runtime_directory;
      sdir="replays";
      break;
  default:
      mdir=keeper_runtime_directory;
      sdir=NULL;
      break;
  }
  
  if (mdir == NULL) {
      dst[0] = '\0';
  } else {
      if (mod_dir == NULL)
          mod_dir = "";
      if (sdir == NULL)
          sdir = "";
      if (fname == NULL)
          fname = "";

      int64_t len_mdir = (int64_t)strlen(mdir);
      int64_t len_mod_dir = (int64_t)strlen(mod_dir);
      int64_t len_sdir = (int64_t)strlen(sdir);
      int64_t len_fname = (int64_t)strlen(fname);
      
      int64_t total_len = len_mdir + len_mod_dir + len_sdir + len_fname + 3; /* +3 for separators */
      
      if (total_len >= (int64_t)dst_size) {
          ERRORMSG("Path construction would overflow: total_len=%" PRId64 ", buffer_size=%" PRIu64 ". Components: mdir=%" PRId64 ", mod_dir=%" PRId64 ", sdir=%" PRId64 ", fname=%" PRId64,
                   (int64_t)(total_len), (uint64_t)dst_size, 
                   (int64_t)(len_mdir), (int64_t)(len_mod_dir), 
                   (int64_t)(len_sdir), (int64_t)(len_fname));
          dst[0] = '\0';
          return dst;
      }

      const char *mod_sep = mod_dir[0] == 0 ? "" : "/";
      const char *dir_sep = sdir[0] == 0 ? "" : "/";
      const char *file_sep = fname[0] == 0 ? "" : "/";
      
      /* OVERFLOW CHECK: snprintf returns number of chars that would have been written.
         If >= dst_size, the buffer was overflowed. Return NULL to signal error. */
      int64_t written = snprintf(dst, dst_size, "%s%s%s%s%s%s%s", 
                             mdir, mod_sep, mod_dir, dir_sep, sdir, file_sep, fname);
      if (written < 0 || (size_t)written >= dst_size) {
          ERRORMSG("Path construction overflow: len=%" PRId64 ", buffer_size=%" PRIu64 ". Components: mdir=%" PRIu64 ", mod_dir=%" PRIu64 ", sdir=%" PRIu64 ", fname=%" PRIu64,
                   (int64_t)(written), (uint64_t)dst_size, 
                   (uint64_t)len_mdir, (uint64_t)len_mod_dir, 
                   (uint64_t)len_sdir, (uint64_t)len_fname);
          dst[0] = '\0';
          return dst;
      }
  }
  return dst;
}

/**
 * Public API: Get path for a game file (no mod overlay).
 * Returns pointer to internal static buffer, or NULL if path construction failed.
 */
char *get_game_file_path(int64_t fgroup, const char *fname)
{
  static char ffullpath[4096];
  return _resolve_file_path_internal(ffullpath, sizeof(ffullpath), NULL, fgroup, fname);
}

/**
 * Public API: Get path for a mod-overlaid file.
 * Returns pointer to internal static buffer, or NULL if path construction failed.
 * mod_dir: the mod subdirectory within keeper_runtime_directory, or NULL for no overlay.
 */
char *get_mod_file_path(const char *mod_dir, int64_t fgroup, const char *fname)
{
  static char ffullpath[4096];
  return _resolve_file_path_internal(ffullpath, sizeof(ffullpath), mod_dir, fgroup, fname);
}

/**
 * Public API: Get path for a game file with formatted filename.
 * Returns pointer to internal static buffer, or NULL if path/format construction failed.
 */
char *get_game_file_path_fmt(int64_t fgroup, const char *fmt_str, ...)
{
  char fname[255] = "";
  va_list val;
  va_start(val, fmt_str);
  vsnprintf(fname, sizeof(fname), fmt_str, val);
  va_end(val);
  
  static char ffullpath[2048];
  return _resolve_file_path_internal(ffullpath, sizeof(ffullpath), NULL, fgroup, fname);
}

/**
 * Public API: Get path for a mod-overlaid file with formatted filename.
 * Returns pointer to internal static buffer, or NULL if path/format construction failed.
 * mod_dir: the mod subdirectory within keeper_runtime_directory, or NULL for no overlay.
 */
char *get_mod_file_path_fmt(const char *mod_dir, int64_t fgroup, const char *fmt_str, ...)
{
  char fname[255] = "";
  va_list val;
  va_start(val, fmt_str);
  vsnprintf(fname, sizeof(fname), fmt_str, val);
  va_end(val);
  
  static char ffullpath[4096];
  return _resolve_file_path_internal(ffullpath, sizeof(ffullpath), mod_dir, fgroup, fname);
}

/* ─────────────────────────────────────────────────────────────────────────
   DEPRECATED: Old API kept for compatibility. Prefer get_*_file_path* instead.
   ───────────────────────────────────────────────────────────────────────── */

char *prepare_file_path_buf_mod(char *dst, int64_t dst_size, const char *mod_dir, int64_t fgroup, const char *fname)
{
  return _resolve_file_path_internal(dst, dst_size, mod_dir, fgroup, fname);
}

char *prepare_file_path_mod(const char *mod_dir, int64_t fgroup, const char *fname)
{
  return get_mod_file_path(mod_dir, fgroup, fname);
}

char *prepare_campaign_levels_path(char *dst, int64_t dst_size, const struct GameCampaign *campgn, const char *fname)
{
  // Same folder FGrp_CmpgLvls resolves to for the loaded campaign.
  dst[0] = '\0';
  if ((campgn == NULL) || (campgn->levels_location[0] == '\0'))
      return dst;
  const char *mdir = install_info.inst_path[0] ? install_info.inst_path : keeper_runtime_directory;
  snprintf(dst, (size_t)dst_size, "%s/%s/%s", mdir, campgn->levels_location, fname);
  return dst;
}

char *prepare_file_path(int64_t fgroup, const char *fname)
{
  return get_game_file_path(fgroup, fname);
}

char *prepare_file_path_va_mod(const char *mod_dir, int64_t fgroup, const char *fmt_str, va_list arg)
{
  char fname[255] = "";
  vsnprintf(fname, sizeof(fname), fmt_str, arg);
  return get_mod_file_path(mod_dir, fgroup, fname);
}

char *prepare_file_fmtpath_mod(const char *mod_dir, int64_t fgroup, const char *fmt_str, ...)
{
  va_list val;
  va_start(val, fmt_str);
  char* result = prepare_file_path_va_mod(mod_dir, fgroup, fmt_str, val);
  va_end(val);
  return result;
}

char *prepare_file_fmtpath(int64_t fgroup, const char *fmt_str, ...)
{
  va_list val;
  va_start(val, fmt_str);
  char* result = prepare_file_path_va_mod(NULL, fgroup, fmt_str, val);
  va_end(val);
  return result;
}

/**
 * Returns the folder specified by LEVELS_LOCATION
 */
int64_t get_level_fgroup(LevelNumber lvnum)
{
    return FGrp_CmpgLvls;
}

/**
 * Loads data file into allocated buffer.
 * @return Returns NULL if the file doesn't exist or is smaller than ldsize;
 * on success, returns a buffer which should be freed after use,
 * and sets ldsize into its size.
 */
unsigned char *load_data_file_to_buffer(int64_t *ldsize, int64_t fgroup, const char *fmt_str, ...)
{
  // Prepare file name
  va_list arg;
  va_start(arg, fmt_str);
  char fname[255] = "";
  vsnprintf(fname, sizeof(fname), fmt_str, arg);
  char ffullpath[2048];
  prepare_file_path_buf(ffullpath, sizeof(ffullpath), fgroup, fname);
  va_end(arg);
  // Load the file
   int64_t fsize = LbFileLengthRnc(ffullpath);
   if (fsize < *ldsize)
   {
       WARNMSG("File \"%s\" doesn't exist or is too small.", fname);
       return NULL;
  }
  unsigned char* buf = KfxCalloc(fsize + 16, 1);
  if (buf == NULL)
  {
    WARNMSG("Can't allocate %" PRId64 " bytes to load \"%s\".",(int64_t)(fsize),fname);
    return NULL;
  }
  fsize = LbFileLoadAt(ffullpath,buf);
  if (fsize < *ldsize)
  {
    WARNMSG("Reading file \"%s\" failed.",fname);
    KfxFree(buf);
    return NULL;
  }
  memset(buf+fsize, '\0', 15);
  *ldsize = fsize;
  return buf;
}






struct LevelInformation *get_level_info(LevelNumber lvnum)
{
  return get_campaign_level_info(&campaign, lvnum);
}

struct LevelInformation *get_or_create_level_info(LevelNumber lvnum, uint64_t lvoptions)
{
    struct LevelInformation* lvinfo = get_campaign_level_info(&campaign, lvnum);
    if (lvinfo != NULL)
    {
        lvinfo->level_type |= lvoptions;
        return lvinfo;
  }
  lvinfo = new_level_info_entry(&campaign, lvnum);
  if (lvinfo != NULL)
  {
    lvinfo->level_type |= lvoptions;
    return lvinfo;
  }
  return NULL;
}

/**
 * Returns first level info structure in the array.
 */
struct LevelInformation *get_first_level_info(void)
{
  if (campaign.lvinfos == NULL)
    return NULL;
  return &campaign.lvinfos[0];
}

/**
 * Returns last level info structure in the array.
 */
struct LevelInformation *get_last_level_info(void)
{
  if ((campaign.lvinfos == NULL) || (campaign.lvinfos_count < 1))
    return NULL;
  return &campaign.lvinfos[campaign.lvinfos_count-1];
}

/**
 * Returns next level info structure in the array.
 * Note that it's not always corresponding to next campaign level; use
 * get_level_info() to get information for specific level. This function
 * is used for sweeping through all level info entries.
 */
struct LevelInformation *get_next_level_info(struct LevelInformation *previnfo)
{
  if (campaign.lvinfos == NULL)
    return NULL;
  if (previnfo == NULL)
    return NULL;
  uint64_t i = previnfo - &campaign.lvinfos[0];
  i++;
  if (i >= campaign.lvinfos_count)
    return NULL;
  return &campaign.lvinfos[i];
}

/**
 * Returns previous level info structure in the array.
 * Note that it's not always corresponding to previous campaign level; use
 * get_level_info() to get information for specific level. This function
 * is used for reverse sweeping through all level info entries.
 */
struct LevelInformation *get_prev_level_info(struct LevelInformation *nextinfo)
{
  if (campaign.lvinfos == NULL)
    return NULL;
  if (nextinfo == NULL)
    return NULL;
  int64_t i = nextinfo - &campaign.lvinfos[0];
  i--;
  if (i < 0)
    return NULL;
  return &campaign.lvinfos[i];
}

int64_t set_level_info_string_index(LevelNumber lvnum, char *stridx, uint64_t lvoptions)
{
    if (campaign.lvinfos == NULL)
        init_level_info_entries(&campaign, 0);
    struct LevelInformation* lvinfo = get_or_create_level_info(lvnum, lvoptions);
    if (lvinfo == NULL)
        return false;
    int64_t k = atoi(stridx);
    if (k > 0)
    {
        lvinfo->name_stridx = k;
        return true;
    }
  return false;
}

int64_t set_level_info_text_name(LevelNumber lvnum, char *name, uint64_t lvoptions)
{
    if (campaign.lvinfos == NULL)
        init_level_info_entries(&campaign, 0);
    struct LevelInformation* lvinfo = get_or_create_level_info(lvnum, lvoptions);
    if (lvinfo == NULL)
        return false;
    snprintf(lvinfo->name, LINEMSG_SIZE, "%s", name);
    if ((lvoptions & LvKind_IsFree) != 0)
    {
        lvinfo->ensign_x += ((LANDVIEW_MAP_WIDTH >> 4) * (LbSinL(lvnum * DEGREES_11_25) >> 6)) >> 10;
        lvinfo->ensign_y -= ((LANDVIEW_MAP_HEIGHT >> 4) * (LbCosL(lvnum * DEGREES_11_25) >> 6)) >> 10;
  }
  return true;
}

TbBool reset_credits(struct CreditsItem *credits)
{
    for (int64_t i = 0; i < CAMPAIGN_CREDITS_COUNT; i++)
    {
        memset(&credits[i], 0, sizeof(struct CreditsItem));
        credits[i].kind = CIK_None;
  }
  return true;
}

TbBool parse_credits_block(struct CreditsItem *credits,char *buf,char *buffer_end_pointer)
{
  const char * block_name = "credits";
  // Find the block
  int64_t len = buffer_end_pointer - buf;
  int64_t pos = 0;
  int64_t k = find_conf_block(buf, &pos, len, block_name);
  if (k < 0)
  {
    WARNMSG("Block [%s] not found in Credits file.", block_name);
    return 0;
  }
  int64_t n = 0;
  while (pos<len)
  {
    if ((buf[pos] != 0) && (buf[pos] != '[') && (buf[pos] != ';'))
    {
      credits[n].kind = CIK_EmptyLine;
      credits[n].font = 2;
      char word_buf[32];
      switch (buf[pos])
      {
      case '*':
        pos++;
        if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          k = LbAtoI32(word_buf);
        else
          k = 0;
        if (k > 0)
        {
          credits[n].kind = CIK_StringId;
          credits[n].font = 1;
          credits[n].num = k;
        }
        break;
      case '&':
        pos++;
        if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          k = atoi(word_buf);
        else
          k = 0;
        if (k > 0)
        {
          credits[n].kind = CIK_StringId;
          credits[n].font = 2;
          credits[n].num = k;
        }
        break;
      case '%':
        pos++;
        credits[n].kind = CIK_DirectText;
        credits[n].font = 0;
        credits[n].str = &buf[pos];
        break;
      case '#':
        pos++;
        credits[n].kind = CIK_DirectText;
        credits[n].font = 1;
        credits[n].str = &buf[pos];
        break;
      default:
        credits[n].kind = CIK_DirectText;
        credits[n].font = 2;
        credits[n].str = &buf[pos];
        break;
      }
      n++;
    }
    // Finishing the line
    while (pos < len)
    {
      if (buf[pos] < 32) break;
      pos++;
    }
    if (buf[pos] == '\r')
    {
      buf[pos] = '\0';
      pos+=2;
    } else
    {
      buf[pos] = '\0';
      pos++;
    }
  }
  if (credits[0].kind == CIK_None) {
    WARNMSG("Credits list empty after parsing [%s] block of Credits file.", block_name);
  }
  return true;
}

/**
 * Loads the credits data for the current campaign.
 */
TbBool setup_campaign_credits_data(struct GameCampaign *campgn)
{
  SYNCDBG(18,"Starting");
  if (campgn->credits_fname[0] == '\0') {
    // CREDITS is optional in the campaign file. Without this check,
    // prepare_file_path() resolves the empty filename to the bare
    // FGrp_LandView directory, which LbFileOpen()/fopen() happily opens
    // (Linux allows opening a directory for reading); ftell() after
    // seeking to its end then returns LONG_MAX instead of failing, so the
    // bogus "file length" sails past the filelen<=0 check below and blows
    // up the KfxCalloc() a few lines down.
    return false;
  }
  char* fname = prepare_file_path(FGrp_LandView, campgn->credits_fname);
  int64_t filelen = LbFileLengthRnc(fname);
  if (filelen <= 0)
  {
    ERRORLOG("Campaign Credits file \"%s\" does not exist or can't be opened",campgn->credits_fname);
    return false;
  }
  campgn->credits_data = (char *)KfxCalloc(filelen + 256, 1);
  if (campgn->credits_data == NULL)
  {
    ERRORLOG("Can't allocate memory for Campaign Credits file \"%s\"",campgn->credits_fname);
    return false;
  }
  char* credits_data_end = campgn->credits_data + filelen + 255;
  int64_t result = true;
  int64_t loaded_size = LbFileLoadAt(fname, campgn->credits_data);
  if (loaded_size < 4)
  {
    ERRORLOG("Campaign Credits file \"%s\" couldn't be loaded or is too small",campgn->credits_fname);
    result = false;
  }
  // Resetting all values to unused
  reset_credits(campgn->credits);
  // Analyzing credits data and filling correct values
  if (result)
  {
    result = parse_credits_block(campgn->credits, campgn->credits_data, credits_data_end);
    if (!result)
      WARNMSG("Parsing credits file \"%s\" credits block failed",campgn->credits_fname);
  }
  SYNCDBG(19,"Finished");
  return result;
}

int64_t is_bonus_level(LevelNumber lvnum)
{
  if (lvnum < 1) return false;
  for (int64_t i = 0; i < CAMPAIGN_LEVELS_COUNT; i++)
  {
    if (campaign.bonus_levels[i] == lvnum)
    {
        SYNCDBG(7,"Level %" PRId64 " identified as bonus",(int64_t)(lvnum));
        return true;
    }
  }
  SYNCDBG(7,"Level %" PRId64 " not recognized as bonus",(int64_t)(lvnum));
  return false;
}

int64_t is_extra_level(LevelNumber lvnum)
{
  if (lvnum < 1) return false;
  for (int64_t i = 0; i < EXTRA_LEVELS_COUNT; i++)
  {
      if (campaign.extra_levels[i] == lvnum)
      {
          SYNCDBG(7,"Level %" PRId64 " identified as extra",(int64_t)(lvnum));
          return true;
      }
  }
  SYNCDBG(7,"Level %" PRId64 " not recognized as extra",(int64_t)(lvnum));
  return false;
}

/**
 * Returns index for Game->bonus_levels associated with given single player level.
 * Gives -1 if there's no store place for the level.
 */
int64_t storage_index_for_bonus_level(LevelNumber bn_lvnum)
{
    if (bn_lvnum < 1)
        return -1;
    int64_t k = 0;
    for (int64_t i = 0; i < CAMPAIGN_LEVELS_COUNT; i++)
    {
        if (campaign.bonus_levels[i] == bn_lvnum)
            return k;
        if (campaign.bonus_levels[i] != 0)
            k++;
  }
  return -1;
}

/**
 * Returns index for Campaign->single_levels associated with given singleplayer level.
 * If the level is not found, returns -1.
 */
int64_t array_index_for_singleplayer_level(LevelNumber sp_lvnum)
{
  if (sp_lvnum < 1) return -1;
  for (int64_t i = 0; i < CAMPAIGN_LEVELS_COUNT; i++)
  {
    if (campaign.single_levels[i] == sp_lvnum)
        return i;
  }
  return -1;
}

/**
 * Returns bonus level number for given singleplayer level number.
 * If no bonus found, returns 0.
 */
LevelNumber bonus_level_for_singleplayer_level(LevelNumber sp_lvnum)
{
    int64_t i = array_index_for_singleplayer_level(sp_lvnum);
    if (i >= 0)
        return campaign.bonus_levels[i];
    return 0;
}

/**
 * Returns first single player level number.
 * On error, returns SINGLEPLAYER_NOTSTARTED.
 */
LevelNumber first_singleplayer_level(void)
{
    int64_t lvnum = campaign.single_levels[0];
    if (lvnum > 0)
        return lvnum;
    return SINGLEPLAYER_NOTSTARTED;
}

/**
 * Returns last single player level number.
 * On error, returns SINGLEPLAYER_NOTSTARTED.
 */
LevelNumber last_singleplayer_level(void)
{
    int64_t i = campaign.single_levels_count;
    if ((i > 0) && (i <= CAMPAIGN_LEVELS_COUNT))
        return campaign.single_levels[i - 1];
    return SINGLEPLAYER_NOTSTARTED;
}

/**
 * Returns first multi player level number.
 * On error, returns SINGLEPLAYER_NOTSTARTED.
 */
LevelNumber first_multiplayer_level(void)
{
  int64_t lvnum = campaign.multi_levels[0];
  if (lvnum > 0)
    return lvnum;
  return SINGLEPLAYER_NOTSTARTED;
}

/**
 * Returns first extra level number.
 * On error, returns SINGLEPLAYER_NOTSTARTED.
 */
LevelNumber first_extra_level(void)
{
    for (uint64_t lvidx = 0; lvidx < campaign.extra_levels_index; lvidx++)
    {
        int64_t lvnum = campaign.extra_levels[lvidx];
        if (lvnum > 0)
            return lvnum;
  }
  return SINGLEPLAYER_NOTSTARTED;
}

/**
 * Returns the extra level number. Gives SINGLEPLAYER_NOTSTARTED if no such level,
 * LEVELNUMBER_ERROR on error.
 */
LevelNumber get_extra_level(int64_t elv_kind)
{
    int64_t i = elv_kind;
    i--;
    if ((i < 0) || (i >= EXTRA_LEVELS_COUNT))
        return LEVELNUMBER_ERROR;
    LevelNumber lvnum = campaign.extra_levels[i];
    SYNCDBG(5, "Extra level kind %" PRId64 " has number %" PRId64, (int64_t)elv_kind, (int64_t)(lvnum));
    if (lvnum > 0)
    {
        return lvnum;
  }
  return SINGLEPLAYER_NOTSTARTED;
}

/**
 * Returns the next single player level. Gives SINGLEPLAYER_FINISHED if
 * last level was won, LEVELNUMBER_ERROR on error.
 */
LevelNumber next_singleplayer_level(LevelNumber sp_lvnum, TbBool ignore)
{
  if (sp_lvnum == SINGLEPLAYER_FINISHED) return SINGLEPLAYER_FINISHED;
  if (sp_lvnum == SINGLEPLAYER_NOTSTARTED) return first_singleplayer_level();
  if (sp_lvnum < 1) return LEVELNUMBER_ERROR;
  int64_t next_level;

  if ((game_get_intralvl_next_level() > 0) && !ignore)
  {
      next_level = game_get_intralvl_next_level();
      game_clear_intralvl_next_level();
      if (next_level < 0)
          return SINGLEPLAYER_FINISHED;

      for (int64_t i = 0; i < CAMPAIGN_LEVELS_COUNT; i++)
      {
          if (campaign.single_levels[i] == next_level)
          {
              return next_level;
          }
      }
      WARNLOG("Trying to jump to level %" PRId64 " that does not exist.", (int64_t)(next_level));
      return LEVELNUMBER_ERROR;
  }

  for (int64_t i = 0; i < CAMPAIGN_LEVELS_COUNT; i++)
  {
    if (campaign.single_levels[i] == sp_lvnum)
    {
      if (i+1 >= CAMPAIGN_LEVELS_COUNT)
        return SINGLEPLAYER_FINISHED;
      if (campaign.single_levels[i+1] <= 0)
        return SINGLEPLAYER_FINISHED;
      return campaign.single_levels[i+1];
    }
  }
  return LEVELNUMBER_ERROR;
}

/**
 * Returns the previous single player level. Gives SINGLEPLAYER_NOTSTARTED if
 * first level was given, LEVELNUMBER_ERROR on error.
 */
LevelNumber prev_singleplayer_level(LevelNumber sp_lvnum)
{
  if (sp_lvnum == SINGLEPLAYER_NOTSTARTED) return SINGLEPLAYER_NOTSTARTED;
  if (sp_lvnum == SINGLEPLAYER_FINISHED) return last_singleplayer_level();
  if (sp_lvnum < 1) return LEVELNUMBER_ERROR;
  for (int64_t i = 0; i < CAMPAIGN_LEVELS_COUNT; i++)
  {
    if (campaign.single_levels[i] == sp_lvnum)
    {
      if (i < 1)
        return SINGLEPLAYER_NOTSTARTED;
      if (campaign.single_levels[i-1] <= 0)
        return SINGLEPLAYER_NOTSTARTED;
      return campaign.single_levels[i-1];
    }
  }
  return LEVELNUMBER_ERROR;
}

/**
 * Returns the next multi player level. Gives SINGLEPLAYER_FINISHED if
 * last level was given, LEVELNUMBER_ERROR on error.
 */
LevelNumber next_multiplayer_level(LevelNumber mp_lvnum)
{
  if (mp_lvnum == SINGLEPLAYER_FINISHED) return SINGLEPLAYER_FINISHED;
  if (mp_lvnum == SINGLEPLAYER_NOTSTARTED) return first_multiplayer_level();
  if (mp_lvnum < 1) return LEVELNUMBER_ERROR;
  for (int64_t i = 0; i < MULTI_LEVELS_COUNT; i++)
  {
    if (campaign.multi_levels[i] == mp_lvnum)
    {
      if (i+1 >= MULTI_LEVELS_COUNT)
        return SINGLEPLAYER_FINISHED;
      if (campaign.multi_levels[i+1] <= 0)
        return SINGLEPLAYER_FINISHED;
      return campaign.multi_levels[i+1];
    }
  }
  return LEVELNUMBER_ERROR;
}

/**
 * Returns the next extra level. Gives SINGLEPLAYER_FINISHED if
 * last level was given, LEVELNUMBER_ERROR on error.
 */
LevelNumber next_extra_level(LevelNumber ex_lvnum)
{
  if (ex_lvnum == SINGLEPLAYER_FINISHED) return SINGLEPLAYER_FINISHED;
  if (ex_lvnum == SINGLEPLAYER_NOTSTARTED) return first_extra_level();
  if (ex_lvnum < 1) return LEVELNUMBER_ERROR;
  for (int64_t i = 0; i < EXTRA_LEVELS_COUNT; i++)
  {
    if (campaign.extra_levels[i] == ex_lvnum)
    {
      i++;
      while (i < EXTRA_LEVELS_COUNT)
      {
        if (campaign.extra_levels[i] > 0)
          return campaign.extra_levels[i];
        i++;
      }
      return SINGLEPLAYER_FINISHED;
    }
  }
  return LEVELNUMBER_ERROR;
}

/**
 * Returns if the level is a single player campaign level,
 * or special non-existing level at start/end of campaign.
 */
int64_t is_singleplayer_like_level(LevelNumber lvnum)
{
  if ((lvnum == SINGLEPLAYER_FINISHED) || (lvnum == SINGLEPLAYER_NOTSTARTED))
    return true;
  return is_singleplayer_level(lvnum);
}

/**
 * Returns if the level is a single player campaign level.
 */
int64_t is_singleplayer_level(LevelNumber lvnum)
{
  if (lvnum < 1)
  {
    SYNCDBG(17,"Level index %" PRId64 " is not correct",(int64_t)(lvnum));
    return false;
  }
  for (int64_t i = 0; i < CAMPAIGN_LEVELS_COUNT; i++)
  {
    if (campaign.single_levels[i] == lvnum)
    {
      SYNCDBG(17,"Level %" PRId64 " identified as SP",(int64_t)(lvnum));
      return true;
    }
  }
  SYNCDBG(17,"Level %" PRId64 " not recognized as SP",(int64_t)(lvnum));
  return false;
}

int64_t is_multiplayer_level(LevelNumber lvnum)
{
  int64_t i;
  if (lvnum < 1) return false;
  for (i=0; i<CAMPAIGN_LEVELS_COUNT; i++)
  {
    if (campaign.multi_levels[i] == lvnum)
    {
        SYNCDBG(17,"Level %" PRId64 " identified as MP",(int64_t)(lvnum));
        return true;
    }
  }
  SYNCDBG(17,"Level %" PRId64 " not recognized as MP",(int64_t)(lvnum));
  return false;
}

/**
 * Returns if the level is 'campaign' level.
 * All levels mentioned in campaign file are campaign levels. Campaign and
 * freeplay levels are exclusive.
 */
int64_t is_campaign_level(LevelNumber lvnum)
{
  if (is_singleplayer_level(lvnum) || is_bonus_level(lvnum)
   || is_extra_level(lvnum) || is_multiplayer_level(lvnum))
    return true;
  return false;
}

/**
 * Returns if the level is 'free play' level, which should be visible
 * in list of levels.
 */
int64_t is_freeplay_level(LevelNumber lvnum)
{
  if (lvnum < 1) return false;
  for (int64_t i = 0; i < FREE_LEVELS_COUNT; i++)
  {
    if (campaign.freeplay_levels[i] == lvnum)
    {
        SYNCDBG(18,"%" PRId64 " is freeplay",(int64_t)(lvnum));
        return true;
    }
  }
  SYNCDBG(18,"%" PRId64 " is NOT freeplay",(int64_t)(lvnum));
  return false;
}

static const LevelNumber unwired_level_number = 0;
static const LevelNumber *config_selected_level_source = &unwired_level_number;
static const LevelNumber *config_loaded_level_source = &unwired_level_number;

void set_config_level_sources(const LevelNumber *selected_level, const LevelNumber *loaded_level)
{
    config_selected_level_source = selected_level ? selected_level : &unwired_level_number;
    config_loaded_level_source = loaded_level ? loaded_level : &unwired_level_number;
}

static LevelNumber config_selected_level_number(void)
{
    return *config_selected_level_source;
}

static LevelNumber config_loaded_level_number(void)
{
    return *config_loaded_level_source;
}

LevelNumber config_level_number(void)
{
    LevelNumber lvnum = config_selected_level_number();
    if (lvnum <= 0)
        lvnum = config_loaded_level_number();
    return lvnum;
}

/**
  * checks if currently in a campaign, and if the provided level number is also part of it.
 */
TbBool is_level_in_current_campaign(LevelNumber lvnum)
{
    if (!is_campaign_level(config_loaded_level_number()))
    {
        return false;
    }
    for (int64_t i = 0; i < CAMPAIGN_LEVELS_COUNT; i++)
    {
        if (campaign.single_levels[i] == lvnum)
        {
            return true;
        }
    }
    return false;
}

/* @comment
 *     The loading items of load_config and load_config_for_mod need to be consistent.
 */
// Every config file load goes through here, so the compat report can name the
// file for issues its parser doesn't know the file of (the legacy command parsers).
static TbBool load_config_file_from(const struct ConfigFileData* file_data, const char *fname, int64_t flags)
{
    compat_report_set_source(fname);
    const TbBool result = file_data->load_func(fname, flags);
    compat_report_set_source(NULL);
    return result;
}

static void load_config_for_mod(const struct ConfigFileData* file_data, int64_t flags, const struct ModConfigItem *mod_item)
{
    set_flag(flags, (CnfLd_AcceptPartial | CnfLd_IgnoreErrors));

    const char* conf_fname = file_data->filename;
    const struct ModExistState *mod_state = &mod_item->state;
    char* fname = NULL;
    char mod_dir[256] = {0};
    sprintf(mod_dir, "%s/%s", MODS_DIR_NAME, mod_item->name);

    if (mod_state->fx_data)
    {
        fname = prepare_file_path_mod(mod_dir, FGrp_FxData, conf_fname);
        if (strlen(fname) > 0)
        {
            load_config_file_from(file_data, fname, flags);
        }
    }

    if (mod_state->cmpg_config)
    {
        fname = prepare_file_path_mod(mod_dir, FGrp_CmpgConfig, conf_fname);
        if (strlen(fname) > 0)
        {
            load_config_file_from(file_data, fname,flags);
        }
    }

    if (mod_state->cmpg_lvls)
    {
        fname = get_mod_file_path_fmt(mod_dir, FGrp_CmpgLvls, "map%05" PRId64 ".%s", (int64_t)(config_selected_level_number()), conf_fname);
        if (fname && strlen(fname) > 0)
        {
            load_config_file_from(file_data, fname,flags);
        }
    }
}

static void load_config_for_mod_list(const struct ConfigFileData* file_data, int64_t flags, const struct ModConfigItem *mod_items, int64_t mod_cnt)
{
    for (int64_t i=0; i<mod_cnt; i++)
    {
        const struct ModConfigItem *mod_item = mod_items + i;
        if (mod_item->state.mod_dir == 0)
            continue;

        load_config_for_mod(file_data, flags, mod_item);
    }
}

/* @comment
 *     The loading items of load_config and load_config_for_mod need to be consistent.
 */
TbBool load_config(const struct ConfigFileData* file_data, int64_t flags)
{
    if (file_data->pre_load_func != NULL)
    {
        file_data->pre_load_func();
    }

    const char* conf_fname = file_data->filename;

    char* fname = prepare_file_path(FGrp_FxData, conf_fname);
    TbBool result = load_config_file_from(file_data, fname, flags);

    if (mods_conf.after_base_cnt > 0)
    {
        load_config_for_mod_list(file_data, flags, mods_conf.after_base_item, mods_conf.after_base_cnt);
    }

    fname = prepare_file_path(FGrp_CmpgConfig, conf_fname);
    if (strlen(fname) > 0)
    {
        load_config_file_from(file_data, fname,flags|CnfLd_AcceptPartial|CnfLd_IgnoreErrors);
    }

    if (mods_conf.after_campaign_cnt > 0)
    {
        load_config_for_mod_list(file_data, flags, mods_conf.after_campaign_item, mods_conf.after_campaign_cnt);
    }

    fname = get_game_file_path_fmt(FGrp_CmpgLvls, "map%05" PRId64 ".%s", (int64_t)(config_selected_level_number()), conf_fname);
    if (fname && strlen(fname) > 0)
    {
        load_config_file_from(file_data, fname,flags|CnfLd_AcceptPartial|CnfLd_IgnoreErrors);
    }

    if (mods_conf.after_map_cnt > 0)
    {
        load_config_for_mod_list(file_data, flags, mods_conf.after_map_item, mods_conf.after_map_cnt);
    }

    if (file_data->post_load_func != NULL)
    {
        file_data->post_load_func();
    }

    return result;
}

/******************************************************************************/
