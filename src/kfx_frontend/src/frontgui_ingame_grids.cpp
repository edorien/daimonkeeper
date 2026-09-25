#include "pre_inc.h"
#include "frontgui_ingame_grids.h"

#include "frontgui_ingame_cells.h"     // fe_hud_cell/fe_hud_bar, grid_pt/grid_sz/grid_begin, COL_*
#include "frontgui_widgets.h"          // imgui.h
#include "frontgui_style.h"            // FeStylePushFont/PopFont, FeFont_*
#include "frontgui_ingame_relief.h"    // relief::well / accents()
#include "frontgui_ingame_layout.h"    // tcl:: -- named virtual-grid positions

#include "globals.h"
#include "frontend.h"                  // activate_room_build_mode, choose_spell, choose_workshop_item
#include "gui_draw.h"                  // gui_room_type_highlighted
#include "frontmenu_ingame_tabs.h"     // go_to_my_next_room_of_type, go_to_next_spell/trap_of_type,
                                        // find_room_type_capacity_total_percentage, gui_trap_type_highlighted
#include "player_data.h"               // get_my_player, my_player_number
#include "packet_data.h"               // set_players_packet_action, PckA_SetPlyrState, PSt_Sell
#include "dungeon_data.h"              // get_my_dungeon, room_buildable / room_list_start / room_resrchable
#include "room_list.h"                 // count_player_rooms_of_type
#include "room_workshop.h"             // is_trap_buildable
#include "config_terrain.h"            // get_room_kind_stats, RoomConfigStats
#include "config_magic.h"              // get_power_model_stats, is_power_available, PwrK_ARMAGEDDON/HOLDAUDNC
#include "config_trapdoor.h"           // get_manufacture_data, is_trap_placeable/_built, get_trap/door_model_stats
#include "power_process.h"             // set_chosen_power, player_uses_power_hold_audience
#include "magic_powers.h"              // compute_power_price
#include "config_strings.h"
#include "kfx_config_state.h"          // conf.slab_conf / magic_conf / trapdoor_conf .*_types_count
#include "kfx_sim_state.h"             // chosen_room_kind / _spridx / _tooltip, manufactr_*, chosen_spell_type
#include "config_keeperfx.h"           // keeperfx_ui_config.hud_position -- GUI_POSITION
#include "frontgui_ingame_icon_overrides.h" // FeIconOverrideActiveInactive -- docs/refactor/ingame-gui/12-png-icon-overrides.md

#include "post_inc.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace {

// portrait-less icon cell: filled bg, the medsym sprite (dimmed when
// unaffordable), a have-one dot, and an ImGui border (gold = selected,
// red = hover, brown = rest). Returns 1/2 on L/R click.
// `ov_category`/`ov_code_name` (docs/refactor/ingame-gui/12-png-icon-overrides.md
// §3.2), when non-null, are tried against the active GUI_ICON_PACK before
// falling back to the legacy `spr` sprite -- room_grid()/spell_grid()/
// trap_grid() pass their item's own code_name; callers with nothing
// data-driven to override (sell_icon has none) just omit them.
int64_t build_icon(const char *sid, int64_t spr, bool have_one, bool afford,
               bool selected, const ImVec2 &p0, const ImVec2 &sz,
               const char *ov_category = nullptr, const char *ov_code_name = nullptr)
{
    FeHudCellOpts o;
    o.rclick   = true;
    o.selected = selected;
    o.have_dot = have_one;

    if (ov_category != nullptr && ov_code_name != nullptr && ov_code_name[0] != '\0')
    {
        int64_t ow = 0, oh = 0;
        bool odim = false;
        void *otex = FeIconOverrideActiveInactive(ov_category, ov_code_name, afford, &ow, &oh, &odim);
        if (otex != nullptr)
        {
            const uint64_t tint = odim ? IM_COL32(255, 255, 255, 110) : IM_COL32_WHITE;
            o.content = [otex, ow, oh, tint](ImDrawList *dl, const ImVec2 &cp0, const ImVec2 &csz) {
                blit_fit_tex(dl, otex, ow, oh, ImVec2(cp0.x + 3.0, cp0.y + 3.0),
                            ImVec2(csz.x - 6.0, csz.y - 6.0), tint);
            };
            return fe_hud_cell(sid, p0, sz, o);
        }
    }

    // Legacy convention (gui_area_room_button/_spell_button/_trap_button,
    // frontmenu_ingame_tabs.c): medsym_sprite_idx+1 is the pre-authored
    // "disabled" sprite frame, not an alpha tint -- swap to it instead of
    // dimming so unaffordable rooms/spells and out-of-stock traps/doors
    // read exactly like they always did (live-tested request).
    o.sprite = spr + (afford ? 0 : 1);
    return fe_hud_cell(sid, p0, sz, o);
}

