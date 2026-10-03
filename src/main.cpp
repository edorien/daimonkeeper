/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file main.cpp
 * @author KeeperFX Team
 * @date 01 Aug 2008
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"

#include "platform/PlatformManager.h"
#include "renderer/RendererManager.h"
#include "frontgui_stylesheet_test.h"

#include "bflib_datetm.h"
#include "bflib_sndlib.h"
#include "bflib_cpu.h"
#include "bflib_crash.h"

#include "api.h"
#include "custom_sprites.h"
#include "front_simple.h"
#include "frontend.h"
#include "front_input.h"
#include "kjm_input.h"
#include "lvl_script_lib.h"
#include "lvl_filesdk1.h"
#include "timer.h"
#include "engine_arrays.h"
#include "front_fmvids.h"
#include "vidfade.h"
#include "config_keeperfx.h"
#include "steam_api.hpp"
#include "main_game.h"
#include "game_session_loop.h"
#include "moonphase.h"
#include <cstdint>

#ifdef FUNCTESTING
  #include "ftests/ftest.h"
#endif

#include "file_path_port_impl.h"
#include "sound_host_port_impl.h"
#include "input_focus_port_impl.h"
#include "display_host_port_impl.h"
#include "sim_port_impl.h"
#include "pathfinding_world_port_impl.h"
#include "ai_port_impl.h"
#include "render_port_impl.h"
#include "net_port_impl.h"
#include "game_port_impl.h"
#include "audio_port_impl.h"
#include "ui_port_impl.h"
#include "script_port_impl.h"
#include "session_loop_port_impl.h"
#include "editor_port_impl.h"
#include "command_line.h"
#include "post_inc.h"

#ifdef _MSC_VER
#define strcasecmp _stricmp
#endif

// autostart_multiplayer_campaign/autostart_multiplayer_level/
// autostart_multiplayer_users_expected/force_player_num moved into
// kfx_config's struct StartupParameters (start_params) -- see
// config_keeperfx.h and docs/refactor/todo/
// check-layering-symbol-level-blind-spot.md. default_loc_player moved to
// kfx_game/src/main_game.c; turns_per_second to kfx_sim_state; the
// remaining plain globals below moved into their owning library's state
// struct (kfx_frontend_state/kfx_net_state/kfx_game_state/kfx_render_state)
// during the src/ -> src/kfx_* refactor.

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/

void init_keeper(void)
{
    SYNCDBG(8,"Starting");
    engine_init();
    init_fp_td_animation_conversion_tables();
    init_colours();
    init_spiral_steps();
    init_key_to_strings();
    // Load configs which may have per-campaign part, and even be modified within a level
    recheck_all_mod_exist();
    init_custom_sprites(SPRITE_LAST_LEVEL);
    load_stats_files();
    check_and_auto_fix_stats();
    init_creature_scores();
    init_top_texture_to_cube_table();
    kfx_config_state.neutral_player_num = PLAYER_NEUTRAL;
    kfx_config_state.atmos_sound_frequency = 800;
    poly_pool_end = &poly_pool[sizeof(poly_pool)-128];
    lbDisplay.GlassMap = pixmap.ghost;
    RendererSetDrawColour(kfx_sim_state.colours[15][15][15]);
    kfx_net_state.comp_player_aggressive  = (comp_player_conf.player_assist_default == comp_player_conf.computer_assist_types[0]);
    kfx_net_state.comp_player_defensive   = (comp_player_conf.player_assist_default == comp_player_conf.computer_assist_types[1]);
    kfx_net_state.comp_player_construct   = (comp_player_conf.player_assist_default == comp_player_conf.computer_assist_types[2]);
    kfx_net_state.comp_player_creatrsonly = (comp_player_conf.player_assist_default == comp_player_conf.computer_assist_types[3]);
    kfx_sim_state.creatures_tend_imprison = 0;
    kfx_sim_state.creatures_tend_flee = 0;
    kfx_sim_state.operation_flags |= GOF_ShowPanel;
    kfx_sim_state.view_mode_flags |= (GNFldD_StatusPanelDisplay | GNFldD_RoomFlameProcessing);
    init_censorship();
    SYNCDBG(9,"Finished");
}

