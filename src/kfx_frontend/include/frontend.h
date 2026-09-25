/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file frontend.h
 *     Header file for frontend.cpp.
 * @par Purpose:
 *     Functions to display and maintain the game menu.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     10 Nov 2008 - 01 Feb 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_FRONTEND_H
#define DK_FRONTEND_H

#include "globals.h"
#include "bflib_guibtns.h"
#include "bflib_sprfnt.h"
#include "gui_frontmenu.h"
#include "game_saves.h"
#include "vidmode.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
// Limits for GUI arrays
#define ACTIVE_BUTTONS_COUNT        100
#define MENU_LIST_ITEMS_COUNT       52
#define FRONTEND_BUTTON_INFO_COUNT 118
// Symbolic names for frontend_button_info[]'s populated slots (frontend.cpp),
// each an exact rename of a numeric position -- FEBtn_Foo = N means slot N
// held that meaning already, nothing renumbered. Only genuine, verified
// caption-table lookups (a button whose draw_call reads
// frontend_button_info[content.lval] via frontend_button_caption_text/font)
// are named here; docs/refactor/gui/01-caption-table-rename.md's Phase A
// found the same content.lval numbers are *also* reused, in other rows, as
// select-list row markers (see FE_SELECTLIST_ROW_BASE), scroll-widget role
// markers, a scroll-box size selector, and an alliance-grid offset -- none
// of those belong in this enum, and slots consumed only that way (or never
// consumed at all) are deliberately left un-named. A few GUIStr values
// legitimately repeat at a second slot with a different font_index (e.g.
// GUIStr_MnuOptions at both 96 and 97); those get an `_<N>` suffix rather
// than a fabricated distinct name. Extend by appending one entry, never by
// renumbering an existing one (bump FRONTEND_BUTTON_INFO_COUNT to match if
// the table itself grows).
enum FrontEndBtnStrIdx {
    FEBtn_MnuMainMenu = 1,
    FEBtn_MnuStartNewGame = 2,
    FEBtn_MnuLoadGame = 3,
    FEBtn_MnuMultiplayer = 4,
    FEBtn_MnuQuit = 5,
    FEBtn_MnuReturnToMain = 6,
    FEBtn_MnuLoadGame_7 = 7,
    FEBtn_MnuContinueGame = 8,
    FEBtn_MnuPlayIntro = 9,
    FEBtn_NetServiceMenu = 10,
    FEBtn_NetSessionMenu = 11,
    FEBtn_MnuOnlineLobbies = 12,
    FEBtn_NetJoinGame = 13,
    FEBtn_NetCreateGame = 14,
    FEBtn_NetStartGame = 15,
    FEBtn_MnuCancel = 16,
    FEBtn_NetName = 19,
    FEBtn_MnuLevel = 22,
    FEBtn_NetSessions = 29,
    FEBtn_MnuGames = 30,
    FEBtn_MnuPlayers = 31,
    FEBtn_MnuLevels = 32,
    FEBtn_NetServices = 33,
    FEBtn_NetMessages = 34,
    FEBtn_NetModemMenu = 53,
    FEBtn_NetSerialMenu = 54,
    FEBtn_NetComPort = 55,
    FEBtn_NetSpeed = 56,
    FEBtn_NetIrq = 61,
    FEBtn_NetInit = 66,
    FEBtn_NetHangup = 67,
    FEBtn_NetDial = 68,
    FEBtn_NetAnswer = 69,
    FEBtn_NetPhoneNumber = 71,
    FEBtn_NetContinue = 72,
    FEBtn_NetContinue_73 = 73,
    FEBtn_Credits = 82,
    FEBtn_MnuOk = 83,
    FEBtn_MnuStatistics = 84,
    FEBtn_MnuHighScoreTable = 85,
    FEBtn_TeamChooseGame = 86,
    FEBtn_TeamGameType = 87,
    FEBtn_NetStart = 88,
    FEBtn_DefineKeys = 92,
    FEBtn_DefineKeys_95 = 95,
    FEBtn_MnuOptions = 96,
    FEBtn_MnuOptions_97 = 97,
    FEBtn_MnuRetToOptions = 98,
    FEBtn_MnuSoundOptions = 99,
    FEBtn_MouseOptions = 100,
    FEBtn_Sensitivity = 101,
    FEBtn_MnuInvertMouse = 102,
    FEBtn_MnuComputer = 103,
    FEBtn_MnuHighScoreTable_104 = 104,
    FEBtn_MnuFreePlayLevels = 106,
    FEBtn_MnuFreePlayLevels_107 = 107,
    FEBtn_MnuLandSelection = 108,
    FEBtn_MnuCampaigns = 109,
    FEBtn_MnuAddComputer = 110,
    FEBtn_MnuReturnToFreePlay = 111,
    FEBtn_MnuMapPacks = 112,
    FEBtn_MnuMpMapPacks = 113,
    FEBtn_MnuReturnToLobby = 114,
    FEBtn_MnuEnterLand = 115,
    FEBtn_MnuPlayLevel = 116,
    FEBtn_MnuSkirmish = 117,
};
#define NET_MESSAGES_COUNT           8
#define NET_MESSAGE_LEN             64
// Row N's Y coordinate in a fixed-spacing vertical (or horizontal) stack of
// GuiButtonInit rows, e.g. .scr_pos_y = FE_ROW_Y(167, 22, 3) for the 4th row
// of a list whose rows start at y=167 and step by 22px. Inserting a row
// becomes bumping every n below it, not recomputing pixel values by hand;
// grids and hand-tuned dialogs don't use this. Axis-agnostic -- the same
// macro lays out a column too (.scr_pos_x = FE_ROW_Y(12, 48, col)). A grid
// is just both axes on the same row, e.g. a 2-column x 3-row grid whose
// cells start at (20,60) and step 180px across / 50px down:
//   .scr_pos_x = FE_ROW_Y(20, 180, col), .scr_pos_y = FE_ROW_Y(60, 50, row)
// with col/row substituted per button (0,0 / 1,0 / 0,1 / 1,1 / 0,2 / 1,2).
#define FE_ROW_Y(base, step, n) ((base) + (step) * (n))
// Sprite limits
#define PANEL_SPRITES_COUNT 514
// FRONTEND_FONTS_COUNT moved to kfx_render's vidmode.h (stage 13.3,
// docs/refactor/stage-13-enforce-and-document.md).
// After that much milliseconds in main menu, demo is started
#define MNU_DEMO_IDLE_TIME 30000
/******************************************************************************/
#pragma pack(1)

