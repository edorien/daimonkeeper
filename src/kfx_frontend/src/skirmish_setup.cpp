/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file skirmish_setup.cpp
 *     See skirmish_setup.h.
 * @par Comment:
 *     None.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "skirmish_setup.h"

#include "bflib_basics.h"
#include "bflib_fileio.h"
#include "config.h"
#include "config_campaigns.h"
#include "config_creature.h"
#include "config_magic.h"
#include "config_terrain.h"
#include "config_trapdoor.h"
#include "level_script_override.h"
#include "map_content_reader.h"
#include "net_game.h"
#include "thing_objects.h" // ObjMdl_SoulCountainer (the Dungeon Heart)
#include "thing_data.h"    // TCls_Object
#include "lvl_filesdk1.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <set>
#include "post_inc.h"

/******************************************************************************/
namespace {

SkirmishSetup s_state;

std::string upper(std::string s)
{
    for (char &c : s)
        c = (char)toupper((unsigned char)c);
    return s;
}

bool avail_locked_for(int64_t kind, int64_t player, const std::string &item)
{
    return s_state.analysis.is_locked(kind, player, item);
}

// Players an operation on `player` (-1 = all) touches.
std::vector<int64_t> target_players(int64_t player)
{
    std::vector<int64_t> v;
    if (player >= 0)
        v.push_back(player);
    else
        for (int64_t p = 0; p < s_state.players; p++)
            v.push_back(p);
    return v;
}

SetupAvailValue avail_value(int64_t kind, int64_t player, const std::string &item)
{
    SetupAvailKey k;
    k.kind = kind;
    k.player = player;
    k.item = upper(item);
    const auto it = s_state.choices.values.avail.find(k);
    return (it == s_state.choices.values.avail.end()) ? SetupAvailValue() : it->second;
}

SkirmishAvailState state_of(int64_t kind, const SetupAvailValue &v)
{
    switch (kind)
    {
    case AvailKind_Creature:
        return (v.a != 0 && v.b != 0) ? SkirmishAvail_Forced : (v.a != 0 ? SkirmishAvail_On : SkirmishAvail_Off);
    case AvailKind_Room:
    case AvailKind_Magic:
        return (v.b != 0) ? SkirmishAvail_On : (v.a != 0 ? SkirmishAvail_Research : SkirmishAvail_Off);
    default: // traps, doors: (buildable, stock)
        return (v.a != 0) ? SkirmishAvail_On : SkirmishAvail_Off;
    }
}

SetupAvailValue value_for(int64_t kind, SkirmishAvailState st, int64_t amount)
{
    SetupAvailValue v;
    switch (kind)
    {
    case AvailKind_Creature:
        v.a = (st == SkirmishAvail_Off) ? 0 : 1;
        v.b = (st == SkirmishAvail_Forced) ? 1 : 0;
        break;
    case AvailKind_Room:
    case AvailKind_Magic:
        v.a = (st == SkirmishAvail_Off) ? 0 : 1;
        v.b = (st == SkirmishAvail_On) ? 1 : 0;
        break;
    default:
        v.a = (st == SkirmishAvail_Off) ? 0 : 1;
        v.b = (st == SkirmishAvail_Off) ? 0 : (amount < 0 ? 0 : amount);
        break;
    }
    return v;
}

bool config_has_item(int64_t field, const std::string &item)
{
    const struct NamedCommand *desc = nullptr;
    switch (field)
    {
    case AvailKind_Creature:
    case SetupField_Pool: desc = creature_desc; break;
    case AvailKind_Room:  desc = room_desc; break;
    case AvailKind_Magic: desc = power_desc; break;
    case AvailKind_Trap:  desc = trap_desc; break;
    case AvailKind_Door:  desc = door_desc; break;
    default: return true;
    }
    return get_rid(desc, item.c_str()) >= 0;
}

bool script_has_item(int64_t field, const std::string &item)
{
    const SetupAnalysis &a = s_state.analysis;
    if (field == SetupField_Pool)
        return a.seed.pool.count(item) > 0 || a.is_locked(SetupField_Pool, -1, item);
    for (const auto &kv : a.seed.avail)
        if (kv.first.kind == field && kv.first.item == item)
            return true;
    for (const SetupLockKey &l : a.locks)
        if (l.field == field && l.item == item)
            return true;
    return false;
}

// Which slots have a Dungeon Heart on the map, straight from the map's thing file (the same readers the
// editor and the game load with). -1 for every slot when the file cannot be read.
struct NativeThingsReader : public KfxNativeMapContentReader
{
    using KfxNativeMapContentReader::read_things;
};
struct ClassicThingsReader : public ClassicMapContentReader
{
    using ClassicMapContentReader::read_things;
};

std::vector<int64_t> read_hearts(LevelNumber lvnum, int64_t players)
{
    std::vector<int64_t> hearts((size_t)players, -1);
    const int64_t fgroup = get_level_fgroup(lvnum);
    char *probe = prepare_file_fmtpath(fgroup, "map%05" PRIu64 ".tngfx", (uint64_t)lvnum);
    const bool native = (probe != nullptr) && LbFileExists(probe);
    char *path = prepare_file_fmtpath(fgroup, native ? "map%05" PRIu64 ".tngfx" : "map%05" PRIu64 ".tng", (uint64_t)lvnum);
    if (path == nullptr || !LbFileExists(path))
        return hearts;
    std::string dir = path;
    const size_t slash = dir.find_last_of("/\\");
    dir = (slash == std::string::npos) ? std::string(".") : dir.substr(0, slash);

    MapContent content;
    const bool ok = native ? NativeThingsReader().read_things(content, dir.c_str(), lvnum)
                           : ClassicThingsReader().read_things(content, dir.c_str(), lvnum);
    if (!ok)
        return hearts;
    hearts.assign((size_t)players, 0);
    for (const MapThingRecord &t : content.things)
        if (t.thing_class == TCls_Object && t.model == ObjMdl_SoulCountainer && t.owner >= 0 && t.owner < players)
            hearts[(size_t)t.owner] = 1;
    return hearts;
}

std::string level_script_file_exists_lua(LevelNumber lvnum)
{
    char *fname = prepare_file_fmtpath(get_level_fgroup(lvnum), "map%05" PRIu64 ".lua", (uint64_t)lvnum);
    return LbFileExists(fname) ? "lua" : "";
}

} // namespace

