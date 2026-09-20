/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file script_setup_analysis.cpp
 *     See script_setup_analysis.h.
 * @par Comment:
 *     None.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "script_setup_analysis.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <set>
#include "post_inc.h"

/******************************************************************************/
namespace {

// docs/refactor/skirmish/01-...md §13.1: identical name AND meaning in
// variable_desc (v1) and dk1_variable_desc (v0). TOTAL_CREATURES is left out
// on purpose (v0 = IF_CONTROLS semantics, v1 = plain total), TOTAL_IMPS is the
// v0 spelling of TOTAL_DIGGERS (handled as an alias below).
const char *const kVarsIdentical[] = {
    "ALL_DUNGEONS_DESTROYED", "BATTLES_LOST", "BATTLES_WON", "CREATURES_ANNOYED",
    "CREATURES_SCAVENGED_GAINED", "CREATURES_SCAVENGED_LOST", "DOORS_DESTROYED",
    "DUNGEON_DESTROYED", "GAME_TURN", "GOLD_POTS_STOLEN", "MONEY", "ROOMS_DESTROYED",
    "SPELLS_STOLEN", "TIMES_BROKEN_INTO", "TOTAL_AREA", "TOTAL_CREATURES_LEFT",
    "TOTAL_DOORS", "TOTAL_GOLD_MINED", "TOTAL_RESEARCH", nullptr,
};

const char *const kVarsV1Only[] = {
    "ACTIVE_BATTLES", "BONUS_TIME", "CONTROLLED_THING", "CREATURES_CONVERTED",
    "CREATURES_FROM_SACRIFICE", "CREATURES_SACRIFICED", "CREATURES_TRANSFERRED",
    "CURRENT_SALARY", "DOORS_SOLD", "EVIL_CREATURES", "EVIL_CREATURES_CONVERTED",
    "GHOSTS_RAISED", "GOOD_CREATURES", "GOOD_CREATURES_CONVERTED", "HEART_HEALTH",
    "KEEPERS_DESTROYED", "MANAGE_SCORE", "MANUFACTURED_SOLD", "MANUFACTURE_GOLD",
    "PLAYER_SCORE", "SCORE", "SKELETONS_RAISED", "TIMES_ANNOYED_CREATURE",
    "TIMES_LEVELUP_CREATURE", "TIMES_TORTURED_CREATURE", "TOTAL_DIGGERS",
    "TOTAL_DOORS_MANUFACTURED", "TOTAL_DOORS_USED", "TOTAL_MANUFACTURED", "TOTAL_SALARY",
    "TOTAL_SCORE", "TOTAL_SLAPS", "TOTAL_TRAPS", "TOTAL_TRAPS_MANUFACTURED",
    "TOTAL_TRAPS_USED", "TRAPS_SOLD", "VAMPIRES_RAISED", "VIEW_TYPE", nullptr,
};

bool in_list(const char *const *list, const std::string &name)
{
    for (int i = 0; list[i] != nullptr; i++)
        if (name == list[i])
            return true;
    return false;
}

// Variable a win/lose clause may use for this file version; `out` gets the
// v1 spelling. False -> the rule is not modelled (kept as a custom rule).
bool win_variable_ok(const std::string &name, int version, std::string &out)
{
    if (in_list(kVarsIdentical, name) || (version >= 1 && in_list(kVarsV1Only, name)))
    {
        out = name;
        return true;
    }
    if (version == 0 && name == "TOTAL_IMPS")
    {
        out = "TOTAL_DIGGERS";
        return true;
    }
    return false;
}

std::string upper(std::string s)
{
    for (char &c : s)
        c = (char)toupper((unsigned char)c);
    return s;
}

bool parse_int(const std::string &s, int &out)
{
    size_t i = (!s.empty() && s[0] == '-') ? 1 : 0;
    if (i >= s.size())
        return false;
    for (size_t k = i; k < s.size(); k++)
        if (!isdigit((unsigned char)s[k]))
            return false;
    out = atoi(s.c_str());
    return true;
}

bool is_identifier(const std::string &s)
{
    if (s.empty())
        return false;
    for (char c : s)
        if (!isalnum((unsigned char)c) && c != '_')
            return false;
    return true;
}

// PLAYERn -> n (>= 0); anything else (ALL_PLAYERS, PLAYER_GOOD, ...) -> -1.
int single_player(const std::string &token)
{
    static const char kPrefix[] = "PLAYER";
    static const size_t kLen = sizeof(kPrefix) - 1;
    if (token.size() <= kLen || token.compare(0, kLen, kPrefix) != 0)
        return -1;
    int n = 0;
    for (size_t i = kLen; i < token.size(); i++)
    {
        if (!isdigit((unsigned char)token[i]))
            return -1;
        n = n * 10 + (token[i] - '0');
    }
    return n;
}

// Expands a player token to concrete player numbers. Sets `all` for
// ALL_PLAYERS. False for tokens the tab does not model (PLAYER_GOOD, ...).
bool expand_players(const std::string &token, int players, std::vector<int> &out, bool &all)
{
    out.clear();
    all = false;
    if (token == "ALL_PLAYERS")
    {
        all = true;
        for (int p = 0; p < players; p++)
            out.push_back(p);
        return true;
    }
    const int p = single_player(token);
    if (p < 0)
        return false;
    out.push_back(p);
    return true;
}

struct Item
{
    size_t line = 0;
    std::string text; // trimmed line content
    std::string cmd;  // leading identifier, verbatim case
    std::vector<std::string> args;
    bool clean = false; // "CMD(args)" and nothing after, not in a /* */ comment
    int depth = 0;      // nesting depth this item sits at (ENDIF: depth after closing)
    bool reusable = false;
    long match = -1; // opener <-> ENDIF item index
    bool opener = false;
    bool endif = false;
};

struct Seg
{
    std::string content, term;
};

std::vector<Seg> split_lines(const std::string &text)
{
    std::vector<Seg> segs;
    size_t pos = 0;
    while (true)
    {
        const size_t nl = text.find('\n', pos);
        Seg s;
        if (nl == std::string::npos)
        {
            s.content = text.substr(pos);
            segs.push_back(s);
            break;
        }
        std::string body = text.substr(pos, nl - pos);
        s.term = "\n";
        if (!body.empty() && body.back() == '\r')
        {
            body.pop_back();
            s.term = "\r\n";
        }
        s.content = body;
        segs.push_back(s);
        pos = nl + 1;
    }
    return segs;
}

const struct { const char *name; int kind; } kAvailCommands[] = {
    { "CREATURE_AVAILABLE", AvailKind_Creature }, { "ROOM_AVAILABLE", AvailKind_Room },
    { "MAGIC_AVAILABLE", AvailKind_Magic },       { "TRAP_AVAILABLE", AvailKind_Trap },
    { "DOOR_AVAILABLE", AvailKind_Door },
};

bool opens_block(const std::string &cmd)
{
    return cmd.compare(0, 2, "IF") == 0 && (cmd.size() == 2 || cmd[2] == '_');
}

} // namespace

