/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file frontgui_skirmish_setup.cpp
 *     The Skirmish screen's Setup tab (see frontgui_skirmish_setup.h).
 * @par Comment:
 *     Layout and state live here; every value lives in skirmish_setup.cpp so
 *     this file is only drawing and click handling. Icon resolution mirrors
 *     the editor toolbox (editor_toolbox.cpp): PNG-pack override first, then
 *     the panel sprite, then a text tile.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "frontgui_skirmish_setup.h"

#include "bflib_basics.h" // JUSTLOG
#include "frontgui_widgets.h" // imgui.h
#include "frontgui_style.h"
#include "frontgui_ingame_cells.h"
#include "frontgui_ingame_icon_overrides.h"
#include "frontgui_sprite_tex.h" // FeGuiPanelSpriteAvailable
#include "skirmish_setup.h"

#include "config_compp.h"
#include "config_creature.h"
#include "config_magic.h"
#include "config_spritecolors.h"
#include "config_strings.h"
#include "config_terrain.h"
#include "config_trapdoor.h"
#include "creature_graphics.h"
#include "custom_sprites.h" // is_custom_icon
#include "kfx_config_state.h"
#include "sprites.h"

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <vector>
#include "post_inc.h"

/******************************************************************************/
namespace {

const float kTile = 44.0f;
const float kGap = 4.0f;

int s_avail_player = -1;            // availability page: -1 = all players
int s_setup_hover_frame = -1000;    // ImGui frame in which the mouse was last over the Setup tab
int s_stock_kind = -1;              // selected trap/door (for the stock field)
std::string s_stock_item;
int s_survive_minutes = 15;
int s_gold_target = 20000;
struct ClauseDraft { int player = 0; int var = 0; int op = 0; int value = 1; };
struct RuleDraft { int win = 1; std::vector<ClauseDraft> clauses = std::vector<ClauseDraft>(1); } s_draft;
char s_status[160];

struct Item
{
    std::string code;   // script name (upper-case)
    std::string label;  // pretty name
    std::string tip;
    short sprite = 0;
    enum Ov { None, ActiveInactive, Single } ov = None;
    const char *ov_category = nullptr;
};

std::string pretty(const std::string &code)
{
    std::string s = code;
    for (char &c : s)
        c = (c == '_') ? ' ' : c;
    return s;
}

const char *kind_title(int kind)
{
    static const char *const t[AvailKind_Count] = { "Creatures", "Rooms", "Spells", "Traps", "Doors" };
    return t[kind];
}

std::vector<Item> items_for_kind(int kind)
{
    std::vector<Item> out;
    auto add_script_only = [&]() {
        for (const std::string &n : skirmish_setup_script_items(kind))
        {
            bool have = false;
            for (const Item &i : out)
                have |= (i.code == n);
            if (have)
                continue;
            Item it;
            it.code = n;
            it.label = pretty(n);
            it.tip = n + " (defined by the level)";
            out.push_back(it);
        }
        if (kind != AvailKind_Creature)
            return;
        // Creatures named only by the level's pool (a mappack creature the base config does not know).
        const SkirmishSetup &s = skirmish_setup();
        std::set<std::string> names;
        for (const auto &kv : s.analysis.seed.pool) names.insert(kv.first);
        for (const auto &kv : s.choices.values.pool) names.insert(kv.first);
        for (const std::string &n : names)
        {
            bool have = false;
            for (const Item &i : out)
                have |= (i.code == n);
            if (have)
                continue;
            Item it;
            it.code = n;
            it.label = pretty(n);
            it.tip = n + " (defined by the level)";
            out.push_back(it);
        }
    };
    switch (kind)
    {
    case AvailKind_Creature:
    {
        const int count = kfx_config_state.conf.crtr_conf.model_count;
        for (int pass = 0; pass < 2; pass++) // keepers first, then heroes
            for (ThingModel m = 1; m < (ThingModel)count; m++)
            {
                const struct CreatureModelConfig *cc = creature_stats_get(m);
                if (cc == nullptr || (cc->model_flags & CMF_IsSpectator) != 0)
                    continue;
                if (((cc->model_flags & CMF_IsEvil) != 0) != (pass == 0))
                    continue;
                Item it;
                it.code = creature_code_name(m);
                it.label = pretty(it.code);
                it.tip = std::string(get_string(cc->namestr_idx)) + " (" + it.code + ")";
                it.sprite = get_creature_model_graphics(m, CGI_HandSymbol);
                it.ov = Item::Single;
                it.ov_category = "creature_icon";
                out.push_back(it);
            }
        break;
    }
    case AvailKind_Room:
    {
        std::vector<std::pair<int, int>> order; // (panel order, room kind)
        const int count = kfx_config_state.conf.slab_conf.room_types_count;
        for (int k = 1; k < count; k++)
        {
            const struct RoomConfigStats *rs = get_room_kind_stats(k);
            if (rs != nullptr && rs->panel_tab_idx > 0)
                order.push_back(std::make_pair((int)rs->panel_tab_idx, k));
        }
        std::sort(order.begin(), order.end());
        for (const auto &o : order)
        {
            const struct RoomConfigStats *rs = get_room_kind_stats(o.second);
            Item it;
            it.code = room_code_name((RoomKind)o.second);
            it.label = pretty(it.code);
            it.tip = std::string(get_string(rs->name_stridx)) + " (" + it.code + ")";
            it.sprite = (short)rs->medsym_sprite_idx;
            it.ov = Item::ActiveInactive;
            it.ov_category = "room";
            out.push_back(it);
        }
        break;
    }
    case AvailKind_Magic:
    {
        std::vector<std::pair<int, int>> order;
        const int count = kfx_config_state.conf.magic_conf.power_types_count;
        for (int k = 1; k < count; k++)
        {
            const struct PowerConfigStats *ps = get_power_model_stats(k);
            if (ps != nullptr && ps->panel_tab_idx > 0)
                order.push_back(std::make_pair((int)ps->panel_tab_idx, k));
        }
        std::sort(order.begin(), order.end());
        for (const auto &o : order)
        {
            const struct PowerConfigStats *ps = get_power_model_stats(o.second);
            Item it;
            it.code = power_code_name((PowerKind)o.second);
            it.label = pretty(it.code);
            it.tip = std::string(get_string(ps->name_stridx)) + " (" + it.code + ")";
            it.sprite = (short)ps->medsym_sprite_idx;
            it.ov = Item::ActiveInactive;
            it.ov_category = "power";
            out.push_back(it);
        }
        break;
    }
    default: // traps and doors come from the workshop's manufacture table, like the in-game grid
    {
        std::vector<std::pair<int, int>> order;
        const int count = kfx_config_state.conf.trapdoor_conf.manufacture_types_count;
        for (int m = 1; m < count; m++)
        {
            const struct ManufactureData *md = get_manufacture_data(m);
            if (md != nullptr && md->panel_tab_idx > 0 && md->tngclass == ((kind == AvailKind_Trap) ? TCls_Trap : TCls_Door))
                order.push_back(std::make_pair((int)md->panel_tab_idx, m));
        }
        std::sort(order.begin(), order.end());
        for (const auto &o : order)
        {
            const struct ManufactureData *md = get_manufacture_data(o.second);
            Item it;
            it.code = (kind == AvailKind_Trap) ? trap_code_name(md->tngmodel) : door_code_name(md->tngmodel);
            it.label = pretty(it.code);
            it.tip = it.code;
            it.sprite = (short)md->medsym_sprite_idx;
            it.ov = Item::ActiveInactive;
            it.ov_category = "trap"; // doors share the trap override category
            out.push_back(it);
        }
        break;
    }
    }
    add_script_only();
    return out;
}

// False for a sprite that cannot be drawn right now (see FeGuiPanelSpriteAvailable): the menu has no
// in-game sheet, so only base-sheet icons draw; campaign/mod custom icons and unresolved icon names
// (INT16_MAX) get a text tile instead of the engine's magenta checkerboard placeholder.
bool sprite_usable(short idx)
{
    return FeGuiPanelSpriteAvailable(idx);
}

// One icon tile (same look as the in-game grids). Returns 1 left click, 2 right click, 0 none.
int draw_item_tile(const char *id, const Item &it, const ImVec2 &p0, FeHudCellOpts o)
{
    void *otex = nullptr;
    int ow = 0, oh = 0;
    bool odim = false;
    if (it.ov == Item::Single)
        otex = FeIconOverrideSingle(it.ov_category, it.code.c_str(), &ow, &oh);
    else if (it.ov == Item::ActiveInactive)
        otex = FeIconOverrideActiveInactive(it.ov_category, it.code.c_str(), !o.dim, &ow, &oh, &odim);
    const bool dim = o.dim || odim;
    if (otex != nullptr)
    {
        o.content = [otex, ow, oh, dim](ImDrawList *dl, const ImVec2 &cp0, const ImVec2 &csz) {
            blit_fit_tex(dl, otex, ow, oh, ImVec2(cp0.x + 3.0f, cp0.y + 3.0f), ImVec2(csz.x - 6.0f, csz.y - 6.0f),
                dim ? IM_COL32(255, 255, 255, 90) : IM_COL32_WHITE);
        };
    }
    else if (sprite_usable(it.sprite))
    {
        o.sprite = it.sprite;
    }
    else
    {
        static std::set<std::string> s_logged;
        if (it.sprite > 0 && s_logged.insert(it.code).second)
            JUSTLOG("Skirmish setup: '%s' has no drawable menu icon (sprite %d): showing a text tile", it.code.c_str(), (int)it.sprite);
        const std::string label = it.label;
        o.content = [label, dim](ImDrawList *dl, const ImVec2 &cp0, const ImVec2 &csz) {
            ImFont *font = ImGui::GetFont();
            const float fs = ImGui::GetFontSize() * 0.75f;
            dl->PushClipRect(ImVec2(cp0.x + 2.0f, cp0.y + 1.0f), ImVec2(cp0.x + csz.x - 2.0f, cp0.y + csz.y - 1.0f), true);
            const ImVec2 ts = font->CalcTextSizeA(fs, 1e9f, csz.x - 6.0f, label.c_str());
            dl->AddText(font, fs, ImVec2(cp0.x + 3.0f, cp0.y + (csz.y - ts.y) * 0.5f),
                dim ? IM_COL32(150, 140, 120, 200) : IM_COL32(230, 220, 190, 255), label.c_str(), nullptr, csz.x - 6.0f);
            dl->PopClipRect();
        };
    }
    int hit = fe_hud_cell(id, p0, ImVec2(kTile, kTile), o);
    // fe_hud_cell() with rclick enabled reports a right-click on the press frame (2) and then reports
    // the button *release* as an ordinary click (1) -- its invisible button fires on release for either
    // mouse button. Left unhandled a right-click would count -1 then +1: "decreases for a fraction of a
    // second, then restores". A release of the right button is never a left-click.
    if (hit == 1 && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        hit = 0;
    return hit;
}

// Small padlock in the tile's lower-left corner: "the level script controls this while it runs".
void draw_lock_overlay()
{
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImU32 gold = IM_COL32(230, 190, 60, 255);
    const float x = a.x + 4.0f, y = b.y - 4.0f;
    dl->AddRectFilled(ImVec2(x, y - 7.0f), ImVec2(x + 9.0f, y), gold, 1.5f);
    dl->AddCircle(ImVec2(x + 4.5f, y - 8.0f), 3.0f, gold, 12, 1.5f);
}

// The coloured player symbol for a slot. Deliberately the fixed base-sheet sprites (red, blue, green,
// yellow = GPS_plyrsym_symbol_player_*_std_b, consecutive) rather than get_player_colored_icon_idx():
// that remaps through the campaign's colored_sprites.zip, whose custom sprites are only loaded once a
// level starts -- in the menu it yields indices with no sprite behind them (the engine's magenta
// checkerboard placeholder). Slots past the four standard colours share the white symbol.
short player_symbol(int slot)
{
    if (slot >= 0 && slot < 4)
        return (short)(GPS_plyrsym_symbol_player_red_std_b + slot);
    return GPS_plyrsym_symbol_player_white_std;
}

std::string player_name(int slot)
{
    const SkirmishSetup &s = skirmish_setup();
    return "Player " + std::to_string(slot + 1) + ((slot == s.human_slot) ? " (you)" : "");
}

void draw_player_icon(int slot, float size)
{
    FeHudCellOpts o;
    o.sprite = player_symbol(slot);
    o.swallow = true;
    char id[16];
    snprintf(id, sizeof(id), "pi%d", slot);
    fe_hud_cell(id, ImGui::GetCursorScreenPos(), ImVec2(size, size), o);
    ImGui::Dummy(ImVec2(size, size));
}

// One caption naming what the level's Lua script also changes among `fields` (SetupField values), or nothing.
// Lua's OnGameStart runs after the classic script (and this setup) and top-level Lua runs before it, so for
// these the final value can differ from what the tab shows.
void lua_note(std::initializer_list<int> fields)
{
    const SetupLuaUse &u = skirmish_setup().analysis.lua;
    SetupLuaUse sub;
    for (int f : fields)
    {
        if (!u.touches(f))
            continue;
        switch (f)
        {
        case SetupField_Money: sub.money = true; break;
        case SetupField_MaxCreatures: sub.max_creatures = true; break;
        case SetupField_GenSpeed: sub.gen_speed = true; break;
        case SetupField_Pool: sub.pool = true; break;
        case SetupField_Controller: sub.controller = true; break;
        case SetupField_Ally: sub.ally = true; break;
        default:
            if (f >= 0 && f < AvailKind_Count)
                sub.avail[f] = true;
        }
    }
    if (!sub.any_setup())
        return;
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 190, 80, 255));
    FeCaption(("The level's Lua script also changes " + sub.describe() + ", so the result may differ from what is shown here.").c_str());
    ImGui::PopStyleColor();
}

