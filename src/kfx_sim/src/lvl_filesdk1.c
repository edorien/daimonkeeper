/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file lvl_filesdk1.c
 *     Level files reading routines fore standard DK1 levels.
 * @par Purpose:
 *     Allows reading level files in DK1 format.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     10 Mar 2009 - 20 Mar 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "lvl_filesdk1.h"

#include "globals.h"
#include "bflib_basics.h"

#include "bflib_dernc.h"
#include "bflib_fileio.h"
#include "bflib_text.h"

#include "config.h"
#include "config_campaigns.h"
#include "config_slabsets.h"
#include "config_sounds.h"
#include "config_strings.h"
#include "config_terrain.h"
#include "config_keeperfx.h"
#include "map_ceiling.h"
#include "map_blocks.h"
#include "map_utils.h"
#include "thing_factory.h"
#include <toml.h>
#include "game_lifecycle.h"
#include "kfx_sim_state.h"
#include "kfx_config_state.h"
#include "light_registry.h"
#include "ports/render_port.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/** Global storage for level file version number.
 * Note that the version number is not stored anywhere on load/save.
 * It is only valid while the level is being loaded, and cannot be used
 * during gameplay. Remember not to use it within script_process_value(),
 * or any other function used beyond first initialization of a level.
  */
int64_t level_file_version = 0;
char *level_strings[STRINGS_MAX+1];
char *level_strings_data;

// load_texture_map_file() (kfx_render's engine_textures.h) is reached
// through RenderPort instead of a same-file bare-extern
// forward-declaration. See docs/refactor/todo/
// check-layering-symbol-level-blind-spot.md.
/******************************************************************************/
#pragma pack(1)

// all these structs have a fixed size to remain compatible with the files out there
struct LegacyCoord3d {
    union {
      unsigned short val;
    } x;
    union {
      unsigned short val;
    } y;
    union {
      unsigned short val;
    } z;
};

struct LegacyCoord2d {
    union {
      unsigned short val;
    } x;
    union {
      unsigned short val;
    } y;
};

struct LegacyInitThing { // sizeof=0x15
    struct LegacyCoord3d mappos;
    unsigned char oclass;
    unsigned char model; // Converted to ThingModel on read
    unsigned char owner;
    unsigned short range;
    unsigned short index;
    unsigned char params[8];
};

struct LegacyInitActionPoint { // sizeof = 8
    struct LegacyCoord2d mappos;
    unsigned short range;
    unsigned short num;
};

struct LegacyInitLight { // sizeof=0x14
    short radius;
    unsigned char intensity;
    unsigned char flags;
    short field_4_unused;
    short field_6_unused;
    short field_8_unused;
    struct LegacyCoord3d mappos;
    unsigned char field_10_unused;
    unsigned char is_dynamic;
    short attached_slb;
};

#pragma pack()

/******************************************************************************/


/**
 * Loads map file with given level number and file extension.
 * @return Returns NULL if the file doesn't exist or is smaller than ldsize;
 * on success, returns a buffer which should be freed after use,
 * and sets ldsize into its size.
 */
unsigned char *load_single_map_file_to_buffer(LevelNumber lvnum,const char *fext,int64_t *ldsize,int64_t flags)
{
  int64_t fgroup = get_level_fgroup(lvnum);
  char* fname = prepare_file_fmtpath(fgroup, "map%05" PRIu64 ".%s", (uint64_t)(lvnum), fext);
  int64_t fsize = LbFileLengthRnc(fname);
  if (fsize < *ldsize)
  {
      if ((flags & LMFF_Optional) == 0)
          WARNMSG("Map file \"map%05" PRIu64 ".%s\" doesn't exist or is too small.", (uint64_t)(lvnum), fext);
      else
          SYNCMSG("Optional file \"map%05" PRIu64 ".%s\" doesn't exist or is too small.", (uint64_t)(lvnum), fext);
      return NULL;
  }
  unsigned char* buf = calloc(fsize + 16, 1);
  if (buf == NULL)
  {
    if ((flags & LMFF_Optional) == 0)
      WARNMSG("Can't allocate %" PRId64 " bytes to load \"map%05" PRIu64 ".%s\".",(int64_t)(fsize),(uint64_t)(lvnum),fext);
    else
      SYNCMSG("Can't allocate %" PRId64 " bytes to load \"map%05" PRIu64 ".%s\".",(int64_t)(fsize),(uint64_t)(lvnum),fext);
    return NULL;
  }
  fsize = LbFileLoadAt(fname,buf);
  if (fsize < *ldsize)
  {
    if ((flags & LMFF_Optional) == 0)
      WARNMSG("Reading map file \"map%05" PRIu64 ".%s\" failed.",(uint64_t)(lvnum),fext);
    else
      SYNCMSG("Reading optional file \"map%05" PRIu64 ".%s\" failed.",(uint64_t)(lvnum),fext);
    free(buf);
    return NULL;
  }
  *ldsize = fsize;
  SYNCDBG(7,"Map file \"map%05" PRIu64 ".%s\" loaded.",(uint64_t)(lvnum),fext);
  return buf;
}

int64_t get_level_number_from_file_name(const char *fname)
{
  if (strnicmp(fname,"map",3) != 0)
    return SINGLEPLAYER_NOTSTARTED;
  // Get level number
  int64_t lvnum = LbStrToI32(&fname[3], NULL, 10);
  if (lvnum <= 0)
    return SINGLEPLAYER_NOTSTARTED;
  return lvnum;
}

/**
 * Analyzes one line of .LIF file buffer. The buffer must be null-terminated.
 * @return Length of the parsed line.
 */
int64_t level_lif_entry_parse(const char *fname, char *buf)
{
  if (buf[0] == '\0')
    return 0;
  int64_t i = 0;
  // Skip spaces and control chars
  while (buf[i] != '\0')
  {
    // Check for commented-out lines
    if (buf[i] == ';')
    {
      // Loop through the entire commented-out line
      while (buf[i] != '\0')
      {
        if ((buf[i] == '\n') || (buf[i] == '\r'))
        {
          break;
        }
        i++;
        if (i >= 10000) // arbritarily big number to prevent an infinte loop if last line is a comment that doesn't have a new line at the end
        {
          WARNMSG("commented-out line from \"%s\" is too long at %" PRId64 " characters", fname,(int64_t)(i));
          return 0;
        }
      }
    }
    if (!isspace(buf[i]) && (buf[i] != ',') && (buf[i] != ';') && (buf[i] != ':') && (buf[i] != '\n') && (buf[i] != '\r'))
      break;
    i++;
  }
  // when the last line of a .lif is a comment, we check here if the end of the file has been reached (and we should exit the function)
  if (buf[i] == '\0')
    return 0;
  // Get level number
  char* cbuf;
  int64_t lvnum = LbStrToI32(&buf[i], &cbuf, 10);
  // If can't read number, return
  if (cbuf == &buf[i])
  {
    WARNMSG("Can't read level number from \"%s\"", fname);
    return 0;
  }
  // Skip spaces and blank chars
  while (cbuf[0] != '\0')
  {
    if (!isspace(cbuf[0]) && (cbuf[0] != ',') && (cbuf[0] != ';') && (cbuf[0] != ':'))
      break;
    cbuf++;
  }
  // IF the next field starts with a "#" then treat it as a string ID for the level's name
    if (cbuf[0] == '#')
    {
      cbuf++;
      if (!set_level_info_string_index(lvnum,cbuf,LvKind_IsFree))
      {
        WARNMSG("Can't set string index of level %" PRId64 " from file \"%s\"", (int64_t)(lvnum), fname);
      }
      cbuf--;
    }
  // Find length of level name; make it null-terminated
  i = 0;
  while (cbuf[i] != '\0')
  {
    if ((cbuf[i] == '\n') || (cbuf[i] == '\r'))
    {
      cbuf[i] = '\0';
      break;
    }
    i++;
  }
  if (i >= LINEMSG_SIZE)
  {
    WARNMSG("Level name from \"%s\" truncated from %" PRId64 " to %" PRId64 " characters", fname,(int64_t)(i),(int64_t)(LINEMSG_SIZE));
    i = LINEMSG_SIZE-1;
    cbuf[i] = '\0';
  }
  if (cbuf[0] == '\0')
  {
    WARNMSG("Can't read level name from \"%s\"", fname);
    return 0;
  }
  // Store level name
  if (add_freeplay_level_to_campaign(&campaign,lvnum) < 0)
  {
    WARNMSG("Can't add freeplay level from \"%s\" to campaign \"%s\"", fname, campaign.name);
    return 0;
  }
  if (!set_level_info_text_name(lvnum,cbuf,LvKind_IsFree))
  {
    WARNMSG("Can't set name of level from file \"%s\"", fname);
    return 0;
  }
  return (cbuf-buf)+i;
}

