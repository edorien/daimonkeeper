/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file front_credits.h
 *     Header file for front_credits.c.
 * @par Purpose:
 *     Credits and story screen displaying routines.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     05 Jan 2009 - 12 Jan 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_FRONT_CREDITS_H
#define DK_FRONT_CREDITS_H

#include "globals.h"

#include "bflib_basics.h"
#include "bflib_sprite.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
#pragma pack(1)

extern struct TbSpriteSheet * frontstory_font;
extern int64_t credits_offset;
extern int64_t credits_end;

#pragma pack()
/******************************************************************************/
void frontstory_load(void);
void frontstory_unload(void);
void frontstory_draw(void);
int64_t frontstory_input(void);
// The story text id frontstory_load() picked (frontstory_text_no is
// file-local) -- exposed so kfx_frontend's ImGui FeSt_STORY_POEM screen
// (docs/refactor/renderer/04-imgui-gui-foundation.md Phase C) shows the
// exact same string frontstory_draw() would have, rather than picking its
// own separately.
int64_t frontstory_get_text_no(void);

void frontcredits_draw(void);
TbBool frontcredits_input(void);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
