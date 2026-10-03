/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file frontgui_compat_badges.h
 *     "Needs a newer KeeperFX" markers for the level and campaign lists.
 * @par Purpose:
 *     Whether a level's script -- or any level of a campaign -- uses commands
 *     this build doesn't know (kfx_game's script_preflight_file()), found
 *     without starting it, so the lists can mark those entries before the
 *     player picks one (docs/rebadge, local notes: the pre-play warning's
 *     earlier sibling).
 *     Results are cached per file and re-checked when the file changes;
 *     scanning is spread over frames (a few files each) so a long list
 *     never stalls: an entry reads "not known yet" until it's been scanned.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_FRONTGUI_COMPAT_BADGES_H
#define DK_FRONTGUI_COMPAT_BADGES_H

#include "globals.h"

#include <string>

struct GameCampaign;

enum FeCompatState {
    FeCompat_NotKnownYet,   // not scanned yet (see the per-frame budget)
    FeCompat_Supported,     // nothing this build doesn't know (or no script)
    FeCompat_Unsupported,   // uses commands this build doesn't know
};

/** One level of a campaign or map pack. `detail` (optional) gets a line for a tooltip. */
enum FeCompatState fe_compat_level_state(const struct GameCampaign *campgn, LevelNumber lvnum, std::string *detail);
/** Any of a campaign's levels (single-player, bonus and extra). */
enum FeCompatState fe_compat_campaign_state(const struct GameCampaign *campgn, std::string *detail);

/** List-row label with the marker when Unsupported ("Name  [!]"), keeping the
 *  row's ImGui ID stable ("###" + id) whatever the label shows. */
std::string fe_compat_row_label(const char *name, enum FeCompatState state, const char *id);
/** Tooltip for the item just drawn, when there's something to say. */
void fe_compat_row_tooltip(enum FeCompatState state, const std::string &detail);

#endif
