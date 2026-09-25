/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_bufrw.c
 *     Header file for bflib_mouse.c.
 * @par Purpose:
 *     Mouse related routines.
 * @par Comment:
 *   Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     12 Feb 2008 - 10 Oct 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef BFLIB_MOUSE_H
#define BFLIB_MOUSE_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#pragma pack(1)

struct TbSprite;
struct TbPoint;

enum TbMouseAction {
    MActn_NONE = 0,
    MActn_MOUSEMOVE,
    MActn_LBUTTONDOWN,
    MActn_LBUTTONUP,
    MActn_RBUTTONDOWN,
    MActn_RBUTTONUP,
    MActn_MBUTTONDOWN,
    MActn_MBUTTONUP,
    MActn_WHEELMOVEUP,
    MActn_WHEELMOVEDOWN,
};

struct mouse_buffer {
        int64_t Valid;//bool
        int64_t Width;
        int64_t Height;
        uint64_t Offset;
        unsigned char Buffer[0x1000];
        int64_t X;
        int64_t Y;
        int64_t XOffset;
        int64_t YOffset;
};

struct mouse_info {
        int64_t XMoveRatio;
        int64_t YMoveRatio;
        int64_t XSpriteOffset;
        int64_t YSpriteOffset;
        //Note: debug info says it has 0x100 items, but this is suspicious..
        unsigned char Sprite[0x1000];
};

struct DevInput {
        int64_t Yaw[16];
        int64_t Roll[16];
        int64_t Pitch[16];
        int64_t AnalogueX[16];
        int64_t AnalogueY[16];
        int64_t AnalogueZ[16];
        int64_t AnalogueU[16];
        int64_t AnalogueV[16];
        int64_t AnalogueR[16];
        int64_t DigitalX[16];
        int64_t DigitalY[16];
        int64_t DigitalZ[16];
        int64_t DigitalU[16];
        int64_t DigitalV[16];
        int64_t DigitalR[16];
        int64_t MinXAxis[16];
        int64_t MinYAxis[16];
        int64_t MinZAxis[16];
        int64_t MinUAxis[16];
        int64_t MinVAxis[16];
        int64_t MinRAxis[16];
        int64_t MaxXAxis[16];
        int64_t MaxYAxis[16];
        int64_t MaxZAxis[16];
        int64_t MaxUAxis[16];
        int64_t MaxVAxis[16];
        int64_t MaxRAxis[16];
        int64_t XCentre[16];
        int64_t YCentre[16];
        int64_t ZCentre[16];
        int64_t UCentre[16];
        int64_t VCentre[16];
        int64_t RCentre[16];
        int64_t HatX[16];
        int64_t HatY[16];
        int64_t HatMax[16];
        int64_t Buttons[16];
        int64_t NumberOfButtons[16];
        int64_t ConfigType[16];
        int64_t MenuButtons[16];
        int64_t Type;
        int64_t NumberOfDevices;
        int64_t DeviceType[16];
        unsigned char Init[16];
};

#pragma pack()
/******************************************************************************/
extern volatile TbBool lbMouseGrab; // set to false if user sets altinput command line option
extern volatile TbBool lbMouseGrabbed; // whether the mouse is current grabbed by the game window
/******************************************************************************/
TbResult LbMouseChangeSpriteAndHotspot(const struct TbSprite *mouseSprite, int64_t hot_x, int64_t hot_y);
TbResult LbMouseSetup(struct TbSprite *mouseSprite);
TbResult LbMouseSetPointerHotspot(int64_t hot_x, int64_t hot_y);
TbResult LbMouseSetPosition(int64_t x, int64_t y);
TbResult LbMouseSetPositionInitial(int64_t x, int64_t y);
void LbMoveHostCursorToGameCursor(void);
TbResult LbMoveGameCursorToHostCursor(void);
TbBool IsMouseInsideWindow(void);
TbResult LbMouseChangeSprite(const struct TbSprite *mouseSprite);
TbResult LbMouseSuspend(void);
void GetPointerHotspot(int64_t *hot_x, int64_t *hot_y);
// The sprite last passed to LbMouseChangeSpriteAndHotspot() -- NULL when
// the pointer is hidden (MousePG_Invisible). Lets the ImGui cursor mirror
// the game's actual current pointer over ImGui panels.
const struct TbSprite *LbMouseGetSprite(void);
TbResult LbMouseIsInstalled(void);
TbResult LbMouseSetWindow(int64_t x, int64_t y, int64_t width, int64_t height);
TbResult LbMouseChangeMoveRatio(int64_t ratio_x, int64_t ratio_y);

void mouseControl(uint64_t action, struct TbPoint *pos);
// LbMouseOnBeginSwap/LbMouseOnEndSwap retired -- they existed only to
// bracket the legacy CPU-buffer cursor draw around present, which
// docs/refactor/renderer/gpu-v2/01-phase-b-2d-compositing.md's cursor
// unification removed (see bflib_mspointer.hpp's own comment).
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