enum DemoItem_Kind {
    DIK_PlaySmkVideo,
    DIK_LoadPacket,
    DIK_SwitchState,
    DIK_ListEnd,
};

enum FrontendMenuStates {
  FeSt_INITIAL = 0,
  FeSt_MAIN_MENU,
  FeSt_FELOAD_GAME,
  FeSt_LAND_VIEW,
  FeSt_NET_SERVICE, /**< Network service selection, wgere player can select Serial/Modem/IPX/TCP IP/1 player. */
  FeSt_NET_SESSION, /**< Network session selection screen, where list of games is displayed, with possibility to join or create own game. */
  FeSt_NET_START, /**< Network game start screen (the menu with chat), when created new session or joined existing session. */
  FeSt_START_KPRLEVEL,
  FeSt_START_MPLEVEL,
  FeSt_QUIT_GAME,
  FeSt_LOAD_GAME, // 10
  FeSt_INTRO,
  FeSt_STORY_POEM,
  FeSt_CREDITS,
  FeSt_DEMO,
  FeSt_UNUSED1,
  FeSt_UNUSED2,
  FeSt_LEVEL_STATS,
  FeSt_HIGH_SCORES,
  FeSt_TORTURE,
  FeSt_UNUSED_STATE1, // 20 - Unused state, draws GUI but not used
  FeSt_OUTRO,
  FeSt_UNUSED_STATE2, // Unused state
  FeSt_UNUSED_STATE3, // Unused state
  FeSt_NETLAND_VIEW,
  FeSt_PACKET_DEMO,
  FeSt_FEDEFINE_KEYS,
  FeSt_FEOPTIONS,
  FeSt_UNUSED_STATE4, // Unused state
  FeSt_STORY_BIRTHDAY,
  FeSt_LEVEL_SELECT, //30
  FeSt_CAMPAIGN_SELECT,
  FeSt_DRAG,
  FeSt_CAMPAIGN_INTRO,
  FeSt_MAPPACK_SELECT,
  FeSt_MP_MAPPACK_SELECT,
  // docs/refactor/editor/phase3/02-slice3-dialogs-menubar.md -- the
  // transient "start the (editor) session" state, reached either from the
  // main menu's Tools -> Editor (a blank New Map) or from an in-session
  // File > New/Open relaunch (frontend_request_editor_relaunch(), below).
  // No classic-menu (`-classicmenu`) equivalent -- the editor requires the
  // ImGui menu. FeSt_EDITOR (the old pre-session New/Open browser this
  // state used to hand off from) is retired -- New/Open/Save now live
  // inside the editor's own File menu instead.
  FeSt_START_EDITOR,
  // Special testing states
  FeSt_FONT_TEST          = 255,
};

