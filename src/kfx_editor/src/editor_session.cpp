/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_session.cpp
 *     The in-game level editor's session lifecycle (docs/refactor/editor/
 *     01-entry-and-editor-session.md): editor_open()/editor_close()/
 *     editor_is_active()/editor_frame(), the simulation_suspended
 *     force-and-lock, and the Esc editor menu (Save/Playtest stubbed --
 *     they land with the map serializer, phase 3). The actual tool
 *     palette (phase 2), map serializer (phase 3) and view/light/AP/script
 *     panels (phases 4-5) are not implemented yet -- see 00-overview.md's
 *     phase roadmap.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "kfx_editor.h"
#include "editor_toolbox.h"
#include "editor_journal.h"
#include "editor_menubar.h"
#include "editor_dialogs.h"
#include "editor_overlay.h"
#include "editor_script.h"
#include "editor_availability.h"
#include "editor_message_helper.h"
#include "editor_command_browser.h"
#include "editor_points.h"

#include "kfx_sim_state.h"
#include "player_data.h"
#include "packet_data.h"
#include "camera_data.h" // player->cameras[]/CamIV_*
#include "dungeon_data.h" // get_player_soul_container
#include "thing_data.h" // thing_is_invalid
#include "slab_data.h" // reveal_whole_map
#include "config_crtrmodel.h" // make_all_creatures_free
#include "config_trapdoor.h" // make_available_all_doors/traps
#include "config_terrain.h" // make_all_rooms_free/make_available_all_researchable_rooms
#include "config_magic.h" // make_all_powers_cost_free/make_available_all_researchable_powers
#include "config_keeperfx.h" // Ft_SkipHeartZoom
#include "config.h" // get_level_fgroup/prepare_file_fmtpath -- editor_level_save_dir()
#include "config_campaigns.h" // struct LevelInformation, get_level_info()
#include "bflib_fileio.h" // LbFileLength -- read_level_script_text()
#include "bflib_dernc.h" // LbFileLoadAt -- read_level_script_text()
#include "frontgui_widgets.h"
#include <imgui.h>
#include <cstring>
#include <string>
#include <vector>
#include "post_inc.h"

/******************************************************************************/
namespace {
    bool s_editor_active = false;
    bool s_editor_dirty = false;
    // docs/refactor/editor/phase3/02-slice3-dialogs-menubar.md -- set in
    // editor_open(), read by File > Save and the Save As dialog
    // (editor_dialogs.cpp) via editor_current_lvnum()/editor_current_save_dir().
    LevelNumber s_editor_lvnum = 0;
    char s_editor_save_dir[512] = "";
    // docs/refactor/editor/phase3/03-slice4-file-dialogs.md -- best-effort
    // read from get_level_info() in editor_open() (empty if this level has
    // no .lof yet); kept current by editor_set_current_level_name() after a
    // successful Save As.
    char s_editor_level_name[LINEMSG_SIZE] = "";
    // docs/refactor/editor/phase3/04-slice5-playtest-settings-overwrite.md
    // -- same best-effort-read/kept-current pattern as the name above, set
    // from the Level Settings dialog now instead.
    int s_editor_level_players = 1;
    bool s_editor_level_is_multiplayer = false;
    // docs/refactor/editor/05-script-and-level-settings.md §1 -- same
    // best-effort-read/kept-current pattern as level name above; DESCRIPTION
    // was already a recognized .lof keyword and an existing
    // LevelInformation::description field, just never wired to anything.
    char s_editor_level_description[LEVEL_DESCRIPTION_LEN] = "";
    // docs/refactor/editor/05-script-and-level-settings.md §0 -- read once
    // in editor_open() (direct disk read, independent of whatever
    // kfx_script's own live-parsed representation of the script looks
    // like), then carried through every subsequent Save/Save As this
    // session so editor_save_map() can write it back verbatim instead of
    // destroying it. Editable mid-session via editor_set_current_level_
    // script_text() (§4.1's script text editor Apply action).
    std::string s_editor_script_text;

