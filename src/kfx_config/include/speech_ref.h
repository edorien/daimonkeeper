/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file speech_ref.h
 *     Speech message reference type, shared by config parsers (which author
 *     and embed it by value) and kfx_frontend's gui_soundmsgs (which plays
 *     it back).
 * @par Comment:
 *     Physically split out of kfx_frontend's gui_soundmsgs.h into this new
 *     low-rank header, same pattern as camera_data.h/packet_data.h: the
 *     type's real authors are config_sounds.c's NamedField parse/assign
 *     machinery and config structs that embed it by value
 *     (config_magic.h/config_terrain.h), not gui_soundmsgs.h/.cpp, which
 *     only ever receives a `const SpeechRef*` to play it back. See
 *     docs/refactor/stage-13-enforce-and-document.md.
 */
/******************************************************************************/
#ifndef DK_SPEECH_REF_H
#define DK_SPEECH_REF_H

#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Holds a speech message reference: either a numeric SMsg_* ID or a custom file path.
 * Exactly one of the two is active: if path[0] != '\0', the path is used; otherwise id is used.
 */
typedef struct SpeechRef {
    int64_t id;
    char path[512];
} SpeechRef;

#ifdef __cplusplus
}
#endif
#endif
