/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_lifecycle.c
 *     Clearing/resetting the simulated game world (map, things, players,
 *     computer state) between levels or before a save/load.
 * @par Comment:
 *     None.
 */
/******************************************************************************/
#include "pre_inc.h"

#include "game_lifecycle.h"

#include "globals.h"
#include "bflib_basics.h"
#include "kfx_sim_state.h"
#include "map_data.h"
#include "slab_data.h"
#include "map_columns.h"
#include "map_ceiling.h"
#include "creature_control.h"
#include "room_data.h"
#include "actionpt.h"
#include "kfx_config_state.h"
#include "player_data.h"
#include "player_instances.h"
#include "player_utils.h"
#include "dungeon_data.h"
#include "thing_data.h"
#include "thing_list.h"
#include "light_registry.h"
#include "ports/audio_port.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

void clear_creature_pool(void)
{
    memset(&kfx_sim_state.pool,0,sizeof(struct CreaturePool));
    kfx_sim_state.pool.is_empty = true;
}

void clear_map(void)
{
    clear_mapmap();
    clear_slabs();
    clear_columns();
}

void clear_things_and_persons_data(void)
{
    struct Thing *thing;
    int64_t i;
    memset(kfx_sim_state.thing_lists, 0, sizeof(kfx_sim_state.thing_lists));
    audio_reset_ambient_sound_thing_idx();
    kfx_sim_state.nodungeon_creatr_list_start = 0;
    for (i=0; i < THINGS_COUNT; i++)
    {
        thing = &kfx_sim_state.things_data[i];
        memset(thing, 0, sizeof(struct Thing));
        thing->owner = PLAYERS_COUNT;
        // Not the map's centre: this runs before the level's map size is read, so that was the previous level's,
        // and a level's state (saved, resynced) depended on what was played before it. Subtile 0's centre is
        // what the first level after start-up always got.
        thing->mappos.x.val = subtile_coord_center(0);
        thing->mappos.y.val = subtile_coord_center(0);

        // Create the list of free indices (skip index 0 since that's INVALID_THING
        if (i > 0) {
            if (i < SYNCED_THINGS_COUNT) {
                kfx_sim_state.synced_free_things[SYNCED_THINGS_COUNT-1-i] = i;
            } else if (i < THINGS_COUNT) {
                kfx_sim_state.unsynced_free_things[THINGS_COUNT-1-i] = i;
            }
        }
    }
    kfx_sim_state.synced_free_things_count = SYNCED_THINGS_COUNT-1; // 1 to 8191. Note: COUNT macros aren't real representations of how many things there should be, all of them are off by 1.
    kfx_sim_state.unsynced_free_things_count = UNSYNCED_THINGS_COUNT-1; // 8192 to 12287

    for (i=0; i < CREATURES_COUNT; i++)
    {
      memset(&kfx_sim_state.cctrl_data[i], 0, sizeof(struct CreatureControl));
    }
}

void clear_computer(void)
{
    int64_t i;
    SYNCDBG(8,"Starting");
    for (i=0; i < COMPUTER_TASKS_COUNT; i++)
    {
        memset(&kfx_sim_state.computer_task[i], 0, sizeof(struct ComputerTask));
    }
    for (i=0; i < GOLD_LOOKUP_COUNT; i++)
    {
        memset(&kfx_sim_state.gold_lookup[i], 0, sizeof(struct GoldLookup));
    }
    for (i=0; i < PLAYERS_COUNT; i++)
    {
        memset(&kfx_sim_state.computer[i], 0, sizeof(struct Computer2));
    }
}

void init_keepers_map_exploration(void)
{
    struct PlayerInfo *player;
    int64_t i;
    for (i=0; i < PLAYERS_COUNT; i++)
    {
      player = get_player(i);
      if (is_active_keeper(player) || player_is_roaming(i))
      {
          // Additional init - the main one is in init_player()
          if ((player->allocflags & PlaF_CompCtrl) != 0) {
              init_keeper_map_exploration_by_terrain(player);
              init_keeper_map_exploration_by_creatures(player);
          }
      }
    }
}

void clear_players_for_save(void)
{
    struct PlayerInfo *player;
    int64_t saved_player_id;
    int64_t saved_player_type;
    int64_t saved_allocation_flags;
    struct Camera cammem;
    int64_t i;
    for (i=0; i < PLAYERS_COUNT; i++)
    {
      player = get_player(i);
      saved_player_id = player->id_number;
      saved_player_type = player->player_type;
      saved_allocation_flags = player->allocflags;
      memcpy(&cammem,&player->cameras[CamIV_FirstPerson],sizeof(struct Camera));
      memset(player, 0, sizeof(struct PlayerInfo));
      player->id_number = saved_player_id;
      player->player_type = saved_player_type;
      set_flag_value(player->allocflags, PlaF_Allocated, ((saved_allocation_flags & PlaF_Allocated) != 0));
      set_flag_value(player->allocflags, PlaF_CompCtrl, ((saved_allocation_flags & PlaF_CompCtrl) != 0));
      set_flag_value(player->allocflags, PlaF_Placeholder, ((saved_allocation_flags & PlaF_Placeholder) != 0));
      memcpy(&player->cameras[CamIV_FirstPerson],&cammem,sizeof(struct Camera));
      set_player_active_camera(player, CamIV_FirstPerson);
    }
}

void delete_all_thing_structures(void)
{
    int64_t i;
    struct Thing *thing;
    for (i=1; i < THINGS_COUNT; i++)
    {
      thing = thing_get(i);
      if (thing_exists(thing)) {
          delete_thing_structure(thing, 1);
      }
        if (i < SYNCED_THINGS_COUNT) {
            kfx_sim_state.synced_free_things[SYNCED_THINGS_COUNT-1-i] = i;
        } else if (i < THINGS_COUNT) {
            kfx_sim_state.unsynced_free_things[THINGS_COUNT-1-i] = i;
        }
    }
    kfx_sim_state.synced_free_things_count = SYNCED_THINGS_COUNT-1;
    kfx_sim_state.unsynced_free_things_count = UNSYNCED_THINGS_COUNT-1;
}

void delete_all_structures(void)
{
    SYNCDBG(6,"Starting");
    delete_all_thing_structures();
    delete_all_control_structures();
    delete_all_room_structures();
    delete_all_action_point_structures();
    light_initialise();
    SYNCDBG(16,"Done");
}

void clear_game_for_save(void)
{
    SYNCDBG(6,"Starting");
    delete_all_structures();
    light_initialise();
    clear_mapwho();
    kfx_sim_state.entrance_room_id = 0;
    kfx_sim_state.action_random_seed = 0;
    kfx_sim_state.ai_random_seed = 0;
    kfx_sim_state.player_random_seed = 0;
    clear_columns();
    clear_players_for_save();
    clear_dungeons();
}

void reset_creature_max_levels(void)
{
    int64_t i;
    int64_t k;
    for (i=0; i < DUNGEONS_COUNT; i++)
    {
        struct Dungeon *dungeon;
        dungeon = get_dungeon(i);
        for (k=1; k < kfx_config_state.conf.crtr_conf.model_count; k++)
        {
            dungeon->creature_max_level[k] = CREATURE_MAX_LEVEL+1;
        }
    }
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