    // Direct disk read of a level's own map%05lu.txt, independent of
    // MapContentReader (that class snapshots a *saved* map's files for
    // in-memory inspection/testing; here the goal is simpler -- read one
    // file into a string before the editor might overwrite it). Missing
    // file -> empty string, same "nothing to preserve" convention
    // MapContentReader::read_script() (kfx_sim) uses.
    std::string read_level_script_text(const char *dir, LevelNumber lvnum)
    {
        char path[600];
        snprintf(path, sizeof(path), "%s/map%05lu.txt", dir, (unsigned long)lvnum);
        long len = LbFileLength(path);
        if (len <= 0)
            return std::string();
        std::vector<char> buf((size_t)len);
        long got = LbFileLoadAt(path, buf.data());
        if (got != len)
            return std::string();
        return std::string(buf.data(), (size_t)len);
    }
    // Restored in editor_close() -- see editor_open()'s own comment on why
    // Ft_SkipHeartZoom is forced on for the session.
    bool s_prev_skip_heart_zoom = false;
    // Restored in editor_close()/editor_deactivate() -- see editor_open()'s
    // own comment on why the normal in-game GUI is force-hidden for the
    // session.
    bool s_prev_show_gui = true;

    // §3's planned "Preview motion" affordance -- toggled from the editor
    // menu below. While on, editor_frame() stops re-forcing
    // simulation_suspended, letting a real turn run (creature idles,
    // lava/particle FX, and -- per a live report -- possibly needed for a
    // just-placed creature to render in the 3D view at all: it exists in
    // the sim and shows on the minimap immediately, but stays invisible in
    // the 3D view, matching things elsewhere this session that turned out
    // to need at least one real update() pass after creation/change before
    // they render correctly (the PI_HeartZoom instance was the other
    // example). Not confirmed yet -- this is the way to test it live.
    bool s_preview_motion = false;

    // Internal cleanup, no PckA_QuitToMainMenu -- see its two call sites
    // (editor_close() sends that packet itself; editor_frame()'s safety net
    // below runs after the game session has *already* ended some other way,
    // where sending it again would be meaningless at best).
    void editor_deactivate(void)
    {
        if (!s_editor_active)
            return;
        kfx_sim_state.simulation_suspended = false;
        set_skip_heart_zoom_feature(s_prev_skip_heart_zoom);
        set_flag_value(kfx_sim_state.operation_flags, GOF_ShowGui, s_prev_show_gui);
        s_editor_active = false;
    }
}

// docs/refactor/editor/phase3/02-slice3-dialogs-menubar.md -- reproduces the
// pattern ftest_editor_save_reload.c had duplicated inline: get_level_fgroup()
// always resolves to FGrp_CmpgLvls regardless of lvnum, prepare_file_fmtpath()
// gives the full file path, and editor_save_map() itself only wants the
// *directory*, so this strips the filename back off. prepare_file_fmtpath()'s
// return is a reused buffer, copied into `out` right away, not held across
// calls. Exported (not anonymous-namespace) so a Catch2 test can exercise it
// directly, same convention as editor_journal_test_force_active() (editor_journal.h).
void editor_level_save_dir(LevelNumber lvnum, char *out, size_t out_size)
{
    short fgroup = get_level_fgroup(lvnum);
    char *p = prepare_file_fmtpath(fgroup, "map%05lu.slb", (unsigned long)lvnum);
    strncpy(out, p, out_size - 1);
    out[out_size - 1] = '\0';
    char *last_slash = strrchr(out, '/');
    if (last_slash != nullptr)
        *last_slash = '\0';
}

LevelNumber editor_current_lvnum(void)
{
    return s_editor_lvnum;
}

const char *editor_current_save_dir(void)
{
    return s_editor_save_dir;
}

const char *editor_current_level_name(void)
{
    return s_editor_level_name;
}

const char *editor_current_level_script_text(void)
{
    return s_editor_script_text.c_str();
}

void editor_set_current_level_script_text(const char *script_text)
{
    s_editor_script_text = (script_text != nullptr) ? script_text : "";
}

int editor_current_level_players(void)
{
    return s_editor_level_players;
}

