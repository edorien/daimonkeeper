/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file script_setup_prelude.cpp
 *     See script_setup_prelude.h.
 * @par Comment:
 *     None.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "script_setup_prelude.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include "post_inc.h"

/******************************************************************************/
namespace {

const char *const kAvailCommandNames[AvailKind_Count] = {
    "CREATURE_AVAILABLE", "ROOM_AVAILABLE", "MAGIC_AVAILABLE", "TRAP_AVAILABLE", "DOOR_AVAILABLE",
};

const char *const kFieldNames[SetupField_Count] = {
    "creature availability", "room availability", "spell availability", "trap availability",
    "door availability", "start gold", "max creatures", "generation speed", "creature pool",
    "computer player", "alliance",
};

void add_issue(std::vector<SetupIssue> *out, SetupIssueSeverity sev, const std::string &msg)
{
    if (out == nullptr)
        return;
    SetupIssue i;
    i.severity = sev;
    i.message = msg;
    out->push_back(i);
}

std::string player_token(int64_t p)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "PLAYER%" PRId64, (int64_t)(p));
    return buf;
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

bool name_in(const char *const *list, const std::string &name)
{
    for (int64_t i = 0; list[i] != nullptr; i++)
        if (name == list[i])
            return true;
    return false;
}

bool same_rules(const std::vector<SetupWinLoseRule> &a, const std::vector<SetupWinLoseRule> &b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
    {
        if (a[i].win != b[i].win || a[i].clauses.size() != b[i].clauses.size())
            return false;
        for (size_t k = 0; k < a[i].clauses.size(); k++)
        {
            const WinLoseClause &x = a[i].clauses[k], &y = b[i].clauses[k];
            if (x.player != y.player || x.variable != y.variable || x.op != y.op || x.value != y.value)
                return false;
        }
    }
    return true;
}

bool same_values(const SetupSeed &a, const SetupSeed &b)
{
    return a.generate_speed == b.generate_speed && a.start_money == b.start_money
        && a.max_creatures == b.max_creatures && a.pool == b.pool && a.avail == b.avail
        && a.controllers == b.controllers;
}