/**
 * Initial video setup - loads only most important files to show startup screens.
 */
TbBool initial_setup(void)
{
    SYNCDBG(6,"Starting");
    // setting this will force video mode change, even if previous one is same
    MinimalResolutionSetup = true;
    // Set size of static textures buffer
    game_load_files[1].SLength = max((uint64_t)TEXTURE_BLOCKS_STAT_COUNT_A*block_dimension*block_dimension,(uint64_t)LANDVIEW_MAP_WIDTH*LANDVIEW_MAP_HEIGHT);
    if (LbDataLoadAllV2(game_load_files))
    {
        ERRORLOG("Unable to load game_load_files");
        return false;
    }
    load_pointer_file(0);
    update_screen_mode_data(320, 200);
    clear_game();
    RendererAddDrawFlags(0x4000u);
    return true;
}

/******************************************************************************/
// Callback tables ("ports") and providers, all installed by wire_ports() below.
// See docs/refactor-pass2/stage-01-callback-hygiene.md.




















/**
 * Installs every callback table and function-pointer provider.
 * Called from LbBullfrogMain() right after the log file is opened and before
 * process_command_line(), so everything that runs at startup
 * (load_configuration()'s parsers, command-line handling, renderer set-up)
 * reaches the real implementations, never a default no-op stub. Wiring a
 * table any later has caused real bugs: INGAME_RES and MATCHMAKING_SERVER
 * from keeperfx.cfg were both silently dropped that way.
 * Value pushes that need loaded configuration (bf_sprfnt_set_language_lwrstr()
 * and friends) stay in setup_game().
 * @return true if every installed table is complete (see ports_verify_wired()).
 */
static TbBool ports_verify_wired(void);

static TbBool wire_ports(void)
{
    set_file_path_port(&kfx_config_file_path_port);
    set_sound_host_port(&kfx_game_sound_host_port);
    set_input_focus_port(&kfx_sim_input_focus_port);
    set_display_host_port(&kfx_frontend_display_host_port);
    set_sim_port(&kfx_sim_port);
    set_pathfinding_world_port(&kfx_sim_pathfinding_world_port);
    set_ai_port(&kfx_ai_port);
    set_render_port(&kfx_render_port);
    set_net_port(&kfx_net_port);
    set_game_port(&kfx_game_port);
    set_audio_port(&kfx_frontend_audio_port);
    set_ui_port(&kfx_frontend_ui_port);
    set_script_port(&kfx_script_port);
    set_session_loop_port(&kfx_apploop_session_loop_port);
    set_editor_port(&kfx_editor_port);
    set_emulate_integer_overflow_provider(&emulate_integer_overflow);
    set_gameturn_source(&kfx_sim_state.play_gameturn);
    set_config_level_sources(&kfx_sim_state.selected_level_number, &kfx_sim_state.loaded_level_number);
    bf_sprfnt_set_font_role_resolver(resolve_font_role);
    set_config_network_is_active_check(network_is_active);
    return ports_verify_wired();
}

/**
 * Checks that no slot of any table wire_ports() installs is NULL (a member
 * left out of a designated initializer is zero-filled without a warning in C).
 * Logs an error for each NULL slot.
 */
static TbBool ports_verify_wired(void)
{
    TbBool complete = true;
    complete &= KFX_TABLE_COMPLETE(&kfx_config_file_path_port, struct FilePathPort);
    complete &= KFX_TABLE_COMPLETE(&kfx_game_sound_host_port, struct SoundHostPort);
    complete &= KFX_TABLE_COMPLETE(&kfx_sim_input_focus_port, struct InputFocusPort);
    complete &= KFX_TABLE_COMPLETE(&kfx_frontend_display_host_port, struct DisplayHostPort);
    complete &= KFX_TABLE_COMPLETE(&kfx_sim_port, struct SimPort);
    complete &= KFX_TABLE_COMPLETE(&kfx_sim_pathfinding_world_port, struct PathfindingWorldPort);
    complete &= KFX_TABLE_COMPLETE(&kfx_ai_port, struct AiPort);
    complete &= KFX_TABLE_COMPLETE(&kfx_render_port, struct RenderPort);
    complete &= KFX_TABLE_COMPLETE(&kfx_net_port, struct NetPort);
    complete &= KFX_TABLE_COMPLETE(&kfx_game_port, struct GamePort);
    complete &= KFX_TABLE_COMPLETE(&kfx_frontend_audio_port, struct AudioFeedbackPort);
    complete &= KFX_TABLE_COMPLETE(&kfx_frontend_ui_port, struct UiPort);
    complete &= KFX_TABLE_COMPLETE(&kfx_script_port, struct ScriptPort);
    complete &= KFX_TABLE_COMPLETE(&kfx_apploop_session_loop_port, struct SessionLoopPort);
    complete &= KFX_TABLE_COMPLETE(&kfx_editor_port, struct EditorPort);
    return complete;
}