/** Analyzes given LIF file buffer.
 *
 * @param fname Name of the LIF file which we've loaded.
 * @param buf The buffer; must be null-terminated.
 * @param buflen Length of the buffer.
 * @return
 */
int64_t level_lif_file_parse(const char *fname, char *buf, int64_t buflen)
{
  if (buf == NULL)
    return false;
  int64_t result = false;
  int64_t pos = 0;
  int64_t i;
  do
  {
    i = level_lif_entry_parse(fname, &buf[pos]);
    if (i > 0)
    {
      result = true;
      pos += i+1;
      if (pos+1 >= buflen)
        break;
    }
  } while (i > 0);
  return result;
}

/**
 * Searches levels folder for LIF files and adds them to campaign levels list.
 */
TbBool find_and_load_lif_files(void)
{
    unsigned char* buf = calloc(MAX_LIF_SIZE, 1);
    if (buf == NULL)
    {
        ERRORLOG("Can't allocate memory for .LIF files parsing.");
        return false;
  }
  int64_t result = false;
  char* fname = prepare_file_path(FGrp_CmpgLvls, "*.lif");
  struct TbFileEntry fe;
  struct TbFileFind * ff = LbFileFindFirst(fname, &fe);
  if (ff) {
    do {
      fname = prepare_file_path(FGrp_CmpgLvls, fe.Filename);
      int64_t i = LbFileLength(fname);
      if ((i < 0) || (i >= MAX_LIF_SIZE)) {
        WARNMSG("File \"%s\" too long (Max size %" PRId64 ")", fe.Filename, (int64_t)(MAX_LIF_SIZE));
      } else if (LbFileLoadAt(fname, buf) != i) {
        WARNMSG("Unable to read .LIF file, \"%s\"", fe.Filename);
      } else {
        buf[i] = '\0';
        if (level_lif_file_parse(fe.Filename, (char *)buf, i)) {
          result = true;
        }
      }
    } while (LbFileFindNext(ff, &fe) >= 0);
    LbFileFindEnd(ff);
  }
  free(buf);
  return result;
}

/**
 * Analyzes given LOF file buffer. The buffer must be null-terminated.
 */