const char *editor_current_level_description(void)
{
    return s_editor_level_description;
}

TbBool editor_current_level_is_multiplayer(void)
{
    return s_editor_level_is_multiplayer;
}

void editor_set_current_lvnum_and_dir(LevelNumber lvnum, const char *dir)
{
    s_editor_lvnum = lvnum;
    strncpy(s_editor_save_dir, dir, sizeof(s_editor_save_dir) - 1);
    s_editor_save_dir[sizeof(s_editor_save_dir) - 1] = '\0';
}

void editor_set_current_level_name(const char *name)
{
    strncpy(s_editor_level_name, (name != nullptr) ? name : "", sizeof(s_editor_level_name) - 1);
    s_editor_level_name[sizeof(s_editor_level_name) - 1] = '\0';
}

void editor_set_current_level_players(int players)
{
    s_editor_level_players = players;
}

void editor_set_current_level_is_multiplayer(TbBool is_multiplayer)
{
    s_editor_level_is_multiplayer = is_multiplayer != 0;
}

void editor_set_current_level_description(const char *description)
{
    strncpy(s_editor_level_description, (description != nullptr) ? description : "", sizeof(s_editor_level_description) - 1);
    s_editor_level_description[sizeof(s_editor_level_description) - 1] = '\0';
}

TbBool editor_is_dirty(void)
{
    return s_editor_dirty;
}

// Internal cross-file use only (same library, not part of kfx_editor.h's
// outside-caller surface) -- lets editor_journal.cpp's record_placement/
// record_rect_terrain mark the session dirty on every forward edit.
void editor_mark_dirty(void)
{
    s_editor_dirty = true;
}

void editor_clear_dirty(void)
{
    s_editor_dirty = false;
}

// §3's "Preview motion" affordance -- moved from the Esc-hub into the new
// View menu (editor_menubar.cpp) this slice; see s_preview_motion's own
// comment for what it actually does.
TbBool editor_preview_motion(void)
{
    return s_preview_motion;
}

void editor_set_preview_motion(TbBool on)
{
    s_preview_motion = on != 0;
}

