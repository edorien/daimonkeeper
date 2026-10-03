#include "pre_inc.h"
#include "frontgui_ingame.h"

#include "frontgui_widgets.h"
#include "frontgui_deferred.h" // FeDeferredQueue
#include "frontgui_style.h"
#include "frontgui_sprite_tex.h" // FeSpriteButton -- classic GUI sprite icons
#include "frontgui_ingame_debug.h" // Phase 2: debug / script overlays
#include "frontgui_ingame_boxmenu.h" // Phase 2: cheat / service box menus
#include "frontgui_ingame_text.h" // Phase 3: Paused caption + onscreen banner
#include "frontgui_ingame_battle.h" // Phase 3: battle-participants box
#include "frontgui_ingame_panel.h" // Phase 4: the sidebar frame (also drives Phase 5 tab bodies)
#include "frontgui_ingame_parchment.h" // Phase 7: the parchment / overhead map
#include "frontgui_hud_layout.h" // HudRegion_Gold, hud_layout_current (GUI_POSITION Bottom)
#include "sprites.h" // GBS_options_button_*

#include "globals.h"
#include "bflib_guibtns.h" // struct GuiMenu
#include "gui_frontmenu.h" // active_menus, menu_stack, no_of_active_menus, turn_on/off_menu
#include "gui_soundmsgs.h" // output_message, SMsg_GameSaved
#include "frontgui_screens.h" // frontgui_options_frame(in_game)
#include "frontend.h" // menu_id_to_number, create_error_box
#include "config_strings.h" // GUIStr_*, CpgStr_PowerKind1
#include "frontmenu_specials.h" // choose_hold_audience, choose_armageddon
#include "player_data.h" // get_my_player
#include "packet_data.h" // set_players_packet_action, PckA_*
#include "kfx_sim_state.h" // kfx_sim_state.game_kind, GOF_Paused
#include "game_saves.h" // save_game_catalogue, load_game, save_game, fill_game_catalogue_slot
#include "config_keeperfx.h" // keeperfx_ui_config.hud_position -- GUI_POSITION
#include "packets.h"

#include "local_state.h"
#include "post_inc.h"

#include <cstdio> // snprintf
#include <cstring> // strncpy
#include <imgui.h> // ImGui::SameLine / CloseCurrentPopup (also via frontgui_widgets.h)