// choices.values with every runtime-locked field forced back to the level's
// own value (edits there are dropped, with one warning per field kind).
SetupSeed apply_locks(const SetupAnalysis &an, const SetupChoices &ch, std::vector<SetupIssue> *issues)
{
    SetupSeed v = ch.values;
    const SetupSeed &s = an.seed;
    bool warned[SetupField_Count] = {};
    auto dropped = [&](int64_t field) {
        if (warned[field])
            return;
        warned[field] = true;
        add_issue(issues, SetupIssue_Warning,
            std::string("Changes to ") + kFieldNames[field] + " ignored where the level script controls it while it runs.");
    };

    // availability: keys in either map
    std::vector<SetupAvailKey> keys;
    for (const auto &kv : s.avail) keys.push_back(kv.first);
    for (const auto &kv : v.avail) keys.push_back(kv.first);
    for (const SetupAvailKey &k : keys)
    {
        if (!an.is_locked(k.kind, k.player, k.item))
            continue;
        const auto si = s.avail.find(k);
        const auto vi = v.avail.find(k);
        const bool had = (si != s.avail.end());
        const bool has = (vi != v.avail.end());
        if (had != has || (had && has && !(si->second == vi->second)))
            dropped(k.kind);
        if (had)
            v.avail[k] = si->second;
        else
            v.avail.erase(k);
    }
    for (int64_t p = 0; p < 16; p++)
    {
        if (an.is_locked(SetupField_Money, p))
        {
            const auto si = s.start_money.find(p);
            const auto vi = v.start_money.find(p);
            if ((si == s.start_money.end()) != (vi == v.start_money.end()) || (si != s.start_money.end() && si->second != vi->second))
                dropped(SetupField_Money);
            if (si != s.start_money.end()) v.start_money[p] = si->second; else v.start_money.erase(p);
        }
        if (an.is_locked(SetupField_MaxCreatures, p))
        {
            const auto si = s.max_creatures.find(p);
            const auto vi = v.max_creatures.find(p);
            if ((si == s.max_creatures.end()) != (vi == v.max_creatures.end()) || (si != s.max_creatures.end() && si->second != vi->second))
                dropped(SetupField_MaxCreatures);
            if (si != s.max_creatures.end()) v.max_creatures[p] = si->second; else v.max_creatures.erase(p);
        }
        if (an.is_locked(SetupField_Controller, p))
        {
            const auto si = s.controllers.find(p);
            const auto vi = v.controllers.find(p);
            if ((si == s.controllers.end()) != (vi == v.controllers.end()) || (si != s.controllers.end() && !(si->second == vi->second)))
                dropped(SetupField_Controller);
            if (si != s.controllers.end()) v.controllers[p] = si->second; else v.controllers.erase(p);
        }
    }
    if (an.is_locked(SetupField_GenSpeed, -1))
    {
        if (v.generate_speed != s.generate_speed)
            dropped(SetupField_GenSpeed);
        v.generate_speed = s.generate_speed;
    }
    std::vector<std::string> pool_names;
    for (const auto &kv : s.pool) pool_names.push_back(kv.first);
    for (const auto &kv : v.pool) pool_names.push_back(kv.first);
    for (const std::string &n : pool_names)
    {
        if (!an.is_locked(SetupField_Pool, -1, n))
            continue;
        const auto si = s.pool.find(n);
        const auto vi = v.pool.find(n);
        if ((si == s.pool.end()) != (vi == v.pool.end()) || (si != s.pool.end() && si->second != vi->second))
            dropped(SetupField_Pool);
        if (si != s.pool.end()) v.pool[n] = si->second; else v.pool.erase(n);
    }
    return v;
}

// One value per player, 0..players-1, all present and equal?
template <class Map> bool uniform(const Map &m, int64_t players, int64_t &value)
{
    if (players <= 0)
        return false;
    for (int64_t p = 0; p < players; p++)
    {
        const auto it = m.find(p);
        if (it == m.end())
            return false;
        if (p == 0)
            value = it->second;
        else if (it->second != value)
            return false;
    }
    return (int64_t)m.size() == players;
}

} // namespace

bool SetupOverride::has_errors() const
{
    for (const SetupIssue &i : issues)
        if (i.severity == SetupIssue_Error)
            return true;
    return false;
}

SetupChoices script_setup_default_choices(const SetupAnalysis &analysis)
{
    SetupChoices c;
    c.values = analysis.seed;
    c.rules = analysis.seed.rules; // a starting point if the user switches to Replace
    return c;
}

bool script_setup_choices_are_default(const SetupAnalysis &analysis, const SetupChoices &choices)
{
    if (!same_values(choices.values, analysis.seed) || !choices.allies.empty())
        return false;
    return !choices.replace_win_lose || same_rules(choices.rules, analysis.seed.rules);
}