// docs/refactor/editor/01-entry-and-editor-session.md §2/§4 -- the hand-off
// to kfx_apploop's `case FeSt_START_EDITOR:` (game_session_loop.cpp), which
// reads these to call startup_local_game_for_editor(). Plain frontend-
// internal globals, not KfxFrontendState fields -- transient for the one
// frame between the request and apploop consuming them, so they have no
// business in that struct's save-game/resync blob (mirrors
// net_service_index_selected's own reasoning, not save_game_slot's -- that
// one really is persisted).
extern LevelNumber editor_pending_lvnum;
extern TbBool editor_pending_is_new;
extern MapSlabCoord editor_pending_new_map_w;
extern MapSlabCoord editor_pending_new_map_h;
extern int64_t editor_pending_new_map_texture;

// docs/refactor/editor/phase3/02-slice3-dialogs-menubar.md -- an in-session
// File > New/Open (kfx_editor/editor_dialogs.cpp) sets this (via
// frontend_request_editor_relaunch() below) before sending the normal
// PckA_QuitToMainMenu quit packet. get_startup_menu_state() (frontend.cpp)
// checks it ahead of its usual "quit always goes to FeSt_MAIN_MENU" branch,
// routing back into FeSt_START_EDITOR (above) instead so the same bootstrap
// that runs for the initial Tools -> Editor entry re-runs for the new
// lvnum/is_new/size -- FeSt_START_EDITOR's handler
// (game_session_loop.cpp) only runs from the frontend loop, never
// mid-session, so this quit-and-relaunch round trip is the real mechanism,
// not a shortcut around it.
extern TbBool editor_pending_relaunch;

// Stashes editor_pending_lvnum/is_new/new_map_w/new_map_h/new_map_texture
// and sets editor_pending_relaunch -- callable directly from kfx_editor
// (ranks above kfx_frontend, same downward-call pattern editor_mapsave.cpp
// already uses for kfx_sim). Caller still has to actually trigger the quit
// itself (editor_close(), kfx_editor) right after -- this only stashes the
// target, it doesn't request the transition on its own.
void frontend_request_editor_relaunch(LevelNumber lvnum, TbBool is_new,
    MapSlabCoord new_map_w, MapSlabCoord new_map_h, int64_t new_map_texture);

// New Map's scratch level number until Save (phase 3) assigns it a real
// slot -- see docs/refactor/editor/00-overview.md O2/F4.
#define EDITOR_SCRATCH_LEVEL_NUMBER 900001
// docs/refactor/editor/phase3/04-slice5-playtest-settings-overwrite.md --
// Playtest's own scratch slot, distinct from EDITOR_SCRATCH_LEVEL_NUMBER
// (New Map) so playtesting an already-named/numbered level never
// overwrites it, and playtesting while editing a *different* in-progress
// map (e.g. still on EDITOR_SCRATCH_LEVEL_NUMBER itself) never collides
// with that either.
#define EDITOR_PLAYTEST_LEVEL_NUMBER 900002

