/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_campaign_progress.c
 * @par Purpose:
 *     Phase A of docs/refactor/gui/05-campaign-progress-and-landview.md --
 *     save/progress.cfg, a `[campaign.fname]`-per-campaign text format
 *     (same find_conf_block()/NamedCommand idiom config_campaigns.c uses
 *     for campgns' own per-level .cfg blocks) recording, per campaign:
 *     which levels are unlocked, plus everything the old fx1contn.sav's
 *     binary IntralevelData held (transferred creatures, bonus
 *     availability, campaign script flags, the next level to play, and
 *     per-level ensign overrides) -- see the design doc's §3.1 key table
 *     for the full field-by-field mapping and rationale.
 *
 *     Used only by the new (non-`-classicmenu`) menu. `-classicmenu`
 *     keeps reading/writing fx1contn.sav via game_saves.c's own
 *     unchanged functions -- see the design doc's §3.4 for why the two
 *     paths are deliberately gated apart rather than merged. This file
 *     never writes fx1contn.sav; reconcile_fx1contn_into_progress()
 *     below only ever reads it, one-directionally.
 * @par Comment:
 *     None.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "game_campaign_progress.h"

#include "config.h"           // find_conf_block/recognize_conf_command/get_conf_parameter_*
#include "config_campaigns.h" // campaigns_list
#include "config_creature.h"  // parse_creature_name/creature_code_name
#include "game_saves.h"       // read_continue_game_progress
// is_bonus_level_visible/set_bonus_level_visibility, get_level_ensign_override,
// struct LevelEnsignOverride: game_merge.h, already pulled in via
// game_campaign_progress.h.
#include "player_data.h"      // get_my_player
#include "bflib_basics.h"
#include "bflib_fileio.h"
#include "bflib_dernc.h"      // LbFileLengthRnc/LbFileLoadAt
#include "kfx_memory.h"       // KfxCalloc/KfxFree

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

// Small, fixed cap rather than a dynamic array: campaign counts are tens,
// not thousands, even with a generous helping of installed mods -- see
// the design doc's own reasoning. campaigns_list (config_campaigns.h)
// itself is the source of truth for "what campaigns exist"; this table
// only ever holds an entry for one that has actually been played.
#define CAMPAIGN_PROGRESS_MAX_ENTRIES 64

static struct CampaignProgressEntry campaign_progress_entries[CAMPAIGN_PROGRESS_MAX_ENTRIES];
static uint64_t campaign_progress_entries_num = 0;

static const char *progress_cfg_filename = "progress.cfg";

const struct NamedCommand progress_cfg_commands[] = {
  {"UNLOCKED_LEVELS",   1},
  {"NEXT_LEVEL",         2},
  {"TRANSFER_CREATURE",  3},
  {"BONUS_AVAILABLE",    4},
  {"CAMPAIGN_FLAG",      5},
  {"ENSIGN_OVERRIDE",    6},
  {NULL,                 0},
};

/******************************************************************************/
struct CampaignProgressEntry *get_campaign_progress(const char *cmpgn_fname, TbBool create_if_missing)
{
    for (uint64_t i = 0; i < campaign_progress_entries_num; i++)
    {
        if (strcasecmp(campaign_progress_entries[i].cmpgn_fname, cmpgn_fname) == 0)
            return &campaign_progress_entries[i];
    }
    if (!create_if_missing)
        return NULL;
    if (campaign_progress_entries_num >= CAMPAIGN_PROGRESS_MAX_ENTRIES)
    {
        ERRORLOG("Cannot track progress for campaign \"%s\" -- CAMPAIGN_PROGRESS_MAX_ENTRIES (%" PRId64 ") reached.",
            cmpgn_fname, (int64_t)(CAMPAIGN_PROGRESS_MAX_ENTRIES));
        return NULL;
    }
    struct CampaignProgressEntry *entry = &campaign_progress_entries[campaign_progress_entries_num];
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->cmpgn_fname, sizeof(entry->cmpgn_fname), "%s", cmpgn_fname);
    campaign_progress_entries_num++;
    return entry;
}

