/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_mspointer.cpp
 *     Graphics drawing support sdk class.
 * @par Purpose:
 *     A link between game engine and the DirectDraw library.
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
#include "renderer/RendererManager.h"
#include "bflib_mspointer.hpp"

#include <string.h>
#include <stdio.h>

#include "bflib_basics.h"
#include "globals.h"
#include "bflib_planar.h"
#include "bflib_mouse.h"
#include "bflib_sprite.h"
#include "bflib_vidsurface.h"
#include "bflib_vidraw.h"

#include "post_inc.h"
/******************************************************************************/
struct SSurface;
/******************************************************************************/
// Global variables
volatile TbBool lbPointerAdvancedDraw;
int64_t cursor_xsteps_array[2*CURSOR_SCALING_XSTEPS];
int64_t cursor_ysteps_array[2*CURSOR_SCALING_YSTEPS];
/******************************************************************************/

void LbCursorSpriteSetScalingWidthClipped(int64_t x, int64_t swidth, int64_t dwidth, int64_t gwidth)
{
    SYNCDBG(17,"Starting %" PRId64 " -> %" PRId64 " at %" PRId64,(int64_t)swidth,(int64_t)dwidth,(int64_t)x);
    if (swidth > CURSOR_SCALING_XSTEPS)
        swidth = CURSOR_SCALING_XSTEPS;
    LbSpriteSetScalingWidthClippedArray(cursor_xsteps_array, x, swidth, dwidth, gwidth);
}

void LbCursorSpriteSetScalingWidthSimple(int64_t x, int64_t swidth, int64_t dwidth)
{
    SYNCDBG(17,"Starting %" PRId64 " -> %" PRId64 " at %" PRId64,(int64_t)swidth,(int64_t)dwidth,(int64_t)x);
    if (swidth > CURSOR_SCALING_XSTEPS)
        swidth = CURSOR_SCALING_XSTEPS;
    LbSpriteSetScalingWidthSimpleArray(cursor_xsteps_array, x, swidth, dwidth);
}

void LbCursorSpriteSetScalingHeightClipped(int64_t y, int64_t sheight, int64_t dheight, int64_t gheight)
{
    SYNCDBG(17,"Starting %" PRId64 " -> %" PRId64 " at %" PRId64,(int64_t)sheight,(int64_t)dheight,(int64_t)y);
    if (sheight > CURSOR_SCALING_YSTEPS)
        sheight = CURSOR_SCALING_YSTEPS;
    LbSpriteSetScalingHeightClippedArray(cursor_ysteps_array, y, sheight, dheight, gheight);
}

void LbCursorSpriteSetScalingHeightSimple(int64_t y, int64_t sheight, int64_t dheight)
{
    SYNCDBG(17,"Starting %" PRId64 " -> %" PRId64 " at %" PRId64,(int64_t)sheight,(int64_t)dheight,(int64_t)y);
    if (sheight > CURSOR_SCALING_YSTEPS)
        sheight = CURSOR_SCALING_YSTEPS;
    LbSpriteSetScalingHeightSimpleArray(cursor_ysteps_array, y, sheight, dheight);
}

/**
 * Draws the mouse pointer sprite on a display buffer.
 */
static int64_t PointerDraw(int64_t x, int64_t y, const struct TbSprite *spr, TbPixel *outbuf, uint64_t scanline)
{
    uint64_t dwidth;
    uint64_t dheight;
    // Prepare bounds
    dwidth = scale_ui_value_lofi(spr->SWidth);
    dheight = scale_ui_value_lofi(spr->SHeight);
    if ( (dwidth <= 0) || (dheight <= 0) )
        return 1;
    if ( (lbDisplay.MouseWindowWidth <= 0) || (lbDisplay.MouseWindowHeight <= 0) )
        return 1;
    // Normally it would be enough to check if ((dwidth+x) >= gwidth), but due to rounding we need to add swidth
    if ((x < 0) || ((dwidth + spr->SWidth + x) >= lbDisplay.MouseWindowWidth))
    {
        LbCursorSpriteSetScalingWidthClipped(x, spr->SWidth, dwidth, lbDisplay.MouseWindowWidth);
    } else {
        LbCursorSpriteSetScalingWidthSimple(x, spr->SWidth, dwidth);
    }
    // Normally it would be enough to check if ((dheight+y) >= gheight), but our simple rounding may enlarge the image
    if ((y < 0) || ((dheight + spr->SHeight + y) >= lbDisplay.MouseWindowHeight))
    {
        LbCursorSpriteSetScalingHeightClipped(y, spr->SHeight, dheight, lbDisplay.MouseWindowHeight);
    } else {
        LbCursorSpriteSetScalingHeightSimple(y, spr->SHeight, dheight);
    }
    int64_t *xstep;
    int64_t *ystep;
    {
        xstep = &cursor_xsteps_array[0];
        ystep = &cursor_ysteps_array[0];
    }
    outbuf = &outbuf[xstep[0] + scanline * ystep[0]];
    const struct TbSourceBuffer buffer = {
        spr->Data,
        spr->SWidth,
        spr->SHeight,
        spr->SWidth,
    };
    return LbSpriteDrawUsingScalingUpDataSolidLR(outbuf, scanline, lbDisplay.MouseWindowHeight, xstep, ystep, &buffer);
}