TbBool level_lof_file_parse(const char *fname, char *buf, int64_t len)
{
    struct LevelInformation *lvinfo;
    int64_t pos;
    char word_buf[32];
    int64_t lvnum;
    int64_t cmd_num;
    int64_t k;
    int64_t n;
    word_buf[0] = 0;
    SYNCDBG(8,"Starting for \"%s\"",fname);
    if (buf == NULL)
        return false;
    lvnum = get_level_number_from_file_name(fname);
    if (lvnum < 1)
    {
        WARNLOG("Incorrect .LOF file name \"%s\", skipped.",fname);
        return false;
    }
    lvinfo = get_or_create_level_info(lvnum, LvKind_None);
    if (lvinfo == NULL)
    {
        WARNMSG("Can't get LevelInformation item to store level %" PRId64 " data from LOF file.",(int64_t)(lvnum));
        return 0;
    }
    lvinfo->location = LvLc_Custom;

    render_load_sprites_for_multi_front(lvinfo->lvnum);
    pos = 0;
#define COMMAND_TEXT(cmd_num) get_conf_parameter_text(cmpgn_map_commands,cmd_num)
    while (pos<len)
    {
        // Finding command number in this line
        cmd_num = recognize_conf_command(buf,&pos,len,cmpgn_map_commands);
        // Now store the config item in correct place
        if (cmd_num == ccr_endOfBlock) break; // if next block starts
        n = 0;
        switch (cmd_num)
        {
        case 1: // NAME_TEXT
            if (get_conf_parameter_whole(buf,&pos,len,lvinfo->name,LINEMSG_SIZE) <= 0)
            {
              WARNMSG("Couldn't read \"%s\" parameter in LOF file '%s'.",
                  COMMAND_TEXT(cmd_num),fname);
              break;
            }
            break;
        case 2: // NAME_ID
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              if (k > 0)
              {
                lvinfo->name_stridx = k;
                n++;
              }
            }
            if ((n < 1) && (strlen(word_buf) != 0))
            {
              WARNMSG("Couldn't recognize \"%s\" number in LOF file '%s'.",
                  COMMAND_TEXT(cmd_num),fname);
            }
            break;
        case 3: // ENSIGN_POS
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                if (k > 0)
                {
                  lvinfo->ensign_x = k;
                  n++;
                }
            }
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                if (k > 0)
                {
                  lvinfo->ensign_y = k;
                  n++;
                }
            }
            if ((n < 2) && (strlen(word_buf) != 0))
            {
              WARNMSG("Couldn't recognize \"%s\" coordinates in LOF file '%s'.",
                  COMMAND_TEXT(cmd_num),fname);
            }
            break;
        case 4: // ENSIGN_ZOOM
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                if (k > 0)
                {
                  lvinfo->ensign_zoom_x = k;
                  n++;
                }
            }
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                if (k > 0)
                {
                  lvinfo->ensign_zoom_y = k;
                  n++;
                }
            }
            if ((n < 2) && (strlen(word_buf) != 0))
            {
              WARNMSG("Couldn't recognize \"%s\" coordinates in LOF file '%s'.",
                  COMMAND_TEXT(cmd_num),fname);
            }
            break;
        case 5: // PLAYERS
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              if (k > 0)
              {
                lvinfo->players = k;
                n++;
              }
            }
            if ((n < 1) && (strlen(word_buf) != 0))
            {
              WARNMSG("Couldn't recognize \"%s\" number in LOF file '%s'.",
                  COMMAND_TEXT(cmd_num),fname);
            }
            break;
        case 6: // ENSIGN
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
            {
                k = get_id(cmpgn_map_ensign_flag_options, word_buf);
                if (k >= 0)
                {
                    lvinfo->ensign_type = k;
                }
                else
                {
                    k = render_get_ensign_id(word_buf);

                    if (k >= 0)
                    {
                        lvinfo->ensign_type = CUSTOM_ENSIGN_BASE + k;
                    } else {
                        WARNMSG("Invalid value '%s' for \"%s\" in '%s' file.", word_buf,
                            COMMAND_TEXT(cmd_num), fname);
                    }
                }
            }
            break;
        case 7: // SPEECH
            if (get_conf_parameter_single(buf,&pos,len,lvinfo->speech_before,DISKPATH_SIZE) > 0)
            {
              n++;
            }
            if (get_conf_parameter_single(buf,&pos,len,lvinfo->speech_after,DISKPATH_SIZE) > 0)
            {
              n++;
            }
            if ((n < 2) && (strlen(word_buf) != 0))
            {
              WARNMSG("Couldn't recognize \"%s\" file names in LOF file '%s'.",
                  COMMAND_TEXT(cmd_num),fname);
            }
            break;
        case 8: // LAND_VIEW
            if (get_conf_parameter_single(buf,&pos,len,lvinfo->land_view,DISKPATH_SIZE) > 0)
            {
              n++;
            }
            if (get_conf_parameter_single(buf,&pos,len,lvinfo->land_window,DISKPATH_SIZE) > 0)
            {
              n++;
            }
            if ((n < 2) && (strlen(word_buf) != 0))
            {
              WARNMSG("Couldn't recognize \"%s\" file names in LOF file '%s'.",
                  COMMAND_TEXT(cmd_num),fname);
            }
            break;
        case 9: // KIND
            while ((k = recognize_conf_parameter(buf,&pos,len,cmpgn_map_cmnds_kind)) > 0)
            {
              switch (k)
              {
              case LvKind_IsSingle:
                if ((lvinfo->level_type & LvKind_IsSingle) == 0)
                {
                    if (add_single_level_to_campaign(&campaign,lvinfo->lvnum) >= 0)
                        n++;
                    lvinfo->level_type |= LvKind_IsSingle;
                } else
                    n++;
                break;
              case LvKind_IsMulti:
                if ((lvinfo->level_type & LvKind_IsMulti) == 0)
                {
                    if (add_multi_level_to_campaign(&campaign,lvinfo->lvnum) >= 0)
                        n++;
                    lvinfo->level_type |= LvKind_IsMulti;
                } else
                    n++;
                break;
              case LvKind_IsBonus:
                if ((lvinfo->level_type & LvKind_IsBonus) == 0)
                {
                    if (add_bonus_level_to_campaign(&campaign,lvinfo->lvnum) >= 0)
                        n++;
                    lvinfo->level_type |= LvKind_IsBonus;
                } else
                    n++;
                break;
              case LvKind_IsExtra:
                if ((lvinfo->level_type & LvKind_IsExtra) == 0)
                {
                    if (add_extra_level_to_campaign(&campaign,lvinfo->lvnum) >= 0)
                        n++;
                    lvinfo->level_type |= LvKind_IsExtra;
                } else
                    n++;
                break;
              case LvKind_IsFree:
                if ((lvinfo->level_type & LvKind_IsFree) == 0)
                {
                    if (add_freeplay_level_to_campaign(&campaign,lvinfo->lvnum) >= 0)
                        n++;
                    lvinfo->level_type |= LvKind_IsFree;
                } else
                    n++;
                break;
              }
              if (n < 1)
              {
                  WARNMSG("Level %" PRId64 " defined in '%s' wasn't added to any list; "
                      "kind is wrong or there's no space.",(int64_t)lvinfo->lvnum,fname);
              }
            }
            break;
        case 11: // DESCRIPTION
            // docs/refactor/editor/05-script-and-level-settings.md -- the
            // struct field already existed (LevelInformation::description,
            // config_campaigns.h) but nothing populated it; AUTHOR has no
            // equivalent field yet and stays ignored below, same as DATE/
            // MAP_FORMAT_VERSION.
            if (get_conf_parameter_whole(buf,&pos,len,lvinfo->description,LEVEL_DESCRIPTION_LEN) <= 0)
            {
              WARNMSG("Couldn't read \"%s\" parameter in LOF file '%s'.",
                  COMMAND_TEXT(cmd_num),fname);
              break;
            }
            break;
        case 10: // AUTHOR
        case 12: // DATE
        case 14: // MAP_FORMAT_VERSION
            // As for now, ignore these
            break;
        case 13: // MAPSIZE
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                if (k > 0)
                {
                  lvinfo->mapsize_x = k;
                  n++;
                }
            }
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                if (k > 0)
                {
                  lvinfo->mapsize_y = k;
                  n++;
                }
            }
            if ((n < 2) && (strlen(word_buf) != 0))
            {
              WARNMSG("Couldn't recognize \"%s\" mapsize in LOF file '%s'.",
                  COMMAND_TEXT(cmd_num),fname);
            }
            break;
        case 15: // SKIRMISH_SETUP
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = get_id(cmpgn_map_skirmish_setup_options, word_buf);
                if (k >= 0)
                    lvinfo->skirmish_setup = (unsigned char)k;
                else
                    WARNMSG("Invalid value '%s' for \"%s\" in LOF file '%s'.", word_buf,
                        COMMAND_TEXT(cmd_num),fname);
            }
            break;
        case ccr_comment:
            break;
        case ccr_endOfFile:
            break;
        default:
            WARNMSG("Unrecognized command (%" PRId64 ") in LOF file '%s', starting on byte %" PRId64 ".",(int64_t)(cmd_num),fname,(int64_t)(pos));
            break;
        }
        skip_conf_to_next_line(buf,&pos,len);
    }
    SYNCDBG(18,"Level %" PRId64 " ensign (%" PRId64 ",%" PRId64 ") zoom (%" PRId64 ",%" PRId64 ")",(int64_t)lvinfo->lvnum,(int64_t)lvinfo->ensign_x,(int64_t)lvinfo->ensign_y,(int64_t)lvinfo->ensign_zoom_x,(int64_t)lvinfo->ensign_zoom_y);
#undef COMMAND_TEXT
    return true;
}

/**
 * Searches levels folder for LOF files and adds them to campaign levels list.
 */
TbBool find_and_load_lof_files(void)
{
    SYNCDBG(16,"Starting");
    unsigned char* buf = calloc(MAX_LIF_SIZE, 1);
    if (buf == NULL)
    {
      ERRORLOG("Can't allocate memory for .LOF files parsing.");
      return false;
    }
    int64_t result = false;
    char* fname = prepare_file_path(FGrp_CmpgLvls, "*.lof");
    struct TbFileEntry fe;
    struct TbFileFind * ff = LbFileFindFirst(fname, &fe);
    if (ff) {
        do {
            fname = prepare_file_path(FGrp_CmpgLvls, fe.Filename);
            int64_t i = LbFileLength(fname);
            if ((i < 0) || (i >= MAX_LIF_SIZE)) {
              WARNMSG("File '%s' too long (Max size %" PRId64 ")", fe.Filename, (int64_t)(MAX_LIF_SIZE));

            } else if (LbFileLoadAt(fname, buf) != i) {
              WARNMSG("Unable to read .LOF file, '%s'", fe.Filename);
            } else {
              buf[i] = '\0';
              if (level_lof_file_parse(fe.Filename, (char *)buf, i))
                result = true;
            }
        } while (LbFileFindNext(ff, &fe) >= 0);
        LbFileFindEnd(ff);
    }
    free(buf);
    return result;
}

TbBool load_column_file(LevelNumber lv_num)
{
    int64_t fsize = 8;
    unsigned char* buf = load_single_map_file_to_buffer(lv_num, "clm", &fsize, LMFF_None);
    if (buf == NULL)
      return false;
    clear_columns();
    uint64_t i = 0;
    int64_t total = llong(&buf[i]);
    i += 4;
    // Validate total amount of columns
    if ((total < 0) || (total > (fsize-8)/sizeof(struct LegacyColumn)))
    {
      total = (fsize-8)/sizeof(struct LegacyColumn);
      WARNMSG("Bad amount of columns in CLM file; corrected to %" PRId64 ".",(int64_t)(total));
    }
    if (total > COLUMNS_COUNT)
    {
      WARNMSG("Only %" PRId64 " columns supported, CLM file has %" PRId64 ".",(int64_t)(COLUMNS_COUNT),(int64_t)(total));
      total = COLUMNS_COUNT;
    }
    // The second lot of 4 bytes here are ignored.
    i += 4;
    // Fill the columns
    for (int64_t k = 0; k < total; k++)
    {
        struct Column* colmn = &kfx_sim_state.columns_data[k];
        struct LegacyColumn lcol;
        memcpy(&lcol, &buf[i], sizeof(lcol));
        column_from_legacy(colmn, &lcol);
        //Update top cube in the column
        int64_t n = find_column_height(colmn);
        set_column_floor_filled_subtiles(colmn, n);
        i += sizeof(struct LegacyColumn);
    }
    free(buf);
    return true;
}

