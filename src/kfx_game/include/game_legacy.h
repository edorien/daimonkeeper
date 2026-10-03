/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_legacy.h
 *     Header file for game_legacy.c.
 * @par Purpose:
 *     Game structure maintain functions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     21 Oct 2009 - 23 Nov 2012
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/

#ifndef DK_GAMELEGACY_H
#define DK_GAMELEGACY_H

#include "bflib_basics.h"
#include "globals.h"

#include "player_data.h"
#include "config_campaigns.h"
#include "config_magic.h"
#include "config_trapdoor.h"
#include "config_objects.h"
#include "config_mods.h"
#include "config_cubes.h"
#include "config_powerhands.h"
#include "config_cubes.h"
#include "config_creature.h"
#include "config_crtrmodel.h"
#include "config_effects.h"
#include "config_objects.h"
#include "config_rules.h"
#include "config_players.h"
#include "config_slabsets.h"
#include "dungeon_data.h"
#include "thing_data.h"
#include "thing_traps.h"
#include "thing_doors.h"
#include "thing_objects.h"
#include "thing_creature.h"
#include "thing_list.h"
#include "room_data.h"
#include "slab_data.h"
#include "map_data.h"
#include "creature_control.h"
#include "creature_battle.h"
#include "map_columns.h"
#include "map_events.h"
#include "player_computer.h"
#include "player_complookup.h"
#include "power_process.h"
#include "net_game.h"
#include "packets.h"
#include "sounds.h"
#include "game_merge.h"
#include "engine_textures.h"
#include "kfx_sim_state.h"
#include "kfx_net_state.h"
#include "kfx_game_state.h"
#include "kfx_config_state.h"
#include "kfx_render_state.h"


#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// GameKinds/GameOperationFlags/GameNumfieldDFlags moved to kfx_sim_state.h
// (stage 13, docs/refactor/stage-13-enforce-and-document.md) alongside
// the struct Game fields they flag (game_kind/operation_flags/
// view_mode_flags), all relocated there in the same pass.
/******************************************************************************/
#pragma pack(1)

// struct LuaFuncsConf/struct Configs moved to kfx_config_state.h (stage
// 13, docs/refactor/stage-13-enforce-and-document.md) alongside the conf
// field below that embeds Configs by value -- Configs' own sub-structs
// (SlabsConfig, PowerHandConfig, ...) are all kfx_config-owned types, so
// this was always really kfx_config's own aggregate type, just homed
// here since stage 9. GUI_MESSAGES_COUNT/GUI_MESSAGES_DELAY/struct
// GuiMessage (gui_msgs.h), moved here in stage 9, moved again to
// kfx_frontend_state.h in stage 10 alongside the messages[] field that
// embeds them -- see docs/refactor/stage-10-kfx-frontend.md.

// struct LogThingDesyncInfo/LogPlayerDesyncInfo/LogRoomDesyncInfo/
// DesyncChecksums/LogDetailedSnapshot moved to kfx_net_state.h (stage
// 8.3) -- only ever used by net_checksums.c. See
// docs/refactor/stage-08-kfx-net.md.

