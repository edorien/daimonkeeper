/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file gui_msgs.h
 *     Header file for gui_msgs.c.
 * @par Purpose:
 *     Game GUI Messages functions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     14 May 2010 - 21 Nov 2012
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_GUI_MSGS_H
#define DK_GUI_MSGS_H

#include "globals.h"
#include "bflib_basics.h"
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

// enum MessageTypes moved to globals.h (stage 6) alongside the other
// small vocabulary enums kfx_sim needs without depending on the frontend
// headers that display them -- see docs/refactor/stage-06-kfx-sim.md.
// GUI_MESSAGES_COUNT/GUI_MESSAGES_DELAY and struct GuiMessage moved to
// kfx_sim_state.h (stage 13.2) -- kfx_sim is the lowest-ranked real
// consumer of the message-display buffers. See
// docs/refactor/stage-13-enforce-and-document.md.
#pragma pack(1)

struct GuiMessage_OLD { // sizeof = 0x45 (69)
    char text[64];
PlayerNumber plyr_idx;
uint64_t expiration_turn;
};

#pragma pack()
/******************************************************************************/
void message_update(void);
void message_draw(void);
// Phase 3: panel-sprite index for message i's icon (colour-remapped where
// the type needs it), -1 for none -- for the ImGui message overlay.
int64_t message_icon_spridx(int64_t i);
void zero_messages(void);
void message_add(char type, int64_t idx, const char *text);
void message_add_custom_icon(int64_t icon_idx, const char *text);
void message_add_fmt(char type, int64_t idx, const char *fmt_str, ...) KFX_PRINTF_FORMAT(3, 4);
void show_game_time_taken(uint64_t fps, uint64_t turns);
void show_real_time_taken(void);
void clear_messages_from_player(char type, PlayerNumber plyr_idx);
void delete_message(unsigned char msg_idx);
void targeted_message_add(char type, PlayerNumber plyr_idx, PlayerNumber target_idx, uint64_t timeout, const char *fmt_str, ...) KFX_PRINTF_FORMAT(5, 6);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
/******************************************************************************/
