/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file kfx_game_state.h
 *     Header file for kfx_game_state.c.
 * @par Purpose:
 *     Holds struct Game's kfx_game-owned field group, migrated out of
 *     game_legacy.h per docs/refactor/stage-09-kfx-game.md (mirrors
 *     stage 6.7/7.2/8.3's kfx_sim_state.h/kfx_net_state.h approach).
 *     `struct Game` is raw-serialized wholesale (see src/net_resync.cpp,
 *     src/game_saves.c, src/main_game.c's clear_complete_game()); this
 *     struct is synced the same way, alongside kfx_sim_state/
 *     kfx_net_state, at all three call sites.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/

#ifndef DK_KFX_GAME_STATE_H
#define DK_KFX_GAME_STATE_H

#include "bflib_basics.h"
#include "globals.h"
#include "config.h"
#include "config_campaigns.h"
#include "lvl_script.h"
#include "light_data.h"
#include "sounds.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

struct GuiBox;

struct KfxGameState {
    struct LevelScript script;
    PlayerNumber script_timer_player;
    unsigned char script_timer_id;
    uint64_t script_timer_limit;
    struct ScriptVariable script_variables[DISPLAY_VARIABLES_LIMIT];
    unsigned char active_script_var_count;

    char campaign_fname[CAMPAIGN_FNAME_LEN];
    TbBool paused_at_gameturn;
    GameTurn play_gameturn;
    TbBool frame_step;

    /* Mischaracterized as kfx_frontend in stage 10's initial research
       (see docs/refactor/stage-10-kfx-frontend.md) -- actual usage is
       exclusively kfx_game (game_loop.c, lvl_script_commands.c,
       console_cmd.c, main_game.c, game_saves.c). heart_lost_display_message
       moved to kfx_sim_state.h (stage 13.4, docs/refactor/
       stage-13-enforce-and-document.md) -- kfx_sim's map_events.c also
       writes it, making kfx_sim the lowest-ranked of its real consumers;
       the other 3 fields here have no kfx_sim/kfx_net consumer. */
    TbBool heart_lost_quick_message;
    uint64_t heart_lost_message_id;
    int64_t heart_lost_message_target;

    /* Moved from struct Game (stage 13, docs/refactor/
       stage-13-enforce-and-document.md) -- flags_gui/timer_real/bonus_time
       are read by kfx_frontend/kfx_script too, but kfx_game is the
       lowest-ranked of their consumer sets; ambient_sound_thing_idx/
       sound_settings/lightst are kfx_game-only. */
    unsigned char flags_gui;
    TbBool timer_real;
    int64_t bonus_time;
    int64_t ambient_sound_thing_idx;
    struct SoundSettings sound_settings;
    struct LightSystemState lightst;

    /* Moved from struct Game (stage 13, docs/refactor/
       stage-13-enforce-and-document.md) -- also read/written by
       kfx_platform's bflib_sndlib.cpp, which can't reach kfx_game_state
       directly (kfx_platform is the lowest-ranked library). See
       bflib_sndlib.h's SoundStateCallbacks for how it gets pointer
       access instead. */
    char music_track; // cdrom / default music track to resume after load
    char music_fname[DISKPATH_SIZE]; // custom music file to resume after load

    /* Moved from kfx_frontend_state (stage 13.2, docs/refactor/
       stage-13-enforce-and-document.md) -- written by kfx_frontend's
       front_input.c, but also read by kfx_game's console_cmd.c, which
       is the lower-ranked of the two. */
    int64_t my_mouse_x;
    int64_t my_mouse_y;

    /* Moved from kfx_frontend_state (stage 13.2, docs/refactor/
       stage-13-enforce-and-document.md) -- written by kfx_frontend's
       gui_boxmenu.c, but also written by kfx_game's console_cmd.c
       (debug console commands), the lower-ranked of the two.
       gui_cheat_box_1/3/4 stay in kfx_frontend_state -- only box_2 is
       touched cross-layer. */
};

extern struct KfxGameState kfx_game_state;

/** Process-local pointer, NOT part of the saved/resynced KfxGameState blob (see KfxFrontendLocal). */
struct KfxGameLocal {
    struct GuiBox *gui_cheat_box_2;
};
extern struct KfxGameLocal kfx_game_local;

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