/******************************************************************************/
SkirmishSetup &skirmish_setup() { return s_state; }

void skirmish_setup_forget()
{
    s_state = SkirmishSetup();
}

void skirmish_setup_load_from_text(LevelNumber lvnum, const std::string &text, int64_t players, bool has_lua,
    unsigned char lof_option, int64_t human_slot, const std::string *lua_text)
{
    s_state = SkirmishSetup();
    s_state.lvnum = lvnum;
    s_state.loaded = true;
    s_state.players = (players < 2) ? 2 : players;
    s_state.human_slot = human_slot;
    s_state.text = text;
    s_state.analysis = script_setup_analyse(text, s_state.players, has_lua, lua_text);
    s_state.hearts.assign((size_t)s_state.players, -1);
    s_state.choices = script_setup_default_choices(s_state.analysis);
    s_state.teams.assign((size_t)s_state.players, 0);
    if (lof_option == SkirmishSetup_Locked)
        s_state.unavailable_reason = "The map's author has disabled setup for this level.";
    else if (s_state.analysis.verdict == SetupVerdict_Unsupported)
        s_state.unavailable_reason = s_state.analysis.reason;
}

void skirmish_setup_sync(LevelNumber lvnum, int64_t human_slot)
{
    if (lvnum <= 0)
    {
        if (s_state.loaded)
            skirmish_setup_forget();
        return;
    }
    if (s_state.loaded && s_state.lvnum == lvnum)
        return;
    const struct LevelInformation *lvinfo = get_level_info(lvnum);
    const int64_t players = (lvinfo != nullptr) ? (int64_t)lvinfo->players : 2;
    const unsigned char option = (lvinfo != nullptr) ? lvinfo->skirmish_setup : (unsigned char)SkirmishSetup_Auto;
    const bool has_lua = !level_script_file_exists_lua(lvnum).empty();

    int64_t len = 1;
    unsigned char *buf = load_single_map_file_to_buffer(lvnum, "txt", &len, LMFF_Optional);
    if (buf == nullptr)
    {
        skirmish_setup_load_from_text(lvnum, std::string(), players, has_lua, option, human_slot);
        s_state.unavailable_reason = has_lua
            ? "This level is scripted in Lua, which the setup tab cannot edit."
            : "The level has no script to customise.";
        return;
    }
    const std::string text((const char *)buf, (size_t)len);
    free(buf);
    std::string lua;
    if (has_lua)
    {
        int64_t lua_len = 1;
        unsigned char *lua_buf = load_single_map_file_to_buffer(lvnum, "lua", &lua_len, LMFF_Optional);
        if (lua_buf != nullptr)
        {
            lua.assign((const char *)lua_buf, (size_t)lua_len);
            free(lua_buf);
        }
    }
    skirmish_setup_load_from_text(lvnum, text, players, has_lua, option, human_slot, has_lua ? &lua : nullptr);
    s_state.hearts = read_hearts(lvnum, s_state.players);
}

