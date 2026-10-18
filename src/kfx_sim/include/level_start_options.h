/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file level_start_options.h
 *     Header file for level_start_options.c.
 * @par Purpose:
 *     One-shot options the Skirmish setup tab asks for at Play and the level start carries out once its players
 *     exist -- things that are not part of the level script (so not in the script override).
 *     Keyed on the level number, like level_script_override.h: one asked for another level is discarded, so it
 *     cannot leak into a later campaign / free-play / network level.
 *     The start takes the request (level_start_options_take()), applies the part that is the simulation's
 *     (level_start_options_apply_world()), and hands the rest (llm_pause) to the library that owns it.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_LEVEL_START_OPTIONS_H
#define DK_LEVEL_START_OPTIONS_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/** What allies share, beyond the level's own rules (the ALLIESSHAREVISION / ALLIESSHAREDROP game rules). */
enum LevelStartSharing {
    LevelStartSharing_None = 0,       /**< the level's own rules, untouched */
    LevelStartSharing_Vision,         /**< allies share what they have explored */
    LevelStartSharing_VisionAndDrop,  /**< ... and may drop their creatures on each other's land */
};

struct LevelStartOptions {
    TbBool reveal_map;                /**< the whole map revealed to every keeper */
    int32_t sharing;                  /**< enum LevelStartSharing */
    TbBool llm_pause;                 /**< an External seat's agent may pause the game although a human plays */
};

/** Asks for `opts` when `lvnum` starts. Replaces any previous request. */
void level_start_options_request(LevelNumber lvnum, const struct LevelStartOptions *opts);
/** Forgets any request. */
void level_start_options_clear(void);
/** The request for `lvnum`, or all-off if there is none or it was for another level. */
struct LevelStartOptions level_start_options_requested(LevelNumber lvnum);
/** Takes the request for the level just started (all-off if none or for another level) and forgets it, whatever
 *  level it was for. */
struct LevelStartOptions level_start_options_take(LevelNumber lvnum);
/** Carries out the simulation's part: every keeper that exists reveals the whole map (reveal_map), and the game
 *  rules of every player share what `sharing` says. Returns how many keepers revealed. */
int64_t level_start_options_apply_world(const struct LevelStartOptions *opts);
/******************************************************************************/
#ifdef __cplusplus
}
#endif

#endif
