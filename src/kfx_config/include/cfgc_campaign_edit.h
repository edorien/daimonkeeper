/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_campaign_edit.h
 *     docs/refactor/editor/fx-plans/08 §11 (f) -- giving a campaign its own configuration folders: the name
 *     of the copy and the folder copy itself, staged in a WriteBatch (all files or none).
 */
/******************************************************************************/
#ifndef DK_CFGC_CAMPAIGN_EDIT_H
#define DK_CFGC_CAMPAIGN_EDIT_H

#include <cstdint>
#include <string>
#include <vector>

#include "cfgc_writebatch.h"

/** Pure: the location text for the campaign's own copy of `location` ("campgns/shared_cfg" ->
 *  "campgns/<stem of cfg_fname>_cfg"): same parent folder, the campaign's file stem plus "_" + `suffix`. */
std::string cfgc_own_location(const std::string &location, const std::string &cfg_fname, const std::string &suffix);

/** Queues a copy of every file under `src` to `dst` (same relative paths). Fails, queueing nothing, when `src`
 *  is not a folder or `dst` already exists and is not empty. */
bool cfgc_plan_folder_copy(const std::string &src, const std::string &dst, WriteBatch &batch, std::string *error);

/** Pure: a folder / file stem for a campaign called `name`: letters, digits and underscores (other characters become "_",
 *  runs collapse, ends trimmed); "campaign" when nothing is left. */
std::string cfgc_campaign_id_from_name(const std::string &name);

/** Pure: the text of a new, empty campaign file (plan 08 K5): `name`, its levels folder `campgns/<id>`, the standard land view,
 *  `human_player` (a colour name) and, when `own_config`, its own configuration and creature folders `campgns/<id>_cfg` /
 *  `_crtr` and, when `own_land`, its land-view folder `campgns/<id>_lnd` (the game finds land-view images only in a campaign's own LAND_LOCATION). It lists level 1 (with an entry) because the game does not list a campaign without a single level, and has `[strings]` / `[speech]` blocks (base game text and speech) because the game refuses to load one without. */
std::string cfgc_new_campaign_text(const std::string &name, const std::string &id, const std::string &human_player, bool own_config,
    bool own_land);

/** Pure: `order` with entry `index` moved by `delta` places (clamped; a move off either end changes nothing). */
std::vector<std::string> cfgc_move_in_order(std::vector<std::string> order, size_t index, int delta);

/** Pure: the text of an order file (campgn_order.txt): the `#` comment lines at the top of `old_text` kept, then one name per line
 *  in `order`, with the file's own line ending. */
std::string cfgc_order_file_text(const std::string &old_text, const std::vector<std::string> &order);

/** A file exists, the last name compared without regard to letter case (the original game's files are upper case, and Linux
 *  file systems care). */
bool cfgc_file_exists_ci(const std::string &path);

/** The files of level `n` in `dir`: every name that starts with `map%05d.` (any letter case), sorted -- the map files and the level's own
 *  configuration, strings and script files. */
std::vector<std::string> cfgc_level_files(const std::string &dir, int64_t n);

/** The level numbers that have a map%05d.slb (any letter case) in `dir`, sorted. */
std::vector<int64_t> cfgc_level_numbers_in_dir(const std::string &dir);

/** Pure: `name` (a file of level `from`) renamed for level `to`, keeping the rest of the name and its letter case. */
std::string cfgc_level_file_rename(const std::string &name, int64_t from, int64_t to);

/** Queues a copy of every file of level `from` in `src_dir` to `dst_dir` as level `to`. Fails, queueing nothing, when the source has no
 *  level files or the destination already has files of level `to`. */
bool cfgc_plan_level_copy(const std::string &src_dir, int64_t from, const std::string &dst_dir, int64_t to, WriteBatch &batch,
    std::string *error);

/** Pure: the text of a new, empty map pack file (free-play `levels/<id>.cfg` or multiplayer `multiplayer/<id>.cfg`; `folder` is "levels" or
 *  "multiplayer"): name, its levels folder `<folder>/<id>`, optional own `_cfg` / `_crtr` folders, human player, and the `[strings]` /
 *  `[speech]` blocks the game needs. Its levels are the map files of the folder, so there are no lists. */
std::string cfgc_new_pack_text(const std::string &name, const std::string &id, const std::string &folder, const std::string &human_player,
    bool own_config);

#endif
