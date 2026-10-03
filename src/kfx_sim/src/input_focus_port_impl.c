/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file input_focus_port_impl.c
 *     kfx_sim's InputFocusPort table: the implementations next to their
 *     provider, installed by main.cpp's wire_ports(). Refactor pass 2, S15.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "input_focus_port_impl.h"
#include "config_keeperfx.h"
#include "kfx_sim_state.h"
#include "player_data.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// Wrappers registered with ports/input_focus_port.h's InputFocusPort (see
// docs/refactor/stage-02-decouple-bflib.md); bflib_inputctrl.cpp can't read
// struct Game directly.
static TbBool is_game_paused(void)
{
    return (kfx_sim_state.operation_flags & GOF_Paused) != 0;
}

static TbBool is_possession_mode_active(void)
{
    return (get_my_player()->view_type == PVT_CreatureContrl) && ((kfx_sim_state.view_mode_flags & GNFldD_CreaturePasngr) == 0);
}

static TbBool is_packet_load_enabled(void)
{
    return kfx_sim_state.replay_active != 0;
}


const struct InputFocusPort kfx_sim_input_focus_port = {
    .freeze_game_on_focus_lost = &freeze_game_on_focus_lost,
    .mute_audio_on_focus_lost = &mute_audio_on_focus_lost,
    .unlock_cursor_when_game_paused = &unlock_cursor_when_game_paused,
    .lock_cursor_in_possession = &lock_cursor_in_possession,
    .is_game_paused = &is_game_paused,
    .is_possession_mode_active = &is_possession_mode_active,
    .is_packet_load_enabled = &is_packet_load_enabled,
    .use_relative_mouse_mode = &use_relative_mouse_mode,
};
/******************************************************************************/
#ifdef __cplusplus
}
#endif