void skirmish_setup_reset_choices()
{
    if (!s_state.loaded)
        return;
    s_state.choices = script_setup_default_choices(s_state.analysis);
    s_state.teams.assign((size_t)s_state.players, 0);
    s_state.external_slots.clear();
}

static SetupChoices choices_with_allies()
{
    SetupChoices c = s_state.choices;
    c.allies.clear();
    for (int64_t i = 0; i < s_state.players; i++)
        for (int64_t j = i + 1; j < s_state.players; j++)
            if (s_state.teams[(size_t)i] != 0 && s_state.teams[(size_t)i] == s_state.teams[(size_t)j])
                c.allies.push_back(std::make_pair(i, j));
    return c;
}

bool skirmish_setup_is_changed()
{
    if (!s_state.enabled())
        return false;
    return !script_setup_choices_are_default(s_state.analysis, choices_with_allies());
}

SetupOverride skirmish_setup_build()
{
    SetupOverride none;
    if (!s_state.enabled())
        return none;
    SetupBuildOptions opt;
    opt.players = s_state.players;
    opt.item_exists = [](int64_t field, const std::string &item) {
        return script_has_item(field, item) || config_has_item(field, item);
    };
    return script_setup_build_override(s_state.text, s_state.analysis, choices_with_allies(), opt);
}

std::vector<SetupIssue> skirmish_setup_play_issues()
{
    std::vector<SetupIssue> out;
    if (!s_state.enabled())
        return out;
    for (const SetupIssue &i : skirmish_setup_build().issues)
        out.push_back(i);
    // A controller for a slot the map has no Dungeon Heart for does nothing: say so (a note, not an error).
    for (const auto &kv : s_state.choices.values.controllers)
    {
        const int64_t slot = kv.first;
        if (slot >= 0 && slot < (int64_t)s_state.hearts.size() && s_state.hearts[(size_t)slot] == 0
            && !(s_state.analysis.seed.controllers.count(slot) && s_state.analysis.seed.controllers.at(slot) == kv.second))
        {
            SetupIssue i;
            i.severity = SetupIssue_Warning;
            i.message = "Player " + std::to_string(slot + 1) + " has no Dungeon Heart on this map, so its computer player setting has no effect.";
            out.push_back(i);
        }
    }
    return out;
}

/******************************************************************************/
std::vector<SkirmishAvailState> skirmish_setup_avail_states(int64_t kind)
{
    switch (kind)
    {
    case AvailKind_Creature:
        return { SkirmishAvail_Off, SkirmishAvail_On, SkirmishAvail_Forced };
    case AvailKind_Room:
    case AvailKind_Magic:
        return { SkirmishAvail_Off, SkirmishAvail_Research, SkirmishAvail_On };
    default:
        return { SkirmishAvail_Off, SkirmishAvail_On };
    }
}

SkirmishAvailState skirmish_setup_avail_state(int64_t kind, int64_t player, const std::string &item, bool *mixed)
{
    if (mixed != nullptr)
        *mixed = false;
    const std::vector<int64_t> ps = target_players(player);
    if (ps.empty())
        return SkirmishAvail_Off;
    const SetupAvailValue first = avail_value(kind, ps[0], item);
    if (mixed != nullptr)
        for (int64_t p : ps)
        {
            const SetupAvailValue v = avail_value(kind, p, item);
            if (state_of(kind, v) != state_of(kind, first) || ((kind == AvailKind_Trap || kind == AvailKind_Door) && v.b != first.b))
                *mixed = true;
        }
    return state_of(kind, first);
}

int64_t skirmish_setup_avail_amount(int64_t player, int64_t kind, const std::string &item)
{
    const std::vector<int64_t> ps = target_players(player);
    return ps.empty() ? 0 : avail_value(kind, ps[0], item).b;
}