namespace {

// ---------------------------------------------------------------------------
// Deferred actions -- turn_off_menu() / packet sends / anything heavy must
// never run from inside an active ImGui window's Begin()/End() scope.
// Requested from a widget callback, applied at the very top of the next
// ingame_imgui_frame(), before any window here is opened. (FeDeferredQueue,
// docs/refactor/ingame-gui/10-maintainability-refactors.md §3.)
// ---------------------------------------------------------------------------
FeDeferredQueue s_deferred;

void request_deferred(void (*fn)(void)) { s_deferred.push(fn); }
void apply_deferred(void)               { s_deferred.drain(); }

// ---------------------------------------------------------------------------
// Once-per-game-turn HUD view-model cache (§3.1). The in-game GUI's derived
// numbers only change on a game-turn boundary, so they are recomputed once per
// turn and the per-frame ImGui submission just lays out the cache -- keeping
// the heavy Dungeon / kfx_sim_state reads off the render path.
//
// Phase 0 scaffold: quit_menu has no dynamic content, so the body is empty.
// Phase 1+ fill IngameHudCache and rebuild_cache().
// ---------------------------------------------------------------------------
GameTurn s_cache_turn = 0;
bool s_cache_valid = false;

void rebuild_cache(void)
{
    // (nothing to compute yet)
}

void refresh_cache_if_stale(void)
{
    const GameTurn turn = get_gameturn();
    if (s_cache_valid && s_cache_turn == turn)
        return;
    s_cache_turn = turn;
    s_cache_valid = true;
    rebuild_cache();
}

// ---------------------------------------------------------------------------

bool game_is_running(void)
{
    return kfx_sim_state.game_kind == GKind_LocalGame
        || kfx_sim_state.game_kind == GKind_MultiGame;
}

// The pause-menu family (the launcher + the screens it opens). Reachable
// via GUI_POSITION=CLASSIC's sidebar just like the modern one -- unlike the
// rest of menu_is_migrated()'s entries (the sidebar frame/tabs themselves,
// and the objective/battle/confirm boxes), classic-sidebar players still get
// the modern ImGui Options/Load/Save/Quit screens rather than the retired
// sprite ones.
bool menu_is_options_family(MenuID menu_id)
{
    switch (menu_id)
    {
        case GMnu_OPTIONS:
        case GMnu_QUIT:
        case GMnu_LOAD:
        case GMnu_SAVE:
            return true;
        default:
            return false;
    }
}

// The migrated-menu registry. Grows one entry per phase.
bool menu_is_migrated(MenuID menu_id)
{
    switch (menu_id)
    {
        case GMnu_QUIT:      return true; // Phase 0 proof menu
        case GMnu_OPTIONS:   return true; // Phase 1a: the pause-menu launcher
        case GMnu_LOAD:      return true; // Phase 1b: save-slot lists
        case GMnu_SAVE:      return true; // Phase 1b
        case GMnu_TEXT_INFO: return true; // Phase 3: objective / event box
        case GMnu_BATTLE:    return true; // Phase 3: battle-participants box
        case GMnu_HOLD_AUDIENCE: return true; // dungeon-special confirm (shares the quit modal)
        case GMnu_ARMAGEDDON:    return true;
        case GMnu_MAIN:      return true; // Phase 4: the sidebar frame + its GMnu_MAIN buttons
        case GMnu_ROOM:      return true; // Phase 5: room build grid
        case GMnu_SPELL:     return true; // Phase 5: power grid
        case GMnu_TRAP:      return true; // Phase 5: manufacture grid
        case GMnu_CREATURE:  return true; // Phase 5: creature activity list
        case GMnu_QUERY:     return true; // Phase 5: query / information panel
        case GMnu_CREATURE_QUERY1:        // Phase 6: creature query / possession detail
        case GMnu_CREATURE_QUERY2:
        case GMnu_CREATURE_QUERY3:
        case GMnu_CREATURE_QUERY4: return true;
        case GMnu_SPELL_LOST: return true; // Phase 6: lost-keeper reduced panel
        default:             return false;
    }
}

// While the ImGui Options window is expanded from the launcher. File-scope
// so the launcher's "Options" button and the window's "Back" button toggle
// the same flag; reset whenever GMnu_OPTIONS is no longer up.
bool s_settings_expanded = false;

// ---------------------------------------------------------------------------
// Shared yes/no confirm modal -- the "Really quit?" (GMnu_QUIT, the Phase 0
// proof menu) and the dungeon-special confirms (GMnu_HOLD_AUDIENCE /
// GMnu_ARMAGEDDON) are structurally identical (title + smd_no / smd_yes),
// so they share this. Both buttons close the menu; "Yes" also fires the
// per-menu action. Deferred (turn_off_menu / packet send must not run
// inside FeBeginModal/FeEndModal).
// ---------------------------------------------------------------------------
MenuID s_confirm_menu = GMnu_QUIT;
void (*s_confirm_yes)(void) = nullptr;

void apply_confirm_close(void) { turn_off_menu(s_confirm_menu); }
void apply_confirm_yes(void)
{
    turn_off_menu(s_confirm_menu);
    if (s_confirm_yes != nullptr) { void (*fn)(void) = s_confirm_yes; s_confirm_yes = nullptr; fn(); }
}

void yes_quit_to_main_menu(void)
{
    // Same packet the legacy gui_quit_game() sends (frontend.cpp).
    set_players_packet_action(get_my_player(), PckA_QuitToMainMenu, 0, 0, 0, 0);
}
void yes_hold_audience(void) { choose_hold_audience(nullptr); } // PckA_HoldAudience
void yes_armageddon(void)    { choose_armageddon(nullptr); }    // PckA_UsePwrArmageddon

void confirm_modal_frame(MenuID menu_id, const char *id_tag, const char *title,
                         const char *subheading, void (*on_yes)(void))
{
    // "label###id": ImGui shows the part before ###, keys the popup on the
    // part after -- title bar reads the localized text, id stays stable.
    char modal_id[96];
    snprintf(modal_id, sizeof(modal_id), "%s###%s", title, id_tag);

    FeOpenModal(modal_id);
    const bool open = FeBeginModal(modal_id);
    if (open)
    {
        if (subheading != nullptr && subheading[0] != '\0')
        {
            FeSubheading(subheading);
            FeSeparator();
        }
        // Icons only (the classic confirm's smd_no / smd_yes sprites);
        // legacy button order is No then Yes. Centre the pair.
        const double ih = ImGui::GetFontSize() * 2.0;
        const double row_w = FeSpriteButtonWidth(GBS_options_button_smd_no, nullptr, ih)
                          + FeSpriteButtonWidth(GBS_options_button_smd_yes, nullptr, ih)
                          + ImGui::GetStyle().ItemSpacing.x;
        const double avail = ImGui::GetContentRegionAvail().x;
        if (avail > row_w)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - row_w) * 0.5);
        if (FeSpriteButton(get_string(GUIStr_ConfirmNo), GBS_options_button_smd_no, nullptr, ih))
        {
            s_confirm_menu = menu_id;
            request_deferred(apply_confirm_close);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (FeSpriteButton(get_string(GUIStr_ConfirmYes), GBS_options_button_smd_yes, nullptr, ih))
        {
            s_confirm_menu = menu_id;
            s_confirm_yes = on_yes;
            request_deferred(apply_confirm_yes);
            ImGui::CloseCurrentPopup();
        }
    }
    FeEndModal(open);
}

