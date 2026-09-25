/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_fmvids.h
 *     Header file for bflib_fmvids.c.
 * @par Purpose:
 *     Full Motion Videos (Smacker,FLIC) decode & play library.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   KeeperFX Team
 * @date     27 Nov 2008 - 30 Dec 2008
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef BFLIB_FMVIDS_H
#define BFLIB_FMVIDS_H

#include "bflib_basics.h"
#include "bflib_video.h"

#ifdef __cplusplus
extern "C" {
#endif

enum SmackerPlayFlags {
    SMK_NoSound            = 0x01,
    SMK_NoStopOnUserInput  = 0x02, // unused?
    SMK_PixelDoubleLine    = 0x04, // unused?
    SMK_InterlaceLine      = 0x08, // unused?
    SMK_FullscreenFit      = 0x10,
    SMK_FullscreenStretch  = 0x20,
    SMK_FullscreenCrop     = 0x40,
    SMK_PixelDoubleWidth   = 0x80, // unused?
    SMK_WriteStatusFile    = 0x100, // was 0x40, unused?
};

// Callbacks the caller supplies for input handling during movie playback
// (implemented in kjm_input.c) -- kept as an injected pair instead of
// bflib_fmvids.cpp calling kjm_input.h's poll_inputs()/clear_key_pressed()
// directly (see docs/refactor/stage-02-decouple-bflib.md).
typedef TbBool (*MoviePollInputsFn)(void);
typedef void (*MovieClearKeyPressedFn)(int64_t key);

TbBool play_smk(const char * filename, int64_t flags, MoviePollInputsFn poll_inputs_fn, MovieClearKeyPressedFn clear_key_pressed_fn);
// anim_stop/anim_record/anim_record_frame (the FLI/FLC movie-recording
// encoder) retired -- docs/refactor/renderer/gpu-v2/
// 01-phase-b-2d-compositing.md B2.

#ifdef __cplusplus
}
#endif
#endif