TbBool load_map_data_file(LevelNumber lv_num)
{
    struct Map *mapblk;
    uint64_t x;
    uint64_t y;
    clear_map();
    int64_t fsize = 2 * (kfx_sim_state.map_subtiles_y + 1) * (kfx_sim_state.map_subtiles_x + 1);
    unsigned char* buf = load_single_map_file_to_buffer(lv_num, "dat", &fsize, LMFF_None);
    if (buf == NULL)
        return false;
    uint64_t i = 0;
    for (y=0; y < (kfx_sim_state.map_subtiles_y+1); y++)
    {
        for (x=0; x < (kfx_sim_state.map_subtiles_x+1); x++)
        {
            mapblk = get_map_block_at(x,y);
            // The file stores the (negated) column index as a 16-bit value; the wrap through int16 is the encoding.
            mapblk->col_idx = (int16_t)(-(int64_t)lword(&buf[i]));
            i += 2;
        }
    }
    free(buf);
    // Clear some bits and do some other setup
    light_request_lightness_reset();
    for (y=0; y < (kfx_sim_state.map_subtiles_y+1); y++)
    {
        for (x=0; x < (kfx_sim_state.map_subtiles_x+1); x++)
        {
            mapblk = get_map_block_at(x,y);
            mapblk->mapwho = 0;
            mapblk->filled_subtiles = 0;
            mapblk->revealed = 0;
        }
    }
    return true;
}

static TbBool load_thing_file(LevelNumber lv_num)
{
    SYNCDBG(5,"Starting");
    int64_t fsize = 2;
    unsigned char* buf = load_single_map_file_to_buffer(lv_num, "tng", &fsize, LMFF_None);
    if (buf == NULL)
      return false;
    uint64_t i = 0;
    int64_t total = lword(&buf[i]);
    i += 2;
    // Validate total amount of things
    if ((total < 0) || (total > (fsize-2)/sizeof(struct LegacyInitThing)))
    {
        total = (fsize-2)/sizeof(struct LegacyInitThing);
        WARNMSG("Bad amount of things in TNG file; corrected to %" PRId64 ".",(int64_t)total);
    }
    if (total > THINGS_COUNT-2)
    {
        WARNMSG("Only %" PRId64 " things supported, TNG file has %" PRId64 ".",(int64_t)(THINGS_COUNT-2),(int64_t)total);
        total = THINGS_COUNT-2;
    }
    // Create things
    for (int64_t k = 0; k < total; k++)
    {
        struct LegacyInitThing litng;
        struct InitThing itng;
        memcpy(&litng, &buf[i], sizeof(struct LegacyInitThing));
        itng.mappos.x.val = litng.mappos.x.val;
        itng.mappos.y.val = litng.mappos.y.val;
        itng.mappos.z.val = litng.mappos.z.val;
        itng.oclass = litng.oclass;
        itng.model  = litng.model;
        itng.owner  = litng.owner;
        itng.range  = litng.range;
        itng.index  = litng.index;
        memcpy(&itng.params, &litng.params, 8);

        thing_create_thing(&itng);
        i += sizeof(struct LegacyInitThing);
    }
    free(buf);
    return true;
}

static int64_t load_kfx_toml_file(LevelNumber lv_num, const char *ext, const char *msg_name,
                                 const char *sections, const char *count_field, const char *section_fmt,
                                 int64_t max_count, TbBool (*section_loader)(VALUE *arg))
{
    SYNCDBG(5,"Starting");
    int64_t fsize = 0;
    unsigned char* buf = load_single_map_file_to_buffer(lv_num, ext, &fsize, LMFF_None);
    if (buf == NULL)
        return -1;
    VALUE file_root, *root_ptr = &file_root;
    char err[255] = "";
    char key[64] = "";

    if (toml_parse((char*)buf, err, sizeof(err), root_ptr))
    {
        WARNMSG("Unable to load %s file\n %s", msg_name, err);
        free(buf);
        return -1;
    }
    VALUE *common_section = value_dict_get(root_ptr, "common");
    if (!common_section)
    {
        WARNMSG("No [common] in %s for level %" PRId64, msg_name, (int64_t)(lv_num));
        value_fini(root_ptr);
        free(buf);
        return -1;
    }
    int64_t total;

    VALUE *item_arr = value_dict_get(root_ptr, sections);
    if (value_type(item_arr) == VALUE_ARRAY)
    {
        total = value_array_size(item_arr);
    }
    else
    {
        item_arr = NULL; // Against bad values
        total = value_int32(value_dict_get(common_section, count_field));
    }
    // Validate total amount of sections
    if (total < 0)
    {
        WARNMSG("Bad amount of secions in %s file", msg_name);
        value_fini(root_ptr);
        free(buf);
        return -1;
    }
    if (total >= max_count)
    {
        WARNMSG("Only %" PRId64 " things supported, file has %" PRId64 ".", (int64_t)(max_count),(int64_t)(total));

    }
    int64_t status = 0;
    // Create sections
    for (int64_t k = 0; k < total; k++)
    {
        VALUE *section;
        if (item_arr)
        {
            // Array form
            section = value_array_get(item_arr, k);
        }
        else
        {
            snprintf(key, sizeof(key), section_fmt, k);
            section = value_dict_get(root_ptr, key);
        }
        if (value_type(section) != VALUE_DICT) {
            WARNMSG("Invalid %s section %" PRId64, msg_name, (int64_t)(k));
            status = 1;
        }
        else if (!section_loader(section)) {
            WARNMSG("Failed to load section %" PRId64 " from %s", (int64_t)(k), msg_name);
            status = 1;
        }
    }
    value_fini(root_ptr);
    free(buf);
    return status;
}

static TbBool load_tngfx_file(LevelNumber lv_num)
{
    return load_kfx_toml_file(lv_num, "tngfx", "TNGFX",
                              "thing", "ThingsCount", "thing%" PRId64, (int64_t)(THINGS_COUNT - 2),
                              &thing_create_thing_adv) == 0;
}

TbBool load_action_point_file(LevelNumber lv_num)
{
  SYNCDBG(5,"Starting");
  int64_t fsize = 4;
  unsigned char* buf = load_single_map_file_to_buffer(lv_num, "apt", &fsize, LMFF_None);
  if (buf == NULL)
    return false;
  uint64_t i = 0;
  int64_t total = llong(&buf[i]);
  i += 4;
  // Validate total amount of action points
  if ((total < 0) || (total > (fsize-4)/sizeof(struct LegacyInitActionPoint)))
  {
    total = (fsize-4)/sizeof(struct LegacyInitActionPoint);
    WARNMSG("Bad amount of action points in APT file; corrected to %" PRId64 ".",(int64_t)(total));
  }
  if (total > ACTN_POINTS_COUNT-1)
  {
    WARNMSG("Only %" PRId64 " action points supported, APT file has %" PRId64 ".",(int64_t)(ACTN_POINTS_COUNT-1),(int64_t)(total));
    total = ACTN_POINTS_COUNT-1;
  }
  // Create action points
  for (int64_t k = 0; k < total; k++)
  {
      struct LegacyInitActionPoint legiapt;
      struct InitActionPoint iapt;
      memcpy(&legiapt, &buf[i], sizeof(struct LegacyInitActionPoint));
      iapt.mappos.x.val = legiapt.mappos.x.val;
      iapt.mappos.y.val = legiapt.mappos.y.val;
      iapt.num          = legiapt.num;
      iapt.range        = legiapt.range;
      if (actnpoint_create_actnpoint(&iapt) == INVALID_ACTION_POINT)
          ERRORLOG("Cannot allocate action point %" PRId64 " during APT load", (int64_t)(k));
    i += sizeof(struct LegacyInitActionPoint);
  }
  free(buf);
  return true;
}

