/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_campaign_ops.h
 *     docs/refactor/editor/fx-plans/08 K5b -- what the Map Editor needs of a campaign when a map is saved into it:
 *     the next level number and the registration of the saved level (list entry and `[mapNNNNN]` entry).
 * @par Comment:
 *     Internal to kfx_editor.
 */
/******************************************************************************/
#ifndef DK_CONTENT_CAMPAIGN_OPS_H
#define DK_CONTENT_CAMPAIGN_OPS_H

#include <cstdint>
#include <string>

#include "cfgc_campaign_levels.h"
#include "content_target.h"

#include <vector>

/** The number a new level of the campaign (its .cfg file name) gets: next after the highest listed, 1 for an empty one
 *  (decision c); 1 when the campaign is unknown. */
int64_t content_campaign_next_level(const std::string &campaign_fname, CampaignListKind kind);

/** Adds the saved level to the campaign's lists (Single or Extra) and gives it an entry named `name`, then reads the
 *  campaign lists again. A level already listed with an entry changes nothing. */
bool content_campaign_register_level(const std::string &campaign_fname, int64_t n, CampaignListKind kind,
    const std::string &name, std::string *error);

/** Creates a new, empty campaign: its .cfg file (`campgns/<id>.cfg`), its levels folder and, with `own_config`, empty
 *  configuration and creature folders; reads the campaign lists again so it shows in the game at once. Fails, changing
 *  nothing, when the id is already used. */
bool content_campaign_create(const std::string &name, const std::string &id, const std::string &human_player, bool own_config,
    std::string *error);

/** A campaign land-view folder to copy the default images from (`rgmap00` + `viframe00`): keeporig's when present, else the first
 *  any `campgns/..._lnd` folder that has them; empty when there is none. */
std::string content_campaign_default_land_source(void);

/** Whether `id` is free: no campaign .cfg file of that name (any letter case). */
bool content_campaign_id_free(const std::string &id, ContentKind kind = ContentKind_Campaign);

/** The campaign's place in the game's campaign menu (0-based, from the list the game holds) and how many there are; false when unknown. */
bool content_campaign_menu_position(const std::string &campaign_fname, size_t *position, size_t *count,
    ContentKind kind = ContentKind_Campaign);

/** Moves the campaign up (-1) or down (+1) in the menu: writes the whole order to `campgns/campgn_order.txt` (its comment lines kept)
 *  and reads the lists again. */
bool content_campaign_move_in_menu(const std::string &campaign_fname, int delta, std::string *error,
    ContentKind kind = ContentKind_Campaign);

/** Reads the campaign, free-play pack and multiplayer pack lists again (after a file or a level was added). */
void content_campaign_rescan_lists(void);

/** One level of a map pack, read from its map files: the pack's levels are the map%05d.slb files of its folder. */
struct PackLevel
{
    int64_t number = 0;
    std::string name;    // NAME_TEXT of its .lof, else the name in its .lif
    std::string players; // PLAYERS of its .lof
    std::string author;
    size_t files = 0;    // every file of the level (map files, its own configuration, strings, script)
};
std::vector<PackLevel> content_pack_levels(const ContentCampaign &pack);

/** The number a new map in a pack gets: the first free one from 1 in its folder (as Editor Maps does). */
int64_t content_pack_next_level(const ContentCampaign &pack);

/** Creates an empty free-play or multiplayer pack (`levels/<id>.cfg` or `multiplayer/<id>.cfg`, its folder, optional own folders). The game
 *  lists a pack once it has a map, so it shows in this editor (as not listed) before that. */
bool content_pack_create(const std::string &name, const std::string &id, ContentKind kind, const std::string &human_player, bool own_config,
    std::string *error);

/** Copies level `from` of `src` (its map files and the level's own files) into `dst` as level `to`. A campaign target lists it (`list`) with an
 *  entry; `remove_from_source` (a campaign source only) also takes it out of the source's lists and entries -- the source's files always stay.
 *  Nothing is changed when it fails (a taken number, no files). */
bool content_campaign_copy_level(const ContentCampaign &src, int64_t from, const ContentCampaign &dst, int64_t to, CampaignListKind list,
    bool remove_from_source, std::string *error);

#endif