// Methods

LbI_PointerHandler::LbI_PointerHandler(void)
{
    this->is_active = false;
    this->needs_redraw = false;
    this->sprite = NULL;
    this->position = NULL;
    this->spr_offset = NULL;
    draw_pos_x = 0;
    draw_pos_y = 0;
}

LbI_PointerHandler::~LbI_PointerHandler(void)
{
    Release();
}

void LbI_PointerHandler::SetHotspot(int64_t x, int64_t y)
{
    int64_t prev_x;
    int64_t prev_y;
    std::lock_guard<std::mutex> guard(lock);
    if (this->is_active)
    {
        // Set new coords, and backup previous ones
        prev_x = spr_offset->x;
        spr_offset->x = x;
        prev_y = spr_offset->y;
        spr_offset->y = y;
        ClipHotspot();
        // If the coords were changed, then update the pointer
        if ((spr_offset->x != prev_x) || (spr_offset->y != prev_y))
        {
            Undraw(true);
            NewMousePos();
            Backup(true);
            Draw(true);
        }
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
    void *surfbuf;
    TbPixel *buf;
    int64_t i;
    int64_t dstwidth;
    int64_t dstheight;
    Release();
    std::lock_guard<std::mutex> guard(lock);
    sprite = spr;
    dstwidth = scale_ui_value_lofi(sprite->SWidth + 1);
    dstheight = scale_ui_value_lofi(sprite->SHeight + 1);
    surf1.Create(dstwidth, dstheight);
    surf2.Create(dstwidth, dstheight);
    surfbuf = LbScreenSurfaceLock(surf1.get());
    if (surfbuf == NULL)
    {
        surf1.Release();
        surf2.Release();
        sprite = NULL;
        return;
    }
    buf = (TbPixel *)surfbuf;
    // pitch() is the surface's raw byte pitch (matches SDL's own convention);
    // memset() below wants bytes, but pointer arithmetic on a TbPixel* and
    // PointerDraw()'s scanline stride both need pixel counts.
    TbBytePitch pitch = surf1.pitch();
    int64_t pitch_px = TbBytePitch_ToPixels(pitch);
    for (i=0; i < dstheight; i++)
    {
        memset(buf, 255, pitch.bytes);
        buf += pitch_px;
    }
    PointerDraw(0, 0, this->sprite, (TbPixel *)surfbuf, pitch_px);
    LbScreenSurfaceUnlock(surf1.get());
    this->position = npos;
    this->spr_offset = noffset;
    ClipHotspot();
    this->is_active = true;
    NewMousePos();
    this->needs_redraw = false;
    LbScreenSurfaceBlit(surf2.get(), this->draw_pos_x, this->draw_pos_y, &rect_1038, 0x10|0x02);
}

void LbI_PointerHandler::Draw(bool a1)
{
    uint64_t flags;
    flags = 0x10 | 0x08 | 0x04;
    if ( a1 )
      flags |= 0x02;
    LbScreenSurfaceBlit(this->surf1.get(), this->draw_pos_x, this->draw_pos_y, &rect_1038, flags);
}

void LbI_PointerHandler::Backup(bool a1)
{
    uint64_t flags;
    flags = 0x10;
    if ( a1 )
      flags |= 0x02;
    this->needs_redraw = false;
    LbScreenSurfaceBlit(this->surf2.get(), this->draw_pos_x, this->draw_pos_y, &rect_1038, flags);
}

void LbI_PointerHandler::Undraw(bool a1)
{
    uint64_t flags;
    flags = 0x10 | 0x08;
    if ( a1 )
      flags |= 0x02;
    LbScreenSurfaceBlit(this->surf2.get(), this->draw_pos_x, this->draw_pos_y, &rect_1038, flags);
}

void LbI_PointerHandler::Release(void)
{
    std::lock_guard<std::mutex> guard(lock);
    if ( this->is_active )
    {
        if ( lbInteruptMouse )
            Undraw(true);
        this->is_active = false;
        this->needs_redraw = false;
        position = NULL;
        sprite = NULL;
        spr_offset = NULL;
        surf1.Release();
        surf2.Release();
    }
}

void LbI_PointerHandler::NewMousePos(void)
{
    this->draw_pos_x = position->x - scale_ui_value_lofi(spr_offset->x);
    this->draw_pos_y = position->y - scale_ui_value_lofi(spr_offset->y);
    int64_t dstwidth;
    int64_t dstheight;
    dstwidth = scale_ui_value_lofi(sprite->SWidth);
    dstheight = scale_ui_value_lofi(sprite->SHeight);
    LbSetRect(&rect_1038, 0, 0, dstwidth, dstheight);
    if (this->draw_pos_x < 0)
    {
        rect_1038.left -= this->draw_pos_x;
        this->draw_pos_x = 0;
    } else
    if (this->draw_pos_x+dstwidth > lbDisplay.PhysicalScreenWidth)
    {
        rect_1038.right += lbDisplay.PhysicalScreenWidth-dstwidth-this->draw_pos_x;
    }
    if (this->draw_pos_y < 0)
    {
        rect_1038.top -= this->draw_pos_y;
        this->draw_pos_y = 0;
    } else
    if (this->draw_pos_y+dstheight > lbDisplay.PhysicalScreenHeight)
    {
        rect_1038.bottom += lbDisplay.PhysicalScreenHeight - dstheight - this->draw_pos_y;
    }
}

bool LbI_PointerHandler::OnMove(void)
{
    std::lock_guard<std::mutex> guard(lock);
    // Same reasoning as OnBeginSwap() below: while ImGui wants the mouse,
    // it's already drawing this cursor itself, so the immediate-redraw path
    // must be skipped entirely here too -- not just to avoid a second visible
    // cursor, but because Undraw() restores whatever Backup() last saved,
    // and OnBeginSwap() no longer calls Backup()/Draw() in that case, so an
    // Undraw() here would paste back a stale backup instead of leaving the
    // (ImGui-drawn) frame alone. Position tracking (NewMousePos()) still
    // needs to happen regardless. RendererScreenOwned() (docs/refactor/
    // renderer/05-imgui-owned-menu-backdrop.md Phase C) is the same
    // "nothing legacy is composited at all for this screen" check
    // RendererSoftware::PresentFrame() uses to skip its own framebuffer
    // blit -- drawing the legacy cursor here when that's true would just be
    // wasted work, since ImGui now draws its own cursor unconditionally
    // (not just on WantCaptureMouse) for exactly these screens.
    if (lbPointerAdvancedDraw && lbInteruptMouse && !ImGuiContextWantCaptureMouse() && !RendererScreenOwned())
    {
        Undraw(true);
        NewMousePos();
        Backup(true);
        Draw(true);
    } else
    {
        NewMousePos();
    }
    return true;
}

void LbI_PointerHandler::OnBeginSwap(void)
{
    // Whenever ImGui wants the mouse, ImGuiContext.cpp already draws this
    // exact sprite itself, every frame, via the foreground draw list (see
    // its own comment on why it needs a stand-in there at all). Drawing it
    // here too baked a second cursor directly into the framebuffer, visibly
    // doubled up on screen -- found live once real resolutions started
    // rendering menus/options through ImGui routinely enough to notice.
    // RendererScreenOwned() (docs/refactor/renderer/
    // 05-imgui-owned-menu-backdrop.md Phase C) covers the rest of the
    // screen for the 15 fully-ImGui-owned states: WantCaptureMouse alone is
    // only true over the actual menu panel, not the surrounding backdrop
    // area, but nothing legacy is composited there any more either (the
    // framebuffer blit itself is skipped for these screens), so drawing
    // the legacy cursor into it would be pure wasted work -- ImGui already
    // draws its own cursor unconditionally there instead.
    if (ImGuiContextWantCaptureMouse() || RendererScreenOwned())
        return;
    std::lock_guard<std::mutex> guard(lock);
    if ( lbPointerAdvancedDraw )
    {
        Backup(false);
        Draw(false);
    } else
    if (RendererLockFramebuffer() == Lb_SUCCESS)
    {
      PointerDraw(position->x - scale_ui_value_lofi(spr_offset->x), position->y - scale_ui_value_lofi(spr_offset->y),
          sprite, RendererGetFramebuffer(), lbDisplay.GraphicsScreenWidth);
      RendererUnlockFramebuffer();
    }
}

void LbI_PointerHandler::OnEndSwap(void)
{
    std::lock_guard<std::mutex> guard(lock);
    if ( lbPointerAdvancedDraw )
    {
        Undraw(false);
        this->needs_redraw = true;
    }
}

/******************************************************************************/