/**
 * Displays 'legal' screens, intro and initializes basic game data.
 * If true is returned, then all files needed for startup were loaded,
 * and there should be the loading screen visible.
 * @return Returns true on success, false on error which makes the
 *   gameplay impossible (usually files loading failure).
 * @note The current screen resolution at end of this function may vary.
 */

int64_t setup_game(void)
{
  struct CPU_INFO cpu_info; // CPU status variable
  int64_t result;
  // Do only a very basic setup
  cpu_detect(&cpu_info);
  SYNCMSG("CPU %s type %" PRId64 " family %" PRId64 " model %" PRId64 " stepping %" PRId64 " features %08" PRIx64,cpu_info.vendor,
      (int64_t)cpu_get_type(&cpu_info),(int64_t)cpu_get_family(&cpu_info),(int64_t)cpu_get_model(&cpu_info),
      (int64_t)cpu_get_stepping(&cpu_info),(uint64_t)(cpu_info.feature_edx));
  if (cpu_info.BrandString)
  {
      SYNCMSG("%s", &cpu_info.brand[0]);
  }
  SYNCMSG("Build image base: %p", PlatformManager_GetImageBase());
  SYNCMSG("Operating System: %s", PlatformManager_GetOSVersion());

  const auto wine_version = PlatformManager_GetWineVersion();
  if (wine_version) {
        SYNCMSG("Running on Wine v%s", wine_version);
        is_running_under_wine = true;
        const auto wine_host = PlatformManager_GetWineHost();
        SYNCMSG("Wine Host: %s", wine_host);
  }

  // Enable features that require more than 32 megs of memory
  features_enabled |= Ft_HiResCreatr;
  // Enable features that require more than 16 megs of memory
  features_enabled |= Ft_EyeLens;
  features_enabled |= Ft_HiResVideo;
  features_enabled |= Ft_BigPointer;
  features_enabled |= Ft_AdvAmbSound;

  // Default feature settings (in case the options are absent from keeperfx.cfg)
  features_enabled &= ~Ft_FreezeOnLoseFocus; // don't freeze the game, if the game window loses focus
  features_enabled &= ~Ft_UnlockCursorOnPause; // don't unlock the mouse cursor from the window, if the user pauses the game
  features_enabled |= Ft_LockCursorInPossession; // lock the mouse cursor to the window, when the user enters possession mode (when the cursor is already unlocked)
  features_enabled |= Ft_RelativeMouseMode; // use SDL relative ("raw") mouse mode; set RELATIVE_MOUSE_MODE=OFF for the grab-and-warp scheme
  features_enabled &= ~Ft_PauseMusicOnGamePause; // don't pause the music, if the user pauses the game
  features_enabled &= ~Ft_MuteAudioOnLoseFocus; // don't mute the audio, if the game window loses focus
  if (start_params.skip_heart_zoom) {
    features_enabled |= Ft_SkipHeartZoom;
  } else {
    features_enabled &= ~Ft_SkipHeartZoom;
  }
  features_enabled &= ~Ft_DisableCursorCameraPanning; // don't disable cursor camera panning
  features_enabled |= Ft_DeltaTime; // enable delta time
  features_enabled |= Ft_NoCdMusic; // use music files (OGG) rather than CD music

  // Reserve the video-mode table's index 0 as the INVALID sentinel before any
  // config parsing can call LbRegisterVideoModeString() (INGAME_RES, case 7)
  // -- LbScreenInitialize() itself doesn't run until setup_screen_mode_zero()
  // much later, and without this, a config-parsed custom resolution would be
  // the table's actual first entry, landing on index 0 itself and being
  // silently rejected by every "mode > 0" caller (screen_vidmode would then
  // stay stuck at its compiled default across every restart).
  LbRegisterDefaultVideoModesIfNeeded();

  // Configuration file
  if ( !load_configuration() )
  {
      // Keep the startup lines whatever the level: they explain the failure.
      LbLogEndStartupBuffering(true);
      ERRORLOG("Configuration load error.");
      return 0;
  }
  // The log level is known now: write out (or, at Off, drop) the startup lines.
  LbLogEndStartupBuffering(get_log_level() != LogLvl_Off);

  // gpu-v2 Phase C.1 (docs/refactor/renderer/gpu-v2/07-phased-delivery.md):
  // RendererInit(RENDERER_SOFTWARE) above already brought up an active
  // renderer for the pre-config-load splash/legal screens -- load_configuration()
  // just above is the earliest point RENDERER's cfg value is actually known, so the real backend
  // swap (if the user asked for one) happens here, not earlier.
  {
      RendererType desired = RendererGetDesiredType();
      if (desired != RendererGetActiveType() && RendererInit(desired) == 0)
      {
          ERRORLOG("Renderer initialisation error.");
          return 0;
      }
  }

  #ifdef FUNCTESTING
    start_params.startup_flags &= ~SFlg_Legal;
    start_params.startup_flags &= ~SFlg_FX;
    features_enabled |= Ft_SkipHeartZoom;
  #endif

  // Process CmdLine overrides
  process_cmdline_overrides();

  // Push resolved config/cmdline state down into bflib_* (see
  // docs/refactor/stage-02-decouple-bflib.md).
  bf_sprfnt_set_language_lwrstr(get_language_lwrstr(install_info.lang_id));
  bf_sprfnt_set_fxdata_dir(prepare_file_path(FGrp_FxData, ""));
  bf_sndlib_set_audio_config(get_language_lwrstr(install_info.lang_id), is_feature_on(Ft_NoCdMusic));
  bf_sound_set_atmos_config(AtmosStart, AtmosEnd, AtmosRepeat, atmos_sounds_enabled());
  // docs/refactor/renderer/04-imgui-gui-foundation.md §3.5/§7 Phase A --
  // kfx_platform can't call kfx_config's is_feature_on() itself (kfx_config
  // ranks above kfx_platform), so push the resolved flag down instead. ImGui itself is unconditional now (every
  // frontend menu needs it, and the in-game HUD's own classic-vs-ImGui
  // choice, GUI_POSITION=CLASSIC, is decided per-draw-call
  // inside kfx_frontend -- see ingame_gui_use_classic_hud()) -- there is no
  // longer a session-wide "ImGui enabled" switch to push down here.
  RendererSetImGuiDemoVisible((start_params.debug_flags & DFlg_ImGuiDemo) != 0);
  FeStyleSheetSetVisible((start_params.debug_flags & DFlg_ImGuiStyleSheet) != 0);
  kfx_config_state.gui_blink_rate = keeperfx_ui_config.gui_blink_rate;
  kfx_config_state.neutral_flash_rate = keeperfx_ui_config.neutral_flash_rate;
  creature_status_size = keeperfx_ui_config.creature_status_size;
  line_box_size = keeperfx_ui_config.line_box_size;
  right_click_tag_mode_toggle = keeperfx_ui_config.right_click_tag_mode_toggle;
  zoom_to_mouse_option = (enum ZoomToMouseOptions)keeperfx_ui_config.zoom_to_mouse_option;
  rotate_around_mouse_option = (enum RotateAroundMouseOptions)keeperfx_ui_config.rotate_around_mouse_option;

  LbIKeyboardOpen();

  if (LbDataLoadAll(legal_load_files) != 0)
  {
      ERRORLOG("Error on allocation/loading of legal_load_files.");
      return 0;
  }

  // Setup polyscans
  setup_bflib_render();

  // View the legal screen
  if (!setup_screen_mode_zero(get_screen_vidmode()))
  {
      ERRORLOG("Unable to set display mode for legal screen");
      return 0;
  }

  if (flag_is_set(start_params.startup_flags, SFlg_Legal))
  {
      if (is_ar_wider_than_original(LbGraphicsScreenWidth(), LbGraphicsScreenHeight()))
      {
        result = init_actv_bitmap_screen(RBmp_SplashLegalWide);
      } else {
        result = init_actv_bitmap_screen(RBmp_SplashLegal);
      }
       if ( result )
      {
          result = show_actv_bitmap_screen(3000);
          free_actv_bitmap_screen();
      } else
          SYNCLOG("Legal image skipped");
  }
  else
  {
      // Make the white screen into a black screen faster
      draw_clear_screen();
  }

  // Now do more setup
  // Prepare the Game structure
  clear_complete_game();
  // Moon phase calculation
  calculate_moon_phase(true,true);
  // Start the sound system
  if (!init_sound())
    WARNMSG("Sound system disabled.");
  // Note: for some reason, signal handlers must be installed AFTER
  // init_sound(). This will probably change when we'll move sound
  // to SDL - then we'll put that line earlier, before setup_game().
  LbErrorParachuteInstall();
  // View second splash screen
  if (flag_is_set(start_params.startup_flags, SFlg_FX))
  {
      if (is_ar_wider_than_original(LbGraphicsScreenWidth(), LbGraphicsScreenHeight()))
      {
        result = init_actv_bitmap_screen(RBmp_SplashFxWide);
      } else {
        result = init_actv_bitmap_screen(RBmp_SplashFx);
      }
      if ( result == 1 )
      {
          result = show_actv_bitmap_screen(4000);
          free_actv_bitmap_screen();
      } else
          SYNCLOG("startup_fx image skipped");
  }

  draw_clear_screen();
  // View Bullfrog company logo animation when new moon
  if ( ( is_new_moon ) || (flag_is_set(start_params.startup_flags, SFlg_Bullfrog)) )
    if (!start_params.no_intro)
    {
        result = moon_video();
        if ( !result ) {
            ERRORLOG("Unable to play new moon movie");
        }
    }

  result = 1;
  // Setup the intro video mode
  if (result && (!start_params.no_intro) )
  {
      if (!setup_screen_mode_zero(get_screen_vidmode()))
      {
        ERRORLOG("Can't enter movies screen mode to play intro");
        result=0;
      }
  }

  if (result == 1)
  {
      draw_clear_screen();
      if (wait_for_installation_files())
      {
          //result = -1; // Helps with better warning message later
      }
      if (!start_params.no_intro)
      {
         if (flag_is_set(start_params.startup_flags, SFlg_EA))
         {
             ea_video();
         }
         if (flag_is_set(start_params.startup_flags, SFlg_Intro))
         {
            result = intro_replay();
         }
      }
  }

  kfx_net_state.frame_skip = start_params.frame_skip;
  redetect_screen_refresh_rate_for_draw();

  // Intro problems shouldn't force the game to quit,
  // so we're re-setting the result flag
  if (result == 0)
      result = 1;

  if (result == 1)
  {
      if (flag_is_set(start_params.operation_flags, GOF_SingleLevel) && !(game_flags2 & (GF2_Connect | GF2_Server)))
          level_load_time_phase(LevelLoadTime_EngineStartup);
      display_loading_screen();
  }
  LbDataFreeAll(legal_load_files);

  if (result == 1)
  {
      if ( !initial_setup() )
        result = 0;
  }

  if (result == 1)
  {
    load_settings();
    if ( !setup_gui_strings_data() )
      result = 0;
  }

  if (result == 1)
  {
      init_keeper();
      set_gamma(settings.gamma_correction, 0);
      set_music_volume(settings.music_volume);
      SetSoundMasterVolume(settings.sound_volume);
      setup_mesh_randomizers();
      setup_stuff();
  }

  return result;
}

