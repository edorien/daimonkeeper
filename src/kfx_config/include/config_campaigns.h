/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file config_campaigns.h
 *     Header file for config_campaigns.c.
 * @par Purpose:
 *     Campaigns handling functions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     05 Jan 2009 - 12 Jan 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_CFGCMPGNS_H
#define DK_CFGCMPGNS_H

#include "globals.h"
#include "bflib_basics.h"

#include "config.h"
#include "config_strings.h"
#include "config_mods.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define CAMPAIGN_LEVELS_COUNT        50
#define EXTRA_LEVELS_COUNT           10
#define MULTI_LEVELS_COUNT         1000
#define FREE_LEVELS_COUNT          5000
#define VISIBLE_HIGH_SCORES_COUNT    10
#define LEVEL_INFO_GROW_DELTA        32
#define HISCORE_NAME_LENGTH          64
#define CAMPAIGNS_LIST_GROW_DELTA     8
#define CAMPAIGN_CREDITS_COUNT      360
/** Strings length */
#define CAMPAIGN_FNAME_LEN           64
#define CAMPAIGN_DESCRIPTION_LEN    256
#define LEVEL_DESCRIPTION_LEN       256
#define LEVEL_AUTHOR_LEN            64

enum CreditsItemKind {
    CIK_None,
    CIK_EmptyLine,
    CIK_DirectText,
    CIK_StringId,
};

enum LandMarkings {
    LndMk_ENSIGNS,
    LndMk_PINPOINTS,
};

enum CampaignTypes {
    CampgnT_Default,
    CampgnT_Campaign,
    CampgnT_Mappack,
    CampgnT_MultiplayerMappack,
    
    CampgnT_COUNT,
};

/******************************************************************************/
struct CreditsItem {
  int64_t kind;
  int64_t font;
  union {
    int64_t num;
    char *str;
  };
};

/*
 * Structure for storing campaign configuration.
 */
struct GameCampaign {
  int64_t fgroup;
  char name[LINEMSG_SIZE];
  char display_name[LINEMSG_SIZE];
  char description[CAMPAIGN_DESCRIPTION_LEN];
  char fname[DISKPATH_SIZE];
  char levels_location[DISKPATH_SIZE];
  char speech_location[DISKPATH_SIZE];
  char land_location[DISKPATH_SIZE];
  char creatures_location[DISKPATH_SIZE];
  char configs_location[DISKPATH_SIZE];
  char media_location[DISKPATH_SIZE];
  LevelNumber single_levels[CAMPAIGN_LEVELS_COUNT];
  LevelNumber multi_levels[MULTI_LEVELS_COUNT];
  LevelNumber bonus_levels[CAMPAIGN_LEVELS_COUNT];
  LevelNumber extra_levels[EXTRA_LEVELS_COUNT];
  LevelNumber freeplay_levels[FREE_LEVELS_COUNT];
  uint64_t single_levels_count;
  uint64_t multi_levels_count;
  uint64_t bonus_levels_count;
  uint64_t extra_levels_count;
  uint64_t freeplay_levels_count;
  uint64_t bonus_levels_index;
  uint64_t extra_levels_index;
  struct LevelInformation *lvinfos;
  uint64_t lvinfos_count;
  // Land view
  uint64_t ambient_good;
  uint64_t ambient_bad;
  char land_view_start[DISKPATH_SIZE];
  char land_window_start[DISKPATH_SIZE];
  char land_view_end[DISKPATH_SIZE];
  char land_window_end[DISKPATH_SIZE];
  unsigned char land_markers;
  char movie_intro_fname[DISKPATH_SIZE];
  char movie_outro_fname[DISKPATH_SIZE];
  // Credits
  char credits_fname[DISKPATH_SIZE];
  char *credits_data;
  struct CreditsItem credits[CAMPAIGN_CREDITS_COUNT];
  // Campaign strings
  unsigned char strings_lang;
  char strings_fname[DISKPATH_SIZE];
  char strings_fname_eng[DISKPATH_SIZE];
  char *strings_data_list[MOD_ITEM_MAX*MOD_ITEM_TYPE_CNT+1];
  int64_t strings_data_count;
  char *strings[STRINGS_MAX+1];
  // High scores
  char hiscore_fname[DISKPATH_SIZE];
  struct HighScore *hiscore_table;
  uint64_t hiscore_count;
  // Human player color
  int64_t human_player;
  TbBool assignCpuKeepers;
  unsigned char default_language;
  char soundtrack_fname[DISKPATH_SIZE];
};

// Written verbatim to the campaign's high-score file (highscores.c), so the
// layout is the file format: {score, name[64], lvnum} = 72 bytes/entry as in
// the original game. `score` was a plain `long`, which made sizeof 80 on
// 64-bit Linux (LP64) and rejected every 72-byte file written by Windows/Wine
// (or the original game) as "bad", regenerating -- i.e. wiping -- the table.
// Keep it (and lvnum) fixed-width; see highscores_test.cpp.
struct HighScore {
  int64_t score;
  char name[HISCORE_NAME_LENGTH];
  LevelNumber lvnum;
};