// Same quit-and-relaunch mechanism as frontend_request_editor_relaunch()
// above, targeting FeSt_START_KPRLEVEL (a normal single-player game start)
// instead of FeSt_START_EDITOR -- reuses the exact same primitive the
// `-level` command-line launch argument uses (main.cpp's "level" parsing),
// set_selected_level_number(), not a new mechanism. The caller
// (kfx_editor) is responsible for having already saved `lvnum` to disk --
// this only stashes the target and requests the transition; editor_close()
// still has to actually send the quit packet right after, same as
// frontend_request_editor_relaunch()'s own contract.
extern TbBool editor_pending_playtest;
// Set while a playtest launched from the editor is running: when the game
// ends, the frontend goes back to the editor (on the playtest's scratch level).
extern TbBool editor_playtest_running;
/** Playtest `lvnum`. A non-empty `campaign_fname` makes that campaign (`pack`: a CampgnT_* value) the current one
 *  before the level starts, so its config, creature and string layers apply to the playtest. */
void frontend_request_editor_playtest(LevelNumber lvnum, uint8_t pack, const char *campaign_fname);
/** From the main menu: make `campaign_fname` (`pack`: a CampgnT_* value) the current campaign and open its level `lvnum` in
 *  the Map Editor (`is_new`: a blank map instead, `lvnum` ignored). Applied at the start of the next frame, like the other frontend transitions. */
// A content tool (Campaign editor) started a game to try a level: set while it runs, so that when it ends (win, lose or quit) the
// frontend returns to the main menu with that tool open again (content_tool_reopen_tool: a ContentTool value, -1 = none).
extern TbBool content_tool_play_running;
extern int64_t content_tool_return_tool; // the tool to reopen when that game ends (set by the request)
extern int64_t content_tool_reopen_tool; // set once it has ended; the main menu opens the tool and clears it
/** From the main menu: make `campaign_fname` (`pack`: a CampgnT_* value) the current campaign and play its level `lvnum` as a normal
 *  single-player game; when the game ends the main menu comes back with content tool `tool` open. Applied at the start of the next frame. */
void frontend_request_content_tool_play(uint8_t pack, const char *campaign_fname, LevelNumber lvnum, int64_t tool);
void frontend_request_map_editor_open(uint8_t pack, const char *campaign_fname, LevelNumber lvnum, TbBool is_new);