void quitmenu_frame(void)
{
    confirm_modal_frame(GMnu_QUIT, "IngameQuit", get_string(GUIStr_MnuQuit),
                        get_string(GUIStr_ConfirmYouSure), yes_quit_to_main_menu);
}

// ---------------------------------------------------------------------------
// GMnu_LOAD / GMnu_SAVE -- the save-slot lists (Phase 1b). Opened from the
// launcher; each is a monopoly menu, so ingame_imgui_modal_active()'s
// topmost-monopoly rule hands input to the ImGui window while it is stacked
// above the launcher. init_{load,save}_menu (the classic create_cb, still
// run by turn_on_menu -> create_menu) sends PckA_UpdatePause(1,1) on open
// and load_game_save_catalogue() -- so the catalogue is fresh and the game
// is paused by the time the first frame draws. On close the pause is
// undone the same way the classic Esc path (turn_off_all_window_menus) and
// gui_save_game do.
// ---------------------------------------------------------------------------

// Captured just before turn_on_menu(), for the load-cancel path to restore
// to (init_load_menu doesn't record a prior state the way init_save_menu
// does; the save paths use player->paused_state_restore instead).
bool s_saveload_prev_paused = false;

// Save: which catalogue row the player picked, and the editable name for it.
int64_t s_save_sel_slot = -1;
char s_save_name[SAVE_TEXTNAME_LEN] = {0};

// Deferred-slot handoff (request_deferred only carries a void(*)(void)).
int64_t s_pending_load_slot = -1;
int64_t s_pending_save_slot = -1;
char s_pending_save_name[SAVE_TEXTNAME_LEN] = {0};

void do_open_save_menu(void)
{
    s_saveload_prev_paused = (kfx_sim_state.operation_flags & GOF_Paused) != 0;
    s_save_sel_slot = -1;
    s_save_name[0] = '\0';
    turn_on_menu(GMnu_SAVE);
}
void do_open_load_menu(void)
{
    s_saveload_prev_paused = (kfx_sim_state.operation_flags & GOF_Paused) != 0;
    turn_on_menu(GMnu_LOAD);
}
void do_open_quit_menu(void) { turn_on_menu(GMnu_QUIT); }