TbBool load_aptfx_file(LevelNumber lv_num)
{
    return load_kfx_toml_file(lv_num, "aptfx", "APTFX",
                              "actionpoint", "ActionPointsCount", "actionpoint%" PRId64, (int64_t)(ACTN_POINTS_COUNT - 1),
                              &actnpoint_create_actnpoint_adv) >= 0;
}

/**
 * Updates "use" property of given columns set, using given SlabSet entries.
 */
TbBool update_columns_use(struct Column *cols,int64_t ccount,struct SlabSet *sset,int64_t scount)
{
    int64_t i;
    int64_t k;
    int64_t ncol;
    for (i = 0; i < ccount; i++)
    {
        cols[i].use = 0;
    }
    for (i=0; i < scount; i++)
        for (k=0; k < 9; k++)
        {
            ncol = -sset[i].col_idx[k];
            if ((ncol >= 0) && (ncol < ccount))
                cols[ncol].use++;
        }
    return true;
}

TbBool columns_add_static_entries(void)
{
    int64_t c[3];

    for (int64_t i=0; i < 3; i++)
      c[i] = 0;
    struct Column lcolmn;
    memset(&lcolmn, 0, sizeof(struct Column));
    int64_t* wptr = &kfx_config_state.col_static_entries[0];
    for (int64_t i=0; i < 3; i++)
    {
        memset(&lcolmn, 0, sizeof(struct Column));
        lcolmn.floor_texture = c[i];
        for (int64_t k = 0; k < 6; k++)
        {
          lcolmn.cubes[0] = player_cubes[k];
          make_solidmask(&lcolmn);
          int64_t ncol = find_column(&lcolmn);
          if (ncol == 0)
            ncol = create_column(&lcolmn);
          struct Column* colmn = get_column(ncol);
          colmn->bitfields |= CLF_ACTIVE;
          *wptr = -(int64_t)ncol;
          wptr++;
        }
    }
    return true;
}

TbBool update_slabset_column_indices(struct Column *cols, int64_t ccount)
{
    struct Column lcolmn;
    memset(&lcolmn,0,sizeof(struct Column));
    // slabset.toml/columnset.toml are fixed, session-wide config -- a gap between them doesn't
    // change across the many level (re)loads that call this function, so re-logging the exact
    // same missing IDs on every single call is pure noise (and, at scale, a real performance
    // problem: it has driven a single ftest into writing 300k+ duplicate log lines). Report each
    // missing column ID once per process instead.
    static unsigned char s_warned_missing_column[COLUMNS_COUNT / 8 + 1];
    for (int64_t i = 0; i < kfx_sim_state.slabset_num; i++)
    {
        struct SlabSet* sset = &kfx_sim_state.slabset[i];
        for (int64_t k = 0; k < 9; k++)
        {
            int64_t n = sset->col_idx[k];
            int64_t ncol;
            if (n >= 0)
            {
                lcolmn.floor_texture = n;
                ncol = find_column(&lcolmn);
                if (ncol == 0)
                {
                    ncol = create_column(&lcolmn);
                    struct Column* colmn = get_column(ncol);
                    colmn->bitfields |= CLF_ACTIVE;
                }
            } else
            {
                if (-n < ccount)
                    ncol = find_column(&cols[-n]);
                else
                    ncol = 0;
                if (ncol == 0)
                {
                    int64_t missing = -n;
                    if ((missing >= 0) && (missing < COLUMNS_COUNT)
                        && !(s_warned_missing_column[missing / 8] & (1 << (missing % 8))))
                    {
                        s_warned_missing_column[missing / 8] |= (unsigned char)(1 << (missing % 8));
                        ERRORLOG("column:%" PRId64 " referenced in slabset.toml but not present in columnset.toml",(int64_t)(missing));
                    }
                    continue;
                }
            }
            sset->col_idx[k] = -ncol;
        }
    }
    return true;
}

TbBool create_columns_from_list(struct Column *cols, int64_t ccount)
{
    for (int64_t i = 1; i < ccount; i++)
    {
        if (cols[i].use)
        {
            int64_t ncol = find_column(&cols[i]);
            if (ncol == 0)
                ncol = create_column(&cols[i]);
            struct Column* colmn = get_column(ncol);
            colmn->bitfields |= CLF_ACTIVE;
        }
    }
    return true;
}

TbBool load_slab_datclm_files(void)
{
    SYNCDBG(5,"Starting");

    int64_t slbset_tot = kfx_config_state.conf.slab_conf.slab_types_count * SLABSETS_PER_SLAB;
    kfx_sim_state.slabset_num = slbset_tot;

    update_columns_use(kfx_config_state.conf.column_conf.cols,kfx_config_state.conf.column_conf.columns_count,kfx_sim_state.slabset,slbset_tot);
    create_columns_from_list(kfx_config_state.conf.column_conf.cols,kfx_config_state.conf.column_conf.columns_count);
    update_slabset_column_indices(kfx_config_state.conf.column_conf.cols,kfx_config_state.conf.column_conf.columns_count);
    return true;
}

TbBool load_slab_file(void)
{
    TbBool result = true;
    if (!load_slab_datclm_files())
        result = false;
    if (!columns_add_static_entries())
        result = false;
    return result;
}

int64_t load_map_wibble_file(uint64_t lv_num)
{
    struct Map *mapblk;
    uint64_t stl_x;
    uint64_t stl_y;
    unsigned char *buf;
    uint64_t i;
    uint64_t k;
    int64_t fsize;
    fsize = (kfx_sim_state.map_subtiles_y+1)*(kfx_sim_state.map_subtiles_x+1);
    buf = load_single_map_file_to_buffer(lv_num,"wib",&fsize,LMFF_None);
    if (buf == NULL)
      return false;
    i = 0;
    for (stl_y=0; stl_y < (kfx_sim_state.map_subtiles_y+1); stl_y++)
      for (stl_x=0; stl_x < (kfx_sim_state.map_subtiles_x+1); stl_x++)
      {
        mapblk = get_map_block_at(stl_x,stl_y);
        k = buf[i];
        set_mapblk_wibble_value(mapblk, k);
        i++;
      }
    free(buf);
    return true;
}

int64_t load_map_ownership_file(LevelNumber lv_num)
{
    uint64_t x;
    uint64_t y;
    unsigned char *buf;
    uint64_t i;
    int64_t fsize;
    fsize = (kfx_sim_state.map_subtiles_y+1)*(kfx_sim_state.map_subtiles_x+1);
    buf = load_single_map_file_to_buffer(lv_num,"own",&fsize,LMFF_None);
    if (buf == NULL)
      return false;
    i = 0;
    for (y=0; y < (kfx_sim_state.map_subtiles_y+1); y++)
      for (x=0; x < (kfx_sim_state.map_subtiles_x+1); x++)
      {
        if ((x < kfx_sim_state.map_subtiles_x) && (y < kfx_sim_state.map_subtiles_y))
        {
            set_slab_owner(subtile_slab(x),subtile_slab(y),buf[i]);
        }
        else
            set_slab_owner(subtile_slab(x),subtile_slab(y),PLAYER_NEUTRAL);
        i++;
      }
    free(buf);
    return true;
}

