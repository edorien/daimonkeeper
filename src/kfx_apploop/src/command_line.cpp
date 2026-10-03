/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file command_line.cpp
 *     The startup defaults and the command-line parser that fills
 *     start_params (config_keeperfx.h) before the game is set up.
 *     Moved out of main.cpp in refactor pass 2, S15.
 */
/******************************************************************************/
#include "pre_inc.h"


#include "bflib_inputctrl.h"
#include "net_lobby.h"

#include "frontend.h"
#include "front_network.h"
#include "lvl_script_lib.h"
#include "frontmenu_ingame_evnt.h"
#include "config_keeperfx.h"
#include "main_game.h"
#include <cstdint>

#ifdef FUNCTESTING
  #include "ftests/ftest.h"
#endif

#include "command_line.h"
#include "post_inc.h"

#ifdef _MSC_VER
#define strcasecmp _stricmp
#endif

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/**
 * Sets to defaults some basic parameters which are
 * later copied into Game structure.
 */
TbBool set_default_startup_parameters(void)
{
    memset(&start_params, 0, sizeof(struct StartupParameters));
    start_params.startup_flags = (SFlg_Legal|SFlg_FX|SFlg_Intro);
    start_params.packet_checksum_verify = 1;
    // Set levels to 0, as we may not have the campaign loaded yet
    start_params.selected_level_number = 0;
    start_params.num_fps = 20;
    start_params.one_player = 1;
    start_params.computer_chat_flags = CChat_None;
    start_params.autostart_multiplayer_users_expected = 2;
    clear_flag(start_params.mode_flags, MFlg_IsDemoMode);
    set_flag(start_params.mode_flags, MFlg_DemoMode);
    return true;
}