// The sell cell -- a big red "$" (no legacy artwork). Returns 1 on click.
int64_t sell_icon(const char *sid, const ImVec2 &p0, const ImVec2 &sz)
{
    FeHudCellOpts o;
    o.glyph     = "$";
    o.glyph_col = relief::accents().bar_bad;
    return fe_hud_cell(sid, p0, sz, o);
}

// One cell standing in for every not-yet-researched item on the panel:
// a big "?" plus the count in the lower-right corner. Unselectable.
void unknown_cell(const char *sid, const ImVec2 &p0, const ImVec2 &sz, int64_t count)
{
    FeHudCellOpts o;
    o.swallow   = true;
    o.glyph     = "?";
    o.glyph_col = COL_SUBTEXT;
    o.count     = count;
    fe_hud_cell(sid, p0, sz, o);
}

// The "big" info strip above the grid -- selected/hovered item's big
// symbol, name, count, gold cost and (rooms only) a capacity bar.
void info_band(int64_t bigsym, const char *name, int64_t count, int64_t cost, double bar_frac)
{
    // Not yet designed for Bottom (docs/refactor/ingame-gui/11-horizontal-layout.md):
    // region B's grid claims the whole panel rect (grid_begin()), so this
    // band's usual tcl::INFO_Y0..Y1 slice would draw on top of it rather
    // than above it. Skip rather than overlap.
    if (keeperfx_ui_config.hud_position == 3) // HudPos_Bottom
        return;

    const ImVec2 b0 = grid_pt(6.0, tcl::INFO_Y0);
    const ImVec2 b1 = grid_pt(134.0, tcl::INFO_Y1);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    relief::well(dl, b0, b1, 3.0);
    const double bh = b1.y - b0.y;

    if (bigsym > 0)
        blit_fit(dl, bigsym, ImVec2(b0.x + 3.0, b0.y + 3.0),
                 ImVec2(bh - 6.0, bh - 6.0), IM_COL32_WHITE);

    const double tx = b0.x + bh + 2.0;
    FeStylePushFont(FeFont_Body);
    if (name != nullptr && name[0] != '\0')
        dl->AddText(ImVec2(tx, b0.y + 4.0), COL_TEXT, name);
    char c[48];
    if (count >= 0 && cost >= 0)
        std::snprintf(c, sizeof(c), "x%" PRId64 "     %" PRId64, (int64_t)(count), (int64_t)(cost));
    else if (cost >= 0)
        std::snprintf(c, sizeof(c), "%" PRId64, (int64_t)(cost));
    else if (count >= 0)
        std::snprintf(c, sizeof(c), "x%" PRId64, (int64_t)(count));
    else
        c[0] = '\0';
    if (c[0] != '\0')
        dl->AddText(ImVec2(tx, b0.y + 6.0 + ImGui::GetFontSize()), COL_SUBTEXT, c);
    FeStylePopFont();

    if (bar_frac >= 0.0)
    {
        const ImVec2 g0(tx, b1.y - 16.0);
        const ImVec2 g1(b1.x - 4.0, b1.y - 4.0);
        FeHudBarOpts bo;
        bo.fill = relief::accents().bar_good;
        fe_hud_bar(g0, g1, bar_frac, bo);
    }
}

} // namespace

// ---- room build grid -------------------------------------------------

struct GridItem { int64_t kind; int64_t order; };

void do_sell_rooms(void)
{
    kfx_sim_state.chosen_room_kind = 0;
    kfx_sim_state.chosen_room_spridx = 0;
    kfx_sim_state.chosen_room_tooltip = 0;
    set_players_packet_action(get_my_player(), PckA_SetPlyrState, PSt_Sell, 0, 0, 0);
}