bool skirmish_setup_avail_locked(int64_t kind, int64_t player, const std::string &item)
{
    for (int64_t p : target_players(player))
        if (avail_locked_for(kind, p, upper(item)))
            return true;
    return false;
}

void skirmish_setup_set_avail(int64_t kind, int64_t player, const std::string &item, SkirmishAvailState state, int64_t amount)
{
    if (!s_state.enabled())
        return;
    const std::string name = upper(item);
    for (int64_t p : target_players(player))
    {
        if (avail_locked_for(kind, p, name))
            continue;
        SetupAvailKey k;
        k.kind = kind;
        k.player = p;
        k.item = name;
        const SetupAvailValue v = value_for(kind, state, amount);
        if (state == SkirmishAvail_Off && s_state.analysis.seed.avail.find(k) == s_state.analysis.seed.avail.end())
            s_state.choices.values.avail.erase(k); // never in the level: back to "not mentioned"
        else
            s_state.choices.values.avail[k] = v;
    }
}

std::vector<std::string> skirmish_setup_script_items(int64_t kind)
{
    std::set<std::string> names;
    for (const auto &kv : s_state.analysis.seed.avail)
        if (kv.first.kind == kind)
            names.insert(kv.first.item);
    for (const SetupLockKey &l : s_state.analysis.locks)
        if (l.field == kind && !l.item.empty())
            names.insert(l.item);
    return std::vector<std::string>(names.begin(), names.end());
}

/******************************************************************************/
void skirmish_setup_set_pool(const std::string &creature, int64_t amount)
{
    if (!s_state.enabled())
        return;
    const std::string name = upper(creature);
    if (s_state.analysis.is_locked(SetupField_Pool, -1, name))
        return;
    if (amount <= 0)
        s_state.choices.values.pool.erase(name);
    else
        s_state.choices.values.pool[name] = amount;
}

void skirmish_setup_set_money(int64_t player, int64_t gold)
{
    if (!s_state.enabled())
        return;
    for (int64_t p : target_players(player))
    {
        if (s_state.analysis.is_locked(SetupField_Money, p))
            continue;
        if (gold <= 0)
            s_state.choices.values.start_money.erase(p);
        else
            s_state.choices.values.start_money[p] = gold;
    }
}

void skirmish_setup_set_max_creatures(int64_t player, int64_t count)
{
    if (!s_state.enabled())
        return;
    for (int64_t p : target_players(player))
    {
        if (s_state.analysis.is_locked(SetupField_MaxCreatures, p))
            continue;
        if (count < 0)
            s_state.choices.values.max_creatures.erase(p);
        else
            s_state.choices.values.max_creatures[p] = count;
    }
}

void skirmish_setup_set_generate_speed(int64_t speed)
{
    if (!s_state.enabled() || s_state.analysis.is_locked(SetupField_GenSpeed, -1))
        return;
    s_state.choices.values.generate_speed = (speed < 0) ? s_state.analysis.seed.generate_speed : speed;
}

/******************************************************************************/
SkirmishControllerChoice skirmish_setup_controller_choice(int64_t slot, int64_t *model)
{
    if (model != nullptr)
        *model = 0;
    if (std::find(s_state.external_slots.begin(), s_state.external_slots.end(), slot) != s_state.external_slots.end())
        return SkirmishCtl_External;
    const auto it = s_state.choices.values.controllers.find(slot);
    if (it == s_state.choices.values.controllers.end())
        return SkirmishCtl_LevelDefault;
    const auto seed = s_state.analysis.seed.controllers.find(slot);
    if (seed != s_state.analysis.seed.controllers.end() && seed->second == it->second)
        return SkirmishCtl_LevelDefault;
    if (model != nullptr)
        *model = it->second.model;
    switch (it->second.kind)
    {
    case SetupController::Roaming: return SkirmishCtl_Roaming;
    case SetupController::Off:     return SkirmishCtl_Off;
    default:                       return SkirmishCtl_Model;
    }
}

