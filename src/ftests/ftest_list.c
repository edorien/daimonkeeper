#include "globals.h"
#include "game_commands.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "ftest.h"

/**
 * Add the header files for all tests below here
 */
#include "tests/ftest_template.h"
#include "tests/ftest_local_view_transitions.h"
#include "tests/ftest_bug_imp_tp_job_attack_door.h"
#include "tests/ftest_bug_pathing_pillar_circling.h"
#include "tests/ftest_bug_imp_goldseam_dig.h"
#include "tests/ftest_bug_invisible_units_cant_select.h"
#include "tests/ftest_bug_pathing_stair_treasury.h"
#include "tests/ftest_bug_ai_bridge.h"
#include "tests/ftest_creature_combat_power_hand.h"
#include "tests/ftest_ai_gesture_single_turn.h"
#include "tests/ftest_ai_gesture_drag_verbs.h"
#include "tests/ftest_ai_seat_identity.h"
#include "tests/ftest_ai_multi_seat.h"
#include "tests/ftest_ai_seat_research.h"
#include "tests/ftest_spectator_handoff.h"
#include "tests/ftest_sim_state_continuity.h"
#include "tests/ftest_script_legacy_golden.h"
#include "tests/ftest_lua_api_golden.h"
#include "tests/ftest_console_cmd_golden.h"
#include "tests/ftest_cheats_golden.h"
#include "tests/ftest_script_creature_config_golden.h"
#include "tests/ftest_campaign_spectate_checkbox.h"
#include "tests/ftest_campaign_external_seat.h"
#include "tests/ftest_campaign_external_seat_checkbox.h"
#include "tests/ftest_ai_seat_speed.h"
#include "tests/ftest_ai_seat_dry_run.h"
#include "tests/ftest_ai_seat_alliance.h"
#include "tests/ftest_ai_seat_intel.h"
#include "tests/ftest_ai_seat_decision.h"
#include "tests/ftest_ai_seat_order_creature.h"
#include "tests/ftest_ai_seat_order_autorelease.h"
#include "tests/ftest_ai_seat_dig_reach.h"
#include "tests/ftest_ai_seat_wall_cue.h"
#include "tests/ftest_ai_seat_pick_up_and_drop.h"
#include "tests/ftest_ai_seat_scenario_text.h"
#include "tests/ftest_ai_seat_force.h"
#include "tests/ftest_ai_seat_room_quality.h"
#include "tests/ftest_ai_seat_custody.h"
#include "tests/ftest_ai_seat_custody_torture.h"
#include "tests/ftest_ai_seat_temple_graveyard.h"
#include "tests/ftest_ai_seat_build_checks.h"
#include "tests/ftest_ai_seat_parity_actions.h"
#include "tests/ftest_ai_seat_specials_gold.h"
#include "tests/ftest_ai_seat_agent_memory.h"
#include "tests/ftest_ai_seat_players.h"
#include "tests/ftest_ai_gesture_order_creature.h"
#include "tests/ftest_ai_seat_diff.h"
#include "tests/ftest_ai_seat_powers.h"
#include "tests/ftest_ai_seat_queue.h"
#include "tests/ftest_ai_seat_release.h"
#include "tests/ftest_skirmish_external_slot.h"
#include "tests/ftest_ai_seat_decided.h"
#include "tests/ftest_ai_seat_verbs.h"
#include "tests/ftest_ai_seat_drag_verbs.h"
#include "tests/ftest_ai_seat_view_fog.h"
#include "tests/ftest_ai_bridge_smoke.h"
#include "tests/ftest_ai_bridge_reference.h"
#include "tests/ftest_ai_bridge_vs_agent.h"
#include "tests/ftest_creature_temple_prayer.h"
#include "tests/ftest_creature_lair_healing.h"
#include "tests/ftest_creature_garden_eating.h"
#include "tests/ftest_room_storage_reposition.h"
#include "tests/ftest_creature_training.h"
#include "tests/ftest_creature_guard_post.h"
#include "tests/ftest_creature_barracks.h"
#include "tests/ftest_creature_prison_capture.h"
#include "tests/ftest_creature_torture_ownership.h"
#include "tests/ftest_net_resync_fake_multiplayer.h"
#include "tests/ftest_net_enet_loopback_host.h"
#include "tests/ftest_net_enet_loopback_join.h"
#include "tests/ftest_gui_packet_parity.h"
#include "tests/ftest_gui_seam_ingame.h"
#include "tests/ftest_editor_place_creature.h"
#include "tests/ftest_editor_paint_terrain.h"
#include "tests/ftest_editor_undo.h"
#include "tests/ftest_editor_save_reload.h"
#include "tests/ftest_editor_points.h"
#include "tests/ftest_editor_script_commands.h"
#include "tests/ftest_editor_palette.h"
#include "tests/ftest_editor_thumbs.h"
#include "tests/ftest_editor_strokes.h"
#include "tests/ftest_editor_session.h"
#include "tests/ftest_editor_brush.h"
#include "tests/ftest_skirmish_setup.h"
#include "tests/ftest_harness_setup_failure.h"
#include "tests/ftest_config_content.h"
// append your test include here, eg: #include "tests/ftest_your_test_header.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief Append the name/init function of your test here so it can be found/executed.
 */