void do_cancel_load_menu(void)
{
    turn_off_menu(GMnu_LOAD);
    // Mirror the classic Esc close (turn_off_all_window_menus ->
    // set_packet_pause_toggle) but as an explicit set to the pre-open
    // state so a game that was already paused when Load opened stays paused.
    set_players_packet_action(get_my_player(), PckA_UpdatePause,
                              s_saveload_prev_paused ? 1 : 0, 0, 0, 0);
}
void do_cancel_save_menu(void)
{
    turn_off_menu(GMnu_SAVE);
    set_players_packet_action(get_my_player(), PckA_UpdatePause,
                              s_saveload_prev_paused ? 1 : 0, 0, 0, 0);
}

void apply_pending_load(void)
{
    const int64_t slot = s_pending_load_slot;
    s_pending_load_slot = -1;
    turn_off_menu(GMnu_LOAD);
    if (!load_game(slot))
    {
        ERRORLOG("Loading game %" PRId64 " failed", (int64_t)(slot));
        // A save from another layout is refused before anything changes, so
        // the current game carries on; say why (refactor pass 2, S09).
        if (last_save_was_refused())
            create_error_box_text("This save is from a different KeeperFX build and can't be loaded.");
    }
    // load_game() rebuilds the level; no pause packet needed (and the old
    // player state is gone). On failure the classic path quits; leave that
    // to the existing error handling rather than duplicating it here.
}

void apply_pending_save(void)
{
    const int64_t slot = s_pending_save_slot;
    s_pending_save_slot = -1;
    fill_game_catalogue_slot(slot, s_pending_save_name);
    if (save_game(slot))
        output_message(SMsg_GameSaved, 0);
    else
        create_error_box(GUIStr_ErrorSaving); // classic GMnu_ERROR_BOX (not migrated yet)
    turn_off_menu(GMnu_SAVE);
    // Same as gui_save_game(): restore whatever pause state init_save_menu
    // recorded before it forced the pause.
    set_players_packet_action(get_my_player(), PckA_UpdatePause,
                              local_state.paused_state_restore ? 1 : 0, 0, 0, 0);
}

void loadmenu_frame(void)
{
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5, io.DisplaySize.y * 0.5),
                            ImGuiCond_Always, ImVec2(0.5, 0.5));
    ImGui::Begin("##IngameLoad", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings
                 | ImGuiWindowFlags_AlwaysAutoResize);

    FeHeading(get_string(GUIStr_MnuLoad));
    FeSeparator();

    const int64_t count = (save_game_catalogue != nullptr) ? save_game_catalogue_count : 0;
    bool any = false;
    const bool open = FeBeginListBox("##ingame_load_list",
                                     ImVec2(io.DisplaySize.x * 0.30, io.DisplaySize.y * 0.38));
    if (open)
    {
        for (int64_t i = 0; i < count; i++)
        {
            const struct CatalogueEntry *ce = &save_game_catalogue[i];
            if ((ce->flags & CEF_InUse) == 0)
                continue;
            any = true;
            ImGui::PushID((int64_t)i); // two saves can share a display name
            if ((ce->flags & CEF_OtherVersion) != 0)
            {
                // Can't be loaded by this build (S09): listed, but not clickable.
                char label[SAVE_TEXTNAME_LEN + 32];
                std::snprintf(label, sizeof(label), "%s (other version)", ce->textname);
                ImGui::BeginDisabled();
                FeListRow(label, false);
                ImGui::EndDisabled();
            }
            else if (FeListRow(ce->textname, false))
            {
                s_pending_load_slot = i;
                request_deferred(apply_pending_load);
            }
            ImGui::PopID();
        }
        if (!any)
            FeBodyText("No saved games.");
    }
    FeEndListBox(open);

    FeSeparator();
    if (FeButton(get_string(GUIStr_Cancel)))
        request_deferred(do_cancel_load_menu);

    ImGui::End();
}

