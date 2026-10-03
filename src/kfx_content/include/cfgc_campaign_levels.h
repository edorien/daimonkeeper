/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_campaign_levels.h
 *     docs/refactor/editor/fx-plans/08 §10.3 (K3) -- the campaign's level lists as a model: single levels with their
 *     parallel bonus level, the extra levels; list edits, number allocation, the text of a list, and the default entry
 *     of a new level.
 * @par Comment:
 *     Pure. SINGLE_LEVELS and BONUS_LEVELS are parallel (bonus[i] belongs to single[i], 0 = none); EXTRA_LEVELS stands alone.
 */
/******************************************************************************/
#ifndef DK_CFGC_CAMPAIGN_LEVELS_H
#define DK_CFGC_CAMPAIGN_LEVELS_H

#include <cstdint>
#include <string>
#include <vector>

#include "cfgc_content.h"
#include "cfgc_writer.h"

struct CampaignLevels
{
    std::vector<int64_t> single;
    std::vector<int64_t> bonus; // same length as `single` (padded with 0)
    std::vector<int64_t> extra;

    /** True when `n` is in any list. */
    bool listed(int64_t n) const;
    /** Every listed level (bonus 0 excluded). */
    std::vector<int64_t> all() const;
};

enum CampaignListKind { CampList_Single = 0, CampList_Bonus, CampList_Extra };

/** Reads the three lists from the file's `[common]` block. */
CampaignLevels cfgc_read_levels(const ConfigContent &content);

/** Appends level `n` to a list (single: a bonus slot of 0 is added). No effect when `n` is already listed. */
void cfgc_levels_add(CampaignLevels &lv, CampaignListKind kind, int64_t n);
/** Sets the bonus level paired with single level `index`. */
void cfgc_levels_set_bonus(CampaignLevels &lv, size_t index, int64_t bonus);
/** Moves entry `index` of a list up (-1) or down (+1); single moves its bonus with it. */
void cfgc_levels_move(CampaignLevels &lv, CampaignListKind kind, size_t index, int delta);
/** Removes entry `index` of a list (a single level also drops its bonus level from the lists). */
void cfgc_levels_remove(CampaignLevels &lv, CampaignListKind kind, size_t index);

/** The level number a new level gets (decision c): next after the highest listed (1 for an empty campaign); bonus and
 *  extra levels start at 100 (or after the highest of their own range). */
int64_t cfgc_next_level_number(const CampaignLevels &lv, CampaignListKind kind);

/** The column pitch of an existing list value ("300   301" -> 6), or 0 when there is none to copy. */
size_t cfgc_list_pitch(const std::string &value);
/** The text of a list: each number left-aligned in `pitch` columns (a minimum of the widest number plus one when 0). */
std::string cfgc_format_level_list(const std::vector<int64_t> &numbers, size_t pitch);

/** Changes turning the file's lists into `lv`: SINGLE / BONUS / EXTRA_LEVELS as new values (EXTRA is reset when empty),
 *  with the pitch of what the file has now. */
ChangeSet cfgc_levels_changes(const ConfigContent &current, const CampaignLevels &lv);

/** Changes creating the `[mapNNNNN]` entry of a new level with a working ensign, spread over the land view by `slot`. */
ChangeSet cfgc_default_entry(int64_t level, int64_t slot, const std::string &name);

/** The campaign file text with level `n` added to a list (Single or Extra) and given a default `[mapNNNNN]` entry named `name`
 *  when it has none. An already listed level only gets the entry. `changed` (may be null) says whether the text differs. */
std::string cfgc_campaign_add_level(const std::string &file_text, int64_t n, CampaignListKind kind, const std::string &name, bool *changed);

/** The campaign file text with level `n` taken out of the lists (a single level's bonus level goes with it; a bonus level only frees its
 *  slot) and, with `remove_entry`, its `[mapNNNNN]` entry removed. `changed` (may be null) says whether the text differs. */
std::string cfgc_campaign_remove_level(const std::string &file_text, int64_t n, bool remove_entry, bool *changed);

#endif