enum IngameButtonDesignationIDs {
    BID_INFO_TAB = BID_DEFAULT+1,
    BID_ROOM_TAB,
    BID_SPELL_TAB,
    BID_MNFCT_TAB,
    BID_CREATR_TAB,//5
    BID_ROOM_TD01,
    BID_ROOM_TD02,
    BID_ROOM_TD03,
    BID_ROOM_TD04,
    BID_ROOM_TD05,//10
    BID_ROOM_TD06,
    BID_ROOM_TD07,
    BID_ROOM_TD08,
    BID_ROOM_TD09,
    BID_ROOM_TD10,//15
    BID_ROOM_TD11,
    BID_ROOM_TD12,
    BID_ROOM_TD13,
    BID_ROOM_TD14,
    //BID_ROOM_TD15, -- no such index
    BID_ROOM_TD16,//20
    BID_POWER_TD01,
    BID_POWER_TD02,
    BID_POWER_TD03,
    BID_POWER_TD04,
    BID_POWER_TD05,//25
    BID_POWER_TD06,
    BID_POWER_TD07,
    BID_POWER_TD08,
    BID_POWER_TD09,
    BID_POWER_TD10,//30
    BID_POWER_TD11,
    BID_POWER_TD12,
    BID_POWER_TD13,
    BID_POWER_TD14,
    BID_POWER_TD15,//35
    BID_POWER_TD16,
    BID_MAP_ZOOM_FS,
    BID_MAP_ZOOM_IN,
    BID_MAP_ZOOM_OU,
    BID_MSG_EV01,//40
    BID_MSG_EV02,
    BID_MSG_EV03,
    BID_MSG_EV04,
    BID_MSG_EV05,
    BID_MSG_EV06,//45
    BID_MSG_EV07,
    BID_MSG_EV08,
    BID_MSG_EV09,
    BID_MSG_EV10,
    BID_MSG_EV11,//50
    BID_MSG_EV12,
    BID_MSG_EV13,
    BID_MNFCT_TD01,
    BID_MNFCT_TD02,
    BID_MNFCT_TD03,//55
    BID_MNFCT_TD04,
    BID_MNFCT_TD05,
    BID_MNFCT_TD06,
    BID_MNFCT_TD07,
    BID_MNFCT_TD08,//60
    BID_MNFCT_TD09,
    BID_MNFCT_TD10,
    BID_MNFCT_TD11,
    BID_MNFCT_TD12,
    BID_MNFCT_TD13,//65
    BID_MNFCT_TD14,
    BID_MNFCT_TD15,
    BID_MNFCT_TD16,
    BID_QRY_IMPRSN,
    BID_QRY_FLEE,//70
    BID_QRY_BTN3,
    BID_CRTR_NXWNDR,
    BID_CRTR_NXWRKR,
    BID_CRTR_NXFIGT,
    BID_QUERY_INFO, //75
    BID_DUNGEON_INFO,
    BID_OPTIONS,
    BID_EVENT_ZOOM,
    BID_OBJ_CLOSE,
    BID_OBJ_SCRL_UP, //80
    BID_OBJ_SCRL_DWN,
    BID_MENU_TITLE,
    BID_POWER_TD17,
    BID_POWER_TD18,
    BID_POWER_TD19, //85
    BID_POWER_TD20,
    BID_POWER_TD21,
    BID_POWER_TD22,
    BID_POWER_TD23,
    BID_POWER_TD24, //90
    BID_POWER_TD25,
    BID_POWER_TD26,
    BID_POWER_TD27,
    BID_POWER_TD28,
    BID_POWER_TD29, //95
    BID_POWER_TD30,
    BID_POWER_TD31,
    BID_POWER_TD32,
    BID_POWER_NXPG,
    BID_ROOM_TD17, //100
    BID_ROOM_TD18,
    BID_ROOM_TD19,
    BID_ROOM_TD20,
    BID_ROOM_TD21,
    BID_ROOM_TD22, //105
    BID_ROOM_TD23,
    BID_ROOM_TD24,
    BID_ROOM_TD25,
    BID_ROOM_TD26,
    BID_ROOM_TD27, //110
    BID_ROOM_TD28,
    BID_ROOM_TD29,
    BID_ROOM_TD30,
    BID_ROOM_TD31,
    BID_ROOM_TD32, //115
    BID_ROOM_NXPG,
    BID_MNFCT_TD17,
    BID_MNFCT_TD18,
    BID_MNFCT_TD19,
    BID_MNFCT_TD20, //120
    BID_MNFCT_TD21,
    BID_MNFCT_TD22,
    BID_MNFCT_TD23,
    BID_MNFCT_TD24,
    BID_MNFCT_TD25, //125
    BID_MNFCT_TD26,
    BID_MNFCT_TD27,
    BID_MNFCT_TD28,
    BID_MNFCT_TD29,
    BID_MNFCT_TD30, //130
    BID_MNFCT_TD31,
    BID_MNFCT_TD32,
    BID_MNFCT_NXPG,
    BID_QUERY_2,
    BID_ASSIST
};

struct GuiMenu;
struct GuiButton;
struct TbLoadFiles;

struct DemoItem { //sizeof = 5
    uint8_t kind;
    union {
      FrontendMenuState state;
      const char *fname;
    };
};

struct NetMessage { // sizeof = 0x45
  unsigned char plyr_idx;
  uint64_t connection_id;
  char text[NET_MESSAGE_LEN];
};