bool SetupLockKey::operator<(const SetupLockKey &o) const
{
    if (field != o.field)
        return field < o.field;
    if (player != o.player)
        return player < o.player;
    return item < o.item;
}

bool SetupAvailKey::operator<(const SetupAvailKey &o) const
{
    if (kind != o.kind)
        return kind < o.kind;
    if (player != o.player)
        return player < o.player;
    return item < o.item;
}

bool SetupAnalysis::is_locked(int field, int player, const std::string &item) const
{
    for (const SetupLockKey &l : locks)
    {
        if (l.field != field)
            continue;
        if (l.player != -1 && player != -1 && l.player != player)
            continue;
        if (!l.item.empty() && !item.empty() && l.item != item)
            continue;
        return true;
    }
    return false;
}

const char *const *script_setup_win_variables_identical(void) { return kVarsIdentical; }
const char *const *script_setup_win_variables_v1_only(void) { return kVarsV1Only; }

SetupAnalysis script_setup_analyse(const std::string &text, int players, bool has_lua_companion, const std::string *lua_text)
{
    SetupAnalysis res;
    res.has_lua_companion = has_lua_companion || (lua_text != nullptr);
    if (lua_text != nullptr)
        res.lua = script_setup_scan_lua(*lua_text);
    res.level_version = script_setup_level_version(text);

    const std::vector<Seg> segs = split_lines(text);
    res.line_count = segs.size();
    res.owned_setup.assign(segs.size(), false);
    res.owned_win_lose.assign(segs.size(), false);

    // ---- lex: skip comments, build the command items --------------------
    std::vector<Item> items;
    bool in_ml = false;
    for (size_t li = 0; li < segs.size(); li++)
    {
        std::string content = segs[li].content;
        bool touched_ml = false;
        if (in_ml)
        {
            const size_t e = content.find("*/");
            if (e == std::string::npos)
                continue;
            content = content.substr(e + 2);
            in_ml = false;
            touched_ml = true;
        }
        std::string t = script_setup_trim(content);
        if (t.compare(0, 2, "/*") == 0)
        {
            const size_t e = t.find("*/", 2);
            touched_ml = true;
            if (e == std::string::npos)
            {
                in_ml = true;
                continue;
            }
            t = script_setup_trim(t.substr(e + 2));
        }
        if (t.empty())
            continue;
        size_t n = 0;
        while (n < t.size() && (isalnum((unsigned char)t[n]) || t[n] == '_'))
            n++;
        Item it;
        it.line = li;
        it.text = t;
        it.cmd = t.substr(0, n);
        if (it.cmd == "REM")
            continue;
        size_t p = n;
        while (p < t.size() && isspace((unsigned char)t[p]))
            p++;
        if (p < t.size() && t[p] == '(')
        {
            const size_t close = t.rfind(')');
            it.clean = !touched_ml && close != std::string::npos && close > p && script_setup_trim(t.substr(close + 1)).empty();
            it.args = script_setup_split_args(t.substr(p));
        }
        else
        {
            it.clean = !touched_ml && p >= t.size(); // bare command (WIN_GAME, ENDIF, ...)
        }
        items.push_back(it);
    }

    // ---- structure: depth, reusable flag, IF/ENDIF matching --------------
    int depth = 0;
    bool reuse = false;
    bool balanced = true;
    std::vector<size_t> open_stack;
    for (size_t i = 0; i < items.size(); i++)
    {
        Item &it = items[i];
        if (opens_block(it.cmd))
        {
            it.opener = true;
            it.depth = depth++;
            open_stack.push_back(i);
            reuse = false;
        }
        else if (it.cmd == "ENDIF")
        {
            it.endif = true;
            if (open_stack.empty())
            {
                // The engine logs "unexpected ENDIF" and ignores it (pop_condition());
                // shipped dk2maps scripts contain several. Tolerated, counted.
                res.stray_endif++;
                it.depth = 0;
            }
            else
            {
                it.match = (long)open_stack.back();
                items[open_stack.back()].match = (long)i;
                open_stack.pop_back();
                it.depth = --depth;
            }
            reuse = false;
        }
        else if (it.cmd == "NEXT_COMMAND_REUSABLE")
        {
            it.depth = depth;
            reuse = true;
        }
        else
        {
            it.depth = depth;
            it.reusable = reuse;
            reuse = false;
        }
    }
    if (!open_stack.empty())
        balanced = false;

    for (const Item &it : items)
    {
        if (it.opener)
            res.if_count++;
        else if (it.cmd == "WIN_GAME")
            res.win_count++;
        else if (it.cmd == "LOSE_GAME")
            res.lose_count++;
    }

    // ---- setup commands: seed (static) or locks (runtime) -----------------
    std::set<SetupLockKey> locks;
    auto lock = [&](int field, int player, const std::string &item) {
        SetupLockKey k;
        k.field = field;
        k.player = player;
        k.item = item;
        locks.insert(k);
    };
    for (const Item &it : items)
    {
        if (it.cmd == "SET_BOX_TOOLTIP")
            res.uses_boxes = true;
        else if (it.cmd == "SET_GAME_RULE")
            res.uses_game_rule = true;
        if (it.text.find("_ACTIVATED") != std::string::npos && it.text.find("BOX") != std::string::npos)
            res.uses_boxes = true; // also on IF lines: IF(PLAYERn,BOXn_ACTIVATED>0)
        if (it.opener || it.endif || it.cmd == "NEXT_COMMAND_REUSABLE")
            continue;

        const bool runtime = (it.depth > 0) || it.reusable;
        std::vector<int> pl;
        bool all = false;
        int kind = -1;
        for (const auto &ac : kAvailCommands)
            if (it.cmd == ac.name)
                kind = ac.kind;

        if (kind >= 0)
        {
            if (it.args.size() != 4 || !expand_players(it.args[0], players, pl, all) || !is_identifier(it.args[1]))
                continue;
            const std::string item = upper(it.args[1]);
            if (runtime)
            {
                lock(kind, all ? -1 : pl[0], item);
                continue;
            }
            int a, b;
            if (!it.clean || !parse_int(it.args[2], a) || !parse_int(it.args[3], b))
                continue;
            if (kind == AvailKind_Creature && res.level_version == 0)
            {
                // v0: 3rd argument ignored, 4th is "available", never forced.
                a = b;
                b = 0;
            }
            for (int p : pl)
            {
                SetupAvailKey key;
                key.kind = kind;
                key.player = p;
                key.item = item;
                SetupAvailValue &v = res.seed.avail[key];
                v.a = a;
                if (kind == AvailKind_Trap || kind == AvailKind_Door)
                    v.b += b; // amounts accumulate in the engine
                else
                    v.b = b;
            }
            res.owned_setup[it.line] = true;
        }
        else if (it.cmd == "START_MONEY" || it.cmd == "MAX_CREATURES")
        {
            const bool money = (it.cmd == "START_MONEY");
            if (it.args.size() != 2 || !expand_players(it.args[0], players, pl, all))
                continue;
            if (runtime)
            {
                lock(money ? SetupField_Money : SetupField_MaxCreatures, all ? -1 : pl[0], std::string());
                continue;
            }
            int v;
            if (!it.clean || !parse_int(it.args[1], v))
                continue;
            for (int p : pl)
            {
                if (money)
                    res.seed.start_money[p] += v; // additive
                else
                    res.seed.max_creatures[p] = v;
            }
            res.owned_setup[it.line] = true;
        }
        else if (it.cmd == "SET_GENERATE_SPEED")
        {
            if (it.args.size() != 1)
                continue;
            if (runtime)
            {
                lock(SetupField_GenSpeed, -1, std::string());
                continue;
            }
            int v;
            if (!it.clean || !parse_int(it.args[0], v))
                continue;
            res.seed.generate_speed = v;
            res.owned_setup[it.line] = true;
        }
        else if (it.cmd == "ADD_CREATURE_TO_POOL")
        {
            if (it.args.size() != 2 || !is_identifier(it.args[0]))
                continue;
            const std::string name = upper(it.args[0]);
            if (runtime)
            {
                lock(SetupField_Pool, -1, name);
                continue;
            }
            int v;
            if (!it.clean || !parse_int(it.args[1], v))
                continue;
            res.seed.pool[name] += v;
            res.owned_setup[it.line] = true;
        }
        else if (it.cmd == "COMPUTER_PLAYER")
        {
            if (it.args.size() != 2 || !expand_players(it.args[0], players, pl, all))
                continue;
            if (runtime)
            {
                lock(SetupField_Controller, all ? -1 : pl[0], std::string());
                continue;
            }
            SetupController c;
            const std::string tok = upper(it.args[1]);
            if (!it.clean)
                continue;
            if (parse_int(tok, c.model))
                c.kind = SetupController::Model;
            else if (res.level_version >= 1 && tok == "ROAMING")
                c.kind = SetupController::Roaming;
            else if (res.level_version >= 1 && tok == "OFF")
                c.kind = SetupController::Off;
            else
                continue;
            for (int p : pl)
                res.seed.controllers[p] = c;
            res.owned_setup[it.line] = true;
        }
        else if (it.cmd == "SET_COMPUTER_GLOBALS" || it.cmd == "SET_COMPUTER_CHECKS"
            || it.cmd == "SET_COMPUTER_EVENT" || it.cmd == "SET_COMPUTER_PROCESS")
        {
            // Static ones stay in the script (they tune whatever controller the
            // prelude installs); only runtime ones lock the slot.
            if (runtime && !it.args.empty() && expand_players(it.args[0], players, pl, all))
                lock(SetupField_Controller, all ? -1 : pl[0], std::string());
        }
        else if (it.cmd == "ALLY_PLAYERS")
        {
            if (runtime && it.args.size() >= 2)
            {
                bool all2 = false;
                std::vector<int> pl2;
                if (expand_players(it.args[0], players, pl, all))
                    lock(SetupField_Ally, all ? -1 : pl[0], std::string());
                if (expand_players(it.args[1], players, pl2, all2))
                    lock(SetupField_Ally, all2 ? -1 : pl2[0], std::string());
            }
        }
    }
    res.locks.assign(locks.begin(), locks.end());

    // ---- win/lose blocks -------------------------------------------------
    if (balanced)
    {
        for (size_t k = 0; k < items.size(); k++)
        {
            const Item &top = items[k];
            if (top.depth != 0)
                continue;
            if (!top.opener)
            {
                if (top.cmd == "WIN_GAME" || top.cmd == "LOSE_GAME")
                    res.custom_win_lose++; // unconditional at top level
                continue;
            }
            const size_t end = (size_t)top.match;
            bool has_result = false;
            for (size_t j = k; j <= end; j++)
                if (items[j].cmd == "WIN_GAME" || items[j].cmd == "LOSE_GAME")
                    has_result = true;
            if (!has_result)
            {
                k = end;
                continue;
            }
            // Walk IF -> IF -> ... -> WIN|LOSE_GAME -> ENDIF chains only.
            SetupWinLoseRule rule;
            bool ok = true;
            size_t j = k;
            while (ok)
            {
                WinLoseClause c;
                std::string var;
                if (items[j].cmd != "IF" || !items[j].clean || !script_setup_parse_if_clause(items[j].text, c)
                    || !win_variable_ok(c.variable, res.level_version, var))
                {
                    ok = false;
                    break;
                }
                c.variable = var;
                rule.clauses.push_back(c);
                const size_t close = (size_t)items[j].match;
                const Item &body = items[j + 1];
                if ((body.cmd == "WIN_GAME" || body.cmd == "LOSE_GAME") && body.clean && j + 2 == close)
                {
                    rule.win = (body.cmd == "WIN_GAME");
                    break;
                }
                if (body.opener && (size_t)body.match + 1 == close)
                {
                    j = j + 1;
                    continue;
                }
                ok = false;
            }
            if (ok)
            {
                rule.first_line = items[k].line;
                rule.last_line = items[end].line;
                for (size_t l = rule.first_line; l <= rule.last_line; l++)
                    res.owned_win_lose[l] = true;
                res.seed.rules.push_back(rule);
            }
            else
                res.custom_win_lose++;
            k = end;
        }
    }

    // ---- verdict ---------------------------------------------------------
    if (items.empty())
    {
        res.verdict = SetupVerdict_Unsupported;
        res.reason = "The level has no script.";
    }
    else if (res.level_version != 0 && res.level_version != 1)
    {
        res.verdict = SetupVerdict_Unsupported;
        res.reason = "The level script uses an unsupported LEVEL_VERSION.";
    }
    else if (!balanced)
    {
        res.verdict = SetupVerdict_Unsupported;
        res.reason = "The level script has unbalanced IF/ENDIF.";
    }
    else if (!res.locks.empty())
    {
        res.verdict = SetupVerdict_Partial;
        res.reason = "Some settings are controlled by the level script while it runs.";
    }
    // A Lua script that changes the tab's fields makes an otherwise Supported level Partial too (it is
    // runtime control we can see is there but not exactly what it sets).
    if (res.verdict != SetupVerdict_Unsupported && res.lua.any_setup())
    {
        const std::string note = "The level's Lua script also changes " + res.lua.describe()
            + "; it runs alongside this setup, so the result may differ from what is shown.";
        if (res.verdict == SetupVerdict_Supported)
        {
            res.verdict = SetupVerdict_Partial;
            res.reason = note;
        }
        else
            res.reason += " " + note;
    }
    return res;
}

