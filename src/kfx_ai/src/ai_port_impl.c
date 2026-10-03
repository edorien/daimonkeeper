/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file ai_port_impl.c
 *     kfx_ai's AiPort table: the implementations next to their
 *     provider, installed by main.cpp's wire_ports(). Refactor pass 2, S15.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "ai_port_impl.h"
#include "player_computer.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

const struct AiPort kfx_ai_port = {
    .setup_a_computer_player = &setup_a_computer_player,
    .toggle_computer_player = &toggle_computer_player,
    .script_support_setup_player_as_computer_keeper = &script_support_setup_player_as_computer_keeper,
    .computer_force_dump_specific_held_thing = &computer_force_dump_specific_held_thing,
    .reactivate_build_process = &reactivate_build_process,
};
/******************************************************************************/
#ifdef __cplusplus
}
#endif
