#include "pre_inc.h"
#include "frontgui_ingame_battle.h"

#include "frontgui_widgets.h"
#include "frontgui_deferred.h"       // FeDeferredQueue
#include "frontgui_style.h"
#include "frontgui_sprite_tex.h"     // FeGuiPanelTexture, FeGuiPanelIconButton

#include "globals.h"
#include "sprites.h"                  // GPS_message_*, GBS_guisymbols_sym_fight
#include "frontmenu_ingame_evnt.h"    // gui_next/previous_battle, gui_get_creature_in_battle, gui_go_to_person_in_battle, battle_creature_over
#include "frontend.h"                 // gui_close_objective
#include "player_data.h"              // my_player_number
#include "dungeon_data.h"             // get_players_num_dungeon, visible_battles
#include "creature_battle.h"          // friendly_battler_list, enemy_battler_list, creature_battle_get, MESSAGE_BATTLERS_COUNT
#include "creature_control.h"         // creature_control_get_from_thing, max_health
#include "creature_graphics.h"        // get_creature_model_graphics, CGI_HandSymbol
#include "thing_data.h"               // thing_get, thing_is_creature
#include "config_strings.h"           // GUIStr_*
#include "config_creature.h"          // creature_code_name
#include "kfx_sim_state.h"
#include "frontgui_hud_layout.h"       // HudRegion_Gold, hud_layout_current (GUI_POSITION Bottom)
#include "config_keeperfx.h"           // keeperfx_ui_config.hud_position -- GUI_POSITION
#include "frontgui_ingame_icon_overrides.h" // FeIconOverrideSingle -- docs/refactor/ingame-gui/12-png-icon-overrides.md

#include "post_inc.h"

#include <imgui.h>

namespace {

// One battler cell at screen point `p0`: the creature's hand-symbol icon
// (a square, so it matches the "vs" symbol and its neighbours) with a slim
// health bar under it. Hovering it sets battle_creature_over (what
// gui_setup_*_over did from the legacy button's mouse-x); click /
// right-click then run the same actions.
void battler_cell(int64_t thing_idx, const ImVec2 &p0, double icon_h)
{
    struct Thing *thing = thing_get(thing_idx);
    if (!thing_is_creature(thing))
        return;

    const int64_t spr_idx = get_creature_model_graphics(thing->model, CGI_HandSymbol);
    int64_t sw = 0, sh = 0;
    void *tex = FeIconOverrideSingle("creature_icon", creature_code_name(thing->model), &sw, &sh);
    if (tex == nullptr)
        tex = FeGuiPanelTexture(spr_idx, &sw, &sh);

    ImGui::SetCursorScreenPos(p0);
    ImGui::PushID((int64_t)thing_idx);
    ImGui::BeginGroup();

    if (tex != nullptr)
        ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(icon_h, icon_h));
    else
        ImGui::Dummy(ImVec2(icon_h, icon_h));

    const struct CreatureControl *cctrl = creature_control_get_from_thing(thing);
    int64_t maxh = (cctrl != nullptr && cctrl->max_health > 0) ? cctrl->max_health : 1;
    int64_t hp = thing->health;
    if (hp < 0) hp = 0;
    const double frac = (double)hp / (double)maxh;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const double bar_h = 4.0;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + icon_h, p.y + bar_h), IM_COL32(20, 12, 8, 220));
    dl->AddRectFilled(p, ImVec2(p.x + icon_h * frac, p.y + bar_h),
                      frac > 0.5 ? IM_COL32(90, 200, 90, 255)
                    : frac > 0.25 ? IM_COL32(220, 200, 60, 255)
                                   : IM_COL32(220, 70, 50, 255));
    ImGui::Dummy(ImVec2(icon_h, bar_h));

    ImGui::EndGroup();

    if (ImGui::IsItemHovered())
    {
        battle_creature_over = thing->index;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            gui_get_creature_in_battle(nullptr);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            gui_go_to_person_in_battle(nullptr);
    }
    ImGui::PopID();
}

// The non-empty entries of one side's battler list for this battle.
int64_t collect_battlers(const int64_t *list, int64_t visbtl_id, int64_t *out)
{
    int64_t n = 0;
    for (int64_t b = 0; b < MESSAGE_BATTLERS_COUNT - 1; b++)
    {
        const int64_t idx = list[MESSAGE_BATTLERS_COUNT * visbtl_id + b];
        if (idx != 0)
            out[n++] = idx;
    }
    return n;
}