void savemenu_frame(void)
{
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5, io.DisplaySize.y * 0.5),
                            ImGuiCond_Always, ImVec2(0.5, 0.5));
    ImGui::Begin("##IngameSave", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings
                 | ImGuiWindowFlags_AlwaysAutoResize);

    FeHeading(get_string(GUIStr_MnuSave));
    FeSeparator();

    // The catalogue already carries every existing save plus exactly one
    // trailing free slot (game_saves.h) -- so this list is "N saves to
    // overwrite + one new slot" with no fixed slot count to manage.
    const int64_t count = (save_game_catalogue != nullptr) ? save_game_catalogue_count : 0;
    const bool open = FeBeginListBox("##ingame_save_list",
                                     ImVec2(io.DisplaySize.x * 0.30, io.DisplaySize.y * 0.34));
    if (open)
    {
        for (int64_t i = 0; i < count; i++)
        {
            const struct CatalogueEntry *ce = &save_game_catalogue[i];
            const bool in_use = (ce->flags & CEF_InUse) != 0;
            char other_label[SAVE_TEXTNAME_LEN + 32];
            const char *row = in_use ? ce->textname : get_string(GUIStr_SlotUnused);
            if (in_use && ((ce->flags & CEF_OtherVersion) != 0))
            {
                // Still overwritable; the label says why it can't be loaded.
                std::snprintf(other_label, sizeof(other_label), "%s (other version)", ce->textname);
                row = other_label;
            }
            ImGui::PushID((int64_t)i); // every free slot shows the same "unused" label
            if (FeListRow(row, i == s_save_sel_slot))
            {
                s_save_sel_slot = i;
                std::snprintf(s_save_name, sizeof(s_save_name), "%s", in_use ? ce->textname : "");
            }
            ImGui::PopID();
        }
    }
    FeEndListBox(open);

    FeSeparator();
    if (s_save_sel_slot >= 0)
    {
        FeTextInput("##ingame_save_name", s_save_name, sizeof(s_save_name));
        ImGui::SameLine();
        const bool named = s_save_name[0] != '\0';
        if (!named) ImGui::BeginDisabled();
        if (FeButton(get_string(GUIStr_MnuSave)))
        {
            s_pending_save_slot = s_save_sel_slot;
            std::snprintf(s_pending_save_name, sizeof(s_pending_save_name), "%s", s_save_name);
            request_deferred(apply_pending_save);
        }
        if (!named) ImGui::EndDisabled();
    }

    if (FeButton(get_string(GUIStr_Cancel)))
        request_deferred(do_cancel_save_menu);

    ImGui::End();
}

// ---------------------------------------------------------------------------
// GMnu_TEXT_INFO -- the objective / event box, bottom-centre (Phase 3).
// Not a monopoly menu: it must not steal world clicks, only its own
// buttons (get_gui_inputs sets busy_doing_gui when ImGui has the mouse --
// ingame_imgui_wants_mouse()). The text is sim-authoritative
// (kfx_sim_state.evntbox_scroll_window.text, written from the script
// objective); ImGui owns its own scroll position (the legacy line-unit
// .start_y isn't synced -- a cosmetic per-client value).
// ---------------------------------------------------------------------------
void do_close_objective(void) { gui_close_objective(nullptr); }   // turn_off_event_box_if_necessary
void do_zoom_to_event(void)   { gui_go_to_event(nullptr); }       // move_local_camera_to_position

// The classic event box layout: a column of icon buttons (zoom, close) at
// the left, the scrolling objective text filling the rest. Buttons use the
// original message-box panel sprites (GPS_message_*), falling back to text
// until their textures are ready.
void event_box_button_column(double icon_h)
{
    // The *_std (idle) sprite frame -- gui_area_new_normal_button() draws
    // sprite_idx+1 when not pressed; the *_act frame is a bright
    // near-white "pressed" highlight (found live: "icons are all white").
    ImGui::BeginGroup();
    // gui_go_to_event() (frontend.cpp) zooms to event->mappos_x/y with no
    // validity check at all -- (0,0) is the documented "no target" state
    // display_objectives_with_icon() (frontend.cpp) leaves an Objective
    // event in when it's raised with no x/y (the common case: a plain
    // narrative objective, not tied to a map location), not a real corner
    // of the map. Zooming there put the camera at the map's literal
    // origin (live-tested: "clicking on objective event marker, the view
    // moves to the bottom of the map") -- hide the button instead of
    // zooming somewhere meaningless.
    const struct Event *ev = &kfx_sim_state.event[my_visible_event_idx];
    const bool has_target = (ev->mappos_x != 0) || (ev->mappos_y != 0);
    if (has_target && FeGuiPanelIconButton("##evt_zoom", GPS_message_message_btn_show_std,
                             get_string(GUIStr_ZoomToArea), icon_h))
        request_deferred(do_zoom_to_event);
    if (FeGuiPanelIconButton("##evt_close", GPS_message_message_btn_accept_std,
                             get_string(GUIStr_CloseWindow), icon_h))
        request_deferred(do_close_objective);
    ImGui::EndGroup();
}