void skirmish_setup_set_controller(int64_t slot, SkirmishControllerChoice choice, int64_t model)
{
    if (!s_state.enabled() || slot == s_state.human_slot || s_state.analysis.is_locked(SetupField_Controller, slot))
        return;
    std::map<int64_t, SetupController> &m = s_state.choices.values.controllers;
    auto &ext = s_state.external_slots;
    ext.erase(std::remove(ext.begin(), ext.end(), slot), ext.end());
    if (choice == SkirmishCtl_External)
    {
        // The slot starts as the level's own computer keeper and is handed to the agent when the game starts
        // (net_claim_pending_external_seats), so the script side stays at the level default.
        ext.push_back(slot);
        choice = SkirmishCtl_LevelDefault;
    }
    if (choice == SkirmishCtl_LevelDefault)
    {
        const auto seed = s_state.analysis.seed.controllers.find(slot);
        if (seed != s_state.analysis.seed.controllers.end())
            m[slot] = seed->second;
        else
            m.erase(slot);
        return;
    }
    SetupController c;
    c.model = model;
    c.kind = (choice == SkirmishCtl_Roaming) ? SetupController::Roaming
        : (choice == SkirmishCtl_Off ? SetupController::Off : SetupController::Model);
    m[slot] = c;
}

void skirmish_setup_set_team(int64_t slot, int64_t team)
{
    if (!s_state.enabled() || slot < 0 || slot >= (int64_t)s_state.teams.size() || s_state.analysis.is_locked(SetupField_Ally, slot))
        return;
    s_state.teams[(size_t)slot] = (team < 0) ? 0 : team;
}

/******************************************************************************/
std::vector<SetupWinLoseRule> skirmish_setup_template(SkirmishRuleTemplate t, int64_t value)
{
    std::vector<SetupWinLoseRule> rules;
    auto one = [](int64_t player, const char *var, const char *op, int64_t v) {
        SetupWinLoseRule r;
        r.win = true;
        WinLoseClause c;
        c.player = player;
        c.variable = var;
        c.op = op;
        c.value = v;
        r.clauses.push_back(c);
        return r;
    };
    switch (t)
    {
    case SkirmishRule_LastKeeper:
        for (int64_t p = 0; p < s_state.players; p++)
            rules.push_back(one(p, "ALL_DUNGEONS_DESTROYED", "==", 1));
        break;
    case SkirmishRule_SurviveMinutes:
        rules.push_back(one(s_state.human_slot, "GAME_TURN", ">=", (value < 1 ? 1 : value) * 20 * 60)); // 20 game turns per second
        break;
    case SkirmishRule_GoldTarget:
        rules.push_back(one(s_state.human_slot, "MONEY", ">=", value < 1 ? 1 : value));
        break;
    }
    return rules;
}

bool skirmish_setup_rule_summary(const SetupWinLoseRule &rule, std::string &out)
{
    out = rule.win ? "Win when " : "Lose when ";
    for (size_t i = 0; i < rule.clauses.size(); i++)
    {
        const WinLoseClause &c = rule.clauses[i];
        std::string var = c.variable;
        for (char &ch : var)
            ch = (ch == '_') ? ' ' : (char)tolower((unsigned char)ch);
        out += (i ? " and " : "") + std::string("player ") + std::to_string(c.player + 1) + ": " + var + " " + c.op + " " + std::to_string(c.value);
    }
    return !rule.clauses.empty();
}

/******************************************************************************/
extern "C" void skirmish_setup_install_for_play(LevelNumber lvnum)
{
    level_script_override_clear();
    net_pending_external_seats_clear();
    if (s_state.enabled() && s_state.lvnum == lvnum)
        for (const int64_t slot : s_state.external_slots)
            if (slot != s_state.human_slot)
                net_pending_external_seats_add((PlayerNumber)slot);
    if (!s_state.enabled() || s_state.lvnum != lvnum || !skirmish_setup_is_changed())
        return;
    const SetupOverride o = skirmish_setup_build();
    if (o.active && !o.has_errors())
    {
        level_script_override_set(lvnum, o.prelude.c_str(), o.masked.c_str());
        JUSTLOG("Skirmish setup: installed a script override for level %" PRId64 " (%" PRId64 " prelude bytes)", (int64_t)lvnum, (int64_t)o.prelude.size());
    }
}

extern "C" int64_t skirmish_setup_play_blocked(LevelNumber lvnum)
{
    if (!s_state.enabled() || s_state.lvnum != lvnum || !skirmish_setup_is_changed())
        return 0;
    return skirmish_setup_build().has_errors() ? 1 : 0;
}
