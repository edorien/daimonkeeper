/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_vidsurface.c
 *     Graphics surfaces support.
 * @par Purpose:
 *     Surfaces used for drawing on screen.
 * @par Comment:
 *     Depends on the video support library, which is SDL in this implementation.
 * @author   Tomasz Lis
 * @date     10 Feb 2010 - 30 Sep 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "bflib_vidsurface.h"

#include "bflib_basics.h"
#include "globals.h"
#include <SDL3/SDL.h>
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

/** Internal drawing surface structure.
 *  Sometimes may be same as screen surface. */
SDL_Surface * lbDrawSurface;

/******************************************************************************/
// The LbScreenSurface*()/struct SSurface family that used to live here --
// a small SDL_Surface RAII+blit wrapper -- was only ever used by
// bflib_mspointer.cpp's legacy CPU-buffer cursor draw (two off-screen
// surfaces for backup/restore around each blit), retired by
// docs/refactor/renderer/gpu-v2/01-phase-b-2d-compositing.md's cursor
// unification (confirmed unused elsewhere by grep before deleting, not
// assumed).
/******************************************************************************/
#ifdef __cplusplus
}
#endif