// ---- General --------------------------------------------------------------
void draw_general()
{
    SkirmishSetup &s = skirmish_setup();
    const bool gen_locked = s.analysis.is_locked(SetupField_GenSpeed, -1);
    ImGui::BeginDisabled(gen_locked);
    int speed = (s.choices.values.generate_speed < 0) ? 0 : s.choices.values.generate_speed;
    FeCaption("Creature arrival interval (game turns; lower = creatures arrive faster)");
    ImGui::SetNextItemWidth(160.0f);
    if (FeInputInt("##genspeed", &speed, 10, 100, 0, 9999))
        skirmish_setup_set_generate_speed(speed > 0 ? speed : -1);
    ImGui::EndDisabled();
    if (s.analysis.seed.generate_speed >= 0)
    {
        ImGui::SameLine();
        FeCaption(("level: " + std::to_string(s.analysis.seed.generate_speed)).c_str());
    }
    if (gen_locked)
        FeCaption("Controlled by the level script.");
    lua_note({ SetupField_Money, SetupField_MaxCreatures, SetupField_GenSpeed, SetupField_Pool });

    FeSeparator();
    FeSubheading("Start gold and creature limit");
    for (int p = -1; p < s.players; p++)
    {
        ImGui::PushID(p + 1);
        if (p < 0)
        {
            FeBodyText("All players");
        }
        else
        {
            draw_player_icon(p, 22.0f);
            ImGui::SameLine();
            FeBodyText(player_name(p).c_str());
        }
        const int who = p; // -1 = all
        const int ref = (p < 0) ? 0 : p;
        const auto gm = s.choices.values.start_money.find(ref);
        int gold = (gm == s.choices.values.start_money.end()) ? 0 : gm->second;
        const auto mc = s.choices.values.max_creatures.find(ref);
        int maxc = (mc == s.choices.values.max_creatures.end()) ? 0 : mc->second;
        ImGui::SameLine(200.0f);
        ImGui::BeginDisabled(who >= 0 && s.analysis.is_locked(SetupField_Money, who));
        ImGui::SetNextItemWidth(150.0f);
        if (FeInputInt("##gold", &gold, 500, 5000, 0, kSetupSensibleGold))
            skirmish_setup_set_money(who, gold);
        ImGui::EndDisabled();
        ImGui::SameLine();
        FeCaption("gold");
        ImGui::SameLine();
        ImGui::BeginDisabled(who >= 0 && s.analysis.is_locked(SetupField_MaxCreatures, who));
        ImGui::SetNextItemWidth(120.0f);
        if (FeInputInt("##maxc", &maxc, 1, 5, 0, 255))
            skirmish_setup_set_max_creatures(who, maxc);
        ImGui::EndDisabled();
        ImGui::SameLine();
        FeCaption("max creatures");
        ImGui::PopID();
    }

    FeSeparator();
    FeSubheading("Creature pool");
    FeCaption("How many of each creature can arrive. Click +1, right-click -1 (Shift: 5).");
    const std::vector<Item> items = items_for_kind(AvailKind_Creature);
    const float width = ImGui::GetContentRegionAvail().x;
    const int cols = std::max(1, (int)((width + kGap) / (kTile + kGap)));
    const ImVec2 base = ImGui::GetCursorScreenPos();
    for (size_t i = 0; i < items.size(); i++)
    {
        const Item &it = items[i];
        const auto pe = s.choices.values.pool.find(it.code);
        const int have = (pe == s.choices.values.pool.end()) ? 0 : pe->second;
        const bool locked = s.analysis.is_locked(SetupField_Pool, -1, it.code);
        FeHudCellOpts o;
        o.rclick = true;
        o.dim = (have == 0);
        o.count = have;
        o.tooltip = it.tip.c_str();
        char id[48];
        snprintf(id, sizeof(id), "pool_%s", it.code.c_str());
        const ImVec2 p0(base.x + (float)(i % cols) * (kTile + kGap), base.y + (float)(i / cols) * (kTile + kGap));
        const int hit = draw_item_tile(id, it, p0, o);
        if (locked)
            draw_lock_overlay();
        const int delta = ImGui::GetIO().KeyShift ? 5 : 1;
        if (hit == 1)
            skirmish_setup_set_pool(it.code, have + delta);
        else if (hit == 2)
            skirmish_setup_set_pool(it.code, std::max(0, have - delta));
    }
    const int rows = (int)((items.size() + (size_t)cols - 1) / (size_t)cols);
    ImGui::SetCursorScreenPos(base);
    ImGui::Dummy(ImVec2(width, (float)rows * (kTile + kGap)));
}