TbBool initialise_map_wlb_auto(void)
{
    struct SlabMap *slb;
    struct SlabConfigStats *slabst;
    uint64_t x;
    uint64_t y;
    int64_t n;
    uint64_t nbridge;
    nbridge = 0;
    for (y = 0; y < kfx_sim_state.map_tiles_y; y++)
    {
        for (x = 0; x < kfx_sim_state.map_tiles_x; x++)
        {
            slb = get_slabmap_block(x, y);
            slabst = get_slab_stats(slb);
            if (slabst->wlb_type == WlbT_Bridge)
            {
                n = slab_kind_from_wlb_type(WlbT_Water);
                int64_t best = 0;
                if (n >= 0)
                    best = slabs_count_near(x, y, 1, n);
                for (int64_t slbkind = 0; slbkind < kfx_config_state.conf.slab_conf.slab_types_count; slbkind++) {
                    if (slab_kind_is_bridgeable(slbkind) && (slabs_count_near(x, y, 1, slbkind) > best)) {
                        n = slbkind;
                        best = slabs_count_near(x, y, 1, slbkind);
                    }
                }
                if (n < 0)
                    n = SlbT_PATH;
                nbridge++;
            }
            else
            {
                n = slb->kind;
            }
            slabst = get_slab_kind_stats(n);
            slb->wlb_type = slabst->wlb_type;
        }
    }
    SYNCMSG("Regenerated WLB flags, unsure for %" PRId64 " bridge blocks.",(int64_t)nbridge);
    return true;
}

TbBool load_map_wlb_file(uint64_t lv_num)
{
    struct SlabMap *slb;
    uint64_t x;
    uint64_t y;
    unsigned char *buf;
    uint64_t i;
    uint64_t n;
    uint64_t nfixes;
    int64_t fsize;
    SYNCDBG(7,"Starting");
    nfixes = 0;
    fsize = kfx_sim_state.map_tiles_y*kfx_sim_state.map_tiles_x;
    buf = load_single_map_file_to_buffer(lv_num,"wlb",&fsize,LMFF_Optional);
    if (buf == NULL)
      return false;
    i = 0;
    for (y=0; y < kfx_sim_state.map_tiles_y; y++)
      for (x=0; x < kfx_sim_state.map_tiles_x; x++)
      {
        slb = get_slabmap_block(x,y);
        n = buf[i];
        slb->wlb_type = buf[i];
        int64_t slbkind = slab_kind_from_wlb_type(n);
        struct SlabConfigStats* slabst = get_slab_stats(slb);
        if ((n != WlbT_None) && ((slbkind < 0) || ((slabst->wlb_type != WlbT_Bridge) && (slb->kind != slbkind)))) {
            nfixes++;
            slb->wlb_type = WlbT_None;
        }
        i++;
      }
    free(buf);
    if (nfixes > 0)
    {
      ERRORLOG("WLB file is muddled - Fixed values for %" PRIu64 " tiles",(uint64_t)(nfixes));
    }
    return true;
}

TbBool initialise_extra_slab_info(uint64_t lv_num)
{
    initialise_map_rooms();
    TbBool result = load_map_wlb_file(lv_num);
    if (!result)
      result = initialise_map_wlb_auto();
    return result;
}

int64_t load_map_slab_file(uint64_t lv_num)
{
    SYNCDBG(7,"Starting");
    struct SlabMap *slb;
    uint64_t x;
    uint64_t y;
    unsigned char *buf;
    uint64_t i;
    uint64_t n;
    int64_t fsize;
    fsize = 2*kfx_sim_state.map_tiles_y*kfx_sim_state.map_tiles_x;
    buf = load_single_map_file_to_buffer(lv_num,"slb",&fsize,LMFF_None);
    if (buf == NULL)
      return false;
    i = 0;
    for (y = 0; y < kfx_sim_state.map_tiles_y; y++)
    {
        for (x = 0; x < kfx_sim_state.map_tiles_x; x++)
        {
            slb = get_slabmap_block(x, y);
            n = lword(&buf[i]);
            if (n > kfx_config_state.conf.slab_conf.slab_types_count)
            {
                WARNMSG("Found invalid Slab Type %" PRId64 " at Tile %" PRId64 ",%" PRId64 ", exceeds limit of %" PRId64, (int64_t)n, (int64_t)(x), (int64_t)(y), (int64_t)(kfx_config_state.conf.slab_conf.slab_types_count));
                n = SlbT_ROCK;
            }
            slb->kind = n;
            i += 2;
        }
    }
    free(buf);
    initialise_map_collides();
    initialise_map_health();
    initialise_extra_slab_info(lv_num);
    return true;
}

int64_t load_map_flag_file(uint64_t lv_num)
{
    SYNCDBG(5,"Starting");
    int64_t fsize = 2 * (kfx_sim_state.map_subtiles_y + 1) * (kfx_sim_state.map_subtiles_x + 1);
    unsigned char* buf = load_single_map_file_to_buffer(lv_num, "flg", &fsize, LMFF_Optional);
    if (buf == NULL)
        return false;
    uint64_t i = 0;
    for (uint64_t stl_y = 0; stl_y < (kfx_sim_state.map_subtiles_y + 1); stl_y++)
    {
        for (uint64_t stl_x = 0; stl_x < (kfx_sim_state.map_subtiles_x + 1); stl_x++)
        {
            struct Map* mapblk = get_map_block_at(stl_x, stl_y);
            mapblk->flags = buf[i];
            i += 2;
        }
    }
    free(buf);
    return true;
}

static TbBool load_static_light_file(uint64_t lv_num)
{
    int64_t fsize = 4;
    unsigned char* buf = load_single_map_file_to_buffer(lv_num, "lgt", &fsize, LMFF_Optional);
    if (buf == NULL)
        return false;
    light_initialise();
    uint64_t i = 0;
    int64_t total = llong(&buf[i]);
    i += 4;
    // Validate total amount of lights
    if ((total < 0) || (total > (fsize-4)/sizeof(struct LegacyInitLight)))
    {
        total = (fsize-4)/sizeof(struct LegacyInitLight);
        WARNMSG("Bad amount of static lights in LGT file; corrected to %" PRId64 ".",(int64_t)(total));
    }
    if (total >= LIGHTS_COUNT)
    {
        WARNMSG("Only %" PRId64 " static lights supported, LGT file has %" PRId64 ".",(int64_t)(LIGHTS_COUNT),(int64_t)(total));
        total = LIGHTS_COUNT-1;
    } else
    if (total >= LIGHTS_COUNT/2)
    {
        WARNMSG("More than %" PRId64 "%% " PRIo64 "f light slots used by static lights.",(int64_t)(100*total/LIGHTS_COUNT));
    }
    // Create the lights
    for (int64_t k = 0; k < total; k++)
    {
        struct LegacyInitLight legilght;
        struct InitLight ilght;
        memcpy(&legilght, &buf[i], sizeof(struct LegacyInitLight));
        ilght.attached_slb = legilght.attached_slb;
        ilght.flags      = legilght.flags;
        ilght.intensity    = legilght.intensity;
        ilght.is_dynamic   = legilght.is_dynamic;
        ilght.radius       = legilght.radius;
        ilght.mappos.x.val = legilght.mappos.x.val;
        ilght.mappos.y.val = legilght.mappos.y.val;
        ilght.mappos.z.val = legilght.mappos.z.val;

        if (light_create_light(&ilght) == 0)
        {
            WARNLOG("Couldn't allocate static light %" PRId64,(int64_t)k);
        }
        i += sizeof(struct LegacyInitLight);
    }
    free(buf);
    return true;
}