void textinfo_frame(void)
{
    ImGuiIO &io = ImGui::GetIO();
    // GUI_POSITION Bottom (docs/refactor/ingame-gui/11-horizontal-layout.md):
    // this box is the same kind of thing region C's message queue is (a
    // notification with text) -- it belongs *in* region C, not floating
    // full-width above the whole strip (that read as disconnected from the
    // HUD, live-tested). hud_layout_frame() already ran this frame (from
    // ingame_panel_frame(), which runs before this switch -- see
    // ingame_imgui_frame()). Can overlap the message queue if both are
    // showing at once -- rare (an open event box while a taunt scrolls in),
    // not handled specially yet.
    if (keeperfx_ui_config.hud_position == 3) // HudPos_Bottom
    {
        const HudRect &r = hud_layout_current().region[HudRegion_Messages];
        ImGui::SetNextWindowPos(ImVec2(r.x0, r.y0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(r.w(), r.h()), ImGuiCond_Always);
    }
    else
    {
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5, io.DisplaySize.y - 10.0),
                                ImGuiCond_Always, ImVec2(0.5, 1.0));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.42, io.DisplaySize.y * 0.17), ImGuiCond_Always);
    }
    ImGui::Begin("##IngameEventBox", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

    event_box_button_column(io.DisplaySize.y * 0.045);
    ImGui::SameLine();
    if (FeBeginScrollArea("##evtbox_text", ImVec2(0, 0)))
        FeBodyText(kfx_sim_state.evntbox_scroll_window.text);
    FeEndScrollArea();

    ImGui::End();
}

void optionsmenu_frame(void)
{
    ImGuiIO &io = ImGui::GetIO();

    if (s_settings_expanded)
    {
        // The shared settings window, in its in-game form (restart-class
        // rows disabled, Define Keys disabled, "Back" instead of "Return
        // to Main" -> ingame_options_back_to_launcher() clears the flag).
        frontgui_options_frame_ingame();
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5, io.DisplaySize.y * 0.5),
                            ImGuiCond_Always, ImVec2(0.5, 0.5));
    // AlwaysAutoResize + no min-width constraint -> the window hugs the
    // widest row (found live: a percent-of-screen min width left a wide
    // band of dead space to the right of the labels).
    ImGui::Begin("##IngameOptionsLauncher", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings
                 | ImGuiWindowFlags_AlwaysAutoResize);

    // No heading -- four self-evident rows; "Options" as both a title and a
    // button reads oddly. One option per row, icon first then the label
    // (the user's chosen layout); icons are the classic options_menu
    // button sprites.
    const double row_icon_h = ImGui::GetFontSize() * 2.0;
    if (FeSpriteButton("##launch_load", GBS_options_button_load,
                       get_string(GUIStr_MnuLoad), row_icon_h))
        request_deferred(do_open_load_menu);
    if (FeSpriteButton("##launch_save", GBS_options_button_save,
                       get_string(GUIStr_MnuSave), row_icon_h))
        request_deferred(do_open_save_menu);
    if (FeSpriteButton("##launch_options", GBS_options_button_graphc,
                       get_string(GUIStr_MnuOptions), row_icon_h))
        s_settings_expanded = true;
    if (FeSpriteButton("##launch_quit", GBS_options_button_exit,
                       get_string(GUIStr_MnuQuit), row_icon_h))
        request_deferred(do_open_quit_menu);

    ImGui::End();
}

// ---------------------------------------------------------------------------