/******************************************************************************/
extern char info_tag;
extern char room_tag;
extern char spell_tag;
extern char trap_tag;
extern char creature_tag;
extern char input_string[8][SAVE_TEXTNAME_LEN + 1];
extern char gui_error_text[256];
extern int64_t net_number_of_services;
extern int64_t net_number_of_players;
extern int64_t net_number_of_enum_players;
extern int64_t net_level_highlighted;
extern struct NetMessage net_message[NET_MESSAGES_COUNT];
extern int64_t net_number_of_messages;
// net_session_index_active_id moved to net_main.h (kfx_net) -- see there.
// net_service_scroll_offset/net_session_scroll_offset/net_player_scroll_offset/
// net_message_scroll_offset moved into net_service_list/net_session_list/
// net_player_list/net_message_list (frontmenu_net.h, docs/refactor/gui/
// 00-overview.md Phase 1's FrontendSelectList engine).
extern struct GuiButton active_buttons[ACTIVE_BUTTONS_COUNT];
extern int64_t frontend_mouse_over_button_start_time;
extern int64_t old_menu_mouse_x;
extern int64_t old_menu_mouse_y;
extern unsigned char menu_ids[3];
// new_objective moved to kfx_sim's kfx_sim_state.h (stage 13.3,
// docs/refactor/stage-13-enforce-and-document.md).
extern int64_t frontend_menu_state;
extern int64_t skip_high_score_screen;
extern int64_t load_game_scroll_offset;
extern unsigned char video_gamma_correction;
// vid_change_query_menu moved to kfx_render's vidmode.h (stage 13.3,
// docs/refactor/stage-13-enforce-and-document.md).
extern TbBool right_click_tag_mode_toggle;
// default_tag_mode moved to kfx_sim's kfx_sim_state.h (stage 13.3,
// docs/refactor/stage-13-enforce-and-document.md).

// *** SPRITES ***
// font_sprites/frontend_font/button_sprites/winfont moved to kfx_render's
// vidmode.h (stage 13.3, docs/refactor/stage-13-enforce-and-document.md).

// Registered with bf_sprfnt_set_font_role_resolver() at startup; maps a
// font pointer to its role for bflib_sprfnt.c (see
// docs/refactor/stage-02-decouple-bflib.md).
enum TbFontRole resolve_font_role(const struct TbSpriteSheet *font);
extern uint64_t playing_bad_descriptive_speech;
extern uint64_t playing_good_descriptive_speech;
extern int64_t scrolling_index;
extern double scrolling_offset;
// packet_left_button_double_clicked/click_space_count moved to net_main.h
// (kfx_net) -- see there.
extern char frontend_alliances;
extern char busy_doing_gui;
extern int64_t gui_last_left_button_pressed_id;
extern int64_t gui_last_right_button_pressed_id;
extern int64_t fe_computer_players;
extern int64_t old_mouse_over_button;
extern int64_t frontend_mouse_over_button;

#pragma pack()
/******************************************************************************/
// Variables - no longer imported
extern struct GuiMenu frontend_main_menu;
extern struct GuiMenu frontend_statistics_menu;
extern struct GuiMenu frontend_high_score_table_menu;
extern struct FrontEndButtonData frontend_button_info[FRONTEND_BUTTON_INFO_COUNT];
extern char gui_message_text[];
extern TbClockMSec gui_message_timeout;

extern struct GuiMenu *menu_list[MENU_LIST_ITEMS_COUNT];

extern int64_t status_panel_width;
extern const uint64_t alliance_grid[4][4];

// TESTFONTS_COUNT/testfont/testfont_palette moved to kfx_render's
// vidmode.h (stage 13.3, docs/refactor/stage-13-enforce-and-document.md).
/******************************************************************************/
const char * mdlf_default(const char *);
/******************************************************************************/
int64_t frontend_font_char_width(int64_t fnt_idx,char c);
int64_t frontend_font_string_width(int64_t fnt_idx, const char *str);

void create_error_box(TextStringId msg_idx);
void create_message_box(const char *title, const char *line1, const char *line2, const char *line3, const char *line4, const char* line5);
void gui_area_text(struct GuiButton *gbtn);
TbBool get_button_area_input(struct GuiButton *gbtn, int64_t a2);
void finish_button_area_input(void);
const char *frontend_button_caption_text(const struct GuiButton *gbtn);
int64_t frontend_button_caption_font(const struct GuiButton *gbtn, int64_t mouse_over_btn_idx);
void maintain_loadsave(struct GuiButton *gbtn);
void gui_video_cluedo_maintain(struct GuiButton *gbtn);
void maintain_zoom_to_event(struct GuiButton *gbtn);
void maintain_scroll_up(struct GuiButton *gbtn);
void maintain_scroll_down(struct GuiButton *gbtn);
void frontend_continue_game_maintain(struct GuiButton *gbtn);
void frontend_main_menu_load_game_maintain(struct GuiButton *gbtn);
void frontend_main_menu_netservice_maintain(struct GuiButton *gbtn);
void frontend_main_menu_highscores_maintain(struct GuiButton *gbtn);
void maintain_loadsave(struct GuiButton *gbtn);
void gui_quit_game(struct GuiButton *gbtn);
void gui_area_slider(struct GuiButton *gbtn);
void frontend_draw_icon(struct GuiButton *gbtn);
void frontend_draw_error_text_box(struct GuiButton *gbtn);
void frontend_maintain_error_text_box(struct GuiButton *gbtn);
int64_t is_toggleable_menu(int64_t mnu_idx);