static TbBool load_lgtfx_file(uint64_t lv_num)
{
    // load_kfx_toml_file returns -1 on a missing/invalid file (see its other
    // callers, load_aptfx_file/load_tngfx_file, which compare against 0
    // explicitly) -- assigning that raw int straight to a TbBool made every
    // outcome truthy, including -1, so load_level_file's "!load_lgtfx_file()
    // -> fall back to classic .lgt" branch never actually triggered for
    // maps that only ship classic-format files (no .lgtfx/.aptfx/.tngfx).
    TbBool ret = load_kfx_toml_file(lv_num, "lgtfx", "LGTFX",
                             "light", "LightsCount", "light%" PRId64, (int64_t)(LIGHTS_COUNT - 1),
                             &light_create_light_adv) >= 0;
    if (light_count_lights() > LIGHTS_COUNT / 2)
    {
        WARNMSG("More than %" PRId64 "%% " PRIo64 "f light slots used by static lights.", (int64_t)(100*light_count_lights()/LIGHTS_COUNT));
    }
    return ret;
}

int64_t load_and_setup_map_info(uint64_t lv_num)
{
    int64_t fsize = 1;
    unsigned char* buf = load_single_map_file_to_buffer(lv_num, "inf", &fsize, LMFF_None);
    if (buf == NULL)
    {
        kfx_config_state.texture_id = 0;
        return false;
    }
    kfx_config_state.texture_id = buf[0];
    free(buf);
    return true;
}

static void load_ext_slabs(LevelNumber lvnum)
{
    int64_t fgroup = get_level_fgroup(lvnum);
    char* fname = prepare_file_fmtpath(fgroup, "map%05" PRIu64 ".slx", (uint64_t)lvnum);
    if (LbFileExists(fname))
    {
        if (kfx_sim_state.map_tiles_x * kfx_sim_state.map_tiles_y != LbFileLoadAt(fname, kfx_config_state.slab_ext_data))
        {
            JUSTLOG("Invalid ExtSlab data from %s", fname);
            memset(kfx_config_state.slab_ext_data, 0, sizeof(kfx_config_state.slab_ext_data));
        }
        SYNCDBG(1, "ExtSlab file:%s ok", fname);
    }
    else
    {
        SYNCDBG(1, "No ExtSlab file:%s", fname);
        memset(kfx_config_state.slab_ext_data, 0, sizeof(kfx_config_state.slab_ext_data));
    }
    memcpy(&kfx_config_state.slab_ext_data_initial,&kfx_config_state.slab_ext_data, sizeof(kfx_config_state.slab_ext_data));
}

void load_map_string_data(struct GameCampaign *campgn, LevelNumber lvnum, int64_t fgroup)
{
    uint8_t lang_id = install_info.lang_id;
    char* fname = prepare_file_fmtpath(fgroup, "map%05" PRIu64 ".%s.dat", (uint64_t)lvnum, get_language_lwrstr(lang_id));
    if (!LbFileExists(fname))
    {
        SYNCDBG(9, "Map string file %s doesn't exist.", fname);
        char buf[2048];
        buf[0] = 0;
        memcpy(&buf, fname, 2048);
        fname = prepare_file_fmtpath(fgroup, "map%05" PRIu64 ".%s.dat", (uint64_t)lvnum, get_language_lwrstr(campgn->default_language));
        lang_id = campgn->default_language;
        if (strcasecmp(fname, buf) == 0)
        {
            return;
        }
        if (!LbFileExists(fname))
        {
            SYNCDBG(9, "Map string file %s doesn't exist.", fname);
            return;
        }
    }
    int64_t filelen = LbFileLengthRnc(fname);
    if (filelen <= 0)
    {
        ERRORLOG("Map Strings file %s does not exist or can't be opened", fname);
        return;
    }
    size_t size = filelen + 256;
    char* raw_data = calloc(size, sizeof(char));
    if (raw_data == NULL)
    {
        ERRORLOG("Can't allocate memory for Map Strings data");
        return;
    }
    int64_t loaded_size = LbFileLoadAt(fname, raw_data );
    if (loaded_size < 16)
    {
        ERRORLOG("Map Strings file couldn't be loaded or is too small");
        free(raw_data);
        return;
    }

    size_t out_buf_size = (size_t)loaded_size * 4 + 1;
    level_strings_data = (char *)calloc(out_buf_size, 1);
    if (level_strings_data == NULL)
    {
        free(raw_data);
        ERRORLOG("Can't allocate memory for UTF-8 GUI Strings data \"%s\"",fname);
        return;

    }

    size_t utf8_size = convert_codepage_to_utf8_buffer(raw_data, (size_t)loaded_size, level_strings_data, out_buf_size, lang_id);
    free(raw_data);
    if (utf8_size == 0)
    {
        free(level_strings_data);
        ERRORLOG("GUI Strings file couldn't be converted to UTF-8 \"%s\"",fname);
        return;
    }

    level_strings_data = realloc(level_strings_data, utf8_size + 1);

    uint64_t loaded_strings_count = count_strings(level_strings_data, utf8_size);
    char* strings_data_end = level_strings_data + utf8_size;
    // Resetting all values to empty strings
    reset_strings(level_strings, STRINGS_MAX);
    // Analyzing strings data and filling correct values
    TbBool result = fill_strings_list(level_strings, level_strings_data, strings_data_end, STRINGS_MAX);
    if (result)
    {
        SYNCMSG("Loaded %" PRIu64 " strings from %s", (uint64_t)(loaded_strings_count), fname);
    }
    SYNCDBG(19, "Finished");
}

// docs/refactor/editor/phase3/00-slice1-native-save.md -- see this
// function's own declaration (lvl_filesdk1.h) for why it exists. Mirrors
// create_blank_map()'s own "Pass 2" loop (this file, below): every slab's
// kind is already final by the time this runs (called from load_level_file()
// only after load_map_slab_file() has set every slb->kind from .slb), so
// deriving columns from it needs no neighbour-kind ordering care beyond
// what place_single_slab_type_on_map() itself already does per slab.
//
// Deliberately does NOT also call initialise_map_collides()/
// initialise_map_health()/initialise_extra_slab_info() -- load_map_slab_file()
// already calls all three unconditionally, and both are purely functions of
// each slab's own kind (not the column table this regenerates), so they're
// already correct regardless of whether .dat/.clm were present.
TbBool regenerate_derived_map_data(void)
{
    for (MapSlabCoord y = 0; y < kfx_sim_state.map_tiles_y; y++)
    {
        for (MapSlabCoord x = 0; x < kfx_sim_state.map_tiles_x; x++)
        {
            struct SlabMap *slb = get_slabmap_block(x, y);
            if (slb->kind >= kfx_config_state.conf.slab_conf.slab_types_count)
            {
                ERRORLOG("Cannot regenerate derived map data: slab (%" PRId64 ",%" PRId64 ") has invalid kind %" PRId64,
                    (int64_t)x, (int64_t)y, (int64_t)slb->kind);
                return false;
            }
            place_single_slab_type_on_map(slb->kind, x, y, slabmap_owner(slb));
        }
    }
    return true;
}