/******************************************************************************/
namespace {

// Lua source with comments and string literals blanked out (newlines kept), so an API name inside a
// comment or a string is not mistaken for a call.
std::string strip_lua_comments_and_strings(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    const size_t n = s.size();
    size_t i = 0;
    // Length of a long-bracket opener at s[i] ("[[", "[=[", ...) -> level, or -1.
    auto long_open = [&](size_t at) -> int {
        if (at >= n || s[at] != '[')
            return -1;
        size_t k = at + 1;
        int level = 0;
        while (k < n && s[k] == '=')
        {
            level++;
            k++;
        }
        return (k < n && s[k] == '[') ? level : -1;
    };
    auto skip_long = [&](size_t at, int level) -> size_t { // returns index just past the closing bracket
        const std::string close = "]" + std::string((size_t)level, '=') + "]";
        const size_t e = s.find(close, at);
        return (e == std::string::npos) ? n : e + close.size();
    };
    auto blank = [&](size_t from, size_t to) {
        for (size_t k = from; k < to && k < n; k++)
            out.push_back(s[k] == '\n' ? '\n' : ' ');
    };
    while (i < n)
    {
        if (s.compare(i, 2, "--") == 0)
        {
            const int lvl = long_open(i + 2);
            if (lvl >= 0)
            {
                const size_t end = skip_long(i + 2, lvl);
                blank(i, end);
                i = end;
            }
            else
            {
                size_t end = s.find('\n', i);
                if (end == std::string::npos)
                    end = n;
                blank(i, end);
                i = end;
            }
        }
        else if (s[i] == '[' && long_open(i) >= 0)
        {
            const size_t end = skip_long(i, long_open(i));
            blank(i, end);
            i = end;
        }
        else if (s[i] == '"' || s[i] == '\'')
        {
            const char q = s[i];
            size_t k = i + 1;
            while (k < n && s[k] != q && s[k] != '\n')
                k += (s[k] == '\\' && k + 1 < n) ? 2 : 1;
            const size_t end = (k < n && s[k] == q) ? k + 1 : k;
            blank(i, end);
            i = end;
        }
        else
        {
            out.push_back(s[i]);
            i++;
        }
    }
    return out;
}

} // namespace

