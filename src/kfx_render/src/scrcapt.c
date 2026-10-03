/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file scrcapt.c
 *     Screen capturing functions.
 * @par Purpose:
 *     Functions to read display buffer and store it in various formats.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     05 Jan 2009 - 12 Jan 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererManager.h"
#include "scrcapt.h"
#include "bflib_basics.h"
#include "bflib_fileio.h"
#include "bflib_dernc.h"
#include "bflib_video.h"
#include "bflib_vidsurface.h"
#include "globals.h"

#include "config.h"

#include <string.h>
#include <ctype.h>
#include "kfx_sim_state.h"
#include "config_settings.h"
#include "ports/ui_port.h"
#include "post_inc.h"
/******************************************************************************/


/******************************************************************************/
// docs/refactor/renderer/gpu-v2/01-phase-b-2d-compositing.md B2:
// RendererScheduleScreenshot() no longer touches lbDrawSurface here at
// all -- the actual capture happens later, post-composite, inside
// RendererSoftware::PresentFrame() (so it sees the ImGui overlay). The
// old LbScreenIsLocked()/RendererLockFramebuffer() dance existed only to
// guarantee lbDrawSurface was safe to read synchronously right here,
// which is meaningless now that this just queues a request.
TbBool take_screenshot(char *fname)
{
    return RendererScheduleScreenshot(fname, kfx_runtime_settings.screenshot_format);
}

TbBool cumulative_screen_shot(void)
{
    char fname[255] = "";
    const char *fext;
    switch (kfx_runtime_settings.screenshot_format)
    {
        case 1:
        fext = "png";
        break;
      case 2:
        fext = "bmp";
        break;
      default:
        ERRORLOG("Screenshot format incorrectly set.");
        return false;
    }
    uint64_t i;
    for (i = 0; i < 10000; i++)
    {
        snprintf(fname, sizeof(fname), "scrshots/scr%05" PRIu64 ".%s", (uint64_t)(i), fext);
        if (!LbFileExists(fname)) break;
    }
    if (i >= 10000)
    {
        ui_show_onscreen_msg(kfx_sim_state.turns_per_second, "No free filename for screenshot.");
        return false;
    }
    TbBool ret = take_screenshot(fname);
    char msg[sizeof(fname) + 32];
    if (ret)
    {
        snprintf(msg, sizeof(msg), "File \"%s\" saved.", fname);
    }
    else
    {
        snprintf(msg, sizeof(msg), "Cannot save \"%s\".", fname);
    }
    ui_show_onscreen_msg(kfx_sim_state.turns_per_second, msg);
    return ret;
}

/**
 * Captures the screen to make a screenshot image.
 * @return Returns 0 if no capturing was performed, nonzero otherwise.
 */
TbBool perform_any_screen_capturing(void)
{
    TbBool captured=0;
    if ((kfx_sim_state.system_flags & GSF_CaptureSShot) != 0)
    {
      captured |= cumulative_screen_shot();
      clear_flag(kfx_sim_state.system_flags, GSF_CaptureSShot);
    }
    // docs/refactor/renderer/gpu-v2/01-phase-b-2d-compositing.md B2:
    // movie recording (GSF_CaptureMovie) retired -- nothing sets that
    // flag any more, see kfx_sim_state.h's own comment on it. The "REC"
    // flash this used to draw here (via LbTextDraw, straight into
    // lbDrawSurface) is dropped rather than ported to an ImGui draw:
    // cumulative_screen_shot() above already raises a "File saved"/
    // "Cannot save" on-screen message through show_onscreen_msg(), which
    // -- unlike this legacy CPU-buffer text -- is already correctly
    // composited (via the in-game ImGui HUD's text overlay,
    // frontgui_ingame_text.cpp, for every migrated session) and visible
    // in a post-composite screenshot; a second, purely decorative "REC"
    // indicator would be redundant with it.
    return captured;
}

/******************************************************************************/
