#include "pre_inc.h"
#include "frontgui_ingame_tabcontent.h"

#include "frontgui_ingame_cells.h"     // fe_hud_set_panel_rect, grid_pt
#include "frontgui_ingame_grids.h"     // room_grid / spell_grid / trap_grid, do_sell_rooms / do_sell_traps
#include "frontgui_ingame_creature.h"  // creature_list / query_panel / creature_query_panel / spell_lost_panel, creature_pick
#include "frontgui_widgets.h"          // imgui.h
#include "frontgui_style.h"            // FeStylePushFont/PopFont, FeFont_* (Bottom's placeholder tab)
#include "frontgui_ingame_relief.h"    // relief::face
#include "frontgui_ingame_layout.h"    // tcl:: -- named virtual-grid positions

#include "globals.h"
#include "frontend.h"                  // activate_room_build_mode, choose_spell, choose_workshop_item (FUNCTESTING)
#include "gui_frontmenu.h"             // menu_is_active
#include "player_data.h"               // get_my_player, PVT_CreatureContrl/CreaturePasngr
#include "packet_data.h"               // set_players_packet_action, PckA_*, PSt_CreatrQuery (FUNCTESTING)
#include "dungeon_data.h"              // CrGUIJob_Any, CrTend_Imprison/Flee (FUNCTESTING)
#include "config_players.h"            // PSt_CreatrQuery (FUNCTESTING)
#include "config_strings.h"            // GUIStr_Empty (FUNCTESTING)
#include "config_keeperfx.h"           // keeperfx_ui_config.hud_position -- GUI_POSITION
#include "frontgui_ingame_icon_overrides.h" // FeIconOverrideTexture -- docs/refactor/ingame-gui/12-png-icon-overrides.md
#include "frontgui_ingame_panel.h"      // ingame_minimal_popup_is_open -- docs/refactor/ingame-gui/13-minimal-layout.md

#include "post_inc.h"

namespace {

// Query / Creature-list / possession aren't reshaped for Bottom yet
// (docs/refactor/ingame-gui/11-horizontal-layout.md) -- their layout is
// built entirely from tcl:: virtual-space constants tuned for a tall
// vertical panel, which would read as a squashed mess against region B's
// wide-short rect. Room/Spell/Trap don't have that problem (grid_begin()/
// grid_cell_pos(), frontgui_ingame_cells.cpp, already size off the panel
// rect directly), so only those three get the real 2-column reflow; this
// is what the rest fall back to meanwhile.
void tab_not_yet_available_horizontal(void)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    FeStylePushFont(FeFont_Caption);
    const char *msg = "Not yet available in this layout";
    const ImVec2 c = grid_pt(0.0, 0.0); // grid_pt(0,0) == s_panel's own origin, mode-agnostic
    dl->AddText(ImVec2(c.x + 8.0, c.y + 8.0), COL_SUBTEXT, msg);
    FeStylePopFont();
}

} // namespace