std::string script_setup_generate_prelude(const SetupAnalysis &an, const SetupChoices &ch, int64_t players,
    std::vector<SetupIssue> *issues)
{
    const SetupSeed v = apply_locks(an, ch, issues);
    std::string out = "REM --- Skirmish setup override (generated; scanned first, at script version 1) ---\n";
    char line[256];

    if (v.generate_speed >= 0)
    {
        snprintf(line, sizeof(line), "SET_GENERATE_SPEED(%" PRId64 ")\n", (int64_t)(v.generate_speed));
        out += line;
    }

    // Money / max creatures: ALL_PLAYERS when every slot agrees, else per player.
    for (int64_t pass = 0; pass < 2; pass++)
    {
        const std::map<int64_t, int64_t> &m = (pass == 0) ? v.start_money : v.max_creatures;
        const char *cmd = (pass == 0) ? "START_MONEY" : "MAX_CREATURES";
        int64_t val = 0;
        if (pass == 0 && uniform(m, players, val))
        {
            if (val != 0)
            {
                snprintf(line, sizeof(line), "%s(ALL_PLAYERS,%" PRId64 ")\n", cmd, (int64_t)(val));
                out += line;
            }
            continue;
        }
        if (pass == 1 && uniform(m, players, val))
        {
            snprintf(line, sizeof(line), "%s(ALL_PLAYERS,%" PRId64 ")\n", cmd, (int64_t)(val));
            out += line;
            continue;
        }
        for (const auto &kv : m)
        {
            if (pass == 0 && kv.second == 0)
                continue; // START_MONEY(…,0) adds nothing
            snprintf(line, sizeof(line), "%s(%s,%" PRId64 ")\n", cmd, player_token(kv.first).c_str(), (int64_t)(kv.second));
            out += line;
        }
    }

    for (const auto &kv : v.pool)
    {
        if (kv.second <= 0)
            continue;
        snprintf(line, sizeof(line), "ADD_CREATURE_TO_POOL(%s,%" PRId64 ")\n", kv.first.c_str(), (int64_t)(kv.second));
        out += line;
    }

    // Availability, grouped by (kind, item); ALL_PLAYERS when every slot has the same value.
    for (int64_t kind = 0; kind < AvailKind_Count; kind++)
    {
        std::map<std::string, std::map<int64_t, SetupAvailValue>> by_item;
        for (const auto &kv : v.avail)
            if (kv.first.kind == kind)
                by_item[kv.first.item][kv.first.player] = kv.second;
        for (const auto &it : by_item)
        {
            bool all = ((int64_t)it.second.size() == players) && players > 0;
            SetupAvailValue first;
            for (int64_t p = 0; p < players && all; p++)
            {
                const auto f = it.second.find(p);
                if (f == it.second.end()) { all = false; break; }
                if (p == 0) first = f->second; else if (!(f->second == first)) all = false;
            }
            if (all)
            {
                snprintf(line, sizeof(line), "%s(ALL_PLAYERS,%s,%" PRId64 ",%" PRId64 ")\n", kAvailCommandNames[kind], it.first.c_str(), (int64_t)(first.a), (int64_t)(first.b));
                out += line;
                continue;
            }
            for (const auto &pv : it.second)
            {
                snprintf(line, sizeof(line), "%s(%s,%s,%" PRId64 ",%" PRId64 ")\n", kAvailCommandNames[kind], player_token(pv.first).c_str(),
                    it.first.c_str(), (int64_t)(pv.second.a), (int64_t)(pv.second.b));
                out += line;
            }
        }
    }

    // Controllers.
    for (const auto &kv : v.controllers)
    {
        switch (kv.second.kind)
        {
        case SetupController::Model:
            snprintf(line, sizeof(line), "COMPUTER_PLAYER(%s,%" PRId64 ")\n", player_token(kv.first).c_str(), (int64_t)(kv.second.model));
            break;
        case SetupController::Roaming:
            snprintf(line, sizeof(line), "COMPUTER_PLAYER(%s,ROAMING)\n", player_token(kv.first).c_str());
            break;
        default:
            snprintf(line, sizeof(line), "COMPUTER_PLAYER(%s,OFF)\n", player_token(kv.first).c_str());
            break;
        }
        out += line;
    }

    // Alliances (additive to whatever the file does).
    for (const auto &pr : ch.allies)
    {
        if (an.is_locked(SetupField_Ally, pr.first) || an.is_locked(SetupField_Ally, pr.second))
        {
            add_issue(issues, SetupIssue_Warning, "Alliance changes ignored where the level script controls them while it runs.");
            continue;
        }
        snprintf(line, sizeof(line), "ALLY_PLAYERS(%s,%s,1)\n", player_token(pr.first).c_str(), player_token(pr.second).c_str());
        out += line;
    }

    // Win / lose rules (Replace mode only): nested IF per clause.
    if (ch.replace_win_lose)
    {
        for (const SetupWinLoseRule &r : ch.rules)
        {
            std::string indent;
            for (const WinLoseClause &c : r.clauses)
            {
                snprintf(line, sizeof(line), "%sIF(%s,%s %s %" PRId64 ")\n", indent.c_str(), player_token(c.player).c_str(),
                    c.variable.c_str(), c.op.c_str(), (int64_t)(c.value));
                out += line;
                indent += "\t";
            }
            out += indent + (r.win ? "WIN_GAME\n" : "LOSE_GAME\n");
            for (size_t k = r.clauses.size(); k > 0; k--)
            {
                indent.pop_back();
                out += indent + "ENDIF\n";
            }
        }
    }
    return out;
}