static const char* determine_log_filename(int64_t argument_count, char *argument_values[])
{
    for (int64_t argument_index = 1; argument_index < argument_count; argument_index++) {
        if (argument_values[argument_index] && (argument_values[argument_index][0] == '-' || argument_values[argument_index][0] == '/')) {
            char* argument_name = argument_values[argument_index] + 1;
            if (strcasecmp(argument_name, "log") == 0 && argument_index + 1 < argument_count) {
                remove(DEFAULT_LOG_FILENAME);
                return argument_values[argument_index + 1];
            }
        }
    }
    return log_file_name;
}

static int64_t reset_game(void)
{
    SYNCDBG(6,"Starting");

    LbMouseSuspend();
    LbIKeyboardClose();
    RendererResetScreen(false);
    LbDataFreeAllV2(game_load_files);
    free_gui_strings_data();
    free_level_strings_data();
    FreeAudio();
    return 1;
}

int64_t LbBullfrogMain(int64_t argc, char *argv[])
{
    int64_t retval;
    retval=0;

    // Determine correct log file based on command line flags
    const char* selected_log_file_name = determine_log_filename(argc, argv);
    LbErrorLogSetup("/", selected_log_file_name, 5);
    // Hold log lines in memory until keeperfx.cfg has set the log level
    // (setup_game(), after load_configuration()).
    LbLogStartStartupBuffering();

    // Before anything that could call through a callback table.
    const TbBool ports_complete = wire_ports();

    retval = process_command_line(argc,argv);
    if (retval < 1)
    {
        LbErrorLogClose();
        return 0;
    }
#ifdef FUNCTESTING
    // Functional tests never depend on the tester's keeperfx.cfg.
    if (flag_is_set(start_params.functest_flags, FTF_Enabled))
        set_log_level_pinned(LogLvl_Normal);
    // An incomplete table is a wiring bug: fail the -ftests run for it.
    if (!ports_complete)
        set_flag(start_params.functest_flags, FTF_TestFailed);
#else
    (void)ports_complete;
#endif

    retval = true;
    retval &= (LbTimerInit() != Lb_FAIL);
    retval &= (RendererScreenInitialize() != Lb_FAIL);
    retval &= (RendererInit(RENDERER_SOFTWARE) != 0);
    LbSetTitle(PROGRAM_NAME);
    LbSetIcon(1);
    RendererSetDoubleBuffering(true);
    srand(LbTimerClock());

#ifdef FUNCTESTING
    ftest_srand();
#endif // FUNCTESTING

    if (!retval)
    {
        static const char *msg_text="Basic engine initialization failed.\n";
        error_dialog_fatal(__func__, 1, msg_text);
        LbErrorLogClose();
        return 0;
    }

    retval = setup_game();
    if (retval == 1)
    {
        steam_api_init();
    }
    if (retval == 1)
    {
        if (is_dbc_language(install_info.lang_id))
        {            
            dbc_initialized = 1;
        }
        load_unifont_files();
    }
    if ( retval == 1 )
    {
        api_init_server();
        game_loop();
    }
    reset_game();
    RendererResetScreen(true);
    RendererShutdown();
    if ( retval == 0 )
    {
        static const char *msg_text="Setting up game failed.\n";
        error_dialog_fatal(__func__, 2, msg_text);
    } else
    if (retval == -1)
    {
        static const char* msg_text = " Game files which have to be copied from original DK are not present.\n\n";
        error_dialog_fatal(__func__, 2, msg_text);
    }
    else
    {
        SYNCDBG(0,"finished properly");
    }

    steam_api_shutdown();
    LbErrorLogClose();
    return 0;
}

int64_t kfxmain(int64_t argc, char *argv[])
{
  try {
  LbBullfrogMain(argc, argv);
  } catch (const std::exception &e)
  {
      char msg[512];
      snprintf(msg, sizeof(msg), "Exception raised: %s", e.what());
      error_dialog(__func__, 1, msg);
      return 1;
  } catch (...)
  {
      error_dialog(__func__, 1, "Exception raised!");
      return 1;
  }

#ifdef FUNCTESTING
  TbBool should_report_failure = flag_is_set(start_params.functest_flags, FTF_TestFailed) && flag_is_set(start_params.functest_flags, FTF_ExitOnTestFailure);
  if(flag_is_set(start_params.functest_flags, FTF_Enabled) && (flag_is_set(start_params.functest_flags, FTF_Abort) || should_report_failure))
  {
      return -1;
  }
#endif

  return 0;
}

#ifdef __cplusplus
}
#endif