enum SkirmishSetupOption {
  SkirmishSetup_Auto = 0, /**< not stated: the tab classifies the script itself */
  SkirmishSetup_Allow,    /**< author says the tab may be used (no extra effect today) */
  SkirmishSetup_Locked,   /**< author disables the tab for this level */
};

struct LevelInformation {
  LevelNumber lvnum;
  char speech_before[DISKPATH_SIZE];
  char speech_after[DISKPATH_SIZE];
  char land_view[DISKPATH_SIZE];
  char land_window[DISKPATH_SIZE];
  char name[LINEMSG_SIZE];
  char description[LEVEL_DESCRIPTION_LEN];
  char author[LEVEL_AUTHOR_LEN];
  TextStringId name_stridx;
  int64_t players;
  int64_t ensign_x;
  int64_t ensign_y;
  int64_t ensign_zoom_x;
  int64_t ensign_zoom_y;
  uint64_t level_type;
  int64_t ensign_type;
  int64_t state;
  int64_t location;
  int64_t mapsize_x;
  int64_t mapsize_y;  
  /** Author's say over the Skirmish setup tab (.lof SKIRMISH_SETUP): a SkirmishSetupOption. */
  unsigned char skirmish_setup;
};

struct CampaignsList {
  struct GameCampaign *items;
  uint64_t items_num;
  uint64_t items_count;
};

/******************************************************************************/
extern struct GameCampaign campaign;
extern struct CampaignsList campaigns_list;
extern struct CampaignsList mappacks_list;
extern struct CampaignsList mp_mappacks_list;
extern const enum TbFileGroups cmpgn_fgroup[CampgnT_COUNT];
extern const char* cmpgn_prefix[CampgnT_COUNT];
extern const struct NamedCommand cmpgn_common_commands[];
extern const struct NamedCommand cmpgn_level_markers_options[];
extern const struct NamedCommand cmpgn_map_commands[];
extern const struct NamedCommand cmpgn_map_ensign_flag_options[];
extern const struct NamedCommand cmpgn_map_skirmish_setup_options[];
extern const struct NamedCommand cmpgn_map_cmnds_kind[];
extern const struct NamedCommand cmpgn_human_player_options[];
/******************************************************************************/
TbBool load_campaign(const char *cmpgn_fname,struct GameCampaign *campgn,int64_t flags, int64_t fgroup);
TbBool free_campaign(struct GameCampaign *campgn);
void clear_level_info(struct LevelInformation *lvinfo);
TbBool clear_campaign(struct GameCampaign *campgn);
int64_t add_single_level_to_campaign(struct GameCampaign *campgn, LevelNumber lvnum);
int64_t add_multi_level_to_campaign(struct GameCampaign *campgn, LevelNumber lvnum);
int64_t add_bonus_level_to_campaign(struct GameCampaign *campgn, LevelNumber lvnum);
int64_t add_extra_level_to_campaign(struct GameCampaign *campgn, LevelNumber lvnum);
int64_t add_freeplay_level_to_campaign(struct GameCampaign *campgn,LevelNumber lvnum);
// Level info support for given campaign
struct LevelInformation *get_campaign_level_info(struct GameCampaign *campgn, LevelNumber lvnum);
TbBool init_level_info_entries(struct GameCampaign *campgn, int64_t num_entries);
TbBool grow_level_info_entries(struct GameCampaign *campgn, int64_t add_entries);
struct LevelInformation *new_level_info_entry(struct GameCampaign *campgn, LevelNumber lvnum);
// Support for lists of campaigns
TbBool init_campaigns_list_entries(struct CampaignsList *clist, int64_t num_entries);
TbBool grow_campaigns_list_entries(struct CampaignsList *clist, int64_t add_entries);
TbBool load_campaigns_list(struct CampaignsList *clist, int64_t fgroup, const char* list_name, const char* order_fname);
TbBool change_campaign(uint8_t pack, const char *cmpgn_fname);
TbBool is_campaign_loaded(void);
TbBool is_campaign_in_list(const char *cmpgn_fname, struct CampaignsList *clist);
TbBool swap_campaigns_in_list(struct CampaignsList *clist, int64_t idx1, int64_t idx2);
void sort_campaigns_quicksort(struct CampaignsList *clist, int64_t beg, int64_t end);
uint8_t prepare_campaign_file_name(const char *cmpgn_fname, char *cmpgn_file, int64_t cmpgn_file_len);
void get_campaign_sanitized_id(const char *cmpgn_fname, char *out, size_t outlen);
TbBool is_map_pack(void);
void set_default_mp_mappack(void);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