SetupOverride script_setup_build_override(const std::string &text, const SetupAnalysis &an, const SetupChoices &ch,
    const SetupBuildOptions &opt)
{
    SetupOverride res;
    std::vector<SetupIssue> &iss = res.issues;
    const int64_t players = opt.players;

    if (an.verdict == SetupVerdict_Unsupported)
    {
        add_issue(&iss, SetupIssue_Error, an.reason);
        return res;
    }

    // ---- ranges, player numbers, names ----------------------------------
    auto check_player = [&](int64_t p, const char *what) {
        if (p < 0 || p >= players)
            add_issue(&iss, SetupIssue_Error, std::string(what) + ": player " + std::to_string(p + 1) + " is outside the level's " + std::to_string(players) + " slots.");
    };
    const SetupSeed &v = ch.values;
    if (v.generate_speed < -1)
        add_issue(&iss, SetupIssue_Error, "Generation speed cannot be negative.");
    if (v.generate_speed == 0)
        add_issue(&iss, SetupIssue_Warning, "Generation speed 0 may stop creatures arriving.");
    for (const auto &kv : v.start_money)
    {
        check_player(kv.first, "Start gold");
        if (kv.second < 0)
            add_issue(&iss, SetupIssue_Error, "Start gold cannot be negative.");
        else if (kv.second > kSetupSensibleGold)
            add_issue(&iss, SetupIssue_Warning, "Start gold above " + std::to_string(kSetupSensibleGold) + " is clamped by the engine.");
    }
    for (const auto &kv : v.max_creatures)
    {
        check_player(kv.first, "Max creatures");
        if (kv.second < 0)
            add_issue(&iss, SetupIssue_Error, "Max creatures cannot be negative.");
    }
    for (const auto &kv : v.pool)
    {
        if (!is_identifier(kv.first))
            add_issue(&iss, SetupIssue_Error, "Creature pool: '" + kv.first + "' is not a valid name.");
        else if (opt.item_exists && !opt.item_exists(SetupField_Pool, kv.first))
            add_issue(&iss, SetupIssue_Error, "Creature pool: unknown creature " + kv.first + ".");
        if (kv.second < 0)
            add_issue(&iss, SetupIssue_Error, "Creature pool amounts cannot be negative.");
    }
    for (const auto &kv : v.avail)
    {
        check_player(kv.first.player, "Availability");
        if (kv.first.kind < 0 || kv.first.kind >= AvailKind_Count)
            add_issue(&iss, SetupIssue_Error, "Availability: bad kind.");
        else if (!is_identifier(kv.first.item))
            add_issue(&iss, SetupIssue_Error, "Availability: '" + kv.first.item + "' is not a valid name.");
        else if (opt.item_exists && !opt.item_exists(kv.first.kind, kv.first.item))
            add_issue(&iss, SetupIssue_Error, std::string("Unknown ") + kFieldNames[kv.first.kind] + " item " + kv.first.item + ".");
        if (kv.second.a < 0 || kv.second.b < 0)
            add_issue(&iss, SetupIssue_Error, "Availability values cannot be negative.");
    }
    for (const auto &kv : v.controllers)
    {
        check_player(kv.first, "Computer player");
        if (kv.second.kind == SetupController::Model && (kv.second.model < 0 || kv.second.model >= 64))
            add_issue(&iss, SetupIssue_Error, "Computer player model " + std::to_string(kv.second.model) + " is out of range (0-63).");
    }
    for (const auto &pr : ch.allies)
    {
        check_player(pr.first, "Alliance");
        check_player(pr.second, "Alliance");
        if (pr.first == pr.second)
            add_issue(&iss, SetupIssue_Error, "A player cannot ally with itself.");
    }

    // ---- win / lose ------------------------------------------------------
    int64_t if_after = an.if_count, win_after = an.win_count, lose_after = an.lose_count;
    if (ch.replace_win_lose)
    {
        int64_t seed_wins = 0, seed_loses = 0, new_wins = 0, new_loses = 0;
        for (const SetupWinLoseRule &r : an.seed.rules)
        {
            if_after -= (int64_t)r.clauses.size();
            (r.win ? seed_wins : seed_loses)++;
        }
        win_after -= seed_wins;
        lose_after -= seed_loses;
        const char *const *ops = script_setup_win_lose_operators(nullptr);
        int64_t n_ops = 0;
        script_setup_win_lose_operators(&n_ops);
        for (const SetupWinLoseRule &r : ch.rules)
        {
            if_after += (int64_t)r.clauses.size();
            (r.win ? new_wins : new_loses)++;
            if (r.clauses.empty())
                add_issue(&iss, SetupIssue_Error, "A win/lose rule needs at least one condition.");
            for (const WinLoseClause &c : r.clauses)
            {
                check_player(c.player, "Win/lose rule");
                if (!name_in(script_setup_win_variables_identical(), c.variable) && !name_in(script_setup_win_variables_v1_only(), c.variable))
                    add_issue(&iss, SetupIssue_Error, "Win/lose rule: variable " + c.variable + " is not supported.");
                bool op_ok = false;
                for (int64_t i = 0; i < n_ops; i++)
                    op_ok |= (c.op == ops[i]);
                if (!op_ok)
                    add_issue(&iss, SetupIssue_Error, "Win/lose rule: bad comparison '" + c.op + "'.");
            }
        }
        win_after += new_wins;
        lose_after += new_loses;
        if (win_after <= 0)
            add_issue(&iss, SetupIssue_Error, "No win condition: nobody could win this level. Add a win rule.");
        if (new_wins + new_loses == 0 && an.custom_win_lose > 0)
            add_issue(&iss, SetupIssue_Warning, "Only the level's own custom win/lose rules remain.");
    }
    if (if_after > kSetupConditionsCount)
        add_issue(&iss, SetupIssue_Error, "Too many conditions for the engine (" + std::to_string(if_after) + " of " + std::to_string(kSetupConditionsCount) + ").");
    if (win_after > kSetupWinConditionsCount)
        add_issue(&iss, SetupIssue_Error, "Too many win rules (" + std::to_string(win_after) + " of " + std::to_string(kSetupWinConditionsCount) + ").");
    if (lose_after > kSetupWinConditionsCount)
        add_issue(&iss, SetupIssue_Error, "Too many lose rules (" + std::to_string(lose_after) + " of " + std::to_string(kSetupWinConditionsCount) + ").");

    res.prelude = script_setup_generate_prelude(an, ch, players, &iss);
    if (res.has_errors())
    {
        res.prelude.clear();
        return res;
    }
    // Judged after locked fields are forced back to the level's own values, so an
    // edit that can only be ignored never installs an override.
    SetupChoices effective = ch;
    effective.values = apply_locks(an, ch, nullptr);
    if (!opt.force && script_setup_choices_are_default(an, effective))
    {
        res.prelude.clear();
        return res; // untouched tab: install nothing
    }
    res.masked = script_setup_mask(text, an, ch.replace_win_lose);
    res.active = true;
    return res;
}
