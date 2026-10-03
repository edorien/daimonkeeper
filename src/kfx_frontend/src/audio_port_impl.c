/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file audio_port_impl.c
 *     kfx_frontend's AudioFeedbackPort table: the implementations next to their
 *     provider, installed by main.cpp's wire_ports(). Refactor pass 2, S15.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "audio_port_impl.h"
#include "gui_soundmsgs.h"
#include "sounds.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

const struct AudioFeedbackPort kfx_frontend_audio_port = {
    .output_message = &output_message,
    .output_room_message = &output_room_message,
    .play_speech_ref = &play_speech_ref,
    .clear_sound_messages = &clear_messages,
    .process_sound_messages = &process_messages,
    .output_message_far_from_thing = &output_message_far_from_thing,
    .thing_play_sample = &thing_play_sample,
    .stop_thing_playing_sample = &stop_thing_playing_sample,
    .create_ambient_sound = &create_ambient_sound,
    .play_sound_if_close_to_receiver = &play_sound_if_close_to_receiver,
    .play_thing_walking = &play_thing_walking,
    .reset_ambient_sound_thing_idx = &reset_ambient_sound_thing_idx,
};
/******************************************************************************/
#ifdef __cplusplus
}
#endif
