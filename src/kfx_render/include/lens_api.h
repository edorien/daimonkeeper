/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file lens_api.h
 *     Header file for lens_api.c.
 * @par Purpose:
 *     Eye lenses support functions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     11 Mar 2010 - 12 May 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_LENS_API_H
#define DK_LENS_API_H

#include "globals.h"
#include "bflib_video.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#pragma pack(1)

/******************************************************************************/
extern uint64_t *eye_lens_memory;
extern TbPixel *eye_lens_spare_screen_memory;

#pragma pack()
/******************************************************************************/
// Shared lens buffers (managed by LensManager, defined in engine_lenses.c)
// NOTE: eye_lens_memory and eye_lens_spare_screen_memory may point to the
// same physical buffer. They represent different logical uses:
//   - eye_lens_spare_screen_memory: Off-screen render target for 3D view
//   - eye_lens_memory: Working memory for effect processing (mist textures, displacement maps)
// These uses occur at different pipeline stages, so combined allocation is safe.
// External code should use accessor functions below. Effects access via #include "lens_api.h"
extern uint64_t eye_lens_width;
extern uint64_t eye_lens_height;
extern uint64_t *eye_lens_memory;
extern TbPixel *eye_lens_spare_screen_memory;

// Accessor functions (type-safe, validates state)
TbPixel* lens_get_render_target(void);
uint64_t lens_get_render_target_width(void);
uint64_t lens_get_render_target_height(void);

// C++ LensManager wrapper functions (for use from C code)
void* LensManager_GetInstance(void);
TbBool LensManager_Init(void* mgr);
void LensManager_Reset(void* mgr);
TbBool LensManager_SetLens(void* mgr, int64_t lens_idx);
int64_t LensManager_GetActiveLens(void* mgr);
const char* LensManager_GetActiveCustomLensName(void* mgr);
TbBool LensManager_IsReady(void* mgr);
void LensManager_Draw(void* mgr, TbPixel* srcbuf, TbPixel* dstbuf,
                      int64_t srcpitch, int64_t dstpitch, int64_t width, int64_t height, int64_t viewport_x);

// Custom lens registration (for LUA integration)
TbBool LensManager_RegisterCustomLens(void* mgr, const char* name, void* effect);
void* LensManager_GetCustomLens(void* mgr, const char* name);
TbBool LensManager_SetLensByName(void* mgr, const char* name);

// LUA lens effect creation
void* LuaLensEffect_Create(const char* name, void* lua_state);
void LuaLensEffect_SetDrawCallback(void* effect, int64_t callback_ref);
/******************************************************************************/
void initialise_eye_lenses(void);
void setup_eye_lens(int64_t nlens);
void reinitialise_eye_lens(int64_t nlens);
void reset_eye_lenses(void);
void draw_lens_effect(TbPixel *dstbuf, int64_t dstpitch, TbPixel *srcbuf, int64_t srcpitch, int64_t width, int64_t height, int64_t viewport_x, int64_t effect);
TbBool lens_is_ready(void);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