void editor_open(LevelNumber lvnum, TbBool is_new)
{
    SYNCDBG(0, "Opening editor session for level %lu (new=%d)", (unsigned long)lvnum, (int)is_new);
    s_editor_active = true;
    // A freshly created blank map has nothing saved yet -- starts dirty.
    s_editor_dirty = is_new != 0;
    s_preview_motion = false;
    s_editor_lvnum = lvnum;
    editor_level_save_dir(lvnum, s_editor_save_dir, sizeof(s_editor_save_dir));
    // docs/refactor/editor/phase3/03-slice4-file-dialogs.md -- best-effort:
    // get_level_info() only has an entry once this level's own .lof has
    // been scanned in (editor_save_map()'s own find_and_load_lof_files()
    // call, or a real level's shipped .lof) -- a genuinely new/unsaved
    // level has none yet, which is exactly when starting from an empty
    // name is correct anyway.
    {
        struct LevelInformation *lvinfo = get_level_info(lvnum);
        editor_set_current_level_name((lvinfo != NULL) ? lvinfo->name : "");
        editor_set_current_level_players((lvinfo != NULL) ? (int)lvinfo->players : 1);
        editor_set_current_level_is_multiplayer((lvinfo != NULL) && ((lvinfo->level_type & LvKind_IsMulti) != 0));
        editor_set_current_level_description((lvinfo != NULL) ? lvinfo->description : "");
    }
    // docs/refactor/editor/05-script-and-level-settings.md §0 -- a genuinely
    // new map has no .txt yet (read_level_script_text() correctly returns
    // empty), which is exactly when editor_save_map()'s own empty-script
    // stub is the right thing to write.
    s_editor_script_text = read_level_script_text(s_editor_save_dir, lvnum);
    // §4 -- a previous session's journal entries reference thing indices
    // that mean nothing (or worse, something else entirely, once slots are
    // reused) in this one.
    editor_journal_reset();
    editor_points_reset();

    // §3, revised after live testing: simulation_suspended alone, *not*
    // GOF_Paused, is what freezes the sim now. game_session_loop.cpp's
    // per-turn update() gate checks both flags
    // (!GOF_Paused && !simulation_suspended), so simulation_suspended on
    // its own already fully freezes creature AI/economy/game-turn advance
    // -- but GOF_Paused turned out to do far more than that:
    // get_packet_control_mouse_clicks() (front_input.c) hard-returns
    // without generating *any* PCtr_LBtn*/RBtn* packet control while
    // GOF_Paused is set, which is what actually turns a world click into
    // a PckA_CheatPlaceTerrain (etc.) packet in the first place. Found
    // live: with GOF_Paused forced, every toolbox tool worked (packets
    // like PckA_SetPlyrState/PckA_CheatSwitchTerrain went through fine --
    // process_packets() itself has no pause gate) but clicking in the
    // dungeon view to actually place/build never did anything, because
    // the click was never turned into a packet at all. GOF_Paused is
    // simply the wrong tool here: it means "no player interaction",
    // not just "no simulation ticks", and an editor session needs exactly
    // the opposite combination. Not setting it at all now.
    kfx_sim_state.simulation_suspended = true;

    // Found live: with simulation_suspended now also gating the per-turn
    // update (game_session_loop.cpp), the normal "PI_HeartZoom" intro --
    // set_player_instance(player, PI_HeartZoom, 0) in game_loop(), right
    // after this function returns and wait_at_frontend() hands control
    // back -- can never finish; it needs several real turns of update() to
    // fly the camera to the Dungeon Heart, and none run once suspended.
    // Result: the session never leaves the intro, fully frozen. This
    // sequence is meaningless for an editor session anyway (no player-
    // camera cutscene wanted while editing) -- skip it outright via the
    // same feature flag -skipheartzoom uses, rather than trying to let a
    // few turns through. Restored in editor_close().
    s_prev_skip_heart_zoom = get_skip_heart_zoom_feature();
    set_skip_heart_zoom_feature(true);

    // User feedback: rendering both the normal in-game GUI (sidebar,
    // minimap, tab panel) and the editor's own File/Edit/View menu bar +
    // toolbox offers no benefit -- an editor session has no use for the
    // gameplay HUD at all. Force-hidden regardless of the player's own
    // Options > Graphics GUI preference (the same flag Tab/Ctrl+Tab
    // toggles during normal play, ingame_panel_frame()'s own early-return
    // gate) -- restored on close so it doesn't leak into a later normal
    // play session.
    s_prev_show_gui = flag_is_set(kfx_sim_state.operation_flags, GOF_ShowGui);
    clear_flag(kfx_sim_state.operation_flags, GOF_ShowGui);

    // No "enable cheats" step needed (docs/refactor/editor/
    // 07-investigation-findings.md F10) -- the underlying PckA_Cheat*
    // handlers just work. Called directly rather than via
    // set_players_packet_action() + PckA_Cheat* -- this is one-time editor
    // session setup, not a player action the packet/replay system needs to
    // see (docs/refactor/editor/07-investigation-findings.md D2's "single-
    // player direct-call exception"), and a direct call has no dependency
    // on when process_packets() next runs relative to the first rendered
    // frame. Reveal the whole map and put the editor player in a
    // build-anything state, same effects the cheat menu's "reveal map"/
    // "everything free" buttons already produce.
    struct PlayerInfo *player = get_my_player();
    reveal_whole_map(player);
    make_all_creatures_free();
    make_all_rooms_free();
    make_all_powers_cost_free();
    make_available_all_researchable_rooms(player->id_number);
    make_available_all_researchable_powers(player->id_number);
    make_available_all_doors(player->id_number);
    make_available_all_traps(player->id_number);

    // Found live (screenshot: solid black viewport that reveal-map alone
    // didn't fix): init_player_cameras() (engine_camera.c, called during
    // the normal init_players_local_game() path every session -- editor or
    // not) points the isometric/front-view cameras at
    // get_player_soul_container(player->id_number)'s position -- the
    // Dungeon Heart. On a heart-less New Map that lookup returns the
    // invalid-thing sentinel, so both cameras end up sitting at world
    // (0,0,z) -- off in the map's corner, not over any of the map at all.
    // Every real level has a Heart before this ever runs, so nothing else
    // in the engine has ever needed a fallback here. Only override when
    // there really is no heart -- opening an *existing* map (Open Map) has
    // a real Dungeon Heart and a correctly-centered camera already; forcing
    // map-center here unconditionally would wrongly override that.
    if (thing_is_invalid(get_player_soul_container(player->id_number)))
    {
        MapCoord center_x = subtile_coord_center(kfx_sim_state.map_subtiles_x / 2);
        MapCoord center_y = subtile_coord_center(kfx_sim_state.map_subtiles_y / 2);
        player->cameras[CamIV_Isometric].mappos.x.val = center_x;
        player->cameras[CamIV_Isometric].mappos.y.val = center_y;
        player->cameras[CamIV_FrontView].mappos.x.val = center_x;
        player->cameras[CamIV_FrontView].mappos.y.val = center_y;
    }
}