void room_grid(void)
{
    struct Dungeon *dungeon = get_my_dungeon();
    if (dungeon_invalid(dungeon))
        return;
    const int64_t count = kfx_config_state.conf.slab_conf.room_types_count;

    std::vector<GridItem> items;
    for (int64_t k = 1; k < count; k++)
    {
        const struct RoomConfigStats *rs = get_room_kind_stats(k);
        if (rs != nullptr && rs->panel_tab_idx > 0)
            items.push_back({ k, rs->panel_tab_idx });
    }
    std::sort(items.begin(), items.end(),
              [](const GridItem &a, const GridItem &b) { return a.order < b.order; });

    const GridGeom g = grid_begin("##rmGrid");

    int64_t hovered_now = 0;
    int64_t slot = 0;
    int64_t hidden = 0;
    for (const GridItem &it : items)
    {
        const int64_t k = it.kind;
        const struct RoomConfigStats *rs = get_room_kind_stats(k);
        const bool buildable = (dungeon->room_buildable[k] & 1) != 0;
        const bool researchable = dungeon->room_resrchable[k] == 1 || dungeon->room_resrchable[k] == 2
            || (dungeon->room_resrchable[k] == 4 && (dungeon->room_buildable[k] & 2));
        if (!buildable)
        {
            if (researchable) hidden++;
            continue;
        }
        const bool afford = dungeon->total_money_owned >= rs->cost;
        const bool have_one = dungeon->room_list_start[k] > 0;

        char sid[16]; std::snprintf(sid, sizeof(sid), "rm%" PRId64, (int64_t)(k));
        const int64_t hit = build_icon(sid, (int64_t)rs->medsym_sprite_idx, have_one, afford,
                                   kfx_sim_state.chosen_room_kind == k,
                                   grid_cell_pos(g, slot), ImVec2(g.cell_w, g.cell_h),
                                   "room", room_code_name(k));
        if (ImGui::IsItemHovered())
            hovered_now = (k < 17) ? k : 0;
        if (hit == 1)
            activate_room_build_mode(k, rs->tooltip_stridx);
        else if (hit == 2)
        {
            go_to_my_next_room_of_type(k);
            kfx_sim_state.chosen_room_kind = k;
            kfx_sim_state.chosen_room_spridx = rs->bigsym_sprite_idx;
            kfx_sim_state.chosen_room_tooltip = rs->tooltip_stridx;
        }
        slot++;
    }
    if (hidden > 0)
        unknown_cell("rmUnk", grid_cell_pos(g, slot++), ImVec2(g.cell_w, g.cell_h), hidden);
    if (sell_icon("rmSell", grid_cell_pos(g, slot++), ImVec2(g.cell_w, g.cell_h)) == 1)
        do_sell_rooms();
    grid_end(g, slot);
    // gui_room_type_highlighted feeds roomspace prediction next turn.
    gui_room_type_highlighted = (char)hovered_now;

    // Info strip -- always drawn (the recess stays put); only its contents
    // change with the hovered (else chosen) room.
    int64_t info_kind = hovered_now;
    if (info_kind <= 0) info_kind = kfx_sim_state.chosen_room_kind;
    if (info_kind > 0 && info_kind < count)
    {
        const struct RoomConfigStats *rs = get_room_kind_stats(info_kind);
        const int64_t pct = find_room_type_capacity_total_percentage(my_player_number, info_kind);
        info_band((int64_t)rs->bigsym_sprite_idx, get_string(rs->name_stridx),
                  (int64_t)count_player_rooms_of_type(my_player_number, info_kind),
                  (int64_t)rs->cost, pct >= 0 ? (double)pct / 256.0 : -1.0);
    }
    else
    {
        info_band(0, nullptr, -1, -1, -1.0);
    }
}

// ---- power (spell) grid --------------------------------------------

