/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_campaign_progress.h
 *     Header file for game_campaign_progress.c.
 * @par Purpose:
 *     Phase A of docs/refactor/gui/05-campaign-progress-and-landview.md --
 *     save/progress.cfg, a per-campaign progress record (which levels are
 *     unlocked, plus everything the old fx1contn.sav's IntralevelData held)
 *     for every campaign the player has ever played, not just one "active"
 *     campaign. Used only by the new (non-`-classicmenu`) menu -- see
 *     the design doc's §3.4 for why `-classicmenu` keeps its own,
 *     completely separate fx1contn.sav path untouched.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef GAME_CAMPAIGN_PROGRESS_H
#define GAME_CAMPAIGN_PROGRESS_H

#include "bflib_basics.h"
#include "config_campaigns.h" // CAMPAIGN_FNAME_LEN, CAMPAIGN_LEVELS_COUNT
#include "game_merge.h"       // struct IntralevelData

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

// One campaign's worth of progress -- struct IntralevelData is reused
// verbatim for everything it already held (transferred_creatures,
// bonus tracking via bonuses_found, campaign_flags, next_level,
// ensign_overrides); unlocked_levels is the one genuinely new piece of
// state, replacing fx1contn.sav's single continue_level_number with an
// explicit set (docs/refactor/gui/05-campaign-progress-and-landview.md §3.1).
struct CampaignProgressEntry {
    char cmpgn_fname[CAMPAIGN_FNAME_LEN];
    LevelNumber unlocked_levels[CAMPAIGN_LEVELS_COUNT];
    uint64_t unlocked_levels_count;
    // Bonus level numbers, stored directly rather than through
    // IntralevelData's own bonuses_found[] bitset -- that bitset's bit
    // index (storage_index_for_bonus_level(), config.h) is only meaningful
    // relative to whichever campaign is *currently* the global `campaign`,
    // which isn't guaranteed to be this entry's own campaign while merely
    // reading/writing progress.cfg for a campaign that isn't active right
    // now (e.g. bulk-loading every campaign's progress at once). Raw level
    // numbers need no such context to interpret correctly.
    LevelNumber bonus_available[BONUS_LEVEL_STORAGE_COUNT];
    uint64_t bonus_available_count;
    struct IntralevelData intralvl;
};

// Parses one already-located `[campaign.fname]` block's contents (buf/len/pos
// exactly like config.h's recognize_conf_command family) into `entry`.
// Exposed (not static) only so game_campaign_progress_test.cpp can feed it
// hand-written buffers directly -- this test binary's environment has no
// save/ directory (see game_heap_test.cpp's own note on the same kind of
// gap for creature.jty), so load_campaign_progress_file() itself can only
// be tested for "no file present" behaviour, never real parsing.
void parse_progress_cfg_campaign_block(struct CampaignProgressEntry *entry, const char *buf, int64_t len, int64_t pos);

// Loads save/progress.cfg into the in-memory table. Safe to call more than
// once (re-reads from disk, discarding any prior in-memory state) -- not
// meant to be called mid-session after campaign_progress_save_now() has
// already been used this run, only at points where re-reading from disk is
// actually wanted (startup, or right before reconcile_fx1contn_into_progress()).
// Returns false only if save/progress.cfg exists but is malformed; a
// missing file (fresh install, nothing played yet under the new menu) is
// not an error -- the in-memory table is simply left empty.
TbBool load_campaign_progress_file(void);

// Writes every in-memory entry back to save/progress.cfg, regenerating the
// whole file -- this format has no unrelated content to preserve around
// it (unlike keeperfx.cfg's writer), so a wholesale rewrite is simplest
// and matches the old save_continue_game()'s own truncate-and-rewrite
// convention.
TbBool save_campaign_progress_file(void);

// Finds the in-memory entry for a campaign by its campgns/*.cfg filename
// (the same string campaigns_list/campaign.fname/the old fx1contn.sav all
// use). Returns NULL if none exists yet and create_if_missing is false;
// otherwise creates and returns a fresh (zeroed, no unlocked levels)
// entry. Does not touch the file on disk -- call save_campaign_progress_file()
// to persist changes made through the returned pointer.
struct CampaignProgressEntry *get_campaign_progress(const char *cmpgn_fname, TbBool create_if_missing);

TbBool campaign_progress_has_unlocked_level(const struct CampaignProgressEntry *entry, LevelNumber lvnum);
// No-op (returns true) if already unlocked or the entry's unlocked_levels
// array is full (CAMPAIGN_LEVELS_COUNT, the same bound campaign.single_levels
// itself uses -- every singleplayer level in a campaign fits by construction).
TbBool campaign_progress_unlock_level(struct CampaignProgressEntry *entry, LevelNumber lvnum);

// Clears every campaign's progress in memory and rewrites an empty
// save/progress.cfg -- the Reset Progress action's own implementation
// (Phase E), exposed here since it belongs next to the rest of this
// file's format ownership.
void reset_all_campaign_progress(void);

// True if at least one in-memory campaign entry has an unlocked level --
// Phase D's continue_game_available() (game_saves.c) uses this to decide
// whether the Main Menu's Continue Game button is enabled under the new
// menu. Reads only the in-memory table; call load_campaign_progress_file()
// (and, if freshness against fx1contn.sav matters, reconcile_fx1contn_into_progress())
// first.
TbBool any_campaign_progress_exists(void);

// Records that `lvnum` was completed/reached for the *currently loaded*
// campaign (the global `campaign.fname`) under the new menu, and persists
// it. Phase A calls this alongside (not instead of) the existing
// save_continue_game() at frontend_save_continue_game()'s own call site --
// see the design doc's §3.4 for why fx1contn.sav keeps being written too
// during this transition (Phase D is what makes the two paths mutually
// exclusive, once Campaign Select actually starts reading progress.cfg).
// Loads progress.cfg fresh and reconciles fx1contn.sav into it first, so
// each call is self-contained and correct regardless of prior in-memory
// state. Returns false (no-op) for a non-positive lvnum, except
// SINGLEPLAYER_FINISHED (-1, config.h) -- "the campaign has no next
// level" is itself meaningful and still updates next_level (nothing gets
// added to unlocked_levels for it, since it isn't a real level).
TbBool campaign_progress_record_level_completed(LevelNumber lvnum);

// One-directional fx1contn.sav -> progress.cfg absorption: if the old
// file names a campaign progress.cfg has no entry for yet (or whose
// unlocked_levels doesn't yet include fx1contn.sav's own continue level),
// synthesize/extend that campaign's entry from it. Never writes
// fx1contn.sav. Safe to call repeatedly -- this is deliberately not a
// one-shot "first run only" migration, since a player can alternate
// between `-classicmenu` and the new menu across different launches of
// the same install (see the design doc's §3.4). No-op if fx1contn.sav
// doesn't exist. Does not itself call save_campaign_progress_file() --
// the caller decides when to persist.
void reconcile_fx1contn_into_progress(void);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