void activate_room_build_mode(RoomKind rkind, TextStringId tooltip_id);
void choose_spell(PowerKind pwkind, TextStringId tooltip_id);
TbBool is_special_power(PowerKind pwkind);
void choose_special_spell(PowerKind pwkind, TextStringId tooltip_id);
void choose_workshop_item(int64_t manufctr_idx, TextStringId tooltip_id);

int64_t frontend_load_data(void);
void frontend_draw_scroll_tab(struct GuiButton *gbtn, int64_t scroll_offset, int64_t first_elem, int64_t last_elem);
int64_t frontend_scroll_tab_to_offset(struct GuiButton *gbtn, int64_t scr_pos, int64_t first_elem, int64_t last_elem);
void frontend_init_options_menu(struct GuiMenu *gmnu);
void frontend_draw_text(struct GuiButton *gbtn);
void frontend_change_state(struct GuiButton *gbtn);
void frontend_draw_enter_text(struct GuiButton *gbtn);
void frontend_draw_small_menu_button(struct GuiButton *gbtn);
void frontend_toggle_computer_players(struct GuiButton *gbtn);
void frontend_draw_computer_players(struct GuiButton *gbtn);
void frontend_draw_mp_mappack(struct GuiButton *gbtn);
void set_packet_start(struct GuiButton *gbtn);
void gui_area_scroll_window(struct GuiButton *gbtn);
void gui_go_to_event(struct GuiButton *gbtn);
void maintain_zoom_to_event(struct GuiButton *gbtn);
void gui_close_objective(struct GuiButton *gbtn);
void gui_scroll_text_up(struct GuiButton *gbtn);
void gui_scroll_text_down(struct GuiButton *gbtn);
void maintain_scroll_up(struct GuiButton *gbtn);
void maintain_scroll_down(struct GuiButton *gbtn);
void gui_scroll_text_down(struct GuiButton *gbtn);
// Main Menu buttons that reach frontend_set_state() by way of real
// side-effecting work (loading a default campaign, starting a campaign)
// each have a _resolve() sibling returning the target FrontendMenuState
// (int, -1 = nothing to do / failed) instead of transitioning -- the
// ImGui screen (frontgui_screens.cpp) calls these directly and requests
// the transition itself, since frontend_set_state() is unsafe to call
// synchronously from inside an active ImGui window; the legacy
// click_events below keep calling frontend_set_state() directly,
// unchanged. Load Game/Options/Quit have fixed targets and no other side
// effects, so the ImGui screen just requests those states directly --
// no resolve wrapper needed for them.
int64_t frontend_ldcampaign_change_state_resolve(void);
void frontend_ldcampaign_change_state(struct GuiButton *gbtn);
int64_t frontend_netservice_change_state_resolve(void);
void frontend_netservice_change_state(struct GuiButton *gbtn);
int64_t frontend_start_skirmish_resolve(void);
void frontend_start_skirmish(struct GuiButton *gbtn);
void frontend_main_menu_skirmish_maintain(struct GuiButton *gbtn);
int64_t frontend_start_new_game_resolve(void);
void frontend_start_new_game(struct GuiButton *gbtn);
void frontend_load_mappacks(struct GuiButton *gbtn);
void frontend_load_mp_mappacks(struct GuiButton *gbtn);
int64_t frontend_load_continue_game_resolve(void);
void frontend_load_continue_game(struct GuiButton *gbtn);
int64_t frontend_save_continue_game(int64_t allow_lvnum_grow);
void frontend_continue_game_maintain(struct GuiButton *gbtn);
void frontend_main_menu_load_game_maintain(struct GuiButton *gbtn);
void frontend_mappacks_maintain(struct GuiButton *gbtn);
void frontend_main_menu_netservice_maintain(struct GuiButton *gbtn);
void frontend_main_menu_highscores_maintain(struct GuiButton *gbtn);
void frontend_main_menu_start_game_maintain(struct GuiButton *gbtn);
void frontend_main_menu_options_maintain(struct GuiButton *gbtn);
void frontend_main_menu_quit_maintain(struct GuiButton *gbtn);
// The width frontend_draw_button_icon's flexible chrome will actually
// render for febtn_idx's caption (fit to whole middle-tile steps -- see
// the definition in frontend.cpp for why raw/unquantized widths cause
// buttons to visually overlap or not resize). Reusable by any screen
// auto-sizing a frontend_draw_button_icon button to its caption text.
int64_t frontend_menu_button_natural_width(uint64_t febtn_idx, int64_t units_per_px);
void frontend_load_data_from_cd(void);
void frontend_load_data_reset(void);
void init_load_menu(struct GuiMenu *gmnu);
void init_save_menu(struct GuiMenu *gmnu);
void init_video_menu(struct GuiMenu *gmnu);
void init_audio_menu(struct GuiMenu *gmnu);
void frontend_init_options_menu(struct GuiMenu *gmnu);
TbBool frontend_is_player_allied(int64_t idx1, int64_t idx2);
void frontend_set_alliance(int64_t idx1, int64_t idx2);
char update_menu_fade_level(struct GuiMenu *gmnu);
void draw_menu_buttons(struct GuiMenu *gmnu);
MenuNumber create_menu(struct GuiMenu *mnu);
void do_button_release_actions(struct GuiButton *gbtn, unsigned char *, Gf_Btn_Callback callback);
void draw_gui(void);
void init_gui(void);
void reinit_all_menus(void);

