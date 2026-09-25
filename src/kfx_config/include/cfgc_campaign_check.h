/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_campaign_check.h
 *     docs/refactor/editor/fx-plans/08 §10.4 -- the campaign checker: what is wrong with a campaign
 *     `.cfg` and the files it refers to, without loading anything into the game.
 * @par Comment:
 *     Pure: the file system and the other campaigns come in through `CampaignCheckEnv`, so it runs on
 *     any file (and in unit tests over the shipped ones).
 */
/******************************************************************************/
#ifndef DK_CFGC_CAMPAIGN_CHECK_H
#define DK_CFGC_CAMPAIGN_CHECK_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "cfgc_document.h"
#include "cfgc_schema.h"

struct CampaignPeer
{
    std::string name;
    std::string fname; // its .cfg file name (compared with CampaignCheckEnv::own_fname)
    std::string cfg_dir;  // resolved CONFIGS_LOCATION
    std::string crtr_dir; // resolved CREATURES_LOCATION
};

struct CampaignCheckEnv
{
    std::string root;      // what campaign-relative locations resolve against
    std::string own_fname; // the checked campaign's .cfg file name (excluded from the sharing test)
    std::function<bool(const std::string &)> file_exists;  // a regular file
    std::function<bool(const std::string &)> dir_exists;
    std::function<int64_t(const std::string &)> file_size; // -1 when missing
    std::function<std::string(const std::string &, size_t)> read_prefix; // first bytes of a file (PNG size check); may be null
    std::vector<CampaignPeer> peers; // every campaign and pack the game knows (may include the checked one)
};

/** The findings, in file order (line 0: not tied to a line). Codes:
 *  Errors  level_files_missing, level_duplicate, human_player, map_entry_duplicate.
 *  Warnings bonus_length, location_missing, location_shared, level_unlisted, entry_unused, landview_missing, landview_size (a PNG that is not 1280 x 960, a .pal that is not 768 bytes; a .raw may be compressed, so its size is not checked), landview_frame / speech_pair (a LAND_VIEW or SPEECH with one name), speech_missing, strings_missing,
 *          strings_short, not_a_header, unknown_key (from the schema).
 *  Info    entry_no_level_list. */
std::vector<CfgDiagnostic> cfgc_check_campaign(const ConfigDocument &doc, const ConfigSchema &schema, const CampaignCheckEnv &env);

/** Pure: the level numbers of a list value ("300 301 302"), in order; words that are not numbers are skipped. */
std::vector<int64_t> cfgc_parse_level_list(const std::string &value);

#endif