struct Game {
    // continue_level_number, selected_level_number moved to
    // kfx_game_state.h (stage 9) -- see docs/refactor/stage-09-kfx-game.md.
    // system_flags, operation_flags, view_mode_flags, mode_flags moved to
    // kfx_sim_state.h (stage 13, docs/refactor/stage-13-enforce-and-document.md)
    // -- each read by several other libraries, but kfx_sim is the
    // lowest-ranked of every one of their consumer sets.
    // flags_gui moved to kfx_game_state.h (stage 13) -- see
    // docs/refactor/stage-13-enforce-and-document.md.
    // easter_eggs_enabled moved to kfx_sim_state.h (stage 13) -- see
    // docs/refactor/stage-13-enforce-and-document.md.
    // eastegg01_cntr/eastegg02_cntr/save_game_slot moved to
    // kfx_frontend_state.h (stage 13) -- see
    // docs/refactor/stage-13-enforce-and-document.md.
    // music_track/music_fname moved to kfx_game_state.h (stage 13) -- see
    // docs/refactor/stage-13-enforce-and-document.md.
    // active_lens_type moved to kfx_render_state.h, mouse_light_pos/
    // something_light_x/_y below moved there too (stage 13, docs/refactor/
    // stage-13-enforce-and-document.md); applied_lens_type moved to
    // kfx_sim_state.h in the same pass since kfx_sim is the lowest-ranked
    // of its three consumers (kfx_game/kfx_render/kfx_sim).
    // players, columns_data, slabset_num/slabset, slabobjs_num/
    // slabobjs_idx/slabobjs, navigation_map, map, slabmap, around_map/
    // around_slab/around_slab_eight/small_around_slab moved to
    // kfx_sim_state.h (stage 6.7 increments 2+5) -- see
    // docs/refactor/stage-06-kfx-sim.md.
    // land_map_start moved to kfx_frontend_state.h (stage 13) -- see
    // docs/refactor/stage-13-enforce-and-document.md.
    // lish (struct LightsShadows) was the last remaining struct-Game
    // field after stage 13's decomposition pass -- extracted to its own
    // kfx_render-owned global (src/kfx_render/include/light_data.h,
    // `extern struct LightsShadows lish;`) in stage 13.3 (docs/refactor/
    // stage-13-enforce-and-document.md): engine_render.c/light_data.c
    // are lish's actual functional owners (hundreds of accesses each),
    // and struct LightsShadows already embedded struct Light
    // (kfx_render/light_data.h) by value, so kfx_render was always its
    // true home. Its lights then moved on to kfx_sim_state.light_registry
    // in refactor pass 2 (S11, docs/refactor-pass2/
    // stage-11-lighting-split.md); lish keeps only the shading.
    //
    // struct Game is kept as a non-empty placeholder (rather than
    // deleted outright) because net_resync.cpp/game_saves.c still treat
    // it as one blob in a fixed serialization chain (raw network resync
    // and the save-game chunk format); `_reserved` exists purely so the
    // struct isn't empty (not all compilers accept a zero-member
    // struct). Save-game loading already tolerates a chunk-size mismatch
    // (see game_saves.c's `hdr.len != sizeof(struct Game)` check, which
    // WARNLOGs and skips the chunk rather than failing) -- old saves
    // just lose their (regenerable) lighting/shadow-cache snapshot, not
    // player progress.
    unsigned char _reserved;
    // cctrl_data, things_data moved to kfx_sim_state.h (stage 6.7
    // increment 3) -- see docs/refactor/stage-06-kfx-sim.md.
    // computer_task, computer, rooms, dungeon, thing_lists moved to
    // kfx_sim_state.h (stage 6.7 increments 4+5) -- see
    // docs/refactor/stage-06-kfx-sim.md.
    // unrevealed_column_idx moved to kfx_sim_state.h (stage 13) -- see
    // docs/refactor/stage-13-enforce-and-document.md.
    // packet_save_enable/load_enable, packet_fname, packet_fopened,
    // packet_save_fp, packet_file_pos, packet_save_head, turns_stored,
    // turns_fastforward, packet_loading_in_progress,
    // packet_checksum_verify, log_things_start_turn/end_turn,
    // turns_packetoff, local_plyr_idx, packet_load_initialized moved to
    // kfx_net_state.h (stage 8.3) -- see docs/refactor/stage-08-kfx-net.md.
    // Originally, save_catalogue was here.
    // campaign_fname moved to kfx_game_state.h (stage 9) -- see
    // docs/refactor/stage-09-kfx-game.md.
    // event[] moved to kfx_sim_state.h (stage 13) -- see
    // docs/refactor/stage-13-enforce-and-document.md.
    // ceiling_height_max/min, ceiling_dist, ceiling_search_dist, ceiling_step
    // moved to kfx_sim_state.h (stage 7.2) -- despite living in struct
    // Game's originally-assumed "render" region (docs/refactor/
    // stage-07-kfx-render.md), these are used exclusively by kfx_sim's
    // map_ceiling.c, not by anything render-owned. See
    // docs/refactor/stage-06-kfx-sim.md.
    // col_static_entries/texture_animation/texture_id moved to
    // kfx_config_state.h (stage 13) -- kfx_config (lvl_filesdk1.c) is the
    // lowest-ranked of their consumer sets. See
    // docs/refactor/stage-13-enforce-and-document.md.
    //unsigned char level_file_number; // merged with level_number to get maps > 255
    // loaded_level_number moved to kfx_game_state.h (stage 9) -- see
    // docs/refactor/stage-09-kfx-game.md.
    // synced_free_things(_count), unsynced_free_things(_count) moved to
    // kfx_sim_state.h (stage 6.7 increment 3) -- see
    // docs/refactor/stage-06-kfx-sim.md.
    // play_gameturn, pckt_gameturn moved to kfx_game_state.h (stage 9) --
    // see docs/refactor/stage-09-kfx-game.md; play_gameturn then to
    // kfx_sim_state.h (refactor pass 2, S10).
    // action_random_seed, ai_random_seed, player_random_seed,
    // unsync_random_seed, sound_random_seed moved to kfx_sim_state.h
    // (stage 6.7 increment 7) -- see docs/refactor/stage-06-kfx-sim.md.
    // time_delta moved to kfx_frontend_state.h (stage 13) -- see
    // docs/refactor/stage-13-enforce-and-document.md.
    // top_cube, small_map_state moved to kfx_sim_state.h (stage 13) -- see
    // docs/refactor/stage-13-enforce-and-document.md.
    // packets[], input_lag_turns, human_players_count moved to
    // kfx_net_state.h (stage 8.3) -- see docs/refactor/stage-08-kfx-net.md.
    // neutral_player_num moved to kfx_config_state.h (stage 13) -- a
    // kfx_config consumer (config_objects.c) has since appeared, making
    // kfx_config the new lowest-ranked of its consumer set (superseding
    // the stage 7.2-era "stays here" reasoning below, which predates that
    // consumer). See docs/refactor/stage-13-enforce-and-document.md.
    // gold_lookup, block_health, entrance_room_id, entrances_count moved to
    // kfx_sim_state.h (stage 6.7 increment 4) -- see
    // docs/refactor/stage-06-kfx-sim.md.
    // ambient_sound_thing_idx moved to kfx_game_state.h (stage 13) -- see
    // docs/refactor/stage-13-enforce-and-document.md.
    // nodungeon_creatr_list_start, game_kind, bookmark moved to
    // kfx_sim_state.h (stage 13) -- see
    // docs/refactor/stage-13-enforce-and-document.md.
    // map_changed_for_navigation moved to kfx_sim_state.h (stage 13) --
    // see docs/refactor/stage-13-enforce-and-document.md.
    // creature_scores moved to kfx_sim_state.h (stage 6.7 increment 3) --
    // see docs/refactor/stage-06-kfx-sim.md.
    // pool moved to kfx_sim_state.h (stage 6.7 increment 5) -- see
    // docs/refactor/stage-06-kfx-sim.md.
    // frame_skip moved to kfx_net_state.h (stage 13) -- see
    // docs/refactor/stage-13-enforce-and-document.md.
    // frame_step, paused_at_gameturn moved to kfx_game_state.h (stage 9)
    // -- see docs/refactor/stage-09-kfx-game.md.
    // pay_day_progress moved to kfx_config_state.h (stage 13) together
    // with conf below -- see the comment there about why they moved as a
    // pair. See docs/refactor/stage-13-enforce-and-document.md.
    // sound_settings moved to kfx_game_state.h (stage 13) -- see
    // docs/refactor/stage-13-enforce-and-document.md.
    // battles moved to kfx_sim_state.h (stage 6.7 increment 7) -- see
    // docs/refactor/stage-06-kfx-sim.md.
    // evntbox_text_objective, evntbox_scroll_window, flash_button_index,
    // active_panel_mnu_idx, comp_player_aggressive/_defensive/_construct/
    // _creatrsonly, creatures_tend_imprison/_flee moved to
    // kfx_frontend_state.h (stage 10) -- see
    // docs/refactor/stage-10-kfx-frontend.md. evntbox_text_buffer,
    // loaded_swipe_idx moved to kfx_sim_state.h/kfx_game_state.h instead,
    // same stage -- mischaracterized as kfx_frontend in the doc's initial
    // research; actual usage is exclusively kfx_sim/kfx_game.
    // bonus_time moved to kfx_game_state.h (stage 13) -- see
    // docs/refactor/stage-13-enforce-and-document.md.
    // armageddon_mappos/_cast_turn/_over_turn/_caster_idx moved to
    // kfx_sim_state.h (stage 13) -- see
    // docs/refactor/stage-13-enforce-and-document.md.
    // hand_over_subtile_x/y, chosen_room_kind/spridx/tooltip,
    // chosen_spell_type/spridx/tooltip, manufactr_element/spridx/tooltip
    // moved to kfx_sim_state.h (stage 6.7 increment 6) -- see
    // docs/refactor/stage-06-kfx-sim.md.
    // conf moved to kfx_config_state.h (stage 13) -- see
    // docs/refactor/stage-13-enforce-and-document.md.
    // turn_last_checked_for_gold, max_custom_box_kind moved to
    // kfx_sim_state.h (stage 13) -- see
    // docs/refactor/stage-13-enforce-and-document.md.
    // computer_chat_flags moved to kfx_game_state.h (stage 10) --
    // mischaracterized as kfx_frontend in the doc's initial research;
    // actual usage is exclusively kfx_game/kfx_sim. quick_messages,
    // messages moved to kfx_frontend_state.h, same stage. See
    // docs/refactor/stage-10-kfx-frontend.md.
    // lightst moved to kfx_game_state.h (stage 13), then into
    // kfx_sim_state.light_registry (refactor pass 2, S11).
    // current_player_turn, script_current_player, triggered_object_location
    // moved to kfx_sim_state.h (stage 9) -- mischaracterized as kfx_game in
    // stage 9's initial research; actual usage is exclusively kfx_sim. See
    // docs/refactor/stage-09-kfx-game.md.
    // box_tooltip moved to kfx_frontend_state.h (stage 10) -- see
    // docs/refactor/stage-10-kfx-frontend.md. fx_lines, active_fx_lines
    // moved to kfx_sim_state.h instead, same stage -- mischaracterized
    // as kfx_frontend in the doc's initial research; actual usage is
    // exclusively kfx_sim.
    // action_points[] moved to kfx_sim_state.h (stage 9), same
    // mischaracterization as above.
    // last_level moved to kfx_render_state.h (stage 13) -- see
    // docs/refactor/stage-13-enforce-and-document.md.
    // script, script_timer_player/id/limit, script_value_type/id,
    // script_variable_player/target/target_type moved to kfx_game_state.h
    // (stage 9) -- see docs/refactor/stage-09-kfx-game.md. timer_real
    // moved to kfx_game_state.h (stage 13) -- kfx_game is the
    // lowest-ranked of its two consumers (kfx_frontend/kfx_game). See
    // docs/refactor/stage-13-enforce-and-document.md.
    // heart_lost_display_message/_quick_message/_message_id/_message_target
    // moved to kfx_game_state.h (stage 10) -- mischaracterized as
    // kfx_frontend in the doc's initial research; actual usage is
    // exclusively kfx_game/kfx_sim. See docs/refactor/stage-10-kfx-frontend.md.
    // slab_ext_data/slab_ext_data_initial moved to kfx_config_state.h
    // (stage 13) -- kfx_config (lvl_filesdk1.c) is the lowest-ranked of
    // their consumer set. See docs/refactor/stage-13-enforce-and-document.md.
    // delta_time moved to kfx_render_state.h, process_turn_time to
    // kfx_net_state.h (stage 13) -- see
    // docs/refactor/stage-13-enforce-and-document.md.
    // flash_button_time moved to kfx_frontend_state.h (stage 10).
    // map_subtiles_x/y, map_tiles_x/y, navigation_map_size_x/y,
    // around_map/around_slab/around_slab_eight/small_around_slab moved to
    // kfx_sim_state.h (stage 6.7 increments 1-2) -- see
    // docs/refactor/stage-06-kfx-sim.md.