TbBool campaign_progress_has_unlocked_level(const struct CampaignProgressEntry *entry, LevelNumber lvnum)
{
    if (entry == NULL)
        return false;
    for (uint64_t i = 0; i < entry->unlocked_levels_count; i++)
    {
        if (entry->unlocked_levels[i] == lvnum)
            return true;
    }
    return false;
}

TbBool campaign_progress_unlock_level(struct CampaignProgressEntry *entry, LevelNumber lvnum)
{
    if (entry == NULL)
        return false;
    if (campaign_progress_has_unlocked_level(entry, lvnum))
        return true;
    if (entry->unlocked_levels_count >= CAMPAIGN_LEVELS_COUNT)
    {
        ERRORLOG("Cannot unlock level %" PRId64 " for campaign \"%s\" -- CAMPAIGN_LEVELS_COUNT (%" PRId64 ") reached.",
            (int64_t)lvnum, entry->cmpgn_fname, (int64_t)(CAMPAIGN_LEVELS_COUNT));
        return false;
    }
    entry->unlocked_levels[entry->unlocked_levels_count] = lvnum;
    entry->unlocked_levels_count++;
    return true;
}

void reset_all_campaign_progress(void)
{
    campaign_progress_entries_num = 0;
    memset(campaign_progress_entries, 0, sizeof(campaign_progress_entries));
    save_campaign_progress_file();
}

TbBool any_campaign_progress_exists(void)
{
    for (uint64_t i = 0; i < campaign_progress_entries_num; i++)
    {
        if (campaign_progress_entries[i].unlocked_levels_count > 0)
            return true;
    }
    return false;
}

/******************************************************************************/
// Parsing -- mirrors config_campaigns.c's parse_campaign_map_block() shape
// (find_conf_block -> recognize_conf_command loop -> per-command switch).

