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

#include <stddef.h>

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
      animation fields, force_render_update) and the registry (stat_light_needs_updating)
  11: upstream #5376 -- replay_active left (replay.load_enable, packet_data.h's struct ReplayState)
  12: upstream #5376 -- system_flags became run_after_victory (the rest are local_system_flags); PlayerInfo
      gained each player's start settings (zoom_distance, frontview_zoom_distance, cheats_allowed,
      skip_heart_zoom, highlight_mode)
  13: gained keeper_assist[] (the combat assistant's rules and counters, docs/newFeatures/llm/)
  14: pass 5 S05 -- this machine's view left for kfx_render's kfx_local_view (SGC_KfxLocalView): hand_over_subtile_*,
      chosen_room_*, chosen_spell_*, manufactr_*, applied_lens_type, small_map_state, active_panel_mnu_idx and
      struct Dungeon's visible_battles; the tendency mirror creatures_tend_imprison/_flee is gone
  15: upstream #5396 -- Thing gained last_turn_pushed, pushed_by_player (a creature pushed into the abyss
      counts as dead in battle)
  16: upstream #5383 -- timestamp, timestamp_tz, timestamp_turn (the host's clock, Lua's GetGameTimestamp) */
#define KFX_SIM_STATE_VER          16
#define KFX_SIM_STATE_SIZE         139937987

/* struct KfxNetState (kfx_net_state.h), SGC_KfxNetState
   2: S10 -- packet_load_enable, local_plyr_idx, human_players_count moved
      to kfx_sim_state
   3: upstream #5376 -- the packet save/replay file fields and pckt_gameturn left (struct ReplayState
      replay, packet_data.h)
   4: pass 5 S05 -- comp_player_* (the computer assistant's settings) left for kfx_render's kfx_local_view */
#define KFX_NET_STATE_VER          4
#define KFX_NET_STATE_SIZE         2937128

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

/* struct KfxLocalView (kfx_render's kfx_local_view.h), SGC_KfxLocalView: this machine's view of the game.
   1: refactor pass 5, S05 -- the fields left kfx_sim_state and kfx_net_state. */
#define KFX_LOCAL_VIEW_VER         1
#define KFX_LOCAL_VIEW_SIZE        98

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
      configuration as its script changed it, the payday progress and the slabs' texture packs).
   2: SlabConfigStats.roughness (terrain.cfg's Roughness, rough terrain) -- in what was padding, so the size
      stayed; the byte's meaning changed.
   3: upstream #5394 -- CreatureModelConfig gained fall_acceleration, inertia_floor, inertia_air
   4: upstream #5402 -- ObjectConfigStats gained inertia_floor, inertia_air (in what was padding: the size
      stayed, the fields after them moved) */
#define KFX_CONFIG_STATE_SIZE         11394912
#define KFX_CONFIG_STATE_VER          4
#define KFX_CONFIG_STATE_SAVED_SIZE   11394872

/* struct IntralevelData (kfx_game's game_merge.h), SGC_IntralevelData */
#define KFX_INTRALEVEL_VER         1
#define KFX_INTRALEVEL_SIZE        81747

/* The script data of a save (SGC_LuaData): what core/serialisation.lua serialises, as bytes the game does not
   interpret itself (the data carries its own format version too). Variable length, so no SIZE.
   0: (unchecked) functions kept as Lua bytecode
   1: security pass S03 -- functions are kept as references (a global's name, or the text of an anonymous function of
      a loaded script), never as bytecode; saves of version 0 are refused before anything is loaded */
#define KFX_LUA_DATA_VER           1
/* lua_get_serialised_data()'s length when serialising failed (a function that can't be kept in a save, say), as
   opposed to NULL with length 0 for "there is no script state": the game is then not saved, instead of saved
   without its script data. */
#define KFX_LUA_SERIALISE_FAILED   ((size_t)-1)

/* struct LightsShadows (kfx_render's lish) was resynced as its own blob, at
   version 1, until S11: it now holds only shading, which isn't saved or sent
   -- the lights moved into kfx_sim_state. */

#endif