// ---- Availability -------------------------------------------------------------
const char *state_name(int kind, SkirmishAvailState st)
{
    switch (st)
    {
    case SkirmishAvail_Research: return "researchable (not usable until researched)";
    case SkirmishAvail_Forced:   return "available, always arrives";
    case SkirmishAvail_On:
        return (kind == AvailKind_Creature) ? "available (can arrive)"
            : ((kind == AvailKind_Trap || kind == AvailKind_Door) ? "available to build" : "available");
    default: return "not available";
    }
}

void draw_player_selector()
{
    const SkirmishSetup &s = skirmish_setup();
    const ImVec2 base = ImGui::GetCursorScreenPos();
    const float sz = 34.0f;
    for (int i = -1; i < s.players; i++)
    {
        FeHudCellOpts o;
        o.selected = (s_avail_player == i);
        if (i < 0)
        {
            o.text = "All";
        }
        else
        {
            o.sprite = player_symbol(i);
        }
        static char tip[64];
        std::string t = (i < 0) ? std::string("Edit every player at once") : player_name(i);
        snprintf(tip, sizeof(tip), "%s", t.c_str());
        o.tooltip = tip;
        char id[16];
        snprintf(id, sizeof(id), "avp%d", i);
        if (fe_hud_cell(id, ImVec2(base.x + (float)(i + 1) * (sz + kGap), base.y), ImVec2(sz, sz), o) == 1)
            s_avail_player = i;
    }
    ImGui::SetCursorScreenPos(base);
    ImGui::Dummy(ImVec2((float)(s.players + 1) * (sz + kGap), sz + kGap));
}