void ingame_tabcontent_draw(double px, double py, double pw, double ph)
{
    if (ingame_gui_use_classic_hud())
        return;
    if (pw <= 0.0 || ph <= 0.0)
        return;
    fe_hud_set_panel_rect(px, py, pw, ph);
    const bool bottom = keeperfx_ui_config.hud_position == 3; // HudPos_Bottom
    const bool minimal = keeperfx_ui_config.hud_position == 4; // HudPos_Minimal

    // The query / lost-keeper panels take priority: possession leaves the
    // pre-possession tab body (e.g. GMnu_CREATURE) still turned on
    // (pinstfs_passenger_control_creature only calls turn_off_all_window_menus,
    // not turn_off_all_panel_menus), then turns GMnu_CREATURE_QUERY1 on over it.
    // In possession the query menu can also lag a frame behind the view
    // switch, so key off the view type directly as well.
    const struct PlayerInfo *me = get_my_player();
    const bool in_possession = me != nullptr
        && (me->view_type == PVT_CreatureContrl || me->view_type == PVT_CreaturePasngr);
    const bool query_priority = in_possession
     || menu_is_active(GMnu_CREATURE_QUERY1) || menu_is_active(GMnu_CREATURE_QUERY2)
     || menu_is_active(GMnu_CREATURE_QUERY3) || menu_is_active(GMnu_CREATURE_QUERY4);

    // Minimal (docs/refactor/ingame-gui/13-minimal-layout.md §2.2/§2.3): the
    // pop-up only draws while explicitly opened (or possession/query is
    // forcing it, same priority every layout already gives that case) --
    // world clicks must never dismiss it (room/spell/creature selection
    // needs repeated panel<->3D clicks), so this is the *only* gate.
    if (minimal && !query_priority && !ingame_minimal_popup_is_open())
        return;

    void (*body)(void) = nullptr;
    if (query_priority)
        body = bottom ? &creature_query_panel_horizontal : &creature_query_panel;
    else if (menu_is_active(GMnu_SPELL_LOST)) body = bottom ? &tab_not_yet_available_horizontal : &spell_lost_panel;
    else if (menu_is_active(GMnu_ROOM))       body = &room_grid;
    else if (menu_is_active(GMnu_SPELL))      body = &spell_grid;
    else if (menu_is_active(GMnu_TRAP))       body = &trap_grid;
    else if (menu_is_active(GMnu_CREATURE))   body = bottom ? &creature_list_horizontal : &creature_list;
    else if (menu_is_active(GMnu_QUERY))      body = bottom ? &query_panel_horizontal : &query_panel;
    if (body == nullptr)
        return;

    if (bottom)
    {
        // Region B's background is panel.cpp's job (draw_panel_horizontal()) --
        // it isn't a slice of a taller vertical panel here, so tcl::BODY_Y0/Y1
        // (below) don't apply.
        body();
        return;
    }

    if (minimal)
    {
        // Its own floating window (frontgui_ingame_panel.cpp's minimap/
        // button-cluster windows are elsewhere on screen entirely) --
        // plain WindowBg, no relief::face()/background-override call at
        // all (decided: the message-box's own plain plate, not the
        // procedural marble -- doc 13 §2.1).
        ImGui::SetNextWindowPos(ImVec2(px, py), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(pw, ph), ImGuiCond_Always);
        // ingame_imgui_frame() (frontgui_ingame.cpp) wraps the whole HUD --
        // this pop-up included, since it's reached via draw_panel_minimal()
        // -- in a WindowBorderSize=0 push (composite-over-gameplay windows
        // read a stray gold line as a bug). That's right for the
        // transparent minimap/button-cluster chrome, but this pop-up has a
        // real opaque plate like the message box (##IngameEventBox, drawn
        // outside that push) -- restore the same 1.5f default
        // (frontgui_style.cpp) so it reads as one bordered panel instead of
        // a floating slab (live-tested request: border to match the
        // message box).
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5);
        ImGui::Begin("##IngameMinimalPopup", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings
                     | ImGuiWindowFlags_NoNavInputs);
        ImGui::PopStyleVar();

        // Every body() this pop-up can call was authored against the
        // vertical layout's full 400-unit virtual space, where content
        // starts around tcl::BODY_Y0/q::HEADER_Y0 (190) -- the space above
        // that is minimap/gold/tabstrip chrome in the *real* vertical
        // sidebar, which doesn't exist here. Feeding the pop-up's real
        // rect straight to fe_hud_set_panel_rect() left content starting
        // ~half-way down the window, a big blank gap above it
        // (live-tested: "too much unnecessary whitespace ... between the
        // top of the panel and the icons/selection recess"). Instead,
        // remap a *virtual* rect so virtual-y=BODY_Y0 lands exactly at the
        // pop-up's own top edge (py) and virtual-y=400 at its bottom --
        // build_minimal() (frontgui_hud_layout.cpp) already sized `ph`
        // assuming this same remap, so the two stay in proportion.
        {
            const double content_frac = (400.0 - tcl::BODY_Y0) / 400.0;
            const double ph_fake = ph / content_frac;
            const double py_fake = py - tcl::BODY_Y0 / 400.0 * ph_fake;
            fe_hud_set_panel_rect(px, py_fake, pw, ph_fake);
        }
        body();
        ImGui::End();
        return;
    }

    // Opaque bg over the tab-content region (the frame left a transparent
    // hole there so 3D doesn't bleed through). A raised, mottled stone face
    // -- same material as the panel head -- so the recessed cells the body
    // draws read as pockets in it. Full inner width (x+2 .. x+w-2), matching
    // the tab-strip channel above. Runs inside the sidebar window's Begin/End.
    ImDrawList *bg_dl = ImGui::GetWindowDrawList();
    const ImVec2 c0(px + 2.0, grid_pt(0.0, tcl::BODY_Y0).y);
    const ImVec2 c1(px + pw - 2.0, grid_pt(0.0, tcl::BODY_Y1).y);
    // docs/refactor/ingame-gui/12-png-icon-overrides.md §7: the vertical
    // layout's *other* half of the panel background (draw_background(),
    // frontgui_ingame_panel.cpp, covers the head); a separate override name
    // since the two are independent draw calls with no shared "whole panel
    // height" to UV-sample one image across -- relief::groove_h() already
    // marks this exact seam, so two images meeting there reads as
    // intentional rather than a compromise.
    {
        int64_t ow = 0, oh = 0;
        void *otex = FeIconOverrideTexture("background_vertical_body", &ow, &oh);
        if (otex != nullptr)
            bg_dl->AddImage((ImTextureID)(intptr_t)otex, c0, c1);
        else
            relief::face(bg_dl, c0, c1);
    }
    body();
}

#ifdef FUNCTESTING
extern "C" void ingame_tabcontent_test_fire(int64_t action, int64_t arg)
{
    struct PlayerInfo *me = get_my_player();
    switch ((enum IngameTabTestAction)action)
    {
    case ITTA_RoomBuild:
        // room_grid() left-click
        activate_room_build_mode((RoomKind)arg, GUIStr_Empty);
        break;
    case ITTA_RoomSell:
        do_sell_rooms();
        break;
    case ITTA_SpellChoose:
        // spell_grid() left-click
        choose_spell((PowerKind)arg, GUIStr_Empty);
        break;
    case ITTA_TrapChoose:
        // trap_grid() left-click
        choose_workshop_item((int64_t)arg, GUIStr_Empty);
        break;
    case ITTA_TrapSell:
        do_sell_traps();
        break;
    case ITTA_CreaturePickAny:
        // creature_list() portrait left-click
        creature_pick((ThingModel)arg, CrGUIJob_Any, true);
        break;
    case ITTA_QueryMode:
        // query_panel() QUERY button
        set_players_packet_action(me, PckA_SetPlyrState, PSt_CreatrQuery, 0, 0, 0);
        break;
    case ITTA_AllyToggle:
        // query_panel() ally toggle
        set_players_packet_action(me, PckA_PlyrToggleAlly, (PlayerNumber)arg, 0, 0, 0);
        break;
    case ITTA_TendImprison:
        set_players_packet_action(me, PckA_ToggleTendency, CrTend_Imprison, 0, 0, 0);
        break;
    case ITTA_TendFlee:
        set_players_packet_action(me, PckA_ToggleTendency, CrTend_Flee, 0, 0, 0);
        break;
    }
}
#endif
