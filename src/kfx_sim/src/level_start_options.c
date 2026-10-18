/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file level_start_options.c
 *     See level_start_options.h.
 * @par Comment:
 *     None.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "level_start_options.h"

#include <string.h>

#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "player_data.h"
#include "slab_data.h"
#include "ports/ui_port.h"
#include "post_inc.h"

/******************************************************************************/
static struct LevelStartOptions requested;
static LevelNumber requested_lvnum = 0;

void level_start_options_request(LevelNumber lvnum, const struct LevelStartOptions *opts)
{
    requested = *opts;
    requested_lvnum = lvnum;
}

void level_start_options_clear(void)
{
    memset(&requested, 0, sizeof(requested));
    requested_lvnum = 0;
}

struct LevelStartOptions level_start_options_requested(LevelNumber lvnum)
{
    struct LevelStartOptions none;
    memset(&none, 0, sizeof(none));
    return ((requested_lvnum != 0) && (requested_lvnum == lvnum)) ? requested : none;
}

struct LevelStartOptions level_start_options_take(LevelNumber lvnum)
{
    const struct LevelStartOptions opts = level_start_options_requested(lvnum);
    level_start_options_clear();
    return opts;
}

int64_t level_start_options_apply_world(const struct LevelStartOptions *opts)
{
    int64_t revealed = 0;
    if (opts->sharing != LevelStartSharing_None)
    {
        const size_t rules_count = sizeof(kfx_config_state.conf.rules) / sizeof(kfx_config_state.conf.rules[0]);
        for (size_t i = 0; i < rules_count; i++)
        {
            kfx_config_state.conf.rules[i].gameplay.allies_share_vision = true;
            kfx_config_state.conf.rules[i].gameplay.allies_share_drop = (opts->sharing == LevelStartSharing_VisionAndDrop);
        }
        ui_panel_map_update(0, 0, kfx_sim_state.map_subtiles_x + 1, kfx_sim_state.map_subtiles_y + 1);
    }
    if (opts->reveal_map)
    {
        for (PlayerNumber plyr_idx = 0; plyr_idx < PLAYERS_COUNT; plyr_idx++)
        {
            struct PlayerInfo *player = get_player(plyr_idx);
            if (!player_exists(player) || !player_is_keeper(plyr_idx))
                continue;
            reveal_whole_map(player);
            revealed++;
        }
    }
    return revealed;
}