// True when this active menu should be handled by the ImGui path this
// frame. visual_state 3 is "fading out" (turn_off_menu) -- stop submitting
// its ImGui window at once rather than for the several frames the legacy
// fade animation would take.
bool menu_slot_is_migrated_and_on(const struct GuiMenu *gmnu)
{
    return gmnu->visual_state != 0 && gmnu->visual_state != 3
        && gmnu->is_turned_on && menu_is_migrated(gmnu->ident);
}

} // namespace

// Called from frontgui_options_frame()'s in-game "Back" button -- collapse
// the settings window back to the 4-button launcher.
extern "C" void ingame_options_back_to_launcher(void)
{
    s_settings_expanded = false;
}

extern "C" TbBool ingame_imgui_menu_active(MenuID menu_id)
{
    // GUI_POSITION=CLASSIC only reverts the sidebar and the rest of
    // menu_is_migrated()'s registry -- the options/quit/load/save family
    // stays on the ImGui path regardless (see menu_is_options_family()).
    const bool classic_gated = ingame_gui_use_classic_hud() && !menu_is_options_family(menu_id);
    return (!classic_gated && game_is_running() && menu_is_migrated(menu_id)) ? 1 : 0;
}

extern "C" TbBool ingame_imgui_wants_mouse(void)
{
    // For non-monopoly migrated windows (the event / battle boxes, the
    // cheat boxes): world clicks under them must be suppressed, but only
    // while the pointer is actually over one. WantCaptureMouse reflects
    // last frame's windows -- a one-frame lag, harmless for a static box.
    return (!ingame_gui_use_classic_hud() && game_is_running() && ImGui::GetIO().WantCaptureMouse) ? 1 : 0;
}

extern "C" TbBool ingame_imgui_modal_active(void)
{
    if (!game_is_running())
        return 0;
    // The topmost turned-on monopoly menu owns input. Walk the menu stack
    // from the top down: if the first monopoly menu found is a migrated
    // one, ImGui owns; if a still-legacy child menu (e.g. GMnu_ERROR_BOX
    // raised by a failed save, or a dungeon-special dialog) is stacked
    // above it, that legacy menu owns and its sprite buttons keep working.
    for (int64_t k = (int64_t)no_of_active_menus - 1; k >= 0; k--)
    {
        const MenuNumber n = menu_id_to_number((MenuID)menu_stack[k]);
        if (n < 0)
            continue;
        const struct GuiMenu *gmnu = &active_menus[n];
        // Shown (2) or fading in (1) and still on -- but NOT fading out
        // (turn_off_menu sets visual_state = 3): a menu on its way out has
        // handed input back, so it must not keep "owning" the screen for
        // the several frames its fade takes.
        if (gmnu->visual_state == 0 || gmnu->visual_state == 3
            || !gmnu->is_turned_on || !gmnu->is_monopoly_menu)
            continue;
        // See ingame_imgui_menu_active(): classic sidebar still hands the
        // options/quit/load/save family to ImGui.
        const bool classic_gated = ingame_gui_use_classic_hud() && !menu_is_options_family(gmnu->ident);
        return (!classic_gated && menu_is_migrated(gmnu->ident)) ? 1 : 0;
    }
    return 0;
}

