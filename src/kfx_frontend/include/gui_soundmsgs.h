/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file gui_soundmsgs.h
 *     Header file for gui_soundmsgs.c.
 * @par Purpose:
 *     Allows to play sound messages during the game.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     29 Jun 2010 - 11 Jul 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_GUI_SOUNDMSGS_H
#define DK_GUI_SOUNDMSGS_H

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_sound.h"
#include "speech_ref.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of queued speeches before new ones are skipped. */
extern int64_t g_speech_queue_limit;

// enum TbSpeechMessages, its SMsg_* alias macros, the MESSAGE_DURATION_*
// macros, and enum OutputMessageKinds/OutputMessageKind all moved to
// globals.h (stage 6/13) so kfx_sim's many output_message()/
// output_room_message() call sites don't need to depend on this frontend
// header -- see docs/refactor/stage-06-kfx-sim.md and
// docs/refactor/stage-13-enforce-and-document.md.

struct Thing;

// SpeechRef moved to speech_ref.h (kfx_config) -- see include above.

TbBool output_message(SoundSmplTblID, int64_t duration);
TbBool output_message_from_path(const char* path, int64_t duration);

/**
 * Plays a speech message from a SpeechRef.
 * Uses path-based playback if the ref contains a file path, otherwise plays by numeric ID.
 */
TbBool play_speech_ref(const SpeechRef* ref, int64_t duration);
TbBool output_custom_message(const char * fname, int64_t duration);
TbBool output_message_far_from_thing(const struct Thing*, SoundSmplTblID, int64_t duration);
void clear_messages(void);
void process_messages(void);
TbBool output_room_message(PlayerNumber, RoomKind, OutputMessageKind);
void script_play_message(TbBool param_is_string, const char msgtype_id, const int64_t msg_id, const char *filename);
#ifdef __cplusplus
}
#endif
#endif
