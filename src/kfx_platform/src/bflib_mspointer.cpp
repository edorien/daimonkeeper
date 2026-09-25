/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_mspointer.cpp
 *     Mouse pointer position/sprite/hotspot tracking.
 * @par Purpose:
 *     Tracks the mouse pointer's position, current sprite and hotspot --
 *     see bflib_mspointer.hpp's own comment for what this stopped doing
 *     (CPU-buffer cursor drawing) and why.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     16 Nov 2008 - 21 Nov 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "bflib_mspointer.hpp"

#include "bflib_basics.h"
#include "globals.h"
#include "bflib_planar.h"
#include "bflib_sprite.h"

#include "post_inc.h"
/******************************************************************************/

// Methods

LbI_PointerHandler::LbI_PointerHandler(void)
{
    this->is_active = false;
    this->sprite = NULL;
    this->position = NULL;
    this->spr_offset = NULL;
}

LbI_PointerHandler::~LbI_PointerHandler(void)
{
    Release();
}

void LbI_PointerHandler::SetHotspot(int64_t x, int64_t y)
{
    std::lock_guard<std::mutex> guard(lock);
    if (this->is_active)
    {
        spr_offset->x = x;
        spr_offset->y = y;
        ClipHotspot();
    }
}

void LbI_PointerHandler::ClipHotspot(void)
{
    if (!this->is_active)
        return;
    if ((sprite != NULL) && (spr_offset != NULL))
    {
        if (spr_offset->x < 0)
        {
          spr_offset->x = 0;
        } else
        if (sprite->SWidth <= spr_offset->x)
        {
          spr_offset->x = sprite->SWidth - 1;
        }
        if (spr_offset->y < 0)
        {
          spr_offset->y = 0;
        } else
        if (spr_offset->y >= sprite->SHeight)
        {
          spr_offset->y = sprite->SHeight - 1;
        }
    }
}

void LbI_PointerHandler::Initialise(const struct TbSprite *spr, struct TbPoint *npos, struct TbPoint *noffset)
{
    Release();
    std::lock_guard<std::mutex> guard(lock);
    sprite = spr;
    this->position = npos;
    this->spr_offset = noffset;
    ClipHotspot();
    this->is_active = true;
}

void LbI_PointerHandler::Release(void)
{
    std::lock_guard<std::mutex> guard(lock);
    if ( this->is_active )
    {
        this->is_active = false;
        position = NULL;
        sprite = NULL;
        spr_offset = NULL;
    }
}

/******************************************************************************/
