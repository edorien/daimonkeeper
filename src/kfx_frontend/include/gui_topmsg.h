/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file gui_topmsg.h
 *     GUI Messages at screen top functions.
 * @par Purpose:
 *     gui_topmsg functions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     14 May 2010 - 21 Jul 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_GUI_TOPMSG_H
#define DK_GUI_TOPMSG_H

#include "globals.h"
#include "bflib_basics.h"
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
#pragma pack(1)

// enum ErrorStatisticEntries moved to globals.h (stage 6) so kfx_sim can
// reference it without depending on this frontend header -- see
// docs/refactor/stage-06-kfx-sim.md.

struct ErrorStatistics {
    uint64_t n;
    uint64_t nprv;
    const char *msg;
};

#pragma pack()
/******************************************************************************/
void erstats_clear(void);
int64_t erstat_inc(int64_t stat_num);
TbBool erstat_check(void);
extern struct ErrorStatistics erstat[];
extern int64_t last_checked_stat_num;
extern double render_onscreen_msg_time;
extern char onscreen_msg_text[]; // Phase 3: read by the ImGui text overlay

// Phase 3: draw_onscreen_direct_messages()'s last "banner should show"
// verdict (render_onscreen_msg_time > 0 || erstat_check()), for the ImGui
// overlay -- see frontgui_ingame_text.cpp / that function's comment.
TbBool onscreen_banner_visible(void);

TbBool is_onscreen_msg_visible(void);
TbBool show_onscreen_msg(int64_t nturns, const char *fmt_str, ...) KFX_PRINTF_FORMAT(2, 3);
TbBool draw_onscreen_direct_messages(void);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