void draw_availability()
{
    SkirmishSetup &s = skirmish_setup();
    if (s_avail_player >= s.players)
        s_avail_player = -1;
    FeCaption("Choose a player, then click a tile to cycle its state; right-click switches it off.");
    FeCaption("Green dot: available.  R: researchable only.  F: always arrives (creatures).  *: differs between players.  Padlock: set by the level script.");
    draw_player_selector();
    lua_note({ SetupField_AvailCreature, SetupField_AvailRoom, SetupField_AvailMagic, SetupField_AvailTrap, SetupField_AvailDoor });
    if (s.analysis.verdict == SetupVerdict_Partial)
        FeCaption("Tiles with a padlock are controlled by the level script while it runs.");

    for (int kind = 0; kind < AvailKind_Count; kind++)
    {
        FeSubheading(kind_title(kind));
        const std::vector<Item> items = items_for_kind(kind);
        const float width = ImGui::GetContentRegionAvail().x;
        const int cols = std::max(1, (int)((width + kGap) / (kTile + kGap)));
        const ImVec2 base = ImGui::GetCursorScreenPos();
        int locked_count = 0;
        for (const Item &it : items)
            locked_count += skirmish_setup_avail_locked(kind, s_avail_player, it.code) ? 1 : 0;
        for (size_t i = 0; i < items.size(); i++)
        {
            const Item &it = items[i];
            bool mixed = false;
            const SkirmishAvailState st = skirmish_setup_avail_state(kind, s_avail_player, it.code, &mixed);
            const bool locked = skirmish_setup_avail_locked(kind, s_avail_player, it.code);
            const bool trapdoor = (kind == AvailKind_Trap || kind == AvailKind_Door);
            const int amount = skirmish_setup_avail_amount(s_avail_player, kind, it.code);
            FeHudCellOpts o;
            o.rclick = true;
            o.dim = (st == SkirmishAvail_Off);
            o.have_dot = (st == SkirmishAvail_On || st == SkirmishAvail_Forced);
            o.hotkey = mixed ? "*" : (st == SkirmishAvail_Research ? "R" : (st == SkirmishAvail_Forced ? "F" : nullptr));
            o.count = (trapdoor && st == SkirmishAvail_On) ? amount : -1;
            o.selected = (trapdoor && s_stock_kind == kind && s_stock_item == it.code);
            static char tip[256];
            snprintf(tip, sizeof(tip), "%s\n%s%s%s", it.tip.c_str(), state_name(kind, st), mixed ? " (differs between players)" : "",
                locked ? "\nControlled by the level script while it runs." : "");
            o.tooltip = tip;
            char id[64];
            snprintf(id, sizeof(id), "av%d_%s", kind, it.code.c_str());
            const ImVec2 p0(base.x + (float)(i % cols) * (kTile + kGap), base.y + (float)(i / cols) * (kTile + kGap));
            const int hit = draw_item_tile(id, it, p0, o);
            if (locked)
                draw_lock_overlay();
            if (hit == 1)
            {
                const std::vector<SkirmishAvailState> cycle = skirmish_setup_avail_states(kind);
                size_t at = 0;
                for (size_t k = 0; k < cycle.size(); k++)
                    if (cycle[k] == st)
                        at = k;
                skirmish_setup_set_avail(kind, s_avail_player, it.code, cycle[(at + 1) % cycle.size()], amount);
                if (trapdoor)
                {
                    s_stock_kind = kind;
                    s_stock_item = it.code;
                }
            }
            else if (hit == 2)
            {
                skirmish_setup_set_avail(kind, s_avail_player, it.code, SkirmishAvail_Off, 0);
            }
        }
        const int rows = (int)((items.size() + (size_t)cols - 1) / (size_t)cols);
        ImGui::SetCursorScreenPos(base);
        ImGui::Dummy(ImVec2(width, (float)rows * (kTile + kGap)));
        if (locked_count > 0 && locked_count * 2 > (int)items.size())
            FeCaption("Most of these are chosen in-game on this level.");
    }

    if (s_stock_kind >= 0 && !s_stock_item.empty())
    {
        FeSeparator();
        FeBodyText(("Stock of " + pretty(s_stock_item)).c_str());
        int stock = skirmish_setup_avail_amount(s_avail_player, s_stock_kind, s_stock_item);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        if (FeInputInt("##stock", &stock, 1, 5, 0, 255))
            skirmish_setup_set_avail(s_stock_kind, s_avail_player, s_stock_item,
                SkirmishAvail_On, stock);
        ImGui::SameLine();
        FeCaption("ready to place at the start");
    }
}

