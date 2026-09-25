/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_mspointer.hpp
 *     Header file for bflib_mspointer.cpp.
 * @par Purpose:
 *     Mouse pointer position/sprite/hotspot tracking.
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
#include "mutex.hpp"

/******************************************************************************/

// Exported class. Tracks the mouse pointer's position/sprite/hotspot only --
// docs/refactor/renderer/gpu-v2/01-phase-b-2d-compositing.md's cursor
// unification retired the CPU-buffer drawing this class used to also do
// (Draw/Backup/Undraw against a pair of off-screen SSurfaces, and
// PointerDraw's direct locked-framebuffer blit) in favour of a single
// ImGui-overlay draw (gui/FrontendImGui.cpp, kfx_frontend) that reads this
// class's tracked state (via LbMouseGetSprite()/GetPointerHotspot(),
// bflib_mouse.h) rather than drawing itself. What's left here is exactly
// the state kfx_frontend's cursor draw, and GetMouseX()/GetMouseY(), still
// need: the current sprite, its hotspot, and the tracked position.
class LbI_PointerHandler {
 public:
    LbI_PointerHandler(void);
    ~LbI_PointerHandler(void);
    void SetHotspot(int64_t x, int64_t y);
    void Initialise(const struct TbSprite *spr, struct TbPoint *, struct TbPoint *);
    void Release(void);
 protected:
    void ClipHotspot(void);
    // Properties
    struct TbPoint *position;
    struct TbPoint *spr_offset;
    bool is_active;
    const struct TbSprite *sprite;
    std::mutex lock;
};

/******************************************************************************/

#endif
