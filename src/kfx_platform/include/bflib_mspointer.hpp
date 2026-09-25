/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_mspointer.hpp
 *     Header file for bflib_mspointer.cpp.
 * @par Purpose:
 *     Graphics drawing support sdk class.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     16 Nov 2008 - 21 Nov 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef BFLIB_MSPOINTER_H
#define BFLIB_MSPOINTER_H

#include "bflib_basics.h"
#include "bflib_planar.h"
#include "bflib_vidsurface.h"
#include "bflib_video.h"
#include "mutex.hpp"

/******************************************************************************/
#define CURSOR_SCALING_XSTEPS MAX_SUPPORTED_SCREEN_WIDTH/10
#define CURSOR_SCALING_YSTEPS MAX_SUPPORTED_SCREEN_HEIGHT/10
extern int64_t cursor_xsteps_array[2*CURSOR_SCALING_XSTEPS];
extern int64_t cursor_ysteps_array[2*CURSOR_SCALING_YSTEPS];

// Had real external linkage but no header declaration at all -- added,
// the usual "add the missing declaration" fix.
void LbCursorSpriteSetScalingWidthClipped(int64_t x, int64_t swidth, int64_t dwidth, int64_t gwidth);
void LbCursorSpriteSetScalingWidthSimple(int64_t x, int64_t swidth, int64_t dwidth);
void LbCursorSpriteSetScalingHeightClipped(int64_t y, int64_t sheight, int64_t dheight, int64_t gheight);
void LbCursorSpriteSetScalingHeightSimple(int64_t y, int64_t sheight, int64_t dheight);
/******************************************************************************/

// RAII wrapper around struct SSurface: Release() always runs on destruction
// (LbScreenSurfaceRelease() is a no-op if nothing was ever Create()'d), so a
// surface can't be leaked by a future early-return that forgets to release it.
class ScopedScreenSurface {
 public:
    ScopedScreenSurface() { LbScreenSurfaceInit(&surf_); }
    ~ScopedScreenSurface() { LbScreenSurfaceRelease(&surf_); }
    ScopedScreenSurface(const ScopedScreenSurface &) = delete;
    ScopedScreenSurface &operator=(const ScopedScreenSurface &) = delete;

    TbResult Create(uint64_t w, uint64_t h)
    {
        LbScreenSurfaceRelease(&surf_);
        return LbScreenSurfaceCreate(&surf_, w, h);
    }
    void Release() { LbScreenSurfaceRelease(&surf_); }
    struct SSurface *get() { return &surf_; }
    TbBytePitch pitch() const { return surf_.pitch; }
 private:
    struct SSurface surf_{};
};

// Exported class
class LbI_PointerHandler {
 public:
    LbI_PointerHandler(void);
    ~LbI_PointerHandler(void);
    void SetHotspot(int64_t x, int64_t y);
    void Initialise(const struct TbSprite *spr, struct TbPoint *, struct TbPoint *);
    void Release(void);
    void NewMousePos(void);
    bool OnMove(void);
    void OnBeginSwap(void);
    void OnEndSwap(void);
 protected:
    void ClipHotspot(void);
    void Draw(bool);
    void Undraw(bool);
    void Backup(bool);
    // Properties
    ScopedScreenSurface surf1;
    ScopedScreenSurface surf2;
    //unsigned char sprite_data[4096];
    struct TbPoint *position;
    struct TbPoint *spr_offset;
    struct TbRect rect_1038;
    int64_t draw_pos_x;
    int64_t draw_pos_y;
    bool is_active;
    bool needs_redraw;
    const struct TbSprite *sprite;
    std::mutex lock;
};

/******************************************************************************/

#endif