// ---- Win / Lose -----------------------------------------------------------------
void draw_win_lose()
{
    SkirmishSetup &s = skirmish_setup();
    SetupChoices &c = s.choices;
    const char *const modes[] = { "Keep the level's rules", "Replace with my rules" };
    int mode = c.replace_win_lose ? 1 : 0;
    FeCaption("A rule ends the game when its conditions hold. Losing when your heart is destroyed is always on.");
    ImGui::SetNextItemWidth(260.0f);
    if (FeCombo("##winmode", &mode, modes, 2))
    {
        c.replace_win_lose = (mode == 1);
        if (c.replace_win_lose && c.rules.empty())
            c.rules = s.analysis.seed.rules;
    }

    if (s.analysis.lua.win || s.analysis.lua.lose)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 190, 80, 255));
        FeCaption("The level's Lua script has its own win/lose logic. Replace only replaces the classic script's rules; it cannot remove Lua's.");
        ImGui::PopStyleColor();
    }
    std::string text;
    if (!c.replace_win_lose)
    {
        for (const SetupWinLoseRule &r : s.analysis.seed.rules)
        {
            skirmish_setup_rule_summary(r, text);
            FeBodyText(text.c_str());
        }
        if (s.analysis.seed.rules.empty())
            FeBodyText("The level defines no rules the tab can show.");
        if (s.analysis.custom_win_lose > 0)
            FeCaption((std::to_string(s.analysis.custom_win_lose) + " more rule(s) in the level script are custom and always kept.").c_str());
        return;
    }

    if (s.analysis.custom_win_lose > 0)
        FeCaption((std::to_string(s.analysis.custom_win_lose) + " custom rule(s) in the level script are kept and cannot be replaced here.").c_str());
    int remove_at = -1;
    for (size_t i = 0; i < c.rules.size(); i++)
    {
        ImGui::PushID((int)i);
        skirmish_setup_rule_summary(c.rules[i], text);
        if (FeButton("Remove"))
            remove_at = (int)i;
        ImGui::SameLine();
        FeBodyText(text.c_str());
        ImGui::PopID();
    }
    if (remove_at >= 0)
        c.rules.erase(c.rules.begin() + remove_at);
    if (c.rules.empty())
        FeCaption("No rules: add at least one win rule or the game cannot be won.");

    FeSeparator();
    FeSubheading("Add a rule");
    if (FeButton("Last keeper standing"))
        for (const SetupWinLoseRule &r : skirmish_setup_template(SkirmishRule_LastKeeper, 0))
            c.rules.push_back(r);
    FeHelpTooltip("Each keeper wins when every other dungeon is destroyed.");
    ImGui::SameLine();
    if (FeButton("Survive"))
        for (const SetupWinLoseRule &r : skirmish_setup_template(SkirmishRule_SurviveMinutes, s_survive_minutes))
            c.rules.push_back(r);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    FeInputInt("##surv", &s_survive_minutes, 1, 5, 1, 600);
    ImGui::SameLine();
    FeCaption("minutes (you win)");
    if (FeButton("Gold target"))
        for (const SetupWinLoseRule &r : skirmish_setup_template(SkirmishRule_GoldTarget, s_gold_target))
            c.rules.push_back(r);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    FeInputInt("##goldt", &s_gold_target, 1000, 10000, 1, kSetupSensibleGold);
    ImGui::SameLine();
    FeCaption("gold (you win)");
    if (FeButton("Level's own rules"))
        c.rules = s.analysis.seed.rules;

    FeSeparator();
    FeSubheading("Custom rule");
    std::vector<const char *> vars;
    for (const char *const *p = script_setup_win_variables_identical(); *p; p++) vars.push_back(*p);
    for (const char *const *p = script_setup_win_variables_v1_only(); *p; p++) vars.push_back(*p);
    int nops = 0;
    const char *const *ops = script_setup_win_lose_operators(&nops);
    std::vector<std::string> players;
    for (int p = 0; p < s.players; p++) players.push_back(player_name(p));
    std::vector<const char *> player_ptrs;
    for (const std::string &n : players) player_ptrs.push_back(n.c_str());
    const char *const results[] = { "Lose", "Win" };
    FeCaption("The rule fires when every condition below holds.");
    ImGui::SetNextItemWidth(90.0f);
    FeCombo("##rres", &s_draft.win, results, 2);
    int remove_clause = -1;
    for (size_t ci = 0; ci < s_draft.clauses.size(); ci++)
    {
        ClauseDraft &cd = s_draft.clauses[ci];
        ImGui::PushID((int)ci);
        ImGui::SetNextItemWidth(150.0f);
        FeCombo("##rply", &cd.player, player_ptrs.data(), (int)player_ptrs.size());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(260.0f);
        FeCombo("##rvar", &cd.var, vars.data(), (int)vars.size());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        FeCombo("##rop", &cd.op, ops, nops);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        FeInputInt("##rval", &cd.value, 1, 100, -2147483647, 2147483647);
        if (s_draft.clauses.size() > 1)
        {
            ImGui::SameLine();
            if (FeButton("-"))
                remove_clause = (int)ci;
        }
        ImGui::PopID();
    }
    if (remove_clause >= 0)
        s_draft.clauses.erase(s_draft.clauses.begin() + remove_clause);
    if (s_draft.clauses.size() < 4 && FeButton("+ condition"))
        s_draft.clauses.push_back(ClauseDraft());
    ImGui::SameLine();
    if (FeButton("Add rule"))
    {
        SetupWinLoseRule r;
        r.win = (s_draft.win == 1);
        for (const ClauseDraft &cd : s_draft.clauses)
        {
            WinLoseClause cl;
            cl.player = cd.player;
            cl.variable = vars[(size_t)cd.var];
            cl.op = ops[cd.op];
            cl.value = cd.value;
            r.clauses.push_back(cl);
        }
        c.rules.push_back(r);
        s_draft = RuleDraft();
    }
}

