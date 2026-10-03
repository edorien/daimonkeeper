/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file state_versions.h
 *     Layout versions of the structs saved and resynced as raw bytes.
 * @par Purpose:
 *     Saved games (and continue-replays) write struct Game, the kfx_*_state
 *     structs and struct IntralevelData as raw chunks, and network resync
 *     sends the same structs. Each gets a version here, written
 *     into its chunk header (or the resync header) and checked on load: a
 *     save or resync whose version or size doesn't match is refused as a
 *     whole, before anything is changed (refactor pass 2, S09,
 *     docs/refactor-pass2/stage-09-versioned-state-chunks.md). There are no
 *     migrations: a save from an older layout can't be loaded.
 *
 *     When a struct's layout changes (a field added, removed, moved to
 *     another struct, or resized):
 *       1. bump its *_VER here;
 *       2. set its *_SIZE to the new sizeof -- the _Static_assert next to
 *          the struct's definition fails the build until you do;
 *       3. add a line to the decisions table in docs/refactor-pass2/
 *          README.md, so the release notes say saves from earlier versions
 *          can't be loaded.
 *     The sizes are the same on every build target: the structs use explicit
 *     int64_t/uint64_t fields and packed layouts throughout (see
 *     kfx_game/tests/save_layout_test.cpp).
 *
 *     Version 0 is what every chunk carried before S09, so such saves are
 *     refused too.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_STATE_VERSIONS_H
#define DK_STATE_VERSIONS_H

/* struct Game (kfx_game's game_legacy.h), the SGC_GameOrig chunk */
#define KFX_GAME_ORIG_VER          1
#define KFX_GAME_ORIG_SIZE         1

/* struct KfxSimState (kfx_sim_state.h), SGC_KfxSimState
   2: S10 -- gained play_gameturn, level_human_player, human_players_count,
      replay_active; struct Dungeon lost camera_deviate_quake/_jump
   3: S11 -- gained light_registry (the lights, from kfx_render's lish)
   4: pass 4 P4-F10 -- entrances_count removed (never read; never cleared between levels)
   5: pass 4 P4-F11 -- CreatureControl.party gained tunnel_last_dist, tunnel_same_dist_turns
   6: pass 4 P4-F12 -- those became tunnel_last_x, tunnel_last_y, tunnel_still_turns
   7: pass 4 S08 -- loaded_swipe_idx removed (kfx_render's, not saved)
   8: gained footstep_timeslice, footstep_near_creatures (update_footsteps_nearest_camera()'s statics)
   9: pass 5 S04 -- the drawing and local-camera fields left: Thing.last_turn_drawn, the room flag's
      last_turn_drawn/display_timer, CreatureControl's thought bubble timers and footstep_variant/_counter,
      footstep_timeslice, footstep_near_creatures (now kfx_render_state's and kfx_game_local's)
  10: pass 5 S11 -- the light registry's drawing state left struct Light (range, the flicker, the never-used
      animation fields, force_render_update) and the registry (stat_light_needs_updating) */
#define KFX_SIM_STATE_VER          10
#define KFX_SIM_STATE_SIZE         139795074

/* struct KfxNetState (kfx_net_state.h), SGC_KfxNetState
   2: S10 -- packet_load_enable, local_plyr_idx, human_players_count moved
      to kfx_sim_state */
#define KFX_NET_STATE_VER          2
#define KFX_NET_STATE_SIZE         2937522

/* struct KfxGameState (kfx_game_state.h), SGC_KfxGameState
   2: S10 -- play_gameturn moved to kfx_sim_state
   3: S11 -- lightst removed (its counters are in kfx_sim_state.light_registry)
   4: pass 4 P4-F8 -- level_file_version (was a global; lost on load and resync)
   5: pass 4 P4-F16 -- unchanged layout; marks the saves that have SGC_KfxConfigState (older ones are refused) */
#define KFX_GAME_STATE_VER         5
#define KFX_GAME_STATE_SIZE        439776

/* struct KfxFrontendState (kfx_frontend_state.h), SGC_KfxFrontendState */
#define KFX_FRONTEND_STATE_VER     1
#define KFX_FRONTEND_STATE_SIZE    1229104

/* Ariadne's navigation mesh (kfx_pathfinding's ariadne_saved_state.h: kfx_pathfinding_state, the triangles,
   points, regions and point-location cache, in that order), SGC_AriadneState and the resync blob.
   1: pass 4 P4-F7 -- first saved (it was rebuilt from the map after a load or resync, which gives a different
      mesh). A test checks ariadne_saved_state_size() against KFX_ARIADNE_STATE_SIZE. */
#define KFX_ARIADNE_STATE_VER      1
#define KFX_ARIADNE_STATE_SIZE     9075648

/* struct KfxConfigState (kfx_config_state.h): its size, which kfx_config_state.h checks in every file (pass 4 P4-F17).
   Saved and resynced: the part from texture_animation to pay_day_progress (KFX_CONFIG_STATE_SAVED_*),
   SGC_KfxConfigState and the resync blob.
   1: pass 4 P4-F16 -- first saved (the split out of struct Game left it out: a load or resync lost the level's
      configuration as its script changed it, the payday progress and the slabs' texture packs). */
#define KFX_CONFIG_STATE_SIZE         11391840
#define KFX_CONFIG_STATE_VER          1
#define KFX_CONFIG_STATE_SAVED_SIZE   11391800

/* struct IntralevelData (kfx_game's game_merge.h), SGC_IntralevelData */
#define KFX_INTRALEVEL_VER         1
#define KFX_INTRALEVEL_SIZE        81747

/* struct LightsShadows (kfx_render's lish) was resynced as its own blob, at
   version 1, until S11: it now holds only shading, which isn't saved or sent
   -- the lights moved into kfx_sim_state. */

#endif