bool SetupLuaUse::any_setup() const
{
    bool a = money || max_creatures || gen_speed || pool || controller || ally || win || lose;
    for (int k = 0; k < AvailKind_Count; k++)
        a = a || avail[k];
    return a;
}

bool SetupLuaUse::touches(int field) const
{
    switch (field)
    {
    case SetupField_Money:        return money;
    case SetupField_MaxCreatures: return max_creatures;
    case SetupField_GenSpeed:     return gen_speed;
    case SetupField_Pool:         return pool;
    case SetupField_Controller:   return controller;
    case SetupField_Ally:         return ally;
    default:
        return (field >= 0 && field < AvailKind_Count) ? avail[field] : false;
    }
}

std::string SetupLuaUse::describe() const
{
    std::vector<std::string> parts;
    if (money) parts.push_back("start gold");
    if (max_creatures) parts.push_back("the creature limit");
    if (gen_speed) parts.push_back("the creature arrival interval");
    if (pool) parts.push_back("the creature pool");
    static const char *const kinds[AvailKind_Count] = { "creature availability", "room availability",
        "spell availability", "trap availability", "door availability" };
    for (int k = 0; k < AvailKind_Count; k++)
        if (avail[k]) parts.push_back(kinds[k]);
    if (controller) parts.push_back("the computer players");
    if (ally) parts.push_back("alliances");
    if (win || lose) parts.push_back("win/lose rules");
    std::string out;
    for (size_t i = 0; i < parts.size(); i++)
        out += ((i == 0) ? "" : (i + 1 == parts.size() ? " and " : ", ")) + parts[i];
    return out;
}