void gui_set_autopilot(struct GuiButton *gbtn);

FrontendMenuState frontend_set_state(FrontendMenuState nstate);
FrontendMenuState get_startup_menu_state(void);
FrontendMenuState get_menu_state_when_back_from_substate(FrontendMenuState substate);
void frontend_input(void);
void frontend_update(int64_t *finish_menu);
int64_t frontend_draw(void);
void create_frontend_error_box(int64_t showTime, const char * text);
void try_restore_frontend_error_box(); // Restore error box if frontend state was switched

int64_t menu_is_active(int64_t idx);
TbBool a_menu_window_is_active(void);
int64_t game_is_busy_doing_gui(void);
void set_gui_visible(TbBool visible);
// Re-applies the engine window's viewport inset for the current
// ingame_gui_use_classic_hud() state -- see its own comment (frontend.cpp).
void refresh_engine_window_for_gui_style(void);
void toggle_gui(void);
void add_message(int64_t plyr_idx, char *msg);
uint64_t toggle_status_menu(int64_t visib);
TbBool toggle_first_person_menu(TbBool visible);
void toggle_gui_overlay_map(void);

void update_player_objectives(PlayerNumber plyr_idx);
void set_level_objective(PlayerNumber plyr_idx, const char *msg_text);
void display_objectives(PlayerNumber plyr_idx,MapSubtlCoord x,MapSubtlCoord y);
void display_objectives_with_icon(PlayerNumber plyr_idx,MapSubtlCoord x,MapSubtlCoord y, int64_t icon_idx);

int64_t toggle_main_cheat_menu(void);
TbBool close_main_cheat_menu(void);
int64_t toggle_instance_cheat_menu(void);
TbBool close_instance_cheat_menu(void);
TbBool open_creature_cheat_menu(void);
TbBool close_creature_cheat_menu(void);
TbBool toggle_creature_cheat_menu(void);
TbBool open_secondary_cheat_menu(void);
TbBool close_secondary_cheat_menu(void);
TbBool toggle_secondary_cheat_menu(void);
void initialise_tab_tags(MenuID menu_id);
void initialise_tab_tags_and_menu(MenuID menu_id);
void turn_off_roaming_menus(void);

void frontend_set_player_number(int64_t plr_num);
TbBool frontend_start_new_campaign(const char *cmpgn_fname);
void frontend_draw_product_version(struct GuiButton *gbtn);
TbBool should_use_delta_time_on_menu(void);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
