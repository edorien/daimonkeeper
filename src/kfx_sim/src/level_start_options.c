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

#include "player_data.h"
#include "slab_data.h"
#include "post_inc.h"

/******************************************************************************/
static TbBool reveal_requested = false;
static LevelNumber reveal_lvnum = 0;

void level_start_reveal_map_request(LevelNumber lvnum)
{
    reveal_requested = true;
    reveal_lvnum = lvnum;
}

void level_start_options_clear(void)
{
    reveal_requested = false;
    reveal_lvnum = 0;
}

TbBool level_start_reveal_map_requested(LevelNumber lvnum)
{
    return reveal_requested && (reveal_lvnum == lvnum);
}

int64_t level_start_options_apply(LevelNumber lvnum)
{
    const TbBool reveal = level_start_reveal_map_requested(lvnum);
    level_start_options_clear();
    int64_t revealed = 0;
    if (!reveal)
        return 0;
    for (PlayerNumber plyr_idx = 0; plyr_idx < PLAYERS_COUNT; plyr_idx++)
    {
        struct PlayerInfo *player = get_player(plyr_idx);
        if (!player_exists(player) || !player_is_keeper(plyr_idx))
            continue;
        reveal_whole_map(player);
        revealed++;
    }
    return revealed;
}