    // skip_initial_input_turns moved to kfx_net_state.h (stage 13) -- see
    // docs/refactor/stage-13-enforce-and-document.md.

    // host_checksums, log_snapshot moved to kfx_net_state.h (stage 8.3)
    // -- see docs/refactor/stage-08-kfx-net.md.
};

#pragma pack()
/******************************************************************************/
extern struct Game game;

// Tabled in GamePort (ports/game_port.def) so
// net_resync.cpp (kfx_net) doesn't need to #include this header directly
// just to memcpy `game`+`kfx_game_state` wholesale as part of the raw-blob
// resync wire format -- see
// docs/refactor/todo/remove-remaining-layering-violations.md.
const char *resync_export_game_state(size_t *len);
TbBool resync_import_game_state(const char *data, size_t len);
// turns_per_second moved to kfx_sim_state.h (stage 13.3, docs/refactor/
// stage-13-enforce-and-document.md).
// fps_limit_current/main/secondary moved to kfx_platform's bflib_video.h
// (stage 13.3, docs/refactor/stage-13-enforce-and-document.md) --
// together with redetect_screen_refresh_rate_for_draw(), a
// misclassified function that only ever touched these and lbWindow.

// network_is_active() moved to kfx_sim_state.h as a static inline
// (stage 13.3, docs/refactor/stage-13-enforce-and-document.md) -- it
// only reads kfx_sim_state.system_flags, and kfx_config is the
// lowest-ranked of its real consumers (kfx_apploop/kfx_frontend/
// kfx_game/kfx_net/kfx_script/kfx_sim also call it).

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
