/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_compat_review.c
 *     After a level loads: log the compat report and decide whether the
 *     player sees it before play.
 * @par Purpose:
 *     See game_compat_review.h. The warning itself is drawn by kfx_frontend
 *     (frontgui_ingame.cpp), which polls compat_report_review_pending().
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "game_compat_review.h"

#include "compat_report.h"
#include "config_campaigns.h"
#include "config_keeperfx.h"
#include "kfx_sim_state.h"
#include "ports/editor_port.h"

#include <stdio.h>
#include "post_inc.h"

TbBool compat_review_wanted(void)
{
    if (compat_report_count() <= 0)
        return false;
    if (kfx_sim_state.game_kind != GKind_LocalGame)
        return false; // multiplayer: the report is logged only, for now
    if (kfx_sim_state.replay_active)
        return false;
    if (editorport_is_active())
        return false;
    if (api_enabled)
        return false; // an agent may be driving; it reads the COMPAT: log lines
#ifdef FUNCTESTING
    if (flag_is_set(start_params.functest_flags, FTF_Enabled))
        return false;
#endif
    return true;
}

void compat_review_after_level_load(LevelNumber lvnum)
{
    char context[128];
    snprintf(context, sizeof(context), "Level %" PRId64 " of %s", (int64_t)lvnum, campaign.name);
    compat_report_log(context);
    compat_report_set_review_pending(compat_review_wanted());
}