extern "C" void ingame_imgui_frame(void)
{
    // Runs before any ImGui window from this module is opened -- see
    // s_pending's comment.
    apply_deferred();

    if (!game_is_running())
        return;

    // GUI_POSITION=CLASSIC reverts the sidebar frame (and the rest of the
    // ImGui-owned HUD below it) to its sprite-based predecessor, but the
    // options/quit/load/save family -- screen-centered popups with no
    // dependency on sidebar layout -- stays on the ImGui path either way
    // (see menu_is_options_family()).
    const TbBool classic_hud = ingame_gui_use_classic_hud();

    // GUI_POSITION can now be flipped live from this very Options menu
    // (menu_is_options_family() above) -- unlike a GOF_ShowGui toggle (Tab),
    // that doesn't otherwise touch the engine window's viewport inset, so
    // poll for the transition here and re-apply it (frontend.cpp's own
    // comment on refresh_engine_window_for_gui_style()).
    {
        static bool s_last_classic_hud = false;
        static bool s_have_last = false;
        if (!s_have_last || s_last_classic_hud != (bool)classic_hud)
        {
            s_have_last = true;
            s_last_classic_hud = (bool)classic_hud;
            refresh_engine_window_for_gui_style();
        }
    }

    // Collapse the settings sub-view whenever the launcher itself is gone
    // (Esc, or any other close of GMnu_OPTIONS).
    if (s_settings_expanded)
    {
        const MenuNumber n = menu_id_to_number(GMnu_OPTIONS);
        if (n < 0 || active_menus[n].visual_state == 0 || !active_menus[n].is_turned_on)
            s_settings_expanded = false;
    }

    refresh_cache_if_stale();

    if (!classic_hud)
    {
        // The HUD windows composite over live gameplay -- no window border
        // (the frontend's gold edge line, style.WindowBorderSize, read as a
        // stray line down the sidebar / around the chat box in-game). The
        // modal panels below (quit / options / save / load / objective /
        // battle) keep it.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0);

        // Phase 4: the always-on sidebar frame. Phase 5's active-tab body is
        // drawn inside its window (frontgui_ingame_panel.cpp) -- one window,
        // no z-order seam between frame and grid.
        ingame_panel_frame();

        // Phase 7: the full-screen parchment / overhead map (own view-mode gate).
        ingame_parchment_frame();

        // Phase 2: debug / script-visible overlays (own *_enabled() gates,
        // no menu involvement) -- draw regardless of which menu is up.
        ingame_debug_overlays_frame();
        // Phase 2: cheat / service box menus (their own GuiBox machinery).
        ingame_boxmenu_frame();
        // Phase 3: loose status text (Paused caption, onscreen warning banner).
        ingame_text_overlays_frame();

        ImGui::PopStyleVar();
    }

    for (int64_t i = 0; i < ACTIVE_MENUS_COUNT; i++)
    {
        const struct GuiMenu *gmnu = &active_menus[i];
        if (!menu_slot_is_migrated_and_on(gmnu))
            continue;
        // Classic sidebar: only the options/quit/load/save family below is
        // still ImGui-owned -- everything else in menu_is_migrated()'s
        // registry (sidebar tabs, objective/battle boxes, ...) falls back
        // to its legacy sprite draw instead.
        if (classic_hud && !menu_is_options_family(gmnu->ident))
            continue;
        switch (gmnu->ident)
        {
            case GMnu_QUIT:    quitmenu_frame();    break;
            case GMnu_OPTIONS: optionsmenu_frame(); break;
            case GMnu_LOAD:      loadmenu_frame();  break;
            case GMnu_SAVE:      savemenu_frame();  break;
            case GMnu_TEXT_INFO: textinfo_frame();  break;
            case GMnu_BATTLE:    battlemenu_frame(); break;
            case GMnu_HOLD_AUDIENCE:
                confirm_modal_frame(GMnu_HOLD_AUDIENCE, "IngameHoldAudience",
                                    get_string(CpgStr_PowerKind1 + 4),
                                    get_string(GUIStr_ConfirmYouSure), yes_hold_audience);
                break;
            case GMnu_ARMAGEDDON:
                confirm_modal_frame(GMnu_ARMAGEDDON, "IngameArmageddon",
                                    get_string(CpgStr_PowerKind1 + 16),
                                    get_string(GUIStr_ConfirmYouSure), yes_armageddon);
                break;
            default:             break;
        }
    }

    // Phase 3: context tooltip -- last, so it sits on top of the menus.
    // (Self-gated on classic_hud -- frontgui_ingame_text.cpp's
    // ingame_tooltip_frame() -- since the classic sidebar/tab content it
    // would normally annotate isn't drawn here.)
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0);
    ingame_tooltip_frame();
    ImGui::PopStyleVar();
}

extern "C" void ingame_quitmenu_confirm(void)
{
    // What the quit-confirm "Yes" does: close the menu + send the packet.
    turn_off_menu(GMnu_QUIT);
    yes_quit_to_main_menu();
}