// ---- Slots & AI ---------------------------------------------------------------------
void draw_slots()
{
    SkirmishSetup &s = skirmish_setup();
    std::vector<std::string> model_names;
    std::vector<int> model_ids;
    for (int i = 0; i < COMPUTER_MODELS_COUNT; i++)
    {
        if (comp_player_conf.computer_types[i].name[0] == '\0')
            continue;
        model_ids.push_back(i);
        // The presets the game picks from at random for a plain skirmish (keepcompp.cfg SkirmishFirst..SkirmishLast).
        const bool skirmish_preset = (i >= comp_player_conf.skirmish_first && i <= comp_player_conf.skirmish_last);
        model_names.push_back(std::to_string(i) + ": " + comp_player_conf.computer_types[i].name + (skirmish_preset ? "  [skirmish preset]" : ""));
    }
    std::vector<const char *> model_ptrs;
    for (const std::string &n : model_names) model_ptrs.push_back(n.c_str());

    FeCaption("Each computer keeper can use a different built-in AI. Unless the level says otherwise, the game picks a random skirmish preset for each.");
    lua_note({ SetupField_Controller, SetupField_Ally });
    for (int p = 0; p < s.players; p++)
    {
        ImGui::PushID(p);
        draw_player_icon(p, 26.0f);
        ImGui::SameLine();
        FeBodyText(player_name(p).c_str());
        ImGui::SameLine(200.0f);
        const bool no_heart = (p < (int)s.hearts.size()) && s.hearts[(size_t)p] == 0;
        if (p == s.human_slot)
        {
            FeCaption(no_heart ? "human (no Dungeon Heart on this map)" : "human");
        }
        else
        {
            const bool ctl_locked = s.analysis.is_locked(SetupField_Controller, p);
            int model = 0;
            const SkirmishControllerChoice ch = skirmish_setup_controller_choice(p, &model);
            // What "level default" means for this slot: the AI the level's script names, else the random skirmish preset.
            const auto seed_ctl = s.analysis.seed.controllers.find(p);
            std::string default_label = "Random skirmish AI (default)";
            if (seed_ctl != s.analysis.seed.controllers.end())
            {
                default_label = (seed_ctl->second.kind == SetupController::Roaming) ? "Level default: roaming"
                    : (seed_ctl->second.kind == SetupController::Off ? "Level default: off"
                    : "Level default: AI " + std::to_string(seed_ctl->second.model));
            }
            const char *const kinds[] = { default_label.c_str(), "Built-in AI", "Roaming", "Off (does nothing)" };
            int kind = (int)ch;
            ImGui::BeginDisabled(ctl_locked);
            ImGui::SetNextItemWidth(230.0f);
            bool changed = FeCombo("##ctl", &kind, kinds, 4);
            int model_idx = 0;
            for (size_t k = 0; k < model_ids.size(); k++)
                if (model_ids[k] == model)
                    model_idx = (int)k;
            if (kind == (int)SkirmishCtl_Model)
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(300.0f);
                if (FeCombo("##mdl", &model_idx, model_ptrs.data(), (int)model_ptrs.size()))
                    changed = true;
            }
            if (changed && !model_ids.empty())
                skirmish_setup_set_controller(p, (SkirmishControllerChoice)kind, model_ids[(size_t)model_idx]);
            ImGui::EndDisabled();
            if (ctl_locked)
            {
                ImGui::SameLine();
                FeCaption("decided by the level script");
            }
            else if (no_heart)
            {
                ImGui::SameLine();
                FeCaption("no Dungeon Heart on this map");
            }
        }
        ImGui::PopID();
    }

    FeSeparator();
    FeSubheading("Teams");
    FeCaption("Players on the same team are allied. Leave at 'None' for free-for-all.");
    for (int p = 0; p < s.players; p++)
    {
        ImGui::PushID(1000 + p);
        draw_player_icon(p, 22.0f);
        ImGui::SameLine();
        FeBodyText(player_name(p).c_str());
        ImGui::SameLine(200.0f);
        const bool ally_locked = s.analysis.is_locked(SetupField_Ally, p);
        const char *const teams[] = { "None", "Team 1", "Team 2", "Team 3", "Team 4" };
        int team = s.teams[(size_t)p];
        ImGui::BeginDisabled(ally_locked);
        ImGui::SetNextItemWidth(120.0f);
        if (FeCombo("##team", &team, teams, 5))
            skirmish_setup_set_team(p, team);
        ImGui::EndDisabled();
        if (ally_locked)
        {
            ImGui::SameLine();
            FeCaption("alliances are set by the level script");
        }
        ImGui::PopID();
    }
}

} // namespace

