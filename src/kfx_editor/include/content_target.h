/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_target.h
 *     docs/refactor/editor/fx-plans/03-content-editors-foundation.md §3/§5 -- what a content
 *     editor edits: a campaign or mappack (and optionally a level of it) turned into the
 *     explicit directories the config content layer (kfx_content cfgc_*) works on.
 * @par Comment:
 *     Internal to kfx_editor. The pure builders take plain strings; only
 *     content_list_campaigns() reads the engine's campaign lists.
 */
/******************************************************************************/
#ifndef DK_CONTENT_TARGET_H
#define DK_CONTENT_TARGET_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "cfgc_stack.h"

enum ContentKind
{
    ContentKind_Campaign = 0, // campgns/<f>.cfg
    ContentKind_FreePlay,     // levels/<f>.cfg (a map pack: its levels are the map files of its folder)
    ContentKind_Multiplayer   // multiplayer/<f>.cfg
};

struct ContentCampaign
{
    ContentKind kind = ContentKind_Campaign;
    bool listed = true;    // the game lists it (a campaign needs a single level, a pack a map with a .lif file)
    std::string name;      // label for the picker ("Dungeon Keeper original campaign")
    std::string fname;     // the campaign's .cfg file name
    bool is_mappack = false;
    std::string cfg_dir;   // CONFIGS_LOCATION, resolved (empty: the campaign has none)
    std::string crtr_dir;  // CREATURES_LOCATION, resolved
    std::string levels_dir; // LEVELS_LOCATION, resolved
    std::vector<int64_t> levels; // every level the campaign lists, sorted, without duplicates
    std::string cfg_file;        // the campaign's own .cfg file (resolved)
    std::map<std::string, std::string> strings; // [strings] LANG = path: lower-case language code -> resolved path
    std::map<int64_t, std::string> name_ids;    // string ids the campaign file uses (NAME_ID of a [mapNNNNN]): id -> "map00001"
};

/** Directory the game resolves campaign-relative locations against (install path, else the runtime directory). */
std::string content_root(void);

/** Every campaign and mappack the game knows, in list order (campaigns first). Reads engine state. */
std::vector<ContentCampaign> content_list_campaigns(void);

/** Every campaign, free-play pack and multiplayer pack: what the game lists plus the .cfg files it does not list yet (a campaign with no
 *  single level, a pack with no map). Campaigns first, then free-play, then multiplayer packs. Reads engine state and the folders. */
std::vector<ContentCampaign> content_list_everything(void);

/** The .cfg file's folder under the game root for a kind ("campgns", "levels", "multiplayer"). */
const char *content_kind_folder(ContentKind kind);

/** Pure: the ConfigTarget of a campaign (null: base only) and level (-1: none). */
ConfigTarget content_target_make(const std::string &base_dir, const std::string &base_crtr_dir,
    const ContentCampaign *camp, int64_t level_number);

/** Pure: the target of a map open in the map editor -- its own folder as the level layer, no campaign layer. */
ConfigTarget content_target_for_map(const std::string &base_dir, const std::string &base_crtr_dir,
    const std::string &level_dir, int64_t level_number);

/** Pure: the campaign or map pack whose levels folder is `dir` (slashes and a trailing separator do not matter),
 *  or null. */
const ContentCampaign *content_find_campaign_for_dir(const std::vector<ContentCampaign> &list, const std::string &dir);

/** Pure: joins a campaign-relative location onto a root; empty in, empty out. */
std::string content_resolve_location(const std::string &root, const std::string &location);

#endif