static TbBool load_level_file(LevelNumber lvnum)
{
    TbBool result;
    TbBool new_format = true;
    int64_t fgroup = get_level_fgroup(lvnum);
    char* fname = prepare_file_fmtpath(fgroup, "map%05" PRIu64 ".slb", (uint64_t)lvnum);
    if (LbFileExists(fname))
    {
        result = true;
        struct GameCampaign *campgn = &campaign;
        load_map_string_data(campgn, lvnum, fgroup);
        // .dat/.clm success is tracked (not discarded, as it used to be --
        // docs/refactor/editor/phase3/00-slice1-native-save.md) so the
        // column table can be regenerated from slab kinds below once
        // load_map_slab_file() has set them, instead of silently loading
        // with an empty/stale column table when either file is absent (a
        // KFX-native editor save never writes .dat/.clm at all).
        TbBool has_dat = load_map_data_file(lvnum);
        load_map_flag_file(lvnum);
        TbBool has_clm = load_column_file(lvnum);
        init_whole_blocks();
        load_slab_file();
        init_columns();
        result = load_lgtfx_file(lvnum);
        if (!result)
        {
            new_format = false;
            result = load_static_light_file(lvnum);
        }
        if (!load_map_ownership_file(lvnum))
          result = false;
        load_map_wibble_file(lvnum);
        load_and_setup_map_info(lvnum);
        render_load_texture_map_file(kfx_config_state.texture_id, lvnum, fgroup);
        if (new_format)
        {
            load_aptfx_file(lvnum);
        }
        else
        {
            load_action_point_file(lvnum);
        }
        if (!load_map_slab_file(lvnum))
          result = false;
        // Regenerate the column table from the slab kinds load_map_slab_file()
        // just set, above, if .dat/.clm weren't both available -- see
        // regenerate_derived_map_data()'s own comment. Only reachable once
        // slab kinds are final, which is why this can't run any earlier in
        // this function. A regen failure is a genuine load failure (an
        // honest error), not silently proceeding with an incomplete map.
        else if ((!has_dat || !has_clm) && !regenerate_derived_map_data())
          result = false;
        if (new_format)
        {
            if (!load_tngfx_file(lvnum))
                result = false;
        }
        else
        {
            if (!load_thing_file(lvnum))
                result = false;
        }
        reinitialise_map_rooms();
        ceiling_init();
        init_keys();
        if (result)
        {
            load_ext_slabs(lvnum);
        }
    } else
    {
        ERRORLOG("The level \"map%05" PRIu64 "\" doesn't exist; creating empty map.",(uint64_t)(lvnum));
        init_whole_blocks();
        load_slab_file();
        init_columns();
        kfx_config_state.texture_id = 0;
        render_load_texture_map_file(kfx_config_state.texture_id, lvnum, fgroup);
        init_top_texture_to_cube_table();
        result = false;
    }
    // Apply per-level sound overrides if levels/mapNNNNN.sounds.cfg exists.
    // (Snapshot was already restored before load_stats_files in main_game.c.)
    load_level_sounds_config(fgroup, lvnum);
    return result;
}

TbBool load_map_file(LevelNumber lvnum)
{
    TbBool result = load_level_file(lvnum);
    if (result)
        set_loaded_level_number(lvnum);
    else
        set_loaded_level_number(SINGLEPLAYER_NOTSTARTED);
    return result;
}

// docs/refactor/editor/01-entry-and-editor-session.md §5 -- "New Map".
//
// Builds the map exactly the way load_map_slab_file() + load_level_file()'s
// own tail build a *real* one, rather than through place_slab_type_on_map()
// (the terrain cheat/editor brush's live-mutation entry point): assign
// every slab's kind as plain data first (mirroring load_map_slab_file()'s
// own `slb->kind = n` loop), derive columns from those kinds via
// place_single_slab_type_on_map() (the doc's own gap analysis names this
// as the safe "rebuild columns from slabset config" primitive -- it does
// no navigation/ceiling work itself), then run the same
// initialise_map_collides/health/extra_slab_info + reinitialise_map_rooms/
// ceiling_init/init_keys tail load_level_file() runs once over the whole,
// already-consistent map.
//
// Found live (SIGABRT inside ceiling_partially_recompute_heights, backtrace
// through create_blank_map <- place_slab_type_on_map_f): place_slab_type_on_map()
// additionally calls update_blocks_around_slab() -> update_blocks_in_area()
// -> navigation triangulation + a *partial* ceiling recompute for the area
// around each just-painted slab -- correct for a single edit to an already-
// finalized map (real gameplay's only use of it), but unsafe called
// incrementally while the map has no ceiling/navigation data at all yet.
// Building the whole map as data first and finalizing once, like a real
// load does, avoids that code path entirely during construction.
TbBool create_blank_map(LevelNumber lvnum, MapSlabCoord tiles_x, MapSlabCoord tiles_y, int64_t texture_set)
{
    int64_t fgroup = get_level_fgroup(lvnum);
    set_map_size(tiles_x, tiles_y);
    // clear_game()'s own clear_mapmap()/clear_slabs()/clear_columns() (main_game.c's
    // init_level(), well before this function runs) sized their clearing loops off
    // whatever kfx_sim_state.map_subtiles_x/map_tiles_x/y held *at that point* --
    // the previously loaded level's dimensions, or 0 if none has loaded yet this
    // process (e.g. going straight from launch to Tools -> Editor -> New Map, the
    // exact sequence this was found under) -- not this map's just-set size. With
    // nothing else in the load path re-clearing for the real size, subtiles/slabs/
    // columns beyond whatever tiny rectangle got cleared earlier keep whatever data
    // was already sitting in that (session-lifetime, not per-level) storage --
    // explains the "invalid thing"/"can't find door" spam seen live (stale mapwho/
    // attached-thing references) and is the leading suspect for the black viewport
    // and unresponsive session reported alongside it. Re-clear now that the size is
    // actually final.
    clear_mapmap();
    clear_slabs();
    clear_columns();
    init_whole_blocks();
    load_slab_file();
    init_columns();
    kfx_config_state.texture_id = texture_set;
    render_load_texture_map_file(kfx_config_state.texture_id, lvnum, fgroup);
    init_top_texture_to_cube_table();

    // Every slab starts as SlbT_ROCK ("HARD" in the palette) -- solid,
    // impenetrable, uniform. Not earth-with-a-rock-border: a blank map is
    // a solid block the editor player carves, matching the original
    // editor's own "New Map" starting state: nothing pre-dug, no assumed
    // dig plan. Also sidesteps needing a border/interior distinction at
    // all -- one slab kind, no earth-vs-rock adjacency cases to get wrong.
    //
    // Pass 1: every slab's kind + owner, as plain data (load_map_slab_file()'s
    // own approach) -- place_single_slab_type_on_map()'s column/style
    // derivation below inspects neighbouring slabs' kind, so every kind
    // must already be final before pass 2 starts.
    for (MapSlabCoord y = 0; y < tiles_y; y++)
    {
        for (MapSlabCoord x = 0; x < tiles_x; x++)
        {
            struct SlabMap *slb = get_slabmap_block(x, y);
            slb->kind = SlbT_ROCK;
            set_slab_owner(x, y, kfx_config_state.neutral_player_num);
        }
    }
    // Pass 2: derive columns from the now-final slab kinds.
    for (MapSlabCoord y = 0; y < tiles_y; y++)
    {
        for (MapSlabCoord x = 0; x < tiles_x; x++)
        {
            place_single_slab_type_on_map(SlbT_ROCK, x, y, kfx_config_state.neutral_player_num);
        }
    }

    initialise_map_collides();
    initialise_map_health();
    initialise_extra_slab_info(lvnum);

    reinitialise_map_rooms();
    ceiling_init();
    init_keys();

    set_loaded_level_number(lvnum);
    return true;
}

void free_level_strings_data()
{
  // Resetting all values to empty strings
  reset_strings(level_strings, STRINGS_MAX);
  // Freeing memory
  free(level_strings_data);
  level_strings_data = NULL;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