void spell_grid(void)
{
    struct PlayerInfo *player = get_my_player();
    struct Dungeon *dungeon = get_my_dungeon();
    if (dungeon_invalid(dungeon))
        return;
    const int64_t count = kfx_config_state.conf.magic_conf.power_types_count;

    std::vector<GridItem> items;
    for (int64_t k = 1; k < count; k++)
    {
        const struct PowerConfigStats *ps = get_power_model_stats(k);
        if (ps != nullptr && ps->panel_tab_idx > 0)
            items.push_back({ k, (int64_t)ps->panel_tab_idx });
    }
    std::sort(items.begin(), items.end(),
              [](const GridItem &a, const GridItem &b) { return a.order < b.order; });

    const GridGeom g = grid_begin("##pwGrid");

    int64_t hovered_now = 0;
    int64_t slot = 0;
    int64_t hidden = 0;
    for (const GridItem &it : items)
    {
        const int64_t k = it.kind;
        const struct PowerConfigStats *ps = get_power_model_stats(k);
        // Shown once researchable or owned (matches legacy gui_area_spell_button).
        if (!dungeon->magic_resrchable[k] && dungeon->magic_level[k] <= 0)
            continue;
        const bool available = is_power_available(player->id_number, k);
        if (!available)
        {
            hidden++;
            continue;
        }
        bool castable = true;
        if (k == PwrK_ARMAGEDDON && kfx_sim_state.armageddon_cast_turn != 0) castable = false;
        if (k == PwrK_HOLDAUDNC && player_uses_power_hold_audience(my_player_number)) castable = false;
        // gui_area_spell_button (frontmenu_ingame_tabs.c) swaps to the
        // disabled sprite purely on compute_power_price() vs gold owned --
        // this build never had that check at all, only the exclusivity one
        // above (live-tested request: "when a room/spell is unaffordable,
        // can the icon switch to the disabled icon").
        const GoldAmount price = compute_power_price(dungeon->owner, k, 0);
        const bool afford = castable && (dungeon->total_money_owned >= price);

        char sid[16]; std::snprintf(sid, sizeof(sid), "pw%" PRId64, (int64_t)(k));
        const int64_t hit = build_icon(sid, (int64_t)ps->medsym_sprite_idx, false, afford,
                                   kfx_sim_state.chosen_spell_type == k,
                                   grid_cell_pos(g, slot), ImVec2(g.cell_w, g.cell_h),
                                   "power", ps->code_name);
        if (ImGui::IsItemHovered())
            hovered_now = k;
        if (castable && hit == 1)
            choose_spell(k, ps->tooltip_stridx);
        else if (castable && hit == 2)
        {
            go_to_next_spell_of_type(k);
            set_chosen_power(k, ps->tooltip_stridx);
        }
        slot++;
    }
    if (hidden > 0)
        unknown_cell("pwUnk", grid_cell_pos(g, slot++), ImVec2(g.cell_w, g.cell_h), hidden);
    grid_end(g, slot);

    int64_t info = hovered_now;
    if (info <= 0) info = kfx_sim_state.chosen_spell_type;
    if (info > 0 && info < count)
    {
        const struct PowerConfigStats *ps = get_power_model_stats(info);
        info_band((int64_t)ps->bigsym_sprite_idx, get_string(ps->name_stridx),
                  -1, (int64_t)ps->cost[0], -1.0);
    }
    else
    {
        info_band(0, nullptr, -1, -1, -1.0);
    }
}

// ---- manufacture (trap/door) grid ---------------------------------

void do_sell_traps(void)
{
    kfx_sim_state.manufactr_element = 0;
    kfx_sim_state.manufactr_spridx = 0;
    kfx_sim_state.manufactr_tooltip = 0;
    set_players_packet_action(get_my_player(), PckA_SetPlyrState, PSt_Sell, 0, 0, 0);
}