int64_t process_command_line(int64_t argc, char *argv[])
{
  char fullpath[CMDLN_MAXLEN+1];
  snprintf(fullpath, CMDLN_MAXLEN, "%s", argv[0]);
  snprintf(keeper_runtime_directory, sizeof(keeper_runtime_directory), "%s", fullpath);
  char *endpos = strrchr( keeper_runtime_directory, '\\');
  if (endpos==NULL)
      endpos=strrchr( keeper_runtime_directory, '/');
  if (endpos!=NULL)
      *endpos='\0';
  else
      strcpy(keeper_runtime_directory, ".");

  AssignCpuKeepers = 0;
  SoundDisabled = 0;
  // Note: the working log file is set up in LbBullfrogMain

  set_default_startup_parameters();

  int64_t bad_param;
  LevelNumber level_num;
  bad_param = 0;
  int64_t narg;
  level_num = LEVELNUMBER_ERROR;
  TbBool one_player_mode = 0;
  narg = 1;
  char bad_params[TEXT_BUFFER_LENGTH] = "\0";
  while ( narg < argc )
  {
      char *par;
      par = argv[narg];
      if ( (par == NULL) || ((par[0] != '-') && (par[0] != '/')) )
          return -1;
      char parstr[CMDLN_MAXLEN+1];
      char pr2str[CMDLN_MAXLEN+1];
      char pr3str[CMDLN_MAXLEN+1];
      snprintf(parstr, CMDLN_MAXLEN, "%s", par + 1);
      if (narg + 1 < argc)
      {
          snprintf(pr2str, CMDLN_MAXLEN, "%s", argv[narg + 1]);
          if (narg + 2 < argc)
              snprintf(pr3str, CMDLN_MAXLEN, "%s", argv[narg + 2]);
          else
              pr3str[0]='\0';
      }
      else
      {
          pr2str[0]='\0';
          pr3str[0]='\0';
      }

      if (strcasecmp(parstr, "nointro") == 0)
      {
        start_params.no_intro = true;
      } else
      if (strcasecmp(parstr, "skipheartzoom") == 0)
      {
        start_params.skip_heart_zoom = true;
      } else
      if (strcasecmp(parstr, "nocd") == 0) // kept for legacy reasons
      {
          WARNLOG("The -nocd commandline parameter is no longer functional. Game music from CD is a setting in keeperfx.cfg instead.");
      } else
      if (strcasecmp(parstr, "columnconvert") == 0) //todo remove once it's no longer in the launcher
      {
          WARNLOG("The -%s commandline parameter is no longer functional.", parstr);
      }
      else
      if (strcasecmp(parstr, "cd") == 0)
      {
          start_params.overrides[Clo_CDMusic] = true;
      } else
      if (strcasecmp(parstr, "1player") == 0)
      {
          start_params.one_player = true;
          one_player_mode = true;
      } else
      if ((strcasecmp(parstr, "s") == 0) || (strcasecmp(parstr, "nosound") == 0))
      {
          SoundDisabled = true;
      } else
      if (strcasecmp(parstr, "headless") == 0)
      {
          // No real display or audio device needed -- SDL still gets a
          // (unshown) window/surface via its "dummy" video driver
          // (VideoDisabled, checked in PlatformLinux/PlatformWindows::
          // VideoInit()), and SoundDisabled skips audio device init
          // entirely (sounds.c). For running src/ftests/ in CI/sandboxed
          // environments, e.g. under coverage instrumentation.
          VideoDisabled = true;
          SoundDisabled = true;
      } else
      if (strcasecmp(parstr, "fps") == 0)
      {
          narg++;
          start_params.num_fps = atoi(pr2str);
          start_params.overrides[Clo_GameTurns] = true;
      } else
      if (strcasecmp(parstr, "fps_draw") == 0)
      {
          narg++;
	  if (parse_draw_fps_config_val(pr2str, &start_params.num_fps_draw_main, &start_params.num_fps_draw_secondary) > 0)
            start_params.overrides[Clo_FramesPerSecond] = true;
      } else
      if (strcasecmp(parstr, "human") == 0)
      {
          narg++;
          default_loc_player = atoi(pr2str);
          start_params.force_player_num = true;
      } else
      if (strcasecmp(parstr, "vidsmooth") == 0)
      {
          kfx_runtime_settings.vid_smooth = true;
          start_params.overrides[Clo_VidSmooth] = true;
      } else
      if ( strcasecmp(parstr,"level") == 0 )
      {
        set_flag(start_params.operation_flags, GOF_SingleLevel);
        level_num = atoi(pr2str);
        start_params.autostart_multiplayer_level = atoi(pr2str);
        narg++;
      } else
      if ( strcasecmp(parstr,"campaign") == 0 )
      {
        strcpy(start_params.selected_campaign, pr2str);
        strcpy(start_params.autostart_multiplayer_campaign, pr2str);
        narg++;
      } else
      if ( strcasecmp(parstr,"altinput") == 0 )
      {
          SYNCLOG("Mouse auto reset disabled");
          lbMouseGrab = false;
          start_params.overrides[Clo_AltInput] = true;
      }
      else if (strcasecmp(parstr,"packetload") == 0)
      {
         if (start_params.packet_save_enable)
            WARNMSG("PacketSave disabled to enable PacketLoad.");
         start_params.packet_load_enable = true;
         start_params.packet_save_enable = false;
         snprintf(start_params.packet_fname, sizeof(start_params.packet_fname), "%s", pr2str);
         set_flag(start_params.debug_flags, DFlg_ShowGameTurns | DFlg_FrameStep);
         narg++;
      } else
      if (strcasecmp(parstr,"packetsave") == 0)
      {
         if (start_params.packet_load_enable)
            WARNMSG("PacketLoad disabled to enable PacketSave.");
         start_params.packet_load_enable = false;
         start_params.packet_save_enable = true;
         snprintf(start_params.packet_fname, sizeof(start_params.packet_fname), "%s", pr2str);
         narg++;
      } else
      if (strcasecmp(parstr,"pause_at_gameturn") == 0)
      {
         set_flag(start_params.debug_flags, DFlg_ShowGameTurns | DFlg_FrameStep | DFlg_PauseAtGameTurn);
         start_params.pause_at_gameturn = atoi(pr2str);
         narg++;
      } else
      if (strcasecmp(parstr,"q") == 0)
      {
         set_flag(start_params.operation_flags, GOF_SingleLevel);
      } else
      if (strcasecmp(parstr,"lightconvert") == 0)
      {
         WARNLOG("The -%s commandline parameter is no longer functional.", parstr); //todo remove once it's no longer in the launcher
      } else
      if (strcasecmp(parstr, "dbgshots") == 0)
      {
          set_flag(start_params.debug_flags, DFlg_ShotsDamage);
      } else
      if (strcasecmp(parstr, "dbgpathfind") == 0)
      {
          set_flag(start_params.debug_flags, DFlg_CreatrPaths);
      } else
      if (strcasecmp(parstr, "imguidemo") == 0)
      {
          // docs/refactor/renderer/04-imgui-gui-foundation.md §7 Phase A
          // exit criteria: prove the ImGui backend wiring with imgui_demo
          // ahead of any real screen migrating.
          set_flag(start_params.debug_flags, DFlg_ImGuiDemo);
      } else
      if (strcasecmp(parstr, "imguistyle") == 0)
      {
          // §7 Phase B exit criteria: the style-sheet test screen
          // exercising every frontgui_widgets.h wrapper.
          set_flag(start_params.debug_flags, DFlg_ImGuiStyleSheet);
      } else
      if (strcasecmp(parstr, "show_game_turns") == 0)
      {
          set_flag(start_params.debug_flags, DFlg_ShowGameTurns);
      } else
      if (strcasecmp(parstr, "mplog") == 0)
      {
          detailed_multiplayer_logging = true;
      } else
      if (strcasecmp(parstr, "netstats") == 0)
      {
          debug_display_network_stats = 1;
      } else
      if (strcasecmp(parstr, "compuchat") == 0)
      {
          if (strcasecmp(pr2str,"scarce") == 0) {
              start_params.computer_chat_flags = CChat_TasksScarce;
          } else
          if (strcasecmp(pr2str,"frequent") == 0) {
              start_params.computer_chat_flags = CChat_TasksScarce|CChat_TasksFrequent;
          } else {
              start_params.computer_chat_flags = CChat_None;
          }
          narg++;
      } else
      if (strcasecmp(parstr, "sessions") == 0) {
          narg++;
          LbNetwork_InitSessionsFromCmdLine(pr2str);
      } else
      if (strcasecmp(parstr, "nomods") == 0) {
          start_params.ignore_mods = true;
      } else
      if (strcasecmp(parstr,"alex") == 0)
      {
         start_params.easter_egg = true;
         start_params.overrides[Clo_EasterEgg] = true;
      }
      else if (strcasecmp(parstr,"connect") == 0)
      {
          narg++;
          LbNetwork_InitSessionsFromCmdLine(pr2str);
          game_flags2 |= GF2_Connect;
      }
      else if (strcasecmp(parstr,"waitusers") == 0)
      {
          start_params.autostart_multiplayer_users_expected = clamp(atoi(pr2str), MIN_NET_USERS, MAX_NET_USERS);
          narg++;
      }
      else if (strcasecmp(parstr,"server") == 0)
      {
          game_flags2 |= GF2_Server;
          int64_t port = atoi(pr2str);
          if (port > 0)
          {
              LbNetwork_SetServerPort(port);
              narg++;
          }
      }
      else if (strcasecmp(parstr, "nick") == 0)
      {
          if (pr2str[0])
          {
              snprintf(net_player_name, sizeof(net_player_name), "%s", pr2str);
              snprintf(tmp_net_player_name, sizeof(net_player_name), "%s", pr2str);
              narg++;
          }
          else
          {
              WARNMSG("No player name given after -nick");
          }
      }
      else if (strcasecmp(parstr,"frameskip") == 0)
      {
         start_params.frame_skip = atoi(pr2str);
         narg++;
      } else
      if (strcasecmp(parstr,"framestep") == 0)
      {
         set_flag(start_params.debug_flags, DFlg_ShowGameTurns | DFlg_FrameStep);
      }
      else if (strcasecmp(parstr, "timer") == 0)
      {
          game_flags2 |= GF2_Timer;
          if (strcasecmp(pr2str, "game") == 0)
          {
              kfx_sim_state.TimerGame = true;
              narg++;
              if (strcasecmp(pr3str, "real") == 0)
              {
                  kfx_sim_state.TimerGameReal = true;
                  narg++;
              }
          }
          else if (strcasecmp(pr2str, "continuous") == 0)
          {
              kfx_sim_state.TimerNoReset = true;
              narg++;
          }
      }
      else if ( strcasecmp(parstr,"config") == 0 )
      {
        strcpy(start_params.config_file, pr2str);
        start_params.overrides[Clo_ConfigFile] = true;
        narg++;
      }
      else if ( strcasecmp(parstr,"Bullfrog") == 0 ) // force playing the Bullfrog video
      {
        set_flag(start_params.startup_flags, SFlg_Bullfrog);
      }
      else if ( strcasecmp(parstr,"ea") == 0 ) // force playing the EA video
      {
        set_flag(start_params.startup_flags, SFlg_EA);
      }
      else if (strcasecmp(parstr, "ftests") == 0)
      {
#ifdef FUNCTESTING
        if(ftest_parse_arg(pr2str)) // handle arg on ftest build
#else
        if(strlen(pr2str) > 0 && pr2str[0] != '-') // ignore arg on regular build
#endif // FUNCTESTING
        {
            ++narg;
        }

#ifdef FUNCTESTING
        set_flag(start_params.functest_flags, FTF_Enabled);
#else
        WARNLOG("Flag '%s' disabled for release builds.", parstr);
#endif // FUNCTESTING
      }
      else if (strcasecmp(parstr, "log") == 0)
      {
          narg++;
      }
      else if(strcasecmp(parstr, "exitonfailedtest") == 0)
      {
#ifdef FUNCTESTING
        set_flag(start_params.functest_flags, FTF_ExitOnTestFailure);
#else
       WARNLOG("Flag '%s' disabled for release builds.", parstr);
#endif // FUNCTESTING
      }
      else if(strcasecmp(parstr, "includelongtests") == 0)
      {
#ifdef FUNCTESTING
        set_flag(start_params.functest_flags, FTF_IncludeLongTests);
#else
       WARNLOG("Flag '%s' disabled for release builds.", parstr);
#endif // FUNCTESTING
      }
      else
      {
        // append bad parstr to bad_params string
        char param_buffer[128] = "";
        snprintf(param_buffer, sizeof(param_buffer), "%s%s", strnlen(bad_params, TEXT_BUFFER_LENGTH) > 0 ? ", " : "" , parstr);
        str_append(bad_params, sizeof(bad_params), param_buffer);
        bad_param=narg;
      }
      narg++;
  }

  if (level_num == LEVELNUMBER_ERROR)
  {
      if (first_singleplayer_level() > 0)
      {
          level_num = first_singleplayer_level();
      }
      else
      {
          level_num = 1;
      }
  }
  else {
      if (one_player_mode) {
          AssignCpuKeepers = 1;
      }
  }
  start_params.selected_level_number = level_num;
  my_player_number = default_loc_player;

#ifdef FUNCTESTING
  ftest_init(); // initialise test framework on ftest build
#endif

  if(bad_param != 0)
  {
    char message[TEXT_BUFFER_LENGTH];
    snprintf(message, sizeof(message), "Incorrect command line parameters: '%s'.\nPlease correct your Run options.", bad_params);
    warning_dialog(__func__, 0, message);
  }

  return (bad_param==0);
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