struct ftest_onlyappendtests__config ftest_onlyappendtests__conf = {

    // place regular tests in this list
    .tests_list = {
         { .test_name="example_template_test",              .init_func=ftest_template_init,                         .level_file="keeporig", .level=8,  .frame_skip=8 },
         { .test_name="bug_imp_tp_attack_door__claim",      .init_func=ftest_bug_imp_tp_attack_door__claim_init,    .level_file="deepdngn", .level=80, .frame_skip=8 },
         { .test_name="bug_imp_tp_attack_door__prisoner",   .init_func=ftest_bug_imp_tp_attack_door__prisoner_init, .level_file="deepdngn", .level=80, .frame_skip=8 },
         { .test_name="bug_imp_tp_attack_door__deadbody",   .init_func=ftest_bug_imp_tp_attack_door__deadbody_init, .level_file="deepdngn", .level=80, .frame_skip=8 },
         { .test_name="bug_imp_goldseam_dig",               .init_func=ftest_bug_imp_goldseam_dig_init,             .level_file="keeporig", .level=1,  .frame_skip=8 },
         { .test_name="bug_pathing_stair_treasury",         .init_func=ftest_bug_pathing_stair_treasury_init,       .level_file="keeporig", .level=1,  .frame_skip=8 },
         { .test_name="bug_pathing_pillar_circling",        .init_func=ftest_bug_pathing_pillar_circling_init,      .level_file="keeporig", .level=1,  .frame_skip=8 },
         { .test_name="creature_combat_power_hand",         .init_func=ftest_creature_combat_power_hand_init,       .level_file="keeporig", .level=11, .frame_skip=8 },
         { .test_name="local_view_transitions",             .init_func=ftest_local_view_transitions_init,           .level_file="keeporig", .level=1,  .frame_skip=8 },
         { .test_name="ai_gesture_single_turn",            .init_func=ftest_ai_gesture_single_turn_init,           .level_file="keeporig", .level=11, .frame_skip=8 },
         { .test_name="ai_gesture_order_creature",         .init_func=ftest_ai_gesture_order_creature_init,        .pre_start_func=ftest_ai_gesture_order_creature_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_gesture_drag_verbs",            .init_func=ftest_ai_gesture_drag_verbs_init,            .pre_start_func=ftest_ai_gesture_drag_verbs_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_seat_identity",                  .init_func=ftest_ai_seat_identity_init,                 .pre_start_func=ftest_ai_seat_identity_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_seat_verbs",                     .init_func=ftest_ai_seat_verbs_init,                    .pre_start_func=ftest_ai_seat_verbs_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_multi_seat",                     .init_func=ftest_ai_multi_seat_init,                    .pre_start_func=ftest_ai_multi_seat_pre_start, .level_file="original", .level=60, .frame_skip=8 },
         { .test_name="ai_seat_decided",                   .init_func=ftest_ai_seat_decided_init,                  .pre_start_func=ftest_ai_seat_decided_pre_start, .level_file="original", .level=60, .frame_skip=8 },
         { .test_name="skirmish_external_slot",            .init_func=ftest_skirmish_external_slot_init,           .pre_start_func=ftest_skirmish_external_slot_pre_start, .level_file="original", .level=60, .frame_skip=8 },
         { .test_name="ai_seat_release",                   .init_func=ftest_ai_seat_release_init,                  .pre_start_func=ftest_ai_seat_release_pre_start, .level_file="original", .level=60, .frame_skip=8 },
         { .test_name="ai_seat_queue",                     .init_func=ftest_ai_seat_queue_init,                    .pre_start_func=ftest_ai_seat_queue_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_seat_powers",                    .init_func=ftest_ai_seat_powers_init,                   .pre_start_func=ftest_ai_seat_powers_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_seat_diff",                      .init_func=ftest_ai_seat_diff_init,                     .pre_start_func=ftest_ai_seat_diff_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_seat_order_creature",            .init_func=ftest_ai_seat_order_creature_init,           .pre_start_func=ftest_ai_seat_order_creature_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_seat_order_autorelease",         .init_func=ftest_ai_seat_order_autorelease_init,        .pre_start_func=ftest_ai_seat_order_autorelease_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_seat_dig_reach",                 .init_func=ftest_ai_seat_dig_reach_init,                .pre_start_func=ftest_ai_seat_dig_reach_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_seat_wall_cue",                  .init_func=ftest_ai_seat_wall_cue_init,                 .pre_start_func=ftest_ai_seat_wall_cue_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_seat_pick_up_and_drop",          .init_func=ftest_ai_seat_pick_up_and_drop_init,         .pre_start_func=ftest_ai_seat_pick_up_and_drop_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_seat_scenario_text",             .init_func=ftest_ai_seat_scenario_text_init,             .pre_start_func=ftest_ai_seat_scenario_text_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_seat_force",                     .init_func=ftest_ai_seat_force_init,                     .pre_start_func=ftest_ai_seat_force_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_seat_room_quality",              .init_func=ftest_ai_seat_room_quality_init,              .pre_start_func=ftest_ai_seat_room_quality_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_seat_custody",                   .init_func=ftest_ai_seat_custody_init,                   .pre_start_func=ftest_ai_seat_custody_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_seat_custody_torture",           .init_func=ftest_ai_seat_custody_torture_init,           .pre_start_func=ftest_ai_seat_custody_torture_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_seat_temple_graveyard",          .init_func=ftest_ai_seat_temple_graveyard_init,          .pre_start_func=ftest_ai_seat_temple_graveyard_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_seat_build_checks",              .init_func=ftest_ai_seat_build_checks_init,              .pre_start_func=ftest_ai_seat_build_checks_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_seat_parity_actions",            .init_func=ftest_ai_seat_parity_actions_init,            .pre_start_func=ftest_ai_seat_parity_actions_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_seat_specials_gold",             .init_func=ftest_ai_seat_specials_gold_init,             .pre_start_func=ftest_ai_seat_specials_gold_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_seat_agent_memory",              .init_func=ftest_ai_seat_agent_memory_init,              .pre_start_func=ftest_ai_seat_agent_memory_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_seat_players",                   .init_func=ftest_ai_seat_players_init,                   .pre_start_func=ftest_ai_seat_players_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_seat_decision",                  .init_func=ftest_ai_seat_decision_init,                 .pre_start_func=ftest_ai_seat_decision_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_seat_intel",                     .init_func=ftest_ai_seat_intel_init,                    .pre_start_func=ftest_ai_seat_intel_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_seat_alliance",                  .init_func=ftest_ai_seat_alliance_init,                 .pre_start_func=ftest_ai_seat_alliance_pre_start, .level_file="original", .level=60, .frame_skip=8 },
         { .test_name="ai_seat_dry_run",                   .init_func=ftest_ai_seat_dry_run_init,                  .pre_start_func=ftest_ai_seat_dry_run_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_seat_speed",                     .init_func=ftest_ai_seat_speed_init,                    .pre_start_func=ftest_ai_seat_speed_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_seat_research",                  .init_func=ftest_ai_seat_research_init,                 .pre_start_func=ftest_ai_seat_research_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="spectator_handoff",                 .init_func=ftest_spectator_handoff_init,                .pre_start_func=ftest_spectator_handoff_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="sim_state_continuity",              .init_func=ftest_sim_state_continuity_init,             .pre_start_func=ftest_sim_state_continuity_pre_start, .level_file="original", .level=60, .frame_skip=8 },
         { .test_name="sim_state_continuity_restart",      .init_func=ftest_sim_state_continuity_restart_init,     .pre_start_func=ftest_sim_state_continuity_pre_start, .level_file="original", .level=60, .frame_skip=8, .repeat_n_times=2 },
         { .test_name="script_legacy_golden",              .init_func=ftest_script_legacy_golden_init,             .level_file="keeporig", .level=11, .frame_skip=8 },
         { .test_name="lua_api_golden",                    .init_func=ftest_lua_api_golden_init,                   .level_file="keeporig", .level=11, .frame_skip=8 },
         { .test_name="console_cmd_golden",                .init_func=ftest_console_cmd_golden_init,               .level_file="keeporig", .level=11, .frame_skip=8 },
         { .test_name="cheats_golden",                     .init_func=ftest_cheats_golden_init,                    .level_file="keeporig", .level=11, .frame_skip=8 },
         { .test_name="script_creature_config_golden",     .init_func=ftest_script_creature_config_golden_init,    .level_file="keeporig", .level=11, .frame_skip=8 },
         { .test_name="campaign_spectate_checkbox",         .init_func=ftest_campaign_spectate_checkbox_init,       .pre_start_func=ftest_campaign_spectate_checkbox_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="campaign_external_seat",             .init_func=ftest_campaign_external_seat_init,           .pre_start_func=ftest_campaign_external_seat_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="campaign_external_seat_checkbox",    .init_func=ftest_campaign_external_seat_checkbox_init,  .pre_start_func=ftest_campaign_external_seat_checkbox_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_seat_drag_verbs",                .init_func=ftest_ai_seat_drag_verbs_init,               .pre_start_func=ftest_ai_seat_drag_verbs_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="ai_seat_view_fog",                  .init_func=ftest_ai_seat_view_fog_init,                 .pre_start_func=ftest_ai_seat_view_fog_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="creature_temple_prayer",             .init_func=ftest_creature_temple_prayer_init,           .level_file="keeporig", .level=11, .frame_skip=8 },
         { .test_name="creature_lair_healing",              .init_func=ftest_creature_lair_healing_init,            .level_file="keeporig", .level=11, .frame_skip=8 },
         { .test_name="creature_garden_eating",             .init_func=ftest_creature_garden_eating_init,           .level_file="keeporig", .level=11, .frame_skip=8 },
         { .test_name="room_storage_reposition",            .init_func=ftest_room_storage_reposition_init,          .level_file="keeporig", .level=11, .frame_skip=8 },
         { .test_name="creature_training",                  .init_func=ftest_creature_training_init,                .level_file="keeporig", .level=11, .frame_skip=8 },
         { .test_name="creature_guard_post",                .init_func=ftest_creature_guard_post_init,              .level_file="keeporig", .level=11, .frame_skip=8 },
         { .test_name="creature_barracks",                  .init_func=ftest_creature_barracks_init,                .level_file="keeporig", .level=11, .frame_skip=8 },
         { .test_name="creature_prison_capture",             .init_func=ftest_creature_prison_capture_init,          .level_file="keeporig", .level=11, .frame_skip=8 },
         { .test_name="creature_torture_ownership",          .init_func=ftest_creature_torture_ownership_init,       .level_file="keeporig", .level=11, .frame_skip=8 },
         { .test_name="net_resync_fake_multiplayer",         .init_func=ftest_net_resync_fake_multiplayer_init,      .level_file="keeporig", .level=11, .frame_skip=8 },
         { .test_name="gui_packet_parity",                   .init_func=ftest_gui_packet_parity_init,                .level_file="keeporig", .level=11, .frame_skip=8 },
         { .test_name="gui_seam_ingame",                     .init_func=ftest_gui_seam_ingame_init,                  .level_file="keeporig", .level=11, .frame_skip=8 },
         { .test_name="editor_place_creature",                .init_func=ftest_editor_place_creature_init,            .level_file="keeporig", .level=1,  .frame_skip=8 },
         { .test_name="editor_paint_terrain",                 .init_func=ftest_editor_paint_terrain_init,             .level_file="keeporig", .level=1,  .frame_skip=8 },
         { .test_name="editor_undo",                          .init_func=ftest_editor_undo_init,                      .level_file="keeporig", .level=1,  .frame_skip=8 },
         { .test_name="editor_save_reload",                   .init_func=ftest_editor_save_reload_init,               .level_file="keeporig", .level=1,  .frame_skip=8 },
         { .test_name="editor_points",                        .init_func=ftest_editor_points_init,                    .level_file="keeporig", .level=1,  .frame_skip=8 },
         { .test_name="editor_script_commands",               .init_func=ftest_editor_script_commands_init,           .level_file="keeporig", .level=1,  .frame_skip=8 },
         { .test_name="editor_palette",                       .init_func=ftest_editor_palette_init,                   .level_file="keeporig", .level=1,  .frame_skip=8 },
         { .test_name="editor_strokes",                       .init_func=ftest_editor_strokes_init,                   .level_file="keeporig", .level=1,  .frame_skip=8 },
         { .test_name="editor_session",                       .init_func=ftest_editor_session_init,                   .level_file="keeporig", .level=1,  .frame_skip=8 },
         { .test_name="editor_brush",                         .init_func=ftest_editor_brush_init,                     .level_file="keeporig", .level=1,  .frame_skip=8 },
         { .test_name="skirmish_setup_override",              .init_func=ftest_skirmish_setup_init,                   .pre_start_func=ftest_skirmish_setup_pre_start, .level_file="original", .level=50, .frame_skip=8 },
         { .test_name="skirmish_setup_locks",                 .init_func=ftest_skirmish_setup_locks_init,             .pre_start_func=ftest_skirmish_setup_locks_pre_start,   .level_file="dk2maps",  .level=220, .frame_skip=8 },
         { .test_name="config_content_anchor",                 .init_func=ftest_config_content_anchor_init,            .level_file="keeporig", .level=1,  .frame_skip=8 },
         { .test_name="config_content_readback",              .init_func=ftest_config_content_readback_init,          .pre_start_func=ftest_config_content_readback_pre_start, .level_file="keeporig", .level=1, .frame_skip=8 },
         { .test_name="config_content_reset",                 .init_func=ftest_config_content_reset_init,             .pre_start_func=ftest_config_content_reset_pre_start,    .level_file="keeporig", .level=1, .frame_skip=8 },
         { .test_name="config_content_tool_smoke",             .init_func=ftest_config_content_tool_smoke_init,        .level_file="keeporig", .level=1,  .frame_skip=8 },
         { .test_name="config_content_rules_editor",          .init_func=ftest_config_content_rules_editor_init,      .pre_start_func=ftest_config_content_rules_editor_pre_start, .level_file="keeporig", .level=1, .frame_skip=8 },
         { .test_name="config_content_trapdoor_editor",       .init_func=ftest_config_content_trapdoor_editor_init,   .pre_start_func=ftest_config_content_trapdoor_editor_pre_start, .level_file="keeporig", .level=1, .frame_skip=8 },
         { .test_name="config_content_spell_editor",          .init_func=ftest_config_content_spell_editor_init,      .pre_start_func=ftest_config_content_spell_editor_pre_start, .level_file="keeporig", .level=1, .frame_skip=8 },
         { .test_name="config_content_creature_editor",       .init_func=ftest_config_content_creature_editor_init,   .pre_start_func=ftest_config_content_creature_editor_pre_start, .level_file="keeporig", .level=1, .frame_skip=8 },
         { .test_name="config_content_landview_png",         .init_func=ftest_config_content_landview_png_init,      .pre_start_func=ftest_config_content_landview_png_pre_start, .level_file="keeporig", .level=1, .frame_skip=8 },
         { .test_name="config_content_campaign_editor",       .init_func=ftest_config_content_campaign_editor_init,   .pre_start_func=ftest_config_content_campaign_editor_pre_start, .level_file="keeporig", .level=1, .frame_skip=8 },
         { .test_name="config_content_text_editor",           .init_func=ftest_config_content_text_editor_init,       .pre_start_func=ftest_config_content_text_editor_pre_start, .level_file="keeporig", .level=1, .frame_skip=8 },
         { .test_name="config_content_room_editor",           .init_func=ftest_config_content_room_editor_init,       .pre_start_func=ftest_config_content_room_editor_pre_start, .level_file="keeporig", .level=1, .frame_skip=8 },
         { .test_name="config_content_scratch_level",         .init_func=ftest_config_content_scratch_level_init,     .pre_start_func=ftest_config_content_scratch_level_pre_start, .level_file="keeporig", .level=900002, .frame_skip=8 },
         { .test_name="editor_thumbs",                        .init_func=ftest_editor_thumbs_init,                    .level_file="keeporig", .level=1,  .frame_skip=8 },

         // editor_fill (PckA_EditorFloodFill) has no ftest coverage: unlike
         // PckA_EditorPlaceTerrainRect (one explicit corner + one ambient
         // corner) or the Redo* verbs (fully explicit), FloodFill's seed
         // position is carried *only* in the packet's ambient pos_x/pos_y,
         // with no actn_par-based alternative at all. Found live while
         // building ftest_editor_paint_terrain.c: an ftest action can never
         // observe that ambient value in the first place (ftest_update()
         // runs before input() each tick, and clear_packets() wipes it
         // right after process_packets() consumes it, so a test's own read
         // always sees the just-wiped 0 -- only process_packets() itself,
         // running between those two points, ever sees the real value).
         // Worse, probing it (not committed) showed it isn't even steerable
         // via camera position under the SDL dummy video driver -- it
         // converges to a fixed subtile-120 X regardless of camera target,
         // with Y pinned right at the map's own border. With no way to read,
         // predict, or control where the flood would start, there is no
         // sound way to pre-carve a bounded test patch for it. Same category
         // of exclusion as Brush/Stamp (docs/refactor/editor/09-toolbox-
         // remainder.md): a real gap in ftest coverage, not a silently
         // skipped one.

         // GUI/cursor-dependent, not headless-safe: drives mouse-cursor/thing-under-hand
         // selection (ftest_util_center_cursor_over_dungeon_view(), player->thing_under_hand)
         // which never reliably resolves without a real display, so it stalls (rather than
         // fails) under -headless -- confirmed against a real KeeperFX install, 6/7 other
         // registered tests pass in well under a minute combined while this one alone ran
         // past 3 minutes without completing a single one of its 10 repeat iterations. Since
         // -ftests (no name) runs every tests_list entry in one process, a stall here means
         // *no* test's coverage data survives (gcov only flushes .gcda on clean exit) --
         // commented out rather than left to intermittently wedge the KFX_FUNCTESTING+
         // KFX_TEST_COVERAGE `coverage` target (see CMakeLists.txt).
         // { .test_name="bug_invisible_units_cant_select", .init_func=ftest_bug_invisible_units_cant_select_init,  .level_file="keeporig", .level=1,  .frame_skip=0 },

         // WIP TEST { .test_name="bug_invisible_units_cant_select",    .init_func=ftest_bug_invisible_units_cant_select_init,  .level_file="lostlvls", .level=103, .frame_skip=0 },
         // append your test to tests_list here, eg: { .test_name="your_test_name",    .init_func=ftest_your_test_name_init, .level_file="lostlvls", .level=103 },
    },

    // place long-running tests in this list, to include them use the -includelongtests flag
    .long_running_tests_list = {
        // Half of a two-process test: needs scripts/ai_bridge_smoke.py talking to the in-game API; run it through
        // scripts/run_ftest_ai_bridge_smoke.sh, not by hand (docs/refactor/AI/LLM/05-testing-and-rollout.md).
        // Half of a two-process test too: scripts/run_ftest_ai_bridge_reference.sh runs it with scripts/ai_bridge_reference_e2e.py.
        { .test_name="ai_bridge_reference",                .init_func=ftest_ai_bridge_reference_init,              .pre_start_func=ftest_ai_bridge_reference_pre_start, .level_file="original", .level=50, .frame_skip=8 },
        // Half of a two-process test too: scripts/run_ftest_ai_bridge_vs_agent.sh runs it with scripts/ai_bridge_vs_agent_e2e.py.
        { .test_name="ai_bridge_vs_agent",                 .init_func=ftest_ai_bridge_vs_agent_init,               .pre_start_func=ftest_ai_bridge_vs_agent_pre_start, .level_file="original", .level=60, .frame_skip=8 },
        { .test_name="ai_bridge_smoke",                    .init_func=ftest_ai_bridge_smoke_init,                  .pre_start_func=ftest_ai_bridge_smoke_pre_start, .level_file="original", .level=50, .frame_skip=8 },
        { .test_name="bug_ai_bridge",                      .init_func=ftest_bug_ai_bridge_init,                    .level_file="keeporig", .level=15, .frame_skip=128, .seed=1, .repeat_n_times=100 },

        // Not actually long-running -- placed here (rather than tests_list)
        // for the same reason bug_invisible_units_cant_select is commented
        // out above: a bare `-ftests` sweep runs every tests_list entry in
        // one process, and these two are each only one half of a real
        // two-process ENet session (docs/refactor/todo/ftest-fake-multiplayer.md
        // Phase 2) -- run alone, net_enet_loopback_host stalls waiting for a
        // client that never connects, which would wedge the same
        // KFX_FUNCTESTING+KFX_TEST_COVERAGE `coverage` target this comment's
        // neighbor above was excluded to protect. Run together via
        // scripts/run_ftest_net_enet_loopback.sh, not via -includelongtests.
        { .test_name="net_enet_loopback_host",              .init_func=ftest_net_enet_loopback_host_init,           .level_file="keeporig", .level=11, .frame_skip=8 },
        { .test_name="net_enet_loopback_join",              .init_func=ftest_net_enet_loopback_join_init,           .level_file="keeporig", .level=11, .frame_skip=8 },

        // Not long-running either, and fails on purpose: its pre_start_func calls FTEST_FAIL_TEST to
        // prove a failed setup aborts the run instead of hanging. In tests_list it stopped every
        // wildcard `-ftests -exitonfailedtest` sweep (including CMakeLists.txt's `coverage` target)
        // with exit 255, so nothing registered after it ever ran. Its pass/fail is judged from
        // outside, by scripts/run_ftest_harness_setup_failure.sh (expects exit 255 plus specific
        // log lines), which runs it by name with -includelongtests.
        { .test_name="harness_setup_failure",               .init_func=ftest_harness_setup_failure_init,            .pre_start_func=ftest_harness_setup_failure_pre_start, .level_file="keeporig", .level=1, .frame_skip=8 },
    }
};


#ifdef __cplusplus
}
#endif

#endif

