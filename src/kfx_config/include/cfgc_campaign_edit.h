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

#endif
