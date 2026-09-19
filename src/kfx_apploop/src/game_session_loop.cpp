/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_session_loop.cpp
 *     Top-level game/frontend session loop.
 * @par Purpose:
 *     game_loop() (the outermost "show frontend menu, then run a level,
 *     repeat" loop), wait_at_frontend() (the frontend-menu sub-loop),
 *     keeper_gameplay_loop() and its gameplay_loop_*() per-frame helpers,
 *     the per-turn update() dispatcher, and the frame-pacing/timing
 *     helpers they all share. Physically extracted out of src/main.cpp
 *     in stage 12.5 (docs/refactor/stage-12-slim-app-target.md) -- see
 *     game_session_loop.h and src/kfx_apploop/CMakeLists.txt for why
 *     this got its own top-ranked library instead of moving into
 *     kfx_game via callback injection like every other "layer X needs
 *     layer Y" case in this refactor.
 * @par Comment:
 *     None.
 */
/******************************************************************************/
#include "pre_inc.h"

#include "game_session_loop.h"

#include "bflib_coroutine.h"
#include "bflib_math.h"
#include "bflib_keybrd.h"
#include "bflib_inputctrl.h"
#include "bflib_datetm.h"
#include "bflib_sprfnt.h"
#include "bflib_fileio.h"
#include "bflib_dernc.h"
#include "bflib_sndlib.h"
#include "bflib_video.h"
#include "bflib_vidraw.h"
#include "bflib_mouse.h"
#include "bflib_planar.h"


#include "ariadne_update.h"
#include "api.h"
#include "front_simple.h"
#include "frontend.h"
#include "front_network.h"
#include "front_input.h"
#include "front_landview.h"
#include "front_torture.h"
#include "net_callbacks.h"
#include "net_main.h"
#include "net_exchange_gameplay.h"
#include "net_lobby.h"
#include "net_resync.h"
#include "net_input_lag.h"
#include "lua_base.h"
#include "lua_triggers.h"
#include "frontmenu_net.h"
#include "gui_frontmenu.h"
#include "config.h"
#include "config_campaigns.h"
#include "player_instances.h"
#include "player_utils.h"
#include "game_saves.h"
#include "engine_render.h"
#include "engine_redraw.h"
#include "engine_camera.h"
#include "vidmode.h"
#include "kjm_input.h"
#include "packets.h"
#include "room_data.h"
#include "room_util.h"
#include "room_entrance.h"
#include "room_library.h"
#include "room_workshop.h"
#include "map_blocks.h"
#include "light_data.h"
#include "thing_effects.h"
#include "local_camera.h"
#include "vidfade.h"
#include "sounds.h"
#include "game_merge.h"
#include "game_legacy.h"
#include "game_loop.h"
#include "timer.h"
#include "main_game.h"
#include "editor_callbacks.h" // docs/refactor/editor/01-entry-and-editor-session.md -- case FeSt_START_EDITOR
#include "game_lifecycle.h"
#include "moonphase.h"
#include "kfx_frontend_state.h"
#include "config_keeperfx.h"
#ifdef FUNCTESTING
#include "ftests/ftest_packet_capture.h"
#endif
#include "frontmenu_ingame_evnt.h"
#include "scrcapt.h"
#include "gui_topmsg.h"
#include "gui_msgs.h"
#include "front_easter.h"
#include "lens_api.h"
#include "bflib_crash.h"
#include "lvl_filesdk1.h"
#include "config_sounds.h"
#include "renderer/RendererManager.h"

#include <cstdint>

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
short do_draw;
static long double process_frame_time = 0;
static long double time_since_last_draw = 0;
static long double average_frame_draw_time = 1;
static long double multiplayer_clock_adjust = 1;
long double host_packet_received = 1;
float interpolate_time = 0;
/******************************************************************************/

void update(void)
{
    struct PlayerInfo *player;
    SYNCDBG(4,"Starting for turn %ld",(long)get_gameturn());

    process_packets();
    update_local_cameras();
    api_update_server();

    if (quit_game || exit_keeper) {
        return;
    }
    if (kfx_sim_state.game_kind == GKind_NonInteractiveState)
    {
        ariadne_clear_map_dirty_for_navigation();
        return;
    }
    player = get_my_player();
    struct UserState *ustate = get_user_state(get_local_user());

    // docs/refactor/editor/01-entry-and-editor-session.md §3 -- simulation_suspended
    // checked here directly, not just via editor_frame()'s GOF_Paused
    // re-assertion: process_packets() above can clear GOF_Paused within
    // this same call (a queued PckA_TogglePause) before this gate runs,
    // letting one full turn (creatures take a step) through before
    // editor_frame() reasserts the flag on the next frame's render. Found
    // live: "each unpause allows creatures to move". simulation_suspended
    // is untouched by PckA_TogglePause, so it can't race the same way.
    if (!flag_is_set(kfx_sim_state.operation_flags,GOF_Paused) && !kfx_sim_state.simulation_suspended)
    {
        for (int i = 1; i < EVENTS_COUNT; i++) {
            kfx_sim_state.event[i].flags &= ~EvF_BtnFalling;
        }
        if (flag_is_set(ustate->additional_flags,UsrAF_LightningPaletteIsActive))
        {
            PaletteSetUserPalette(player->user_id, engine_palette);
            clear_flag(ustate->additional_flags, UsrAF_LightningPaletteIsActive);
        }
        clear_active_dungeons_stats();
        update_creature_pool_state();
        if ((get_gameturn() & 0x01) != 0)
            update_animating_texture_maps();
        update_things();
        process_rooms();
        process_dungeons();
        update_research();
        update_manufacturing();
        event_process_events();
        update_all_events();
        process_level_script();
        process_fx_lines();
        lua_on_game_tick();
        if ((kfx_sim_state.view_mode_flags & GNFldD_ComputerPlayerProcessing) != 0)
            process_computer_players2();
        process_players();
        process_action_points();
        player = get_my_player();
        if (player->view_mode == PVM_CreatureView)
        {
            struct Thing *thing = thing_get(player->controlled_thing_idx);
            update_first_person_object_ambience(thing);
        }
        update_footsteps_nearest_camera(get_player_active_camera(player));
        PaletteFadePlayer(player);
        process_armageddon();
        update_global_lighting();
#if (BFDEBUG_LEVEL > 9)
        lights_stats_debug_dump();
        things_stats_debug_dump();
        creature_stats_debug_dump();
#endif
        kfx_game_state.play_gameturn++;
        if (kfx_net_state.turns_packetoff == kfx_game_state.play_gameturn)
            exit_keeper = 1;
    }

    message_update();
    update_all_players_cameras();
    update_player_sounds();
    SYNCDBG(6,"Finished");
}

void find_frame_rate(void)
{
    static TbClockMSec prev_time2=0;
    static TbClockMSec cntr_time2=0;
    unsigned long curr_time;
    curr_time = LbTimerClock();
    cntr_time2++;
    if (curr_time-prev_time2 >= 1000)
    {
        double time_fdelta = 1000.0*((double)(cntr_time2))/(curr_time-prev_time2);
        prev_time2 = curr_time;
        kfx_frontend_state.time_delta = (unsigned long)(time_fdelta*256.0);
        cntr_time2 = 0;
    }
}

void packet_load_find_frame_rate(unsigned long incr)
{
    static TbClockMSec start_time=0;
    static TbClockMSec extra_frames=0;
    TbClockMSec curr_time;
    curr_time = LbTimerClock();
    if ((curr_time-start_time) < 5000)
    {
        extra_frames += incr;
    } else
    {
        double time_fdelta = 1000.0*((double)(extra_frames+incr))/(curr_time-start_time);
        start_time = curr_time;
        kfx_frontend_state.time_delta = (unsigned long)(time_fdelta*256.0);
        extra_frames = 0;
    }
}

/**
 * Checks if the game screen needs redrawing.
 */
short display_should_be_updated_this_turn(void)
{
    if ((kfx_sim_state.operation_flags & GOF_Paused) != 0)
      return true;
    if ( (kfx_net_state.turns_fastforward == 0) && (!kfx_net_state.packet_loading_in_progress) )
    {
      find_frame_rate();
      if ( (kfx_net_state.frame_skip == 0) || ((get_gameturn() % kfx_net_state.frame_skip) == 0) )
        return true;
    } else
    if ( ((get_gameturn() & 0x3F)==0) ||
         ((kfx_net_state.packet_loading_in_progress) && ((get_gameturn() & 7)==0)) )
    {
      packet_load_find_frame_rate(64);
      return true;
    }
    return false;
}

/**
 * Makes last updates to the video buffer, and swaps buffers to show
 * the new image.
 */
TbBool keeper_screen_swap(void)
{
/*  // For resolution 640x480, move the graphics data 40 lines lower
  if ( lbDisplay.ScreenMode == Lb_SCREEN_MODE_640_480_8 )
    if (RendererLockFramebuffer() == Lb_SUCCESS)
    {
      int i;
      int scrmove_x=0;
      int scrmove_y=40;
      int scanline_len=640;
      for (i=400;i>=0;i--)
        memcpy(lbDisplay.WScreen+scanline_len*(i+scrmove_y)+scrmove_x, lbDisplay.WScreen+scanline_len*i, scanline_len-scrmove_x);
      memset(lbDisplay.WScreen, 0, scanline_len*scrmove_y);
      RendererUnlockFramebuffer();
    }*/
  RendererPresentFrame();
  return true;
}

/**
 * Waits until the next game turn. Delay is usually controlled by
 * num_fps variable.
 */
TbBool keeper_wait_for_next_turn(void)
{
    const long double tick_ns_one_sec = 1000000000.0;
    long double tick_ns_one_frame = -1;
    if ((kfx_sim_state.view_mode_flags & GNFldD_WaitSleepMode) != 0)
    {
        // No idea when such situation occurs
        tick_ns_one_frame = tick_ns_one_sec;
    }
    if (kfx_net_state.frame_skip >= 0)
    {
        // Standard delaying system
        int32_t num_fps = kfx_sim_state.turns_per_second;
        if (kfx_net_state.frame_skip > 0)
            num_fps *= kfx_net_state.frame_skip;

        tick_ns_one_frame = tick_ns_one_sec/num_fps;
    }

    if (tick_ns_one_frame >= 0) {
        static long double tick_ns_last_turn = 0;

        long double tick_ns_cur = get_time_tick_ns();
        long double tick_ns_used = tick_ns_cur - tick_ns_last_turn;
        long double tick_ns_delay = tick_ns_one_frame - tick_ns_used;
        if (multiplayer_speed_adjustment_ns != 0) {
            tick_ns_delay += multiplayer_speed_adjustment_ns;
        }

        long double tick_ns_end = tick_ns_cur;
        // tick_ns_used: every level, initialized_time_point will be reset, so tick_ns_used may be less than 0 when enter level for the non-first time, Skip it directly to solve the problem.
        if (tick_ns_delay > 0 && tick_ns_used >= 0) {
            tick_ns_end = tick_ns_cur + tick_ns_delay;
            LbSleepUntilExt(tick_ns_end);
        }
        tick_ns_last_turn = tick_ns_end;
        return true;
    }

    return false;
}

static bool use_delta_time()
{
    // Always enable interpolation in multiplayer games.
    return is_feature_on(Ft_DeltaTime) || network_is_active();
}

static void update_frontend_delta_time()
{
    static int64_t prev = 0;
    const int64_t now = get_time_tick_ns();
    const int64_t ns = now - prev;
    prev = now;
    const long double dt = ns / 1e9L * kfx_sim_state.turns_per_second;
    kfx_render_state.delta_time = min(max(dt, 0.L), 1.L);
}

static void update_gameplay_delta_time()
{
    if (use_delta_time()) {
        static int64_t prev = 0;
        const int64_t now = get_time_tick_ns();
        const int64_t ns = now - prev;
        prev = now;

        const long double seconds = max(ns / 1e9L, 0.L);
        const long double turns = seconds * kfx_sim_state.turns_per_second;
        const long double frames = seconds * fps_limit_current;

        kfx_net_state.process_turn_time += turns * multiplayer_clock_adjust * max(kfx_net_state.frame_skip, 1);

        // This sets kfx_render_state.delta_time, which is used to pace locally-displayed
        // things (eg. tooltip scroll speed).  It should not be affected by
        // multiplayer clock adjustment or frameskip.
        time_since_last_draw += turns;

        // Like process_turn_time, but for the video frame rate.
        process_frame_time += frames;
    } else {
        // Set to 1 so that these variables don't affect anything. (if something is multiplied by 1 it doesn't change)
        time_since_last_draw = 1;
        kfx_render_state.delta_time = 1;
        kfx_net_state.process_turn_time = 1;
        process_frame_time = 1;
    }
}

static bool keeper_wait_for_screen_focus()
{
    do {
        if ( !poll_inputs() )
        {
          force_application_close();
          break;
        }
        if (LbIsActive())
          return true;
        if (network_is_active())
          return true;
        if (!freeze_game_on_focus_lost())
          return true;
        LbSleepFor(50);
        update_gameplay_delta_time();
        kfx_net_state.process_turn_time = 1.0;
        time_since_last_draw = 1.0;
    } while ((!exit_keeper) && (!quit_game));
    return false;
}

static void gameplay_loop_draw()
{
    if (use_delta_time())
        do_draw = true;

    update_gameplay_delta_time();

    if (kfx_net_state.process_turn_time > 1.0 && time_since_last_draw < 1.0)
        do_draw = false;

    // Frame rate limiter
    if (fps_limit_current > 0)
    {
        frametime_start_measurement(Frametime_Sleep);
        if (process_frame_time < 1.0)
        {
            if (kfx_net_state.process_turn_time < 1.0)
                SDL_Delay(1);
            do_draw = false;
        }
        else
        {
            process_frame_time = min(1.L, process_frame_time - 1.L);
        }
        frametime_end_measurement(Frametime_Sleep);
    }

    // Floats are used a lot in the drawing related functions. But keep in mind integers are typically preferred for logic related functions.
    frametime_start_measurement(Frametime_Draw);

    // Update lights
    update_light_render_area();

    if (quit_game || exit_keeper) {
        do_draw = false;
    }
    if ( do_draw ) {
        if (frametime_enabled())
            framerate_measurement_capture(Framerate_Draw);
        kfx_render_state.delta_time = min(time_since_last_draw, 1.L);
        time_since_last_draw = 0;
        interpolate_time = min(max(kfx_net_state.process_turn_time, 0.L), 1.L);
        keeper_screen_redraw();
    }
    keeper_wait_for_screen_focus();
    // Direct information/error messages
    if (RendererLockFramebuffer() == Lb_SUCCESS) {
        if ( do_draw ) {
            perform_any_screen_capturing();
        }
        draw_onscreen_direct_messages();
        RendererUnlockFramebuffer();
    }
    // Move the graphics window to center of screen buffer and swap screen
    if ( do_draw ) {
        keeper_screen_swap();
    }
    frametime_end_measurement(Frametime_Draw);

    if ( do_draw ) {
        update_gameplay_delta_time();
        const long double delta = time_since_last_draw - average_frame_draw_time;
        average_frame_draw_time += delta * max(average_frame_draw_time, .05L) / 20;
    }
}

static void gameplay_loop_logic()
{
    if(flag_is_set(start_params.debug_flags, DFlg_PauseAtGameTurn))
    {
        static GameTurn previous_gameturn = 0;
        if(get_gameturn() >= start_params.pause_at_gameturn && get_gameturn() != previous_gameturn)
        {
            if(!kfx_game_state.paused_at_gameturn)
            {
                kfx_game_state.paused_at_gameturn = true;

                kfx_net_state.frame_skip = 0;
                if(kfx_net_state.packet_load_enable)
                {
                    disable_packet_mode();
                }
                set_packet_pause_toggle();
            }
        }
        previous_gameturn = get_gameturn();
    }

    if (use_delta_time())
    {
        update_gameplay_delta_time();
        if (kfx_net_state.input_lag_turns == 0 && network_is_active())
        {
            // Aim to exchange network packets before the turn ends.  If drawing
            // another frame could miss this deadline, skip it.
            // In a 3-4 player game, clients must be 2 frames early.
            const int frames = 1 + (netstate.my_id != SERVER_ID && kfx_net_state.active_players_count > 2);
            const long double offset = frames * average_frame_draw_time * multiplayer_clock_adjust * max(kfx_net_state.frame_skip, 1);
            if (kfx_net_state.process_turn_time + offset < 1.0)
                return;
        }
        else
        {
            if (kfx_net_state.process_turn_time < 1.0)
                return;
        }
    }

    frametime_start_measurement(Frametime_Logic);
    if (frametime_enabled())
        framerate_measurement_capture(Framerate_Logic);

#ifdef FUNCTESTING
    if(flag_is_set(start_params.functest_flags, FTF_Enabled))
    {
        FTestFrameworkState ftstate = ftest_update(NULL);
        if(ftstate == FTSt_InvalidState || ftstate == FTSt_TestsCompletedSuccessfully)
        {
            quit_game = true;
            exit_keeper = true;
            return;
        }
    }
#endif // FUNCTESTING
    do_draw = display_should_be_updated_this_turn() || (!LbIsActive());
    poll_inputs();
    input_eastegg();
    input();
    exchange_packets();
#ifdef FUNCTESTING
    if (flag_is_set(start_params.functest_flags, FTF_Enabled))
        ftest_packet_capture_tick();
#endif

    update_gameplay_delta_time();
    if (timer_enabled())
    {
        if (kfx_sim_state.TimerGame)
        {
            TbBool won_level = (get_my_player()->victory_state == VicS_WonLevel);
            if (!won_level)
            {
                TimerTurns = get_gameturn();
            }
            if (kfx_sim_state.TimerGameReal)
            {
                if (!won_level)
                {
                    if (TimerTurns != 0)
                    {
                        uint32_t turns = kfx_sim_state.turns_per_second;
                        if (kfx_net_state.frame_skip > 0)
                        {
                            turns *= kfx_net_state.frame_skip;
                        }
                        if (TimerTurns % turns == 0)
                        {
                            update_game_time(&kfx_sim_state.GameT, &kfx_sim_state.GameSeconds);
                        }
                    }
                }
            }
        }
        else
        {
            if (!kfx_sim_state.TimerFreeze)
            {
                update_time();
            }
        }
    }
    if (kfx_net_state.process_turn_time > kfx_sim_state.turns_per_second + 1)
        kfx_net_state.process_turn_time = kfx_sim_state.turns_per_second + 1;

    // Adjust client time scaling
    if (netstate.my_id != SERVER_ID && network_is_active())
    {
        if (kfx_net_state.input_lag_turns == 0)
        {
            // Adjust the clock rate so that the host packet is received at
            // process_turn_time == 1.0 (on average).  If it is received later,
            // reduce the scaling factor (< 1.0) so that the next turn takes a
            // little longer in real time.  Vice-versa if it is early.

            multiplayer_clock_adjust = 1 + (1 - host_packet_received) / 20;
        }
        else
        {
            const long double tick_ns_one_turn = 1e9L / kfx_sim_state.turns_per_second;
            const long double tick_ns_adjusted_turn = tick_ns_one_turn + multiplayer_speed_adjustment_ns;
            assert (tick_ns_adjusted_turn > 0);
            multiplayer_clock_adjust = tick_ns_one_turn / tick_ns_adjusted_turn;
        }
    }
    else multiplayer_clock_adjust = 1.0;
    host_packet_received = 1.0;

    while (kfx_net_state.process_turn_time < 1.0)
    {
        gameplay_loop_draw();
        update_gameplay_delta_time();
    }
    kfx_net_state.process_turn_time -= 1.0;

    update();

    frametime_end_measurement(Frametime_Logic);

    if(kfx_game_state.frame_step)
    {
        kfx_game_state.frame_step = false;
        set_packet_pause_toggle();
    }
}

static void gameplay_loop_network()
{
    if (! network_is_active())
        return;

    network_update(sim_packets, sizeof(struct Packet));
}

static void gameplay_loop_timestep()
{
    if (! use_delta_time()) {
        frametime_start_measurement(Frametime_Sleep);
        // Make delay if the machine is too fast
        if ( (!kfx_net_state.packet_load_enable) || (kfx_net_state.turns_fastforward == 0) ) {
            keeper_wait_for_next_turn();
        }
        frametime_end_measurement(Frametime_Sleep);
    }
}

void network_yield_poll_gameplay(void)
{
    // Deliberately no gameplay_loop_draw(): drawing while the startup sync is still
    // exchanging frames could render half-initialised state (upstream #5282).
    if (!poll_inputs()) {
        exit_keeper = 1;
    }
}

void network_yield_waiting_gameplay_packets(void)
{
    poll_inputs();
    gameplay_loop_draw();
    update_gameplay_delta_time();
    // Reduce game speed during lag spikes.
    if (kfx_net_state.process_turn_time > 2.0)
        kfx_net_state.process_turn_time = 2.0;
}

void network_yield_draw_frontend(void)
{
    update_frontend_delta_time();
    if (frontend_menu_state == FeSt_NETLAND_VIEW) {
        check_mouse_scroll();
        update_velocity();
    }
    if (frontend_menu_state == FeSt_TORTURE) {
        fronttorture_update();
    }
    if (frontend_menu_state == FeSt_NET_START) {
        poll_inputs();
        frontnet_start_input();
    }
    frontend_draw();
    RendererPresentFrame();
}

void keeper_gameplay_loop(void)
{
    struct PlayerInfo *player;
    SYNCDBG(5,"Starting");
    player = get_my_player();
    PaletteSetUserPalette(player->user_id, engine_palette);
    if ((kfx_sim_state.operation_flags & GOF_SingleLevel) != 0) {
        initialise_eye_lenses();
    }
    SYNCDBG(0,"Entering the gameplay loop for level %d",(int)get_loaded_level_number());
    LbErrorParachuteUpdate(); // For some reasone parachute keeps changing; Remove when won't be needed anymore

    initial_time_point();
    kfx_net_state.process_turn_time = 1.0; // Begin initial turn as soon as possible (like original game)
    LbSleepExtInit();

    //the main gameplay loop starts
    while ((!quit_game) && (!exit_keeper))
    {
        frametime_start_measurement(Frametime_FullFrame);
        if (frametime_enabled())
            framerate_measurement_capture(Framerate_FullFrame);
        gameplay_loop_logic();
        gameplay_loop_draw();
        gameplay_loop_network();
        gameplay_loop_timestep();

        bf_datetm_set_frame_timing(kfx_render_state.delta_time, kfx_sim_state.turns_per_second);
        frametime_end_measurement(Frametime_FullFrame);
    } // end while
    SYNCDBG(0,"Gameplay loop finished after %lu turns",(unsigned long)get_gameturn());

    // Reset the game kind because we are not in a game anymore at this point
    kfx_sim_state.game_kind = GKind_Unset;

    api_event("GAME_ENDED");
}

static TbBool wait_at_frontend(void)
{
    struct PlayerInfo *player;
    // This is an improvised coroutine-like stuff
    CoroutineLoop loop;
    memset(&loop, 0, sizeof(loop));

    SYNCDBG(0,"Falling into frontend menu.");
    // Moon phase calculation
    calculate_moon_phase(true,false);
    update_extra_levels_visibility();
    // Returning from Demo Mode
    if (kfx_sim_state.mode_flags & MFlg_IsDemoMode)
    {
      close_packet_file();
      kfx_net_state.packet_load_enable = 0;
    }
    kfx_frontend_state.save_game_slot = -1;
    // Make sure campaigns are loaded
    if (!load_campaigns_list(&campaigns_list ,FGrp_Campgn ,"campaigns","campgn_order.txt"))
    {
      ERRORLOG("No valid campaign files found");
      exit_keeper = 1;
      return true;
    }
    // Make sure mappacks are loaded
    if (!load_campaigns_list(&mappacks_list,FGrp_VarLevels,"mappacks","mappck_order.txt"))
    {
      WARNMSG("No valid mappack files found");
    }
    if (!load_campaigns_list(&mp_mappacks_list,FGrp_MpLevels,"multiplayer mappacks","mp_mappck_order.txt"))
    {
      WARNMSG("No valid multiplayer mappack files found");
    }
    //Set level number and campaign (for single level mode: GOF_SingleLevel)
    if ((start_params.operation_flags & GOF_SingleLevel) != 0)
    {
        TbBool result = false;
        if (start_params.selected_campaign[0] != '\0')
        {
            result = change_campaign(CampgnT_Default, start_params.selected_campaign);
        }
        if (!result) {
            if (!change_campaign(CampgnT_Default,"")) {
                WARNMSG("Unable to load default campaign for the specified level CMD Line parameter");
            }
            else if (start_params.selected_campaign[0] != '\0') { // only show this log message if the user actually specified a campaign
                WARNMSG("Unable to load campaign associated with the specified level CMD Line parameter, default loaded.");
            }
            else {
                JUSTLOG("No campaign specified. Default campaign loaded for selected level (%u).", start_params.selected_level_number);
            }
        }
        set_selected_level_number(start_params.selected_level_number);
        //kfx_sim_state.selected_level_number = start_params.selected_level_number;
    }
    else
    {
        set_selected_level_number(first_singleplayer_level());
    }
    // Init load/save catalogue
    initialise_load_game_slots();

    #ifdef FUNCTESTING
    if(flag_is_set(start_params.functest_flags, FTF_Enabled)) //override for functional tests
    {
        FTestFrameworkState ft_prev_state = FTSt_InvalidState;
        FTestFrameworkState ft_current_state = ftest_update(&ft_prev_state);

        TbBool user_aborted_tests = ft_prev_state == FTSt_TestIsProcessingActions && ft_current_state == FTSt_TestIsProcessingActions;
        if(user_aborted_tests)
        {
            FTEST_FAIL_TEST("User aborted tests");
        }

        if(ft_current_state == FTSt_InvalidState || ft_current_state == FTSt_TestsCompletedSuccessfully || user_aborted_tests)
        {
            quit_game = true;
            exit_keeper = true;
            return true;
        }
        faststartup_network_game(&loop);
        coroutine_process(&loop);
        return true;
    }
    #endif

    // Prepare to enter PacketLoad game
    if (kfx_net_state.packet_load_enable)
    {
      faststartup_saved_packet_game();
      return true;
    }
    // Load single-player level directly from command line arguments (-server and -connect bypass this, autoloading a multiplayer map is handled elsewhere)
    if ((kfx_sim_state.operation_flags & GOF_SingleLevel) != 0 && !(game_flags2 & (GF2_Connect | GF2_Server)))
    {
      faststartup_network_game(&loop);
      coroutine_process(&loop);
      return true;
    }

    if ( !setup_screen_mode_minimal(get_screen_vidmode()) )
    {
      FatalError = 1;
      exit_keeper = 1;
      return true;
    }
    RendererClearScreen(0);
    RendererPresentFrame();
    if (frontend_load_data() != Lb_SUCCESS)
    {
      ERRORLOG("Unable to load frontend data");
      exit_keeper = 1;
      return true;
    }
    memset(scratch, 0, PALETTE_SIZE);
    RendererPaletteSet(scratch);
    frontend_set_state(get_startup_menu_state());

    // Once the Mouse Sprite initialization is complete, the sprite's position needs to be reset because it defaults to (0, 0).
    // Note that we cannot use LbMoveGameCursorToHostCursor for this, because the buffer position may remain unchanged.
    LbMouseSetPositionInitial(lbDisplay.MMouseX, lbDisplay.MMouseY);

    try_restore_frontend_error_box();

    poll_inputs();
    clear_mouse_pressed_lrbutton();

    short finish_menu = 0;
    clear_flag(kfx_sim_state.mode_flags, MFlg_DemoMode);
    // TODO move to separate function
    // Begin the frontend loop
    long fe_last_loop_time = LbTimerClock();
    do
    {
      if (!poll_inputs())
      {
        force_application_close();
        SYNCDBG(0,"Windows Control exit condition invoked");
        break;
      }
      update_mouse();
      update_key_modifiers();
      old_mouse_over_button = frontend_mouse_over_button;
      frontend_mouse_over_button = 0;

      frontend_input();
      if ( exit_keeper )
      {
        SYNCDBG(0,"Frontend Input exit condition invoked");
        break; // end while
      }

      frontend_update(&finish_menu);
      if ( exit_keeper )
      {
        SYNCDBG(0,"Frontend Update exit condition invoked");
        break; // end while
      }

      if ((!finish_menu) && (LbIsActive()))
      {
        frontend_draw();
        RendererPresentFrame();
      }

      if (!SoundDisabled)
      {
        process_3d_sounds();
        MonitorStreamedSoundTrack();
      }

      // fade_palette_in's fade_in() trigger removed per
      // docs/refactor/renderer/05-imgui-owned-menu-backdrop.md Phase 0 --
      // the between-screens fade existed to mask loading time on
      // decades-old hardware and is no longer needed.
      if (is_feature_on(Ft_DeltaTime) == true && should_use_delta_time_on_menu()) {
        update_frontend_delta_time();
      } else {
        int32_t frame_time;
        frame_time = max(1, 1000 / kfx_sim_state.turns_per_second);
        kfx_render_state.delta_time = 1;
        LbSleepUntil(fe_last_loop_time + frame_time);
      }
      fe_last_loop_time = LbTimerClock();

      api_update_server();

    } while (!finish_menu);

    LbPaletteFade(0, 8, Lb_PALETTE_FADE_CLOSED);
    RendererClearScreen(0);
    RendererPresentFrame();
    FrontendMenuState prev_state;
    prev_state = frontend_menu_state;
    frontend_set_state(FeSt_INITIAL);
    if (exit_keeper)
    {
      player = get_my_player();
      player->display_flags &= ~PlaF6_PlyrHasQuit;
      return true;
    }
    reenter_video_mode();

    level_load_time_phase(LevelLoadTime_EngineStartup);
    display_loading_screen();

    short flgmem;
    switch (prev_state)
    {
    case FeSt_START_KPRLEVEL:
          my_player_number = default_loc_player;
          kfx_sim_state.game_kind = GKind_LocalGame;
          clear_flag(kfx_sim_state.system_flags, GSF_NetworkActive);
          player = get_my_player();
          player->is_active = 1;
          startup_network_game(&loop, true);
          break;
    case FeSt_START_MPLEVEL:
          set_flag(kfx_sim_state.system_flags, GSF_NetworkActive);
          skip_high_score_screen = 1;
          kfx_sim_state.game_kind = GKind_MultiGame;
          player = get_my_player();
          player->is_active = 1;
          startup_network_game(&loop, false);
          break;
    case FeSt_LOAD_GAME:
          flgmem = kfx_frontend_state.save_game_slot;
          clear_flag(kfx_sim_state.system_flags, GSF_NetworkActive);
          RendererClearScreen(0);
          RendererPresentFrame();
          level_load_time_phase(LevelLoadTime_Data);
          if (!load_game(kfx_frontend_state.save_game_slot))
          {
              ERRORLOG("Loading game %d failed; quitting.",(int)kfx_frontend_state.save_game_slot);
              quit_game = 1;
          }
          level_load_time_phase(LevelLoadTime_GameSetup);
          kfx_frontend_state.save_game_slot = flgmem;
          break;
    case FeSt_PACKET_DEMO:
          kfx_sim_state.mode_flags |= MFlg_IsDemoMode;
          startup_saved_packet_game();
          set_gui_visible(false);
          clear_flag(kfx_sim_state.operation_flags, GOF_ShowPanel);
          break;
    case FeSt_START_EDITOR:
          // docs/refactor/editor/01-entry-and-editor-session.md §2/§4 --
          // editor_pending_* (frontend.h) were stashed by the FeSt_EDITOR
          // browser (frontgui_editorbrowser_frame). startup_local_game_for_editor
          // is a kfx_game function (below kfx_apploop, no header violation);
          // the coroutine always runs to completion synchronously below for
          // a local game (no network wait), so editor_callbacks->request_open()
          // right after coroutine_process() sees a fully loaded, paused sim.
          my_player_number = default_loc_player;
          kfx_sim_state.game_kind = GKind_LocalGame;
          clear_flag(kfx_sim_state.system_flags, GSF_NetworkActive);
          player = get_my_player();
          player->is_active = 1;
          if (editor_pending_is_new)
              editor_request_blank_map(editor_pending_new_map_w, editor_pending_new_map_h, editor_pending_new_map_texture);
          startup_local_game_for_editor(&loop, editor_pending_lvnum, /*suspend=*/true, /*trim_post_init=*/true);
          break;
    }

    coroutine_add(&loop, &set_not_has_quit);
    coroutine_process(&loop);
    if (loop.error)
    {
        frontend_set_state(FeSt_INITIAL);
        return false;
    }
    if (prev_state == FeSt_START_EDITOR)
    {
        editor_callbacks->request_open(editor_pending_lvnum, editor_pending_is_new);
    }
    return true;
}

void game_loop(void)
{
#if (BFDEBUG_LEVEL > 0)
    unsigned long playtime = 0;
#endif
    SYNCDBG(0,"Entering gameplay loop.");

    while ( !exit_keeper )
    {
      update_mouse();
      while (!wait_at_frontend())
      {
          if (exit_keeper)
              break;
      }
      if ( exit_keeper )
        break;

      int32_t mspos_x_bak = lbDisplay.MMouseX;
      int32_t mspos_y_bak = lbDisplay.MMouseY;

      if (kfx_sim_state.game_kind == GKind_LocalGame)
      {
        if (kfx_frontend_state.save_game_slot == -1)
        {
            if (is_feature_on(Ft_SkipHeartZoom) == false) {
                for (int i = 0; i < PLAYERS_COUNT; i++) {
                    struct PlayerInfo *player = get_player(i);
                    if (player_exists(player) && ((player->allocflags & PlaF_CompCtrl) == 0)) {
                        set_player_instance(player, PI_HeartZoom, 0);
                    }
                }
            } else {
                if (!kfx_net_state.packet_load_enable) {
                    toggle_status_menu(1); // Required when skipping PI_HeartZoom
                }
            }
        } else
        {
          kfx_frontend_state.save_game_slot = -1;
        }
      } else {
          for (int i = 0; i < PLAYERS_COUNT; i++) {
              struct PlayerInfo *player = get_player(i);
              if (player_exists(player) && ((player->allocflags & PlaF_CompCtrl) == 0)) {
                  set_player_instance(player, PI_HeartZoom, 0);
              }
          }
      }

      // Try to keep the mouse position unchanged when entering the level.
      // The main considerations are:
      // 1. SKIP_HEART_ZOOM: the mouse icon position will be reset to the top-left corner (0, 0), but the actual mouse position remains unchanged.
      // 2. PI_HeartZoom: the mouse will be moved to the center of the screen.
      LbMouseSetPosition(mspos_x_bak, mspos_y_bak);

      unsigned long starttime;
#if (BFDEBUG_LEVEL > 0)
      unsigned long endtime;
#endif
      struct Dungeon *dungeon;
      // get_my_dungeon() can't be used here because players are not initialized yet
      dungeon = get_dungeon(my_player_number);
      starttime = LbTimerClock();
      dungeon->lvstats.start_time = starttime;
      dungeon->lvstats.end_time = starttime;
      kfx_sim_state.GameSeconds = 0;
      kfx_sim_state.GameT.Seconds = 0;
      kfx_sim_state.GameT.Minutes = 0;
      kfx_sim_state.GameT.Hours = 0;
      if (!kfx_sim_state.TimerNoReset)
      {
          if (is_feature_on(Ft_SkipHeartZoom))
          {
              kfx_sim_state.timerstarttime = starttime;
          }
          else
          {
              kfx_sim_state.TimerFreeze = true;
          }
          memset(&kfx_sim_state.Timer, 0, sizeof(kfx_sim_state.Timer));
      }
      RendererClearScreen(0);
      RendererPresentFrame();
      kfx_net_state.frame_skip = 0;
      keeper_gameplay_loop();
      set_pointer_graphic_none();
      RendererClearScreen(0);
      RendererPresentFrame();
      stop_atmos_sounds();
      stop_music(true);
      stop_streamed_samples();
      free_level_strings_data();
      turn_off_all_menus();
      delete_all_structures();
      clear_mapwho();
      // Reset sounds back to the fxdata baseline so the main menu (and any
      // subsequent campaign/freeplay selection) hears unmodified defaults.
      sound_reset_to_fxdata_baseline();
#if (BFDEBUG_LEVEL > 0)
      endtime = LbTimerClock();
#endif
      quit_game = 0;
      if ((kfx_sim_state.operation_flags & GOF_SingleLevel) != 0)
          exit_keeper=true;
#if (BFDEBUG_LEVEL > 0)
      playtime += endtime-starttime;
#endif
      SYNCDBG(0,"Play time is %lu seconds",playtime>>10);
      reset_eye_lenses();
      close_packet_file();
      kfx_net_state.packet_load_enable = false;
      kfx_net_state.packet_save_enable = false;
    } // end while

    // Stop the movie recording if it's on
    if ((kfx_sim_state.system_flags & GSF_CaptureMovie) != 0) {
        movie_record_stop();
    }
    ShutDownSDLAudio();
    SYNCDBG(7,"Done");
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