void editor_close(void)
{
    if (!s_editor_active)
        return;
    struct PlayerInfo *player = get_my_player();
    set_players_packet_action(player, PckA_QuitToMainMenu, 0, 0, 0, 0);
    editor_deactivate();
}

TbBool editor_is_active(void)
{
    return s_editor_active;
}

void editor_frame(void)
{
    if (!s_editor_active)
        return;

    // Safety net: found live -- quitting via the *normal* in-game pause
    // menu (not our own "Exit to Main Menu") left the toolbox rendering
    // over the main menu forever, because nothing but that one button ever
    // called editor_close(). keeper_gameplay_loop() (game_session_loop.cpp)
    // unconditionally resets game_kind to GKind_Unset once its loop ends,
    // by every exit path (quit, level lost/won, ...) -- treat that as "the
    // session is over" regardless of how, and clean up here instead of
    // depending on catching every possible exit route individually.
    if (kfx_sim_state.game_kind != GKind_LocalGame)
    {
        editor_deactivate();
        return;
    }

    // §3, revised: re-assert simulation_suspended every frame (not
    // GOF_Paused -- see editor_open()'s comment on why) so nothing
    // (a stray PckA_TogglePause, e.g.) can lift the freeze while active --
    // unless Preview Motion is on, in which case leave it lifted so a real
    // turn can run.
    kfx_sim_state.simulation_suspended = !s_preview_motion;

    // Was an F10 keypress originally (front_input.c's
    // get_options_menu_inputs(), the normal in-game pause menu, reads raw
    // lbKeyOn[]/is_key_pressed() independent of ImGui's own key tracking, so
    // both this menu and the normal GMnu_OPTIONS pause launcher opened at
    // once on the same Escape press), then a toolbox "Menu" button opening
    // an Esc-equivalent hub modal (editor_open_menu()); retired in
    // docs/refactor/editor/phase3/02-slice3-dialogs-menubar.md once every
    // item that hub carried (Save/Save As/Exit -> File, Preview Motion ->
    // View) had a real home in the new menu bar instead.

    // docs/refactor/editor/02-editing-toolbox.md -- shown whenever the
    // session is active, same as the original editor's always-visible
    // toolbox.
    editor_menubar_frame();
    editor_toolbox_frame();
    editor_journal_frame();
    editor_dialogs_frame();
    editor_overlay_frame();
    editor_script_frame();
    editor_availability_frame();
    editor_message_helper_frame();
    editor_command_browser_frame();
}

void editor_notify_playtest_end(void)
{
    // docs/refactor/editor/phase3/04-slice5-playtest-settings-overwrite.md
    // -- Playtest itself is implemented (editor_dialogs.cpp), but a
    // playtest session quitting/winning/losing still just lands wherever a
    // normal single-player game would (main menu, level stats, ...), not
    // back in the editor -- that needs a "return to editor" ribbon/session
    // hand-off this hook would drive, deliberately not built this slice
    // (kept a no-op; see that doc's own scope note on why).
}
/******************************************************************************/