// A block per visible battle: friendly cells | crossed-swords "vs" | enemy
// cells. Each side fills outward from the "vs" symbol; when a side has
// more battlers than fit in its half, the rest wrap onto further lines
// below (friendlies stay right-aligned against the symbol, enemies
// left-aligned), and the symbol is centred vertically across all lines --
// a long side used to run over the symbol and into the other side.
void battle_row(int64_t visbtl_id, double icon_h)
{
    const struct Dungeon *dungeon = get_players_num_dungeon(my_player_number);
    const BattleIndex battle_id = dungeon->visible_battles[visbtl_id];
    const struct CreatureBattle *battle = creature_battle_get(battle_id);
    if (creature_battle_invalid(battle) || battle->fighters_num == 0)
        return;

    const double avail = ImGui::GetContentRegionAvail().x;
    const double cell = icon_h * 1.10;
    const double gap  = icon_h * 0.35;
    const double half_vs = icon_h * 0.5;
    const double cx = avail * 0.5;
    const double line_h = icon_h + 4.0 + icon_h * 0.12; // icon + health bar + spacing

    int64_t friends[MESSAGE_BATTLERS_COUNT], enemies[MESSAGE_BATTLERS_COUNT];
    const int64_t nf = collect_battlers(friendly_battler_list, visbtl_id, friends);
    const int64_t ne = collect_battlers(enemy_battler_list, visbtl_id, enemies);

    int64_t per_line = (int64_t)((cx - half_vs - gap) / cell);
    if (per_line < 1) per_line = 1;
    const int64_t lines_f = (nf + per_line - 1) / per_line;
    const int64_t lines_e = (ne + per_line - 1) / per_line;
    int64_t lines = lines_f > lines_e ? lines_f : lines_e;
    if (lines < 1) lines = 1;
    const double block_h = (double)lines * line_h - icon_h * 0.12;

    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImGui::PushID(visbtl_id);

    // Friendlies: the first per_line closest to the symbol, reading
    // left-to-right like before.
    for (int64_t i = 0; i < nf; i++)
    {
        const int64_t line = i / per_line;
        const int64_t col = i % per_line;
        const int64_t on_line = (line + 1) * per_line <= nf ? per_line : nf - line * per_line;
        const double x = cx - half_vs - gap - (double)(on_line - col) * cell + (cell - icon_h) * 0.5;
        battler_cell(friends[i], ImVec2(o.x + x, o.y + (double)line * line_h), icon_h);
    }

    ImGui::SetCursorScreenPos(ImVec2(o.x + cx - half_vs, o.y + (block_h - icon_h) * 0.5));
    int64_t fw = 0, fh = 0;
    void *fight = FeSpriteTexture(GBS_guisymbols_sym_fight, &fw, &fh);
    if (fight != nullptr && fh > 0)
        ImGui::Image((ImTextureID)(intptr_t)fight, ImVec2(icon_h, icon_h));
    else
        ImGui::TextUnformatted("vs");

    for (int64_t i = 0; i < ne; i++)
    {
        const int64_t line = i / per_line;
        const int64_t col = i % per_line;
        const double x = cx + half_vs + gap + (double)col * cell + (cell - icon_h) * 0.5;
        battler_cell(enemies[i], ImVec2(o.x + x, o.y + (double)line * line_h), icon_h);
    }

    // Reserve the whole block so the next battle (and the scroll area's
    // extent) start below it.
    ImGui::SetCursorScreenPos(o);
    ImGui::Dummy(ImVec2(avail, block_h + icon_h * 0.35)); // + row gap
    ImGui::PopID();
}

void do_close_battle(void) { gui_close_objective(nullptr); }
void do_prev_battle(void)  { gui_previous_battle(nullptr); }
void do_next_battle(void)  { gui_next_battle(nullptr); }

// Deferred: gui_close_objective / gui_*_battle turn menus off -- never from
// inside an open window. See frontgui_deferred.h.
FeDeferredQueue s_deferred;

} // namespace

void battlemenu_frame(void)
{
    ImGuiIO &io = ImGui::GetIO();

    s_deferred.drain();

    battle_creature_over = 0; // recomputed each frame from hover

    // GUI_POSITION Bottom (docs/refactor/ingame-gui/11-horizontal-layout.md):
    // see textinfo_frame()'s (frontgui_ingame.cpp) identical fix -- this box
    // belongs in region C, not floating full-width above the whole strip.
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
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.48, io.DisplaySize.y * 0.24), ImGuiCond_Always);
    }
    ImGui::Begin("##IngameBattleBox", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

    const double icon_h = io.DisplaySize.y * 0.058;

    // Left column: just the close button now -- prev / next battle is the
    // scroll wheel over the list (user's call), no arrow buttons.
    ImGui::BeginGroup();
    if (FeGuiPanelIconButton("##btl_close", GPS_message_message_btn_accept_std,
                             get_string(GUIStr_CloseWindow), icon_h))
        s_deferred.push(&do_close_battle);
    ImGui::EndGroup();
    ImGui::SameLine();

    // The battle rows in a scroll area -- a wheel past the top / bottom
    // pages through battles via the same gui_previous_battle /
    // gui_next_battle the legacy arrows used.
    const bool open = FeBeginScrollArea("##battle_rows", ImVec2(0, 0));
    if (open)
    {
        const double wheel = io.MouseWheel;
        if (wheel != 0.0 && ImGui::IsWindowHovered())
            s_deferred.push(wheel < 0.0 ? &do_next_battle : &do_prev_battle);

        for (int64_t i = 0; i < 3; i++)
            battle_row(i, icon_h);
    }
    FeEndScrollArea();

    ImGui::End();
}
