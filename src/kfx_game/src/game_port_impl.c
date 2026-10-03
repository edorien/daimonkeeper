/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_port_impl.c
 *     kfx_game's GamePort table: the implementations next to their
 *     provider, installed by main.cpp's wire_ports(). Refactor pass 2, S15.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "game_port_impl.h"
#include "game_loop.h"
#include "game_saves.h"
#include "game_merge.h"
#include "console_cmd.h"
#include "game_legacy.h"
#include "main_game.h"
#include "game_heap.h"
#include "lvl_script_lib.h"
#include "game_campaign_progress.h"
#include "game_replay.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
static int64_t get_intralvl_next_level(void)
{
    return intralvl.next_level;
}
static void clear_intralvl_next_level(void)
{
    intralvl.next_level = 0;
}


const struct GamePort kfx_game_port = {
    .process_dungeon_destroy = &process_dungeon_destroy,
    .initialise_devastate_dungeon_from_heart = &initialise_devastate_dungeon_from_heart,
    .add_transfered_creature = &add_transfered_creature,
    .clear_transfered_creatures = &clear_transfered_creatures,
    .get_transferred_creature = &get_transferred_creature,
    .activate_bonus_level_for_singleplayer = &activate_bonus_level_for_singleplayer,
    .get_intralvl_next_level = &get_intralvl_next_level,
    .clear_intralvl_next_level = &clear_intralvl_next_level,
    .cmd_exec = &cmd_exec,
    .stop_replay_recording = &stop_replay_recording,
    .replay_record_network_stopped = &replay_record_network_stopped,
    .replay_record_resync = &replay_record_resync,
    .replay_record_chat_message = &replay_record_chat_message,
    .resync_export_game_state = &resync_export_game_state,
    .resync_import_game_state = &resync_import_game_state,
    .reinit_level_after_load = &reinit_level_after_load,
    .setup_heap_manager = &setup_heap_manager,
    .reset_heap_manager = &reset_heap_manager,
    .he_alloc = &he_alloc,
    .script_strdup = &script_strdup,
    .script_strval = &script_strval,
    .reset_campaign_progress = &reset_all_campaign_progress,
};
/******************************************************************************/
#ifdef __cplusplus
}
#endif