void trap_grid(void)
{
    struct PlayerInfo *player = get_my_player();
    struct Dungeon *dungeon = get_my_dungeon();
    if (dungeon_invalid(dungeon))
        return;
    const int64_t count = kfx_config_state.conf.trapdoor_conf.manufacture_types_count;

    std::vector<GridItem> items;
    for (int64_t m = 1; m < count; m++)
    {
        const struct ManufactureData *md = get_manufacture_data(m);
        if (md != nullptr && md->panel_tab_idx > 0)
            items.push_back({ m, (int64_t)md->panel_tab_idx });
    }
    std::sort(items.begin(), items.end(),
              [](const GridItem &a, const GridItem &b) { return a.order < b.order; });

    const GridGeom g = grid_begin("##mfGrid");

    int64_t hovered_model = 0;
    int64_t hovered_m = 0;
    int64_t slot = 0;
    int64_t hidden = 0;
    for (const GridItem &it : items)
    {
        const int64_t m = it.kind;
        const struct ManufactureData *md = get_manufacture_data(m);
        const bool placeable = is_trap_placeable(player->id_number, md->tngmodel)
                            || is_trap_built(player->id_number, md->tngmodel);
        const bool buildable = is_trap_buildable(player->id_number, md->tngmodel);
        if (!placeable)
        {
            if (buildable) hidden++;
            continue;
        }

        char sid[16]; std::snprintf(sid, sizeof(sid), "mf%" PRId64, (int64_t)(m));
        // Doors fold under the "trap" override category too (docs/refactor/
        // ingame-gui/12-png-icon-overrides.md §2) -- both render in this one
        // merged tab; only which *_code_name() function names the tngmodel
        // differs.
        const char *ov_code = (md->tngclass == TCls_Door) ? door_code_name(md->tngmodel)
                                                           : trap_code_name(md->tngmodel);
        // gui_area_trap_button (frontmenu_ingame_tabs.c): i = sprite_idx +
        // (amount < 1) -- this build always drew the enabled icon
        // regardless of how many are left to place (live-tested request:
        // "when item amount < 1 for traps/doors use the disabled icon").
        const uint64_t amount = (md->tngclass == TCls_Door)
            ? dungeon->mnfct_info.door_amount_placeable[md->tngmodel]
            : dungeon->mnfct_info.trap_amount_placeable[md->tngmodel];
        const bool afford = amount >= 1;
        const int64_t hit = build_icon(sid, (int64_t)md->medsym_sprite_idx, false, afford,
                                   kfx_sim_state.manufactr_element == m,
                                   grid_cell_pos(g, slot), ImVec2(g.cell_w, g.cell_h),
                                   "trap", ov_code);
        if (ImGui::IsItemHovered())
        {
            hovered_model = md->tngmodel;
            hovered_m = m;
        }
        if (hit == 1)
            choose_workshop_item(m, md->tooltip_stridx);
        else if (hit == 2)
        {
            go_to_next_trap_of_type(md->tngmodel, player->id_number);
            kfx_sim_state.manufactr_element = m;
            kfx_sim_state.manufactr_spridx = md->bigsym_sprite_idx;
            kfx_sim_state.manufactr_tooltip = md->tooltip_stridx;
        }
        slot++;
    }
    if (hidden > 0)
        unknown_cell("mfUnk", grid_cell_pos(g, slot++), ImVec2(g.cell_w, g.cell_h), hidden);
    if (sell_icon("mfSell", grid_cell_pos(g, slot++), ImVec2(g.cell_w, g.cell_h)) == 1)
        do_sell_traps();
    grid_end(g, slot);
    gui_trap_type_highlighted = (char)hovered_model;

    int64_t info_m = hovered_m;
    if (info_m <= 0) info_m = kfx_sim_state.manufactr_element;
    if (info_m > 0 && info_m < count)
    {
        const struct ManufactureData *md = get_manufacture_data(info_m);
        int64_t bigsym = (int64_t)md->bigsym_sprite_idx;
        const char *name = "";
        int64_t req = -1;
        if (md->tngclass == TCls_Trap)
        {
            const struct TrapConfigStats *ts = get_trap_model_stats(md->tngmodel);
            if (ts != nullptr) { name = get_string(ts->name_stridx); req = (int64_t)ts->manufct_required; }
        }
        else
        {
            const struct DoorConfigStats *ds = get_door_model_stats(md->tngmodel);
            if (ds != nullptr) { name = get_string(ds->name_stridx); req = (int64_t)ds->manufct_required; }
        }
        info_band(bigsym, name, -1, req, -1.0);
    }
    else
    {
        info_band(0, nullptr, -1, -1, -1.0);
    }
}