void parse_progress_cfg_campaign_block(struct CampaignProgressEntry *entry, const char *buf, int64_t len, int64_t pos)
{
#define COMMAND_TEXT(cmd_num) get_conf_parameter_text(progress_cfg_commands,cmd_num)
    while (pos < len)
    {
        int64_t cmd_num = recognize_conf_command(buf, &pos, len, progress_cfg_commands);
        if (cmd_num == ccr_endOfBlock)
            break;
        char word_buf[CAMPAIGN_DESCRIPTION_LEN];
        char name_buf[CREATURE_NAME_MAX];
        switch (cmd_num)
        {
        case 1: // UNLOCKED_LEVELS
            while (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
            {
                LevelNumber lvnum = (LevelNumber)atoi(word_buf);
                if (lvnum > 0)
                    campaign_progress_unlock_level(entry, lvnum);
            }
            break;
        case 2: // NEXT_LEVEL
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
                entry->intralvl.next_level = (char)atoi(word_buf);
            else
                CONFWRNLOG("Couldn't recognize \"%s\" parameter in '%s' file.", COMMAND_TEXT(cmd_num), progress_cfg_filename);
            break;
        case 3: // TRANSFER_CREATURE = <player> <index> <model_name> <exp_level> <count> <name...>
        {
            int64_t player_idx = -1, slot_idx = -1, exp_level = 0, count = 0;
            ThingModel model = 0;
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
                player_idx = atoi(word_buf);
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
                slot_idx = atoi(word_buf);
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
                model = parse_creature_name(word_buf);
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
                exp_level = atoi(word_buf);
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
                count = atoi(word_buf);
            name_buf[0] = '\0';
            get_conf_parameter_whole(buf, &pos, len, name_buf, sizeof(name_buf));
            if ((player_idx < 0) || (player_idx >= PLAYERS_COUNT)
             || (slot_idx < 0) || (slot_idx >= TRANSFER_CREATURE_STORAGE_COUNT)
             || (model <= 0))
            {
                CONFWRNLOG("Couldn't recognize \"%s\" parameters in '%s' file.", COMMAND_TEXT(cmd_num), progress_cfg_filename);
                break;
            }
            struct CreatureStorage *stored = &entry->intralvl.transferred_creatures[player_idx][slot_idx];
            stored->model = model;
            stored->exp_level = (CrtrExpLevel)exp_level;
            stored->count = (unsigned char)count;
            snprintf(stored->creature_name, sizeof(stored->creature_name), "%s", name_buf);
            break;
        }
        case 4: // BONUS_AVAILABLE -- see struct CampaignProgressEntry's own
                // comment for why this is stored as raw level numbers here,
                // not synced into IntralevelData.bonuses_found[] at parse
                // time (that needs `campaign` to already be this entry's
                // own campaign, not guaranteed true while parsing).
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
            {
                LevelNumber bn_lvnum = (LevelNumber)atoi(word_buf);
                if ((bn_lvnum > 0) && (entry->bonus_available_count < BONUS_LEVEL_STORAGE_COUNT))
                {
                    entry->bonus_available[entry->bonus_available_count] = bn_lvnum;
                    entry->bonus_available_count++;
                }
                else
                    CONFWRNLOG("Couldn't recognize \"%s\" parameter in '%s' file.", COMMAND_TEXT(cmd_num), progress_cfg_filename);
            }
            else
                CONFWRNLOG("Couldn't recognize \"%s\" parameter in '%s' file.", COMMAND_TEXT(cmd_num), progress_cfg_filename);
            break;
        case 5: // CAMPAIGN_FLAG = <player> <flag_index> <value>
        {
            int64_t player_idx = -1, flag_idx = -1;
            int64_t value = 0;
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
                player_idx = atoi(word_buf);
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
                flag_idx = atoi(word_buf);
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
                value = LbAtoI32(word_buf);
            if ((player_idx < 0) || (player_idx >= PLAYERS_FOR_CAMPAIGN_FLAGS)
             || (flag_idx < 0) || (flag_idx >= CAMPAIGN_FLAGS_PER_PLAYER))
            {
                CONFWRNLOG("Couldn't recognize \"%s\" parameters in '%s' file.", COMMAND_TEXT(cmd_num), progress_cfg_filename);
                break;
            }
            entry->intralvl.campaign_flags[player_idx][flag_idx] = value;
            break;
        }
        case 6: // ENSIGN_OVERRIDE = <lvnum> <ensign_type>
        {
            LevelNumber lvnum = 0;
            int64_t ensign_type = -1;
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
                lvnum = (LevelNumber)atoi(word_buf);
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
                ensign_type = atoi(word_buf);
            if ((lvnum <= 0) || (ensign_type < 0))
            {
                CONFWRNLOG("Couldn't recognize \"%s\" parameters in '%s' file.", COMMAND_TEXT(cmd_num), progress_cfg_filename);
                break;
            }
            // get_level_ensign_override()/set_level_ensign() (game_merge.c)
            // operate on the *global* intralvl -- the currently active
            // campaign's live state, not necessarily this entry's own
            // (progress.cfg can be read for a campaign that isn't the
            // active one, e.g. while just browsing/reconciling). Search and
            // write entry->intralvl.ensign_overrides[] directly instead,
            // mirroring exactly what those functions do against the global.
            struct LevelEnsignOverride *override = NULL;
            for (int64_t i = 0; i < ENSIGN_OVERRIDES_COUNT; i++)
            {
                if (entry->intralvl.ensign_overrides[i].lvnum == lvnum)
                {
                    override = &entry->intralvl.ensign_overrides[i];
                    break;
                }
            }
            if (override == NULL)
            {
                for (int64_t i = 0; i < ENSIGN_OVERRIDES_COUNT; i++)
                {
                    if (entry->intralvl.ensign_overrides[i].lvnum == 0)
                    {
                        override = &entry->intralvl.ensign_overrides[i];
                        break;
                    }
                }
            }
            if (override == NULL)
            {
                CONFWRNLOG("No free ensign override slot for level %" PRId64 " in '%s' file.", (int64_t)lvnum, progress_cfg_filename);
                break;
            }
            override->lvnum = lvnum;
            override->active = true;
            override->ensign_type = (int64_t)ensign_type;
            break;
        }
        case ccr_comment:
            break;
        case ccr_endOfFile:
            break;
        default:
            CONFWRNLOG("Unrecognized command in '%s' file.", progress_cfg_filename);
            break;
        }
        skip_conf_to_next_line(buf, &pos, len);
    }
#undef COMMAND_TEXT
}

TbBool load_campaign_progress_file(void)
{
    campaign_progress_entries_num = 0;
    memset(campaign_progress_entries, 0, sizeof(campaign_progress_entries));

    char *fname = prepare_file_path(FGrp_Save, progress_cfg_filename);
    int64_t len = LbFileLengthRnc(fname);
    if (len < 2)
        return true; // no file yet -- not an error, just nothing played under the new menu yet
    if (len > 1024 * 1024)
    {
        ERRORLOG("Campaign progress file \"%s\" is implausibly large (%" PRId64 " bytes) -- refusing to load.", fname, (int64_t)(len));
        return false;
    }
    char *buf = (char *)KfxCalloc((size_t)len + 256, 1);
    if (buf == NULL)
        return false;
    len = LbFileLoadAt(fname, buf);
    TbBool result = (len > 0);
    if (result)
    {
        for (uint64_t i = 0; i < campaigns_list.items_num; i++)
        {
            struct GameCampaign *campgn = &campaigns_list.items[i];
            int64_t pos = 0;
            int64_t k = find_conf_block(buf, &pos, len, campgn->fname);
            if (k < 0)
                continue; // no progress recorded for this campaign yet
            struct CampaignProgressEntry *entry = get_campaign_progress(campgn->fname, true);
            if (entry == NULL)
                continue;
            parse_progress_cfg_campaign_block(entry, buf, len, pos);
        }
    }
    KfxFree(buf);
    return result;
}

/******************************************************************************/
// Writing -- wholesale regeneration, matching save_continue_game()'s own
// truncate-and-rewrite convention (this format has no unrelated content
// to preserve around it, unlike keeperfx.cfg's writer).

static TbBool write_line(TbFileHandle fh, const char *line)
{
    size_t len = strlen(line);
    return (size_t)LbFileWrite(fh, line, len) == len;
}

TbBool save_campaign_progress_file(void)
{
    char *fname = prepare_file_path(FGrp_Save, progress_cfg_filename);
    TbFileHandle fh = LbFileOpen(fname, Lb_FILE_MODE_NEW);
    if (!fh)
    {
        WARNMSG("Cannot open campaign progress file \"%s\" for writing.", fname);
        return false;
    }
    char line[CAMPAIGN_DESCRIPTION_LEN + 64];
    TbBool ok = true;
    for (uint64_t e = 0; ok && (e < campaign_progress_entries_num); e++)
    {
        struct CampaignProgressEntry *entry = &campaign_progress_entries[e];
        snprintf(line, sizeof(line), "[%s]\n", entry->cmpgn_fname);
        ok = ok && write_line(fh, line);

        if (entry->unlocked_levels_count > 0)
        {
            int64_t n = snprintf(line, sizeof(line), "UNLOCKED_LEVELS =");
            for (uint64_t i = 0; (i < entry->unlocked_levels_count) && (n < (int64_t)sizeof(line) - 16); i++)
                n += snprintf(line + n, sizeof(line) - n, " %" PRId64, (int64_t)entry->unlocked_levels[i]);
            snprintf(line + n, sizeof(line) - n, "\n");
            ok = ok && write_line(fh, line);
        }

        if (entry->intralvl.next_level != 0)
        {
            snprintf(line, sizeof(line), "NEXT_LEVEL = %" PRId64 "\n", (int64_t)entry->intralvl.next_level);
            ok = ok && write_line(fh, line);
        }

        for (int64_t p = 0; ok && (p < PLAYERS_COUNT); p++)
        {
            for (int64_t i = 0; ok && (i < TRANSFER_CREATURE_STORAGE_COUNT); i++)
            {
                struct CreatureStorage *stored = &entry->intralvl.transferred_creatures[p][i];
                if (stored->model <= 0)
                    continue;
                snprintf(line, sizeof(line), "TRANSFER_CREATURE = %" PRId64 " %" PRId64 " %s %" PRId64 " %" PRId64 " %s\n",
                    (int64_t)(p), (int64_t)(i), creature_code_name(stored->model), (int64_t)stored->exp_level,
                    (int64_t)stored->count, stored->creature_name);
                ok = ok && write_line(fh, line);
            }
        }

        for (uint64_t i = 0; ok && (i < entry->bonus_available_count); i++)
        {
            snprintf(line, sizeof(line), "BONUS_AVAILABLE = %" PRId64 "\n", (int64_t)entry->bonus_available[i]);
            ok = ok && write_line(fh, line);
        }

        for (int64_t p = 0; ok && (p < PLAYERS_FOR_CAMPAIGN_FLAGS); p++)
        {
            for (int64_t f = 0; ok && (f < CAMPAIGN_FLAGS_PER_PLAYER); f++)
            {
                int64_t value = entry->intralvl.campaign_flags[p][f];
                if (value == 0)
                    continue;
                snprintf(line, sizeof(line), "CAMPAIGN_FLAG = %" PRId64 " %" PRId64 " %" PRId64 "\n", (int64_t)(p), (int64_t)(f), (int64_t)(value));
                ok = ok && write_line(fh, line);
            }
        }

        for (int64_t i = 0; ok && (i < ENSIGN_OVERRIDES_COUNT); i++)
        {
            struct LevelEnsignOverride *override = &entry->intralvl.ensign_overrides[i];
            if (override->lvnum == 0)
                continue;
            snprintf(line, sizeof(line), "ENSIGN_OVERRIDE = %" PRId64 " %" PRId64 "\n", (int64_t)override->lvnum, (int64_t)override->ensign_type);
            ok = ok && write_line(fh, line);
        }

        ok = ok && write_line(fh, "\n");
    }
    LbFileClose(fh);
    return ok;
}

TbBool campaign_progress_record_level_completed(LevelNumber lvnum)
{
    // SINGLEPLAYER_FINISHED (-1, config.h) is a real, meaningful value here
    // -- "the campaign has no next level" -- and must still update
    // next_level below (Phase B reads it back). Only SINGLEPLAYER_NOTSTARTED
    // (0) and anything else non-positive is a genuinely uninteresting call.
    if ((lvnum != SINGLEPLAYER_FINISHED) && (lvnum <= 0))
        return false;
    if (!load_campaign_progress_file())
        return false;
    reconcile_fx1contn_into_progress();
    struct CampaignProgressEntry *entry = get_campaign_progress(campaign.fname, true);
    if (entry == NULL)
        return false;
    if (lvnum != SINGLEPLAYER_FINISHED)
        campaign_progress_unlock_level(entry, lvnum);

    // Called right after a level completes, with `campaign`/the global
    // `intralvl` both genuinely corresponding to this entry's own campaign
    // -- exactly the context transferred_creatures/campaign_flags/
    // ensign_overrides need to be read out of the live global correctly.
    memcpy(&entry->intralvl, &intralvl, sizeof(struct IntralevelData));
    // Overloaded meaning for progress.cfg's own purposes -- see this
    // struct's field comment (game_campaign_progress.h) -- not
    // intralvl's own (largely unused) next_level value just copied above.
    entry->intralvl.next_level = (char)lvnum;

    // bonuses_found[]'s bit index is only meaningful relative to `campaign`
    // (storage_index_for_bonus_level(), config.h) -- valid right here,
    // since `campaign` is genuinely this entry's own campaign at this
    // point, so translate to the raw level numbers this entry stores
    // instead (struct CampaignProgressEntry's own comment on why).
    entry->bonus_available_count = 0;
    struct PlayerInfo *player = get_my_player();
    for (uint64_t i = 0; i < campaign.bonus_levels_count; i++)
    {
        LevelNumber bn_lvnum = campaign.bonus_levels[i];
        if (!is_bonus_level_visible(player, bn_lvnum))
            continue;
        if (entry->bonus_available_count >= BONUS_LEVEL_STORAGE_COUNT)
            break;
        entry->bonus_available[entry->bonus_available_count] = bn_lvnum;
        entry->bonus_available_count++;
    }

    return save_campaign_progress_file();
}

/******************************************************************************/
void reconcile_fx1contn_into_progress(void)
{
    char cmpgn_fname[CAMPAIGN_FNAME_LEN];
    LevelNumber continue_lvnum = 0;
    struct IntralevelData old_intralvl;
    memset(&old_intralvl, 0, sizeof(old_intralvl));
    if (!read_continue_game_progress(cmpgn_fname, &continue_lvnum, &old_intralvl))
        return; // no fx1contn.sav, or it doesn't parse -- nothing to absorb

    struct CampaignProgressEntry *entry = get_campaign_progress(cmpgn_fname, true);
    if (entry == NULL)
        return;
    if (campaign_progress_has_unlocked_level(entry, continue_lvnum))
        return; // already absorbed (or independently reached under the new menu)

    // fx1contn.sav only ever recorded one "next level to play" -- every
    // singleplayer level strictly before it (in the campaign's own single_levels
    // order) must have been completed to have reached it linearly.
    struct GameCampaign *campgn = NULL;
    for (uint64_t i = 0; i < campaigns_list.items_num; i++)
    {
        if (strcasecmp(campaigns_list.items[i].fname, cmpgn_fname) == 0)
        {
            campgn = &campaigns_list.items[i];
            break;
        }
    }
    if (campgn != NULL)
    {
        for (uint64_t i = 0; i < campgn->single_levels_count; i++)
        {
            LevelNumber lvnum = campgn->single_levels[i];
            if (lvnum == continue_lvnum)
                break;
            campaign_progress_unlock_level(entry, lvnum);
        }
    }
    memcpy(&entry->intralvl, &old_intralvl, sizeof(struct IntralevelData));
    // save_continue_game() (game_saves.c) never itself populated
    // IntralevelData's own next_level field -- it's repurposed here as
    // "the continue level" for progress.cfg's own purposes, so set it
    // explicitly from fx1contn.sav's separate continue_level_number instead.
    entry->intralvl.next_level = (char)continue_lvnum;

    // KNOWN LIMITATION: old_intralvl.bonuses_found[] just got copied above
    // along with everything else, but its bit index is only meaningful
    // relative to whichever campaign was the global `campaign` back when
    // fx1contn.sav was written (storage_index_for_bonus_level(), config.h)
    // -- not safely decodable here without loading this campaign as active
    // first, which reconciliation deliberately avoids (see this function's
    // header comment: cheap and side-effect-free is the point). The copied
    // bytes are harmless dead weight (this file's own bonus_available[]
    // array is what's actually read, not intralvl.bonuses_found[]), but
    // any bonus levels fx1contn.sav had marked available are not carried
    // into bonus_available[] here -- flagged rather than silently claimed
    // correct. A player who reaches this via reconciliation may need to
    // re-earn bonus-level availability under the new menu.
    TbBool old_bonus_bits_set = false;
    for (int64_t i = 0; i < BONUS_LEVEL_STORAGE_COUNT; i++)
    {
        if (old_intralvl.bonuses_found[i] != 0)
        {
            old_bonus_bits_set = true;
            break;
        }
    }
    if (old_bonus_bits_set)
    {
        WARNMSG("Campaign \"%s\"'s old %s had bonus levels marked available; "
            "these are not carried over by progress.cfg reconciliation.",
            cmpgn_fname, continue_game_filename);
    }
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