/******************************************************************************/
void frontgui_skirmish_setup_draw(float height)
{
    SkirmishSetup &s = skirmish_setup();
    const bool open = FeBeginScrollArea("##SkirmishSetupScroll", ImVec2(0, height));
    if (open)
    {
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
            s_setup_hover_frame = ImGui::GetFrameCount();
        if (!s.loaded)
        {
            FeBodyText("Select a level.");
        }
        else if (!s.enabled())
        {
            FeSubheading("Setup is not available for this level");
            FeBodyText(s.unavailable_reason.c_str());
        }
        else
        {
            const bool changed = skirmish_setup_is_changed();
            if (changed)
            {
                if (FeButton("Reset to the level's defaults"))
                    skirmish_setup_reset_choices();
                ImGui::SameLine();
                FeCaption("Setup changed: it applies when you press Play.");
            }
            else
            {
                FeCaption("Showing the level's own setup. Change anything below and it applies when you press Play.");
            }
            if (s.analysis.verdict == SetupVerdict_Partial)
                FeCaption(s.analysis.reason.c_str());
            if (s.analysis.uses_boxes)
                FeCaption("This level has in-game faction/choice mechanics: some setup is applied when you choose in-game.");
            if (s.analysis.uses_game_rule)
                FeCaption("This level also changes game rules from its script.");
            if (s.analysis.has_lua_companion && !s.analysis.lua.any_setup())
                FeCaption("This level also has a Lua script; it does not change these settings.");

            for (const SetupIssue &i : skirmish_setup_play_issues())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, (i.severity == SetupIssue_Error) ? IM_COL32(230, 90, 80, 255) : IM_COL32(230, 190, 80, 255));
                const std::string line = std::string((i.severity == SetupIssue_Error) ? "Problem: " : "Note: ") + i.message;
                FeBodyText(line.c_str());
                ImGui::PopStyleColor();
            }
            FeSeparator();
            if (FeCollapsingHeader("General", true))
                draw_general();
            if (FeCollapsingHeader("Availability"))
                draw_availability();
            if (FeCollapsingHeader("Win / Lose"))
                draw_win_lose();
            if (FeCollapsingHeader("Slots & AI"))
                draw_slots();
        }
    }
    FeEndScrollArea();
}

const char *frontgui_skirmish_setup_status(void)
{
    s_status[0] = '\0';
    for (const SetupIssue &i : skirmish_setup_play_issues())
        if (i.severity == SetupIssue_Error)
        {
            snprintf(s_status, sizeof(s_status), "Setup problem: %s", i.message.c_str());
            break;
        }
    return s_status;
}

int frontgui_skirmish_setup_captures_right_click(void)
{
    // The screen draws every frame the tab is visible; allow one frame of latency between the draw
    // that saw the hover and the (earlier-in-the-frame) legacy input pass that asks.
    return (ImGui::GetCurrentContext() != nullptr) && (ImGui::GetFrameCount() - s_setup_hover_frame <= 1);
}