SetupLuaUse script_setup_scan_lua(const std::string &lua_text)
{
    SetupLuaUse use;
    use.scanned = true;
    const std::string s = strip_lua_comments_and_strings(lua_text);
    const size_t n = s.size();
    std::string prev_ident;
    size_t i = 0;
    while (i < n)
    {
        if (!(isalpha((unsigned char)s[i]) || s[i] == '_'))
        {
            i++;
            continue;
        }
        size_t j = i;
        while (j < n && (isalnum((unsigned char)s[j]) || s[j] == '_'))
            j++;
        const std::string id = s.substr(i, j - i);
        size_t k = j;
        while (k < n && (s[k] == ' ' || s[k] == '\t'))
            k++;
        const bool call = (k < n && s[k] == '(');
        // A definition ("function StartMoney(...)") is not a call.
        if (call && prev_ident != "function")
        {
            if (id == "StartMoney") use.money = true;
            else if (id == "MaxCreatures") use.max_creatures = true;
            else if (id == "SetGenerateSpeed") use.gen_speed = true;
            else if (id == "AddCreatureToPool") use.pool = true;
            else if (id == "CreatureAvailable") use.avail[AvailKind_Creature] = true;
            else if (id == "RoomAvailable") use.avail[AvailKind_Room] = true;
            else if (id == "MagicAvailable") use.avail[AvailKind_Magic] = true;
            else if (id == "TrapAvailable") use.avail[AvailKind_Trap] = true;
            else if (id == "DoorAvailable") use.avail[AvailKind_Door] = true;
            else if (id == "ComputerPlayer" || id == "SetComputerProcess" || id == "SetComputerChecks"
                || id == "SetComputerGlobals" || id == "SetComputerEvent") use.controller = true;
            else if (id == "AllyPlayers") use.ally = true;
            else if (id == "WinGame") use.win = true;
            else if (id == "LoseGame") use.lose = true;
            else if (id == "Research" || id == "Research_order") use.research = true;
        }
        prev_ident = id;
        i = j;
    }
    return use;
}

std::string script_setup_mask(const std::string &text, const SetupAnalysis &analysis, bool mask_win_lose)
{
    const std::vector<Seg> segs = split_lines(text);
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < segs.size(); i++)
    {
        const bool blank = i < analysis.owned_setup.size()
            && (analysis.owned_setup[i] || (mask_win_lose && analysis.owned_win_lose[i]));
        if (!blank)
            out += segs[i].content;
        out += segs[i].term;
    }
    return out;
}
